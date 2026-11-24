#include "stack_details.hpp"
#include "picodbg/debugger.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace picodbg;

namespace {
constexpr int outputLimit = 32 * 1024 * 1024;
const QRegularExpression whitespace("\\s+");

struct Die {
    QString tag;
    int parent;
    QHash<QString, QString> attributes;
};

QString text(const Die& die, const char* name) {
    auto value = die.attributes.value(QString::fromLatin1(name));
    const int end = value.indexOf(')');
    if (value.startsWith('(') && end >= 0) value = value.mid(end + 1).trimmed();
    const int suffix = value.lastIndexOf("): ");
    return suffix >= 0 ? value.mid(suffix + 3) : value;
}

QString expression(const QString& value) {
    const int start = value.indexOf("(DW_OP_");
    return start >= 0 && value.endsWith(')') ? value.mid(start + 1, value.size() - start - 2) : QString();
}

bool range(const Die& die, uint32_t& begin, uint32_t& end) {
    bool lowOk, highOk;
    begin = text(die, "low_pc").toUInt(&lowOk, 0);
    uint64_t high = text(die, "high_pc").toULongLong(&highOk, 0);
    if (!die.attributes.value("high_pc").startsWith("(addr)")) high += begin;
    if (!lowOk || !highOk || high <= begin || high > std::numeric_limits<uint32_t>::max()) return false;
    end = static_cast<uint32_t>(high);
    return true;
}

int reference(const Die& die, const QHash<uint32_t, int>& offsets) {
    static const QRegularExpression ref("<0x([a-fA-F0-9]+)>");
    const auto match = ref.match(die.attributes.value("type"));
    return match.hasMatch() ? offsets.value(match.captured(1).toUInt(nullptr, 16), -1) : -1;
}

FrameType resolveType(const QVector<Die>& dies, const QHash<uint32_t, int>& offsets, int index, unsigned depth = 0) {
    if (index < 0 || depth >= 12) return {};
    const auto& die = dies[index];
    const auto name = text(die, "name");
    const auto width = text(die, "byte_size").toUInt(nullptr, 0);
    if (die.tag == "pointer_type") {
        auto result = resolveType(dies, offsets, reference(die, offsets), depth + 1);
        result.name = (result.name.isEmpty() ? QString("void") : result.name) + " *";
        result.kind = FrameType::Pointer;
        result.size = width ? width : 4;
        result.count = 1;
        result.readOnly = false;
        result.isArray = false;
        return result;
    }
    if (die.tag == "base_type" || die.tag == "enumeration_type") {
        auto result = frameType(name);
        result.size = width;
        const auto encoding = text(die, "encoding").section(' ', 0, 0).section('\t', 0, 0).toUInt(nullptr, 0);
        if (encoding == 5 || encoding == 6) result.kind = FrameType::Signed;
        else if (encoding == 7 || encoding == 8) result.kind = FrameType::Unsigned;
        else if (encoding == 4) result.kind = FrameType::Float;
        else if (encoding == 2) result.kind = FrameType::Boolean;
        return result;
    }
    if (die.tag == "array_type" || die.tag == "typedef" || die.tag == "const_type" ||
        die.tag == "volatile_type" || die.tag == "restrict_type") {
        auto result = resolveType(dies, offsets, reference(die, offsets), depth + 1);
        if (die.tag == "const_type") result.readOnly = true;
        if (die.tag == "typedef" && !name.isEmpty()) result.name = name;
        if (die.tag == "array_type") {
            const bool nested = result.isArray;
            result.isArray = true;
            unsigned length = 1;
            unsigned dimensions = 0;
            for (int i = index + 1; i < dies.size() && dies[i].parent == index; ++i) {
                if (dies[i].tag != "subrange_type") continue;
                bool valid = false;
                auto count = text(dies[i], "count").toUInt(&valid, 0);
                if (!valid) {
                    count = text(dies[i], "upper_bound").toUInt(&valid, 0);
                    valid = valid && count < 4096;
                    ++count;
                }
                if (!valid || count == 0 || count > 4096 / length) { result.size = 0; break; }
                length *= count;
                ++dimensions;
            }
            if (dimensions != 1 || nested || result.size > std::numeric_limits<unsigned>::max() / length) result.size = 0;
            result.count = length;
            result.name += QString("[%1]").arg(length);
        }
        return result;
    }
    FrameType result;
    result.name = name;
    result.size = width;
    return result;
}

QString formatScalar(uint64_t raw, const FrameType& type) {
    if (type.kind == FrameType::Pointer || type.kind == FrameType::Unknown)
        return QString("0x%1").arg(raw, static_cast<int>(type.size * 2), 16, QLatin1Char('0'));
    if (type.kind == FrameType::Boolean) return raw ? QString("true") : QString("false");
    if (type.kind == FrameType::Float) {
        if (type.size == 4) {
            const auto bits = static_cast<uint32_t>(raw);
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            return QString::number(value, 'g', 9);
        }
        if (type.size == 8) {
            double value;
            std::memcpy(&value, &raw, sizeof(value));
            return QString::number(value, 'g', 17);
        }
    }
    if (type.kind == FrameType::Signed && (raw & (uint64_t(1) << (type.size * 8 - 1)))) {
        const auto mask = type.size == 8 ? std::numeric_limits<uint64_t>::max() : (uint64_t(1) << (type.size * 8)) - 1;
        return QString("-") + QString::number((~raw & mask) + 1);
    }
    return QString::number(raw);
}

QString findReadelf(const QString& compiler) {
    const QFileInfo gcc(compiler);
    const auto name = gcc.fileName();
    const auto index = name.lastIndexOf("gcc");
    if (index >= 0) {
        const auto tool = gcc.dir().filePath(name.left(index) + "readelf" + name.mid(index + 3));
        if (QFileInfo(tool).isExecutable()) return tool;
    }
    auto tool = QStandardPaths::findExecutable("arm-none-eabi-readelf");
    return tool.isEmpty() ? QStandardPaths::findExecutable("readelf") : tool;
}
}

FrameType frameType(QString name) {
    FrameType type;
    type.name = name;
    type.readOnly = name.lastIndexOf(QRegularExpression("\\bconst\\b")) > name.lastIndexOf('*');
    name.remove(QRegularExpression("\\b(?:const|volatile|restrict)\\b"));
    name = name.simplified();
    static const QRegularExpression array(R"(^(.+?)\s*\[(\d+)\]$)");
    const auto match = array.match(name);
    if (match.hasMatch()) {
        type.isArray = true;
        name = match.captured(1).trimmed();
        type.count = match.captured(2).toUInt();
    }
    if (name.endsWith('*')) { type.kind = FrameType::Pointer; type.size = 4; }
    else if (name == "bool" || name == "_Bool") { type.kind = FrameType::Boolean; type.size = 1; }
    else if (name == "float" || name == "double") { type.kind = FrameType::Float; type.size = name == "float" ? 4 : 8; }
    else {
        static const QRegularExpression unsignedWord("\\bunsigned\\b");
        static const QRegularExpression signWords("\\b(?:unsigned|signed)\\b");
        const bool unsignedType = name.contains(unsignedWord);
        const bool explicitSign = name.contains(signWords);
        auto base = name;
        base.remove(signWords);
        base = base.simplified();
        if (base.isEmpty() && explicitSign) base = "int";
        static const QRegularExpression integer(R"(^(char|short(?: int)?|(?:long ){1,2}int|long long|long|int)$)");
        static const QRegularExpression alias(R"(^(u?)int(8|16|32|64)_t$)");
        const auto matchAlias = alias.match(name);
        if (integer.match(base).hasMatch()) {
            type.kind = unsignedType || (!explicitSign && base == "char") ? FrameType::Unsigned : FrameType::Signed;
            type.size = base == "char" ? 1 : base.startsWith("short") ? 2 : base.startsWith("long long") ? 8 : 4;
        } else if (matchAlias.hasMatch()) {
            type.size = matchAlias.captured(2).toUInt() / 8;
            type.kind = matchAlias.captured(1).isEmpty() ? FrameType::Signed : FrameType::Unsigned;
        }
    }
    if (type.count > 4096 || type.count == 0) type.size = 0;
    return type;
}

bool stackMemory(const DebugTarget& target, uint32_t address, unsigned size) {
    const auto* region = regionAt(target.description(), address);
    return size && region && regionWritable(*region) && region->name.find("SRAM") != std::string::npos
        && uint64_t(address) + size <= region->end;
}

static bool readFrameBits(const DebugTarget& target, unsigned core, const FrameField& field, uint64_t& raw) {
    const auto size = field.type.size;
    if (!size || size > 8) return false;
    if (field.reg >= 0 && target.registerAvailable(core, static_cast<unsigned>(field.reg))) {
        if (size > 4 && (field.reg != 0 || !target.registerAvailable(core, 1))) return false;
        raw = target.registerValue(core, static_cast<unsigned>(field.reg));
        if (size < 4) raw &= (uint64_t(1) << (size * 8)) - 1;
        if (size > 4) raw |= uint64_t(target.registerValue(core, 1)) << 32;
        return true;
    }
    if (!field.inMemory || !stackMemory(target, field.address, size)) return false;
    raw = 0;
    for (unsigned i = 0; i < size; ++i) raw |= uint64_t(target.peek(field.address + i, 1, core)) << (8 * i);
    return true;
}

QString frameValue(const DebugTarget& target, unsigned core, const FrameField& field) {
    if (!field.rendered.isEmpty()) return field.rendered;
    uint64_t raw;
    try { if (readFrameBits(target, core, field, raw)) return formatScalar(raw, field.type); }
    catch (const Error&) { return "…"; }
    return {};
}
QString frameRawValue(const DebugTarget& target, unsigned core, const FrameField& field, bool binary) {
    if (!field.type.size || field.type.size > 8) return "—";
    try {
        uint64_t raw;
        if (!readFrameBits(target, core, field, raw)) {
            if (field.rendered.isEmpty()) return "—";
            auto rendered = field.rendered;
            if (field.type.kind == FrameType::Pointer) rendered = rendered.section(' ', 0, 0);
            raw = parseFrameValue(rendered, field.type);
        }
        if (binary) return "0b" + QString::number(raw, 2);
        return "0x" + QString::number(raw, 16).rightJustified(static_cast<int>(field.type.size * 2u), QLatin1Char('0'));
    } catch (const Error&) { return "…"; }
}

uint64_t parseFrameValue(const QString& input, const FrameType& type) {
    const auto text = input.trimmed();
    if (type.kind == FrameType::Boolean) {
        if (text == "true" || text == "1") return 1;
        if (text == "false" || text == "0") return 0;
        throw Error(ErrorKind::InvalidArgument, "Entrez true, false, 0 ou 1.");
    }
    if (type.kind == FrameType::Float) {
        bool ok = false;
        const double value = QLocale::c().toDouble(text, &ok);
        uint64_t bits = 0;
        if (ok && std::isfinite(value)) {
            if (type.size == 8) { std::memcpy(&bits, &value, sizeof(value)); return bits; }
            const auto scalar = static_cast<float>(value);
            if (type.size == 4 && std::isfinite(scalar)) {
                uint32_t raw;
                std::memcpy(&raw, &scalar, sizeof(scalar));
                return raw;
            }
        }
        throw Error(ErrorKind::InvalidArgument, "Entrez un nombre décimal fini.");
    }
    static const QRegularExpression literal(R"(^(?:-?[0-9]+|0[xX][0-9a-fA-F]+)$)");
    const bool hex = text.startsWith("0x", Qt::CaseInsensitive);
    const auto width = type.size * 8;
    if (!width || width > 64 || !literal.match(text).hasMatch())
        throw Error(ErrorKind::InvalidArgument, "Entrez un entier décimal ou hexadécimal.");
    const auto mask = width == 64 ? std::numeric_limits<uint64_t>::max() : (uint64_t(1) << width) - 1;
    bool ok = false;
    if (!hex && type.kind == FrameType::Signed) {
        const auto value = text.toLongLong(&ok, 10);
        const auto limit = uint64_t(1) << (width - 1);
        const auto minimum = width == 64 ? std::numeric_limits<int64_t>::min() : -static_cast<int64_t>(limit);
        if (ok && value >= minimum && (value < 0 || uint64_t(value) < limit)) return uint64_t(value) & mask;
    } else if (!text.startsWith('-')) {
        const auto value = text.toULongLong(&ok, hex ? 16 : 10);
        if (ok && value <= mask) return value;
    }
    throw Error(ErrorKind::InvalidArgument, "La valeur dépasse la taille du type.");
}

StackDebugInfo::StackDebugInfo(QObject* parent) : QObject(parent) {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    environment.remove("DEBUGINFOD_URLS");
    process_.setProcessEnvironment(environment);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        stop(); status_ = "Lecture DWARF interrompue"; if (updated) updated();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        stop(); status_ = "readelf indisponible"; if (updated) updated();
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus exit) {
            const bool parsing = deadline_.isActive();
            readOutput();
            if (!parsing || !deadline_.isActive()) return;
            deadline_.stop();
            if (code == 0 && exit == QProcess::NormalExit) parse();
            output_.clear();
            status_ = functions_.empty() ? "Aucune information DWARF de pile" : QString();
            if (updated) updated();
        });
}

StackDebugInfo::~StackDebugInfo() { stop(); }

void StackDebugInfo::stop() {
    deadline_.stop();
    if (process_.state() != QProcess::NotRunning) {
        const QSignalBlocker blocker(&process_);
        process_.kill();
        process_.waitForFinished(500);
    }
    if (process_.isOpen()) process_.readAllStandardOutput();
    output_.clear();
}

void StackDebugInfo::index(const QString& path, const QString& compiler) {
    stop();
    functions_.clear();
    rules_.clear();
    locations_.clear();
    status_ = "Aucun ELF de débogage";
    if (path.isEmpty()) return;
    const auto tool = findReadelf(compiler);
    if (tool.isEmpty()) { status_ = "readelf indisponible"; return; }
    status_ = "Lecture des informations DWARF…";
    deadline_.start(15000);
    process_.start(tool, {"--wide", "--debug-dump=info,loc,frames-interp,no-follow-links", QFileInfo(path).absoluteFilePath()});
}

void StackDebugInfo::readOutput() {
    output_ += process_.readAllStandardOutput();
    if (output_.size() <= outputLimit) return;
    stop();
    status_ = "Informations DWARF trop volumineuses";
    if (updated) updated();
}

void StackDebugInfo::parse() {
    static const QRegularExpression node(R"(^\s*<(\d+)><([0-9a-f]+)>:\s*Abbrev Number:\s*(?:0|[0-9]+ \(DW_TAG_([a-z_]+)\))\s*$)");
    static const QRegularExpression attr(R"(^\s*<[0-9a-f]+>\s+DW_AT_([a-z_]+)\s*:\s*(.*)$)");
    static const QRegularExpression fde(R"(\bFDE\b\s+cie=([0-9a-f]+)\s+pc=([0-9a-f]+)\.\.([0-9a-f]+))");
    static const QRegularExpression cfa(R"(^(?:r(\d+)|(sp))([+-]\d+)$)");
    static const QRegularExpression registerColumn(R"(^r(\d+)$)");
    static const QRegularExpression memorySlot(R"(^c([+-]\d+)$)");
    static const QRegularExpression returnReg(R"(\bra=(\d+)\b)");
    static const QRegularExpression locView(R"(^\s*([0-9a-f]+)\s+v[0-9a-f]+\s+v[0-9a-f]+\s+views at [0-9a-f]+ for:\s*$)");
    static const QRegularExpression locBase(R"(^\s*([0-9a-f]+)\s+[0-9a-f]+\s+\(base address\)\s*$)");
    static const QRegularExpression locRange(R"(^\s*(?:([0-9a-f]+)\s+)?([0-9a-f]+)\s+([0-9a-f]+)\s+\((DW_OP_.*)\)\s*$)");
    static const QRegularExpression locReference(R"(^(0x[0-9a-f]+|[0-9]+)\s+\(location list\)$)");
    QVector<Die> dies;
    QHash<uint32_t, int> offsets;
    QVector<int> parents;
    int current = -1;
    uint32_t ruleEnd = 0;
    int returnRegister = -1;
    QHash<uint32_t, int> returnRegisters;
    QVector<std::pair<int, bool>> savedColumns;
    bool inLocations = false;
    int64_t listOffset = -1;
    for (const auto& raw : output_.split('\n')) {
        const auto line = QString::fromUtf8(raw);
        if (line.startsWith("Contents of the ")) {
            inLocations = line.startsWith("Contents of the .debug_loc section") ||
                          line.startsWith("Contents of the .debug_loclists section");
            ruleEnd = 0;
            listOffset = -1;
            current = -1;
            continue;
        }
        if (inLocations) {
            if (line.contains("<End of list>")) { listOffset = -1; continue; }
            auto match = locView.match(line);
            if (!match.hasMatch()) match = locBase.match(line);
            if (match.hasMatch()) {
                if (listOffset < 0) listOffset = match.captured(1).toUInt(nullptr, 16);
                continue;
            }
            match = locRange.match(line);
            if (!match.hasMatch()) continue;
            if (listOffset < 0 && !match.captured(1).isEmpty()) listOffset = match.captured(1).toUInt(nullptr, 16);
            bool beginOk, endOk;
            const auto begin = match.captured(2).toUInt(&beginOk, 16);
            const auto end = match.captured(3).toUInt(&endOk, 16);
            if (listOffset >= 0 && beginOk && endOk && begin < end)
                locations_[static_cast<uint32_t>(listOffset)].push_back({begin, end, match.captured(4)});
            continue;
        }
        if (line.contains(" CIE ")) {
            const auto match = returnReg.match(line);
            bool offsetOk;
            const auto offset = line.simplified().section(' ', 0, 0).toUInt(&offsetOk, 16);
            if (offsetOk && match.hasMatch()) returnRegisters.insert(offset, match.captured(1).toInt());
            ruleEnd = 0;
            continue;
        }
        auto match = fde.match(line);
        if (match.hasMatch()) {
            returnRegister = returnRegisters.value(match.captured(1).toUInt(nullptr, 16), -1);
            ruleEnd = match.captured(3).toUInt(nullptr, 16);
            savedColumns.clear();
            continue;
        }
        if (ruleEnd) {
            const auto columns = line.simplified().split(' ');
            if (columns.size() < 2) continue;
            if (columns.front() == "LOC") {
                savedColumns.clear();
                for (int i = 2; i < columns.size(); ++i) {
                    const auto reg = registerColumn.match(columns[i]);
                    const int number = columns[i] == "ra" ? returnRegister : columns[i] == "lr" ? 14 :
                        reg.hasMatch() ? reg.captured(1).toInt() : -1;
                    savedColumns.push_back({number, number >= 0 && number == returnRegister});
                }
                continue;
            }
            bool ok = false;
            const auto at = columns[0].toUInt(&ok, 16);
            if (!ok || at >= ruleEnd) continue;
            const auto rule = cfa.match(columns[1]);
            Rule state{ruleEnd, 99, 0, {}};
            bool offsetOk = false;
            const auto offset = rule.captured(3).toInt(&offsetOk);
            if (rule.hasMatch() && offsetOk) {
                state.reg = rule.captured(2) == "sp" ? 13u : rule.captured(1).toUInt();
                state.offset = offset;
                if (columns.size() - 2 == savedColumns.size()) {
                    for (int i = 0; i < savedColumns.size(); ++i) {
                        const auto& column = savedColumns[i];
                        const auto slot = memorySlot.match(columns[i + 2]);
                        bool slotOk;
                        const auto displacement = slot.captured(1).toInt(&slotOk);
                        if (slotOk && column.first >= 0 && column.first < 16)
                            state.saved.push_back({static_cast<unsigned>(column.first), displacement, column.second});
                    }
                }
            }
            rules_[at] = state;
            continue;
        }
        match = node.match(line);
        if (match.hasMatch()) {
            const int depth = match.captured(1).toInt();
            if (depth > parents.size() || depth > 64) { current = -1; continue; }
            parents.resize(depth);
            current = -1;
            if (match.captured(3).isEmpty()) continue;
            current = dies.size();
            offsets.insert(match.captured(2).toUInt(nullptr, 16), current);
            dies.push_back({match.captured(3), parents.isEmpty() ? -1 : parents.back(), {}});
            parents.push_back(current);
        } else if (current >= 0) {
            match = attr.match(line);
            if (match.hasMatch())
                dies[current].attributes.insert(match.captured(1), match.captured(2).trimmed());
        }
    }
    int function = -1;
    uint32_t functionStart = 0, functionEnd = 0;
    for (int i = 0; i < dies.size(); ++i) {
        const auto& die = dies[i];
        if (die.tag == "subprogram") {
            function = range(die, functionStart, functionEnd) ? i : -1;
            if (function >= 0) functions_[functionStart] = {functionEnd, expression(die.attributes.value("frame_base")),
                resolveType(dies, offsets, reference(die, offsets)), {}};
            continue;
        }
        const bool argument = die.tag == "formal_parameter";
        if (function < 0 || (!argument && die.tag != "variable")) continue;
        uint32_t begin = functionStart, end = functionEnd;
        int scope = die.parent;
        while (scope > function && dies[scope].tag == "lexical_block") {
            uint32_t low, high;
            if (!range(dies[scope], low, high)) { scope = -1; break; }
            begin = std::max(begin, low);
            end = std::min(end, high);
            scope = dies[scope].parent;
        }
        const auto name = text(die, "name");
        if (scope == function && begin < end && !name.isEmpty()) {
            const auto list = locReference.match(text(die, "location"));
            const int64_t offset = list.hasMatch() ? int64_t(list.captured(1).toUInt(nullptr, 0)) : -1;
            functions_.at(functionStart).variables.push_back({name, argument ? QString("Argument") : QString("Locale"),
                expression(die.attributes.value("location")), resolveType(dies, offsets, reference(die, offsets)), begin, end, offset});
        }
    }
}

FrameDetails StackDebugInfo::frame(const DebugTarget& target, unsigned core, uint32_t pc, const StackFrame* caller) const {
    FrameDetails details;
    static const QRegularExpression location(R"(^DW_OP_(fbreg|breg(\d+)|reg(\d+))(?: \(r\d+\))?(?::\s*(-?\d+))?$)");
    const bool hasSp = caller ? caller->hasSp : target.registerAvailable(core, 13);
    const uint32_t sp = hasSp ? (caller ? caller->sp : target.registerValue(core, 13)) : 0;
    auto find = rules_.upper_bound(pc);
    if (find != rules_.begin()) {
        const auto& rule = (--find)->second;
        const bool cfaRegisterKnown = rule.reg == 13 ? hasSp : !caller && rule.reg < 16 && target.registerAvailable(core, rule.reg);
        if (pc < rule.end && cfaRegisterKnown) {
            const int64_t base = int64_t(rule.reg == 13 ? sp : target.registerValue(core, rule.reg)) + rule.offset;
            details.hasBase = base >= 0 && base <= std::numeric_limits<uint32_t>::max();
            if (details.hasBase) {
                details.base = static_cast<uint32_t>(base);
                for (const auto& slot : rule.saved) {
                    const int64_t address = base + slot.offset;
                    if (!hasSp || address < sp || address > 0xffffffffll ||
                        !stackMemory(target, static_cast<uint32_t>(address), 4)) continue;
                    FrameField field;
                    field.savedRegister = static_cast<int>(slot.reg);
                    field.savedFramePointer = !slot.isReturn && slot.reg == rule.reg && (slot.reg == 7 || slot.reg == 11);
                    const auto name = QString::fromStdString(registerName(slot.reg));
                    field.name = slot.isReturn ? "Adresse de retour sauvegardée" :
                        field.savedFramePointer ? QString("Ancien pointeur de trame (%1)").arg(name) :
                        QString("%1 sauvegardé").arg(name);
                    field.role = slot.isReturn ? "Retour" : "Registre sauvegardé";
                    field.type = frameType(slot.isReturn || field.savedFramePointer ? "void *" : "unsigned int");
                    field.type.kind = FrameType::Pointer; // A saved machine word is displayed in hex.
                    field.inMemory = field.editable = true;
                    field.address = static_cast<uint32_t>(address);
                    details.fields.push_back(field);
                }
            }
        }
    }
    if (caller) return details;
    auto fn = functions_.upper_bound(pc);
    if (fn == functions_.begin() || pc >= (--fn)->second.end) return details;
    const auto& function = fn->second;
    details.result = function.result;
    const auto frameBase = [&](const QString& op, int64_t& base) {
        if (op == "DW_OP_call_frame_cfa" && details.hasBase) { base = details.base; return true; }
        const auto match = location.match(op);
        if (!match.hasMatch() || (match.captured(2).isEmpty() && match.captured(3).isEmpty())) return false;
        const auto reg = match.captured(2).isEmpty() ? match.captured(3).toUInt() : match.captured(2).toUInt();
        if (reg >= 16 || !target.registerAvailable(core, reg)) return false;
        base = int64_t(target.registerValue(core, reg)) + match.captured(4).toInt();
        return true;
    };
    for (const auto& variable : function.variables) {
        if (pc < variable.begin || pc >= variable.end) continue;
        FrameField field;
        field.name = variable.name; field.role = variable.role; field.type = variable.type;
        auto op = variable.location;
        if (variable.locationList >= 0) {
            const auto found = locations_.find(static_cast<uint32_t>(variable.locationList));
            if (found != locations_.end()) for (const auto& interval : found->second) {
                if (pc >= interval.begin && pc < interval.end) {
                    if (!op.isEmpty() && op != interval.expression) { op.clear(); break; }
                    op = interval.expression;
                }
            }
        }
        const auto match = location.match(op);
        if (match.hasMatch()) {
            if (!match.captured(3).isEmpty() && match.captured(4).isEmpty()) {
                const auto reg = match.captured(3).toUInt();
                if (reg < 13 && !field.type.isArray && field.type.size && field.type.size <= 4 && target.registerAvailable(core, reg)) {
                    field.reg = static_cast<int>(reg);
                    field.editable = !field.type.readOnly && field.type.kind != FrameType::Unknown;
                }
            } else if (match.captured(3).isEmpty() && !match.captured(4).isEmpty()) {
                int64_t base = 0;
                const bool located = match.captured(1) == "fbreg" ?
                    frameBase(function.base, base) : frameBase(op, base);
                if (match.captured(1) == "fbreg") base += match.captured(4).toInt();
                const auto size = field.type.size * field.type.count;
                if (located && base >= 0 && base <= 0xffffffffll && hasSp && base >= sp &&
                    stackMemory(target, static_cast<uint32_t>(base), size)) {
                    const auto address = static_cast<uint32_t>(base);
                    const auto* simulator = dynamic_cast<const Debugger*>(&target);
                    // A fixed GCC spill location can describe the reserved slot before the store in the prologue.
                    // Addresses at or above the CFA can hold incoming stack arguments written by the caller.
                    if (variable.role == "Argument" && variable.locationList < 0 && details.hasBase &&
                        uint64_t(address) + size <= details.base && simulator && !simulator->stackBytesWritten(core, address, size)) {
                        field.rendered = "Copie en pile non observée";
                    } else {
                        field.address = address;
                        field.inMemory = true;
                        field.editable = !field.type.readOnly && field.type.size <= 8 && field.type.kind != FrameType::Unknown;
                    }
                }
            }
        }
        if (!field.inMemory && field.reg < 0 && field.rendered.isEmpty()) field.rendered = "Emplacement indisponible";
        details.fields.push_back(field);
        if (details.fields.size() >= 64) break;
    }
    return details;
}
