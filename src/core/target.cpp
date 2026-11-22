#include "picodbg/target.hpp"
#include "picodbg/peripherals.hpp"
#include <cctype>

namespace picodbg {
namespace {
// Small, read-only expression language. It deliberately cannot call target code
// or perform assignments: refreshing a watch must not mutate a debug session.
class Expression {
public:
    Expression(const DebugTarget& debugger, unsigned core, const std::string& text)
        : debugger_(debugger), core_(core), text_(text) {}
    uint32_t evaluate() {
        const auto value = binary(0); space();
        if (pos_ != text_.size()) fail("Texte inattendu");
        return value;
    }
private:
    const DebugTarget& debugger_;
    unsigned core_;
    const std::string& text_;
    size_t pos_ = 0;
    unsigned depth_ = 0;
    void space() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    void fail(const std::string& reason) const { throw Error(ErrorKind::InvalidArgument, reason + " dans l'expression à la position " + std::to_string(pos_)); }
    bool take(const std::string& token) { space(); if (text_.compare(pos_, token.size(), token) == 0) { pos_ += token.size(); return true; } return false; }
    uint32_t atom() {
        if (++depth_ > 64) fail("Expression trop imbriquée");
        const auto result = atomImpl(); --depth_; return result;
    }
    uint32_t atomImpl() {
        space();
        if (take("(")) { const auto n = binary(0); if (!take(")")) fail("Parenthèse attendue"); return n; }
        if (take("~")) return ~atom();
        if (take("-")) return 0u - atom();
        if (take("+")) return atom();
        if (take("*")) return debugger_.peek(atom(), 4, core_);
        space();
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (!std::isalnum(c) && c != '_' && c != '$' && c != ':' && c != '.') break;
            ++pos_;
        }
        if (start == pos_) fail("Valeur attendue");
        std::string name = text_.substr(start, pos_ - start);
        if (std::isdigit(static_cast<unsigned char>(name[0]))) {
            try {
                size_t used = 0;
                const uint64_t n = std::stoull(name, &used, name.compare(0, 2, "0x") == 0 || name.compare(0, 2, "0X") == 0 ? 16 : 10);
                if (used != name.size() || n > 0xffffffffull) fail("Entier 32 bits attendu");
                return static_cast<uint32_t>(n);
            } catch (const std::invalid_argument&) { fail("Entier invalide"); }
              catch (const std::out_of_range&) { fail("Entier trop grand"); }
        }
        if (name == "read8" || name == "read16" || name == "read32") {
            if (!take("(")) fail("Parenthèse attendue");
            const auto address = binary(0);
            if (!take(")")) fail("Parenthèse attendue");
            return debugger_.peek(address, name == "read8" ? 1u : name == "read16" ? 2u : 4u, core_);
        }
        if (!name.empty() && name[0] == '$') name.erase(0, 1);
        for (unsigned i = 0; i < 16; ++i) if (name == registerName(i) || name == "r" + std::to_string(i)) return debugger_.registerValue(core_, i);
        if (name == "xpsr") return debugger_.registerValue(core_, 16);
        if (name == "msp") return debugger_.registerValue(core_, 17);
        if (name == "psp") return debugger_.registerValue(core_, 18);
        if (name == "control") return debugger_.registerValue(core_, 19);
        if (name == "primask") return debugger_.registerValue(core_, 20);
        const auto symbol = debugger_.image.symbols.find(name);
        if (symbol != debugger_.image.symbols.end()) return symbol->second;
        fail("Symbole inconnu : " + name); return 0;
    }
    uint32_t binary(unsigned level) {
        if (level == 6) return atom();
        uint32_t lhs = binary(level + 1);
        while (true) {
            std::string op;
            switch (level) {
            case 0: if (take("|")) op = "|"; break;
            case 1: if (take("^")) op = "^"; break;
            case 2: if (take("&")) op = "&"; break;
            case 3: if (take("<<")) op = "<<"; else if (take(">>")) op = ">>"; break;
            case 4: if (take("+")) op = "+"; else if (take("-")) op = "-"; break;
            case 5: if (take("*")) op = "*"; else if (take("/")) op = "/"; else if (take("%")) op = "%"; break;
            }
            if (op.empty()) return lhs;
            const uint32_t rhs = binary(level + 1);
            if (op == "|") lhs |= rhs;
            else if (op == "^") lhs ^= rhs;
            else if (op == "&") lhs &= rhs;
            else if (op == "+") lhs += rhs;
            else if (op == "-") lhs -= rhs;
            else if (op == "*") lhs *= rhs;
            else if (op == "/" || op == "%") { if (!rhs) fail("Division par zéro"); lhs = op == "/" ? lhs / rhs : lhs % rhs; }
            else { if (rhs >= 32) fail("Décalage hors 0..31"); lhs = op == "<<" ? lhs << rhs : lhs >> rhs; }
        }
    }
};
}
uint32_t DebugTarget::evaluate(const std::string& expression, unsigned core) const {
    if (core > 1 || expression.size() > 4096) throw Error(ErrorKind::InvalidArgument, "Expression/cœur invalide");
    return Expression(*this, core, expression).evaluate();
}
uint32_t DebugTarget::registerValue(unsigned core, unsigned index) const {
    if (!registerAvailable(core, index)) throw Error(ErrorKind::Unsupported, "Registre CPU indisponible");
    const Cpu& cpu = cpus.at(core);
    if (index < 16) return cpu.r[index];
    const uint32_t extras[] = {cpu.xpsr, cpu.msp, cpu.psp, cpu.control, cpu.primask};
    return extras[index - 16];
}
void DebugTarget::writeCpuRegister(unsigned core, unsigned index, uint32_t value) {
    if (!registerAvailable(core, index) || executing() || busy())
        throw Error(ErrorKind::InvalidArgument, "Registre CPU indisponible. Arrêtez la cible et choisissez un registre exposé.");
    writeCpuValue(core, index, value);
}
void DebugTarget::checkMemoryWrite(uint32_t address, uint64_t value, unsigned size, unsigned core) const {
    if (!loaded() || busy() || executing())
        throw Error(ErrorKind::InvalidArgument, "L’écriture nécessite une cible arrêtée et disponible");
    if (core > 1 || !size || size > 8 || uint64_t(address) + size > 0x100000000ull)
        throw Error(ErrorKind::InvalidArgument, "Plage d’écriture invalide", address);
    if (size < 8 && value >= (uint64_t(1) << (8 * size)))
        throw Error(ErrorKind::InvalidArgument, "La valeur dépasse la largeur sélectionnée", address);
    for (unsigned i = 0; i < size; ++i) {
        const auto* region = regionAt(description(), address + i);
        if (!region || !regionWritable(*region))
            throw Error(ErrorKind::Unsupported, "Adresse hors mémoire inscriptible", address + i);
    }
}
void DebugTarget::writeMemory(uint32_t address, uint32_t value, unsigned size, unsigned core) {
    if (size != 1 && size != 2 && size != 4)
        throw Error(ErrorKind::InvalidArgument, "Plage d’écriture invalide", address);
    checkMemoryWrite(address, value, size, core);
    writeTargetValue(address, value, size, core);
}
void DebugTarget::writeMemoryBytes(uint32_t address, uint64_t value, unsigned size, unsigned core) {
    checkMemoryWrite(address, value, size, core);
    writeTargetBytes(address, value, size, core);
}
void DebugTarget::writeTargetBytes(uint32_t address, uint64_t value, unsigned size, unsigned core) {
    for (unsigned i = 0; i < size; ++i)
        writeTargetValue(address + i, static_cast<uint32_t>((value >> (8 * i)) & 0xffu), 1, core);
}
void DebugTarget::writePeripheralRegister(uint32_t address, uint32_t value, unsigned core) {
    if (!loaded() || busy() || executing())
        throw Error(ErrorKind::InvalidArgument, "L’écriture nécessite une cible arrêtée et disponible");
    if (core > 1 || (address & 3u))
        throw Error(ErrorKind::InvalidArgument, "Adresse de registre ou cœur invalide", address);
    for (const auto& device : peripheralCatalog(description().chip)) {
        for (const auto& reg : device.registers) {
            if (reg.address != address) continue;
            if (!registerWritable(reg))
                throw Error(ErrorKind::Unsupported, "Registre en lecture seule", address);
            if (!hardware() && device.coverage == "Non émulé")
                throw Error(ErrorKind::Unsupported, "Périphérique non émulé", address);
            writeTargetValue(address, value, 4, core);
            return;
        }
    }
    throw Error(ErrorKind::Unsupported, "Registre absent du catalogue", address);
}
void DebugTarget::removeWatchpoint(size_t index) {
    if (index >= watchpoints.size()) throw Error(ErrorKind::InvalidArgument, "Watchpoint inconnu");
    watchpoints.erase(watchpoints.begin() + static_cast<ptrdiff_t>(index));
}
}
