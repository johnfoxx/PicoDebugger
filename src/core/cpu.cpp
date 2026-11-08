#include "picodbg/cpu.hpp"
#include <limits>

namespace picodbg {
namespace {
const uint32_t N = 1u << 31, Z = 1u << 30, C = 1u << 29, V = 1u << 28;
void nz(Cpu& cpu, uint32_t value) {
    cpu.xpsr = (cpu.xpsr & ~(N | Z)) | (value & N) | (value == 0 ? Z : 0u);
}
void carry(Cpu& cpu, bool bit) { cpu.xpsr = (cpu.xpsr & ~C) | (bit ? C : 0u); }
uint32_t add(Cpu& cpu, uint32_t a, uint32_t b, bool c, bool flags) {
    const uint64_t wide = uint64_t(a) + b + (c ? 1u : 0u);
    const uint32_t result = static_cast<uint32_t>(wide);
    if (flags) {
        nz(cpu, result); carry(cpu, (wide >> 32) != 0);
        cpu.xpsr = (cpu.xpsr & ~V) | (((~(a ^ b) & (a ^ result)) >> 3) & V);
    }
    return result;
}
uint32_t signExtend(uint32_t value, unsigned bits) {
    const uint32_t sign = uint32_t(1) << (bits - 1);
    return (value ^ sign) - sign;
}
unsigned bitCount(uint32_t value) { unsigned n = 0; for (; value; value &= value - 1) ++n; return n; }
uint32_t shifted(Cpu& cpu, uint32_t value, uint32_t amount, Op op) {
    if (amount == 0) return value;
    switch (op) {
    case Op::Lsl:
        carry(cpu, amount <= 32 && ((value >> (32 - amount)) & 1u));
        return amount < 32 ? value << amount : 0;
    case Op::Lsr:
        carry(cpu, amount <= 32 && ((value >> (amount - 1)) & 1u));
        return amount < 32 ? value >> amount : 0;
    case Op::Asr:
        if (amount >= 32) { carry(cpu, value & N); return value & N ? 0xffffffffu : 0; }
        carry(cpu, (value >> (amount - 1)) & 1u);
        return (value >> amount) | (value & N ? ~(0xffffffffu >> amount) : 0u);
    case Op::Ror:
        amount %= 32;
        if (amount) value = (value >> amount) | (value << (32 - amount));
        carry(cpu, value & N); return value;
    default: return value;
    }
}
void exceptionReturn(Cpu& cpu, Memory& memory, unsigned core, uint32_t token) {
    if (token != 0xfffffff1u && token != 0xfffffff9u && token != 0xfffffffdu)
        throw Error(ErrorKind::BusFault, "EXC_RETURN invalide", token);
    if (!cpu.handlerMode()) throw Error(ErrorKind::BusFault, "EXC_RETURN hors handler", token);
    cpu.syncSp();
    const bool usePsp = token & 4u;
    const uint32_t sp = usePsp ? cpu.psp : cpu.msp;
    uint32_t frame[8];
    for (unsigned i = 0; i < 8; ++i) frame[i] = memory.read(sp + 4 * i, 4, core);
    if (!(frame[7] & 0x01000000u) || (frame[6] & 1u)) throw Error(ErrorKind::BusFault, "Contexte d'exception invalide", sp);
    if (((token & 8u) != 0) != ((frame[7] & 0x1ffu) == 0)) throw Error(ErrorKind::BusFault, "Mode de retour d'exception incohérent", sp);
    for (unsigned i = 0; i < 4; ++i) cpu.r[i] = frame[i];
    cpu.r[12] = frame[4]; cpu.r[14] = frame[5]; cpu.r[15] = frame[6];
    const uint32_t restoredSp = sp + 32 + ((frame[7] & 0x200u) ? 4u : 0u);
    cpu.xpsr = frame[7] & ~0x200u;
    if (usePsp) cpu.psp = restoredSp; else cpu.msp = restoredSp;
    if (!cpu.handlerMode()) cpu.control = (cpu.control & ~2u) | (usePsp ? 2u : 0u);
    cpu.r[13] = usePsp ? cpu.psp : cpu.msp;
}
void branchExchange(Cpu& cpu, Memory& memory, unsigned core, uint32_t address) {
    if ((address & 0xfffffff0u) == 0xfffffff0u) { exceptionReturn(cpu, memory, core, address); return; }
    if (!(address & 1u)) throw Error(ErrorKind::BusFault, "Branchement vers un état non-Thumb", address);
    cpu.r[15] = address & ~1u;
}
uint32_t systemValue(const Cpu& cpu, uint32_t sys) {
    switch (sys) {
    case 0: return cpu.xpsr & 0xf8000000u;
    case 1: return cpu.xpsr & 0xf80001ffu;
    case 2: return cpu.xpsr & 0xf8000000u;
    case 3: return cpu.xpsr & 0xf80001ffu;
    case 5: case 7: return cpu.xpsr & 0x1ffu;
    case 6: return 0; // EPSR bits read as zero through MRS.
    case 8: return cpu.msp;
    case 9: return cpu.psp;
    case 16: return cpu.primask;
    case 20: return cpu.control;
    default: throw Error(ErrorKind::Unsupported, "Registre système non émulé : " + std::to_string(sys));
    }
}
}
void Cpu::reset(uint32_t pc, uint32_t sp) {
    *this = Cpu(); r[15] = pc & ~1u; r[13] = msp = sp & ~3u; held = false;
}
void Cpu::syncSp() {
    r[13] &= ~3u;
    if (!handlerMode() && (control & 2u)) psp = r[13]; else msp = r[13];
}
bool Cpu::condition(unsigned code) const {
    const bool n = xpsr & N, z = xpsr & Z, c = xpsr & C, v = xpsr & V;
    switch (code) {
    case 0: return z; case 1: return !z; case 2: return c; case 3: return !c;
    case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
    case 8: return c && !z; case 9: return !c || z; case 10: return n == v;
    case 11: return n != v; case 12: return !z && n == v; case 13: return z || n != v;
    case 14: return true; default: return false;
    }
}
std::string Cpu::flags() const {
    return "N=" + std::to_string(bool(xpsr & N)) + " Z=" + std::to_string(bool(xpsr & Z)) +
        " C=" + std::to_string(bool(xpsr & C)) + " V=" + std::to_string(bool(xpsr & V)) +
        " T=" + std::to_string(bool(xpsr & 0x01000000u)) + " IPSR=" + std::to_string(xpsr & 0x1ffu);
}
bool enterException(Cpu& cpu, Memory& memory, unsigned core, unsigned number, uint32_t returnPc) {
    if (memory.description().chip == Chip::RP2350)
        throw Error(ErrorKind::Unsupported, "Entrée d'exception Cortex-M33/TrustZone non émulée", returnPc);
    if (cpu.lockup || (number == 3 && (cpu.xpsr & 0x1ffu) == 3)) { cpu.lockup = true; return false; }
    cpu.syncSp();
    const bool wasHandler = cpu.handlerMode(), wasPsp = !wasHandler && (cpu.control & 2u);
    const uint32_t oldSp = cpu.r[13];
    const uint32_t newSp = (oldSp - 32u) & ~7u;
    const uint32_t savedXpsr = (cpu.xpsr & ~0x200u) | ((oldSp & 4u) ? 0x200u : 0u);
    try {
        const uint32_t vector = memory.read(memory.system(core).vtor + number * 4u, 4, core);
        if (!(vector & 1u)) throw Error(ErrorKind::BusFault, "Vecteur d'exception non-Thumb", vector);
        const uint32_t frame[] = {cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[12], cpu.r[14], returnPc, savedXpsr};
        for (unsigned i = 0; i < 8; ++i) memory.write(newSp + 4u * i, frame[i], 4, core);
        if (wasPsp) cpu.psp = newSp; else cpu.msp = newSp;
        cpu.r[13] = cpu.msp;
        cpu.r[14] = wasHandler ? 0xfffffff1u : wasPsp ? 0xfffffffdu : 0xfffffff9u;
        cpu.xpsr = (cpu.xpsr & ~0x1ffu) | number;
        cpu.r[15] = vector & ~1u; cpu.sleeping = false;
        return true;
    } catch (const Error& e) {
        if (e.kind == ErrorKind::Unsupported) throw;
        if (number != 3) return enterException(cpu, memory, core, 3, returnPc);
        cpu.lockup = true; cpu.lastFault += "; échec d'empilement/vecteur HardFault : " + std::string(e.what());
        return false;
    }
}
StepResult execute(Cpu& cpu, Memory& memory, unsigned core) {
    StepResult result;
    if (cpu.held || cpu.lockup) return result;
    auto& s = memory.system(core);
    if (!cpu.primask && !cpu.handlerMode()) {
        unsigned exception = s.systickPending ? 15u : 0u;
        if (s.enabled & s.pending) {
            for (unsigned i = 0; i < 32; ++i) if ((s.enabled & s.pending) & (1u << i)) { exception = 16u + i; break; }
            if (s.systickPending) exception = 15; // Default equal priorities: lowest exception number wins.
        }
        if (exception) {
            enterException(cpu, memory, core, exception, cpu.r[15]);
            if (exception == 15) s.systickPending = false; else s.pending &= ~(1u << (exception - 16));
            result.exception = true; result.detail = "Entrée exception " + std::to_string(exception); return result;
        }
    }
    if (cpu.sleeping) {
        if ((cpu.sleepOnEvent && cpu.event) || (!cpu.sleepOnEvent && ((s.enabled & s.pending) || s.systickPending))) {
            cpu.sleeping = false; cpu.event = false;
        } else return result;
    }
    const uint32_t pc = cpu.r[15];
    if (pc & 1u) throw Error(ErrorKind::BusFault, "PC non aligné", pc);
    const Instruction d = decode(memory, pc, core, false);
    if (d.op == Op::Unsupported) throw Error(ErrorKind::Unsupported, "Instruction non émulée : " + d.text, pc);
    const auto reg = [&cpu, pc](unsigned index) { return index == 15 ? pc + 4u : cpu.r[index]; };
    const uint32_t a = reg(d.rn), b = d.immediateOperand ? d.immediate : reg(d.rm);
    cpu.r[15] = pc + d.size;
    uint32_t value = 0;
    bool write = false, logicalFlags = false;
    switch (d.op) {
    case Op::Lsl: case Op::Lsr: case Op::Asr: case Op::Ror:
        value = shifted(cpu, a, d.immediateOperand ? b : b & 0xffu, d.op); write = logicalFlags = true; break;
    case Op::Add: value = add(cpu, a, b, false, d.setFlags); write = true; break;
    case Op::Sub: value = add(cpu, a, ~b, true, d.setFlags); write = true; break;
    case Op::Cmp: add(cpu, a, ~b, true, true); break;
    case Op::Cmn: add(cpu, a, b, false, true); break;
    case Op::Adc: value = add(cpu, a, b, cpu.xpsr & C, true); write = true; break;
    case Op::Sbc: value = add(cpu, a, ~b, cpu.xpsr & C, true); write = true; break;
    case Op::Rsb: value = add(cpu, 0, ~b, true, true); write = true; break;
    case Op::Mov: value = b; write = true; logicalFlags = d.setFlags; break;
    case Op::And: value = a & b; write = logicalFlags = true; break;
    case Op::Eor: value = a ^ b; write = logicalFlags = true; break;
    case Op::Orr: value = a | b; write = logicalFlags = true; break;
    case Op::Bic: value = a & ~b; write = logicalFlags = true; break;
    case Op::Mvn: value = ~b; write = logicalFlags = true; break;
    case Op::Mul: value = a * b; write = logicalFlags = true; break;
    case Op::Tst: nz(cpu, a & b); break;
    case Op::Adr: value = (a & ~3u) + d.immediate; write = true; break;
    case Op::AddSp: cpu.r[13] += d.signExtend ? 0u - d.immediate : d.immediate; break;
    case Op::Load: {
        const uint32_t address = (d.rn == 15 ? a & ~3u : a) + b;
        value = memory.read(address, d.width, core);
        if (d.signExtend) value = signExtend(value, d.width * 8);
        write = true; break;
    }
    case Op::Store: memory.write(a + b, cpu.r[d.rd], d.width, core); break;
    case Op::Push: case Op::Pop: case Op::Ldm: case Op::Stm: {
        if (!d.immediate) throw Error(ErrorKind::Unsupported, "Liste de registres vide / instruction imprévisible", pc);
        const unsigned count = bitCount(d.immediate);
        uint32_t address = d.op == Op::Push ? cpu.r[13] - count * 4 : d.op == Op::Pop ? cpu.r[13] : a;
        if (address & 3u) throw Error(ErrorKind::Alignment, "Transfert multiple non aligné", address);
        const uint32_t end = address + count * 4;
        uint32_t newPc = 0; bool loadPc = false;
        for (unsigned i = 0; i < 16; ++i) if (d.immediate & (1u << i)) {
            if (d.op == Op::Push || d.op == Op::Stm) memory.write(address, cpu.r[i], 4, core);
            else {
                const uint32_t loaded = memory.read(address, 4, core);
                if (i == 15) { newPc = loaded; loadPc = true; } else cpu.r[i] = loaded;
            }
            address += 4;
        }
        if (d.op == Op::Push) cpu.r[13] -= count * 4;
        else if (d.op == Op::Pop) cpu.r[13] = end;
        else if (d.op == Op::Stm || !(d.immediate & (1u << d.rn))) cpu.r[d.rn] = end;
        if (loadPc) branchExchange(cpu, memory, core, newPc);
        break;
    }
    case Op::Sxtb: value = signExtend(b & 0xffu, 8); write = true; break;
    case Op::Sxth: value = signExtend(b & 0xffffu, 16); write = true; break;
    case Op::Uxtb: value = b & 0xffu; write = true; break;
    case Op::Uxth: value = b & 0xffffu; write = true; break;
    case Op::Rev: value = ((b & 0xffu) << 24) | ((b & 0xff00u) << 8) | ((b >> 8) & 0xff00u) | (b >> 24); write = true; break;
    case Op::Rev16: value = ((b & 0x00ff00ffu) << 8) | ((b & 0xff00ff00u) >> 8); write = true; break;
    case Op::Revsh: value = signExtend(((b & 0xffu) << 8) | ((b >> 8) & 0xffu), 16); write = true; break;
    case Op::Branch: cpu.r[15] = d.immediate; break;
    case Op::CondBranch: if (cpu.condition(d.condition)) cpu.r[15] = d.immediate; break;
    case Op::Cbz: if ((a != 0) == d.signExtend) cpu.r[15] = d.immediate; break;
    case Op::Bl: cpu.r[14] = (pc + 4u) | 1u; cpu.r[15] = d.immediate; break;
    case Op::Bx: branchExchange(cpu, memory, core, b); break;
    case Op::Blx: cpu.r[14] = (pc + 2u) | 1u; branchExchange(cpu, memory, core, b); break;
    case Op::Svc:
        if (cpu.primask || cpu.handlerMode()) throw Error(ErrorKind::BusFault, "SVC masqué ou imbriqué", pc);
        enterException(cpu, memory, core, 11, pc + 2u); result.exception = true; result.detail = "SVC"; break;
    case Op::Bkpt: result.breakpoint = true; result.detail = "BKPT " + std::to_string(d.immediate); break;
    case Op::Udf: throw Error(ErrorKind::BusFault, "Instruction UDF (HardFault)", pc);
    case Op::Cps: cpu.primask = d.immediate; break;
    case Op::Mrs:
        if (d.rd >= 13) throw Error(ErrorKind::Unsupported, "Destination MRS réservée", pc);
        cpu.syncSp(); value = systemValue(cpu, d.immediate); write = true; break;
    case Op::Msr:
        cpu.syncSp();
        switch (d.immediate) {
        case 0: case 1: case 2: case 3: {
            const uint32_t mask = memory.description().chip == Chip::RP2040 ? 0xf0000000u : 0xf8000000u;
            cpu.xpsr = (cpu.xpsr & ~mask) | (a & mask); break;
        }
        case 8: cpu.msp = a & ~3u; break;
        case 9: cpu.psp = a & ~3u; break;
        case 16: cpu.primask = a & 1u; break;
        case 20:
            if (a & ~2u) throw Error(ErrorKind::Unsupported, "CONTROL : mode non privilégié/extension non émulé", pc);
            if (!cpu.handlerMode()) cpu.control = a & 2u;
            break;
        default: throw Error(ErrorKind::Unsupported, "MSR système non émulé", pc);
        }
        cpu.r[13] = !cpu.handlerMode() && (cpu.control & 2u) ? cpu.psp : cpu.msp; break;
    case Op::Movw: case Op::Movt:
        if (d.rd >= 13) throw Error(ErrorKind::Unsupported, "Destination MOVW/MOVT réservée", pc);
        value = d.op == Op::Movw ? d.immediate : (cpu.r[d.rd] & 0xffffu) | (d.immediate << 16); write = true; break;
    case Op::Wfi: cpu.sleeping = true; cpu.sleepOnEvent = false; break;
    case Op::Wfe:
        if (cpu.event) cpu.event = false; else { cpu.sleeping = true; cpu.sleepOnEvent = true; } break;
    case Op::Sev: result.sendEvent = true; break;
    case Op::Barrier: case Op::Nop: break; // Sequential memory model: no buffered writes.
    case Op::Unsupported: break;
    }
    if (logicalFlags) nz(cpu, value);
    if (write) {
        if (d.rd == 15) cpu.r[15] = value & ~1u; else cpu.r[d.rd] = value;
    }
    cpu.syncSp(); ++cpu.instructions;
    return result;
}
}
