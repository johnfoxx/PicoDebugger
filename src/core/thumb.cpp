#include "picodbg/thumb.hpp"
#include <sstream>

namespace picodbg {
namespace {
uint32_t extend(uint32_t value, unsigned bits) {
    const uint32_t sign = uint32_t(1) << (bits - 1);
    return (value ^ sign) - sign;
}
std::string imm(uint32_t value) { return "#" + std::to_string(value); }
std::string regs(uint32_t mask) {
    std::string s = "{";
    for (unsigned n = 0; n < 16; ++n) if (mask & (1u << n)) { if (s.size() > 1) s += ", "; s += registerName(n); }
    return s + "}";
}
std::string special(uint32_t n) {
    switch (n) {
    case 0: return "apsr"; case 1: return "iapsr"; case 2: return "eapsr"; case 3: return "xpsr";
    case 5: return "ipsr"; case 6: return "epsr"; case 7: return "iepsr";
    case 8: return "msp"; case 9: return "psp"; case 16: return "primask"; case 20: return "control";
    default: return "sysreg_" + std::to_string(n);
    }
}
}
std::string registerName(unsigned index) {
    if (index == 13) return "sp";
    if (index == 14) return "lr";
    if (index == 15) return "pc";
    return "r" + std::to_string(index);
}
Instruction decode(Memory& memory, uint32_t address, unsigned core, bool inspect) {
    Instruction d; d.address = address;
    const uint32_t h = inspect ? memory.peek(address, 2, core) : memory.read(address, 2, core, Access::Execute);
    d.raw = h;
    std::string mnemonic;
    d.rd = h & 7u; d.rn = (h >> 3) & 7u; d.rm = (h >> 6) & 7u;
    if ((h & 0xf800u) >= 0xe800u) {
        d.size = 4;
        const uint32_t low = inspect ? memory.peek(address + 2, 2, core) : memory.read(address + 2, 2, core, Access::Execute);
        d.raw |= low << 16;
        if ((h & 0xf800u) == 0xf000u && (low & 0xd000u) == 0xd000u) {
            const uint32_t s = (h >> 10) & 1u;
            const uint32_t i1 = 1u ^ ((low >> 13) & 1u) ^ s, i2 = 1u ^ ((low >> 11) & 1u) ^ s;
            d.op = Op::Bl;
            d.immediate = address + 4 + extend((s << 24) | (i1 << 23) | (i2 << 22) | ((h & 0x3ffu) << 12) | ((low & 0x7ffu) << 1), 25);
            d.text = "bl " + hex(d.immediate); return d;
        }
        if (h == 0xf3efu && (low & 0xf000u) == 0x8000u) {
            d.op = Op::Mrs; d.rd = (low >> 8) & 15u; d.immediate = low & 0xffu;
            d.text = "mrs " + registerName(d.rd) + ", " + special(d.immediate); return d;
        }
        if ((h & 0xfff0u) == 0xf380u && (low & 0xff00u) == 0x8800u) {
            d.op = Op::Msr; d.rn = h & 15u; d.immediate = low & 0xffu;
            d.text = "msr " + special(d.immediate) + ", " + registerName(d.rn); return d;
        }
        if (h == 0xf3bfu && (low == 0x8f4fu || low == 0x8f5fu || low == 0x8f6fu)) {
            d.op = Op::Barrier;
            d.text = low == 0x8f4f ? "dsb sy" : low == 0x8f5f ? "dmb sy" : "isb sy"; return d;
        }
        if (memory.description().chip == Chip::RP2350 && (h & 0xfbf0u) == 0xf240u) {
            d.op = Op::Movw;
        } else if (memory.description().chip == Chip::RP2350 && (h & 0xfbf0u) == 0xf2c0u) {
            d.op = Op::Movt;
        }
        if (d.op == Op::Movw || d.op == Op::Movt) {
            if (low & 0x8000u) d.op = Op::Unsupported;
            else {
                d.rd = (low >> 8) & 15u;
                d.immediate = ((h & 15u) << 12) | (((h >> 10) & 1u) << 11) | (((low >> 12) & 7u) << 8) | (low & 0xffu);
                d.text = std::string(d.op == Op::Movw ? "movw " : "movt ") + registerName(d.rd) + ", " + imm(d.immediate); return d;
            }
        }
        d.text = ".inst.w " + hex(h, 4) + " " + hex(low, 4) + " ; non pris en charge"; return d;
    }
    if ((h & 0xf800u) < 0x1800u) {
        static const Op ops[] = {Op::Lsl, Op::Lsr, Op::Asr};
        static const char* names[] = {"lsls", "lsrs", "asrs"};
        const unsigned op = (h >> 11) & 3u;
        d.op = ops[op]; mnemonic = names[op]; d.immediateOperand = true; d.immediate = (h >> 6) & 31u;
        if (op && !d.immediate) d.immediate = 32;
    } else if ((h & 0xf800u) == 0x1800u) {
        d.op = (h & 0x200u) ? Op::Sub : Op::Add; mnemonic = d.op == Op::Sub ? "subs" : "adds";
        d.immediateOperand = h & 0x400u; d.immediate = d.rm;
    } else if ((h & 0xe000u) == 0x2000u) {
        static const Op ops[] = {Op::Mov, Op::Cmp, Op::Add, Op::Sub};
        static const char* names[] = {"movs", "cmp", "adds", "subs"};
        const unsigned op = (h >> 11) & 3u;
        d.op = ops[op]; mnemonic = names[op]; d.rd = (h >> 8) & 7u; d.rn = d.rd; d.immediateOperand = true; d.immediate = h & 0xffu;
    } else if ((h & 0xfc00u) == 0x4000u) {
        static const Op ops[] = {Op::And, Op::Eor, Op::Lsl, Op::Lsr, Op::Asr, Op::Adc, Op::Sbc, Op::Ror, Op::Tst, Op::Rsb, Op::Cmp, Op::Cmn, Op::Orr, Op::Mul, Op::Bic, Op::Mvn};
        static const char* names[] = {"ands", "eors", "lsls", "lsrs", "asrs", "adcs", "sbcs", "rors", "tst", "rsbs", "cmp", "cmn", "orrs", "muls", "bics", "mvns"};
        const unsigned op = (h >> 6) & 15u;
        d.op = ops[op]; mnemonic = names[op]; d.rm = d.rn; d.rn = d.rd;
    } else if ((h & 0xfc00u) == 0x4400u) {
        const unsigned op = (h >> 8) & 3u;
        d.rd = (h & 7u) | ((h >> 4) & 8u); d.rn = d.rd; d.rm = (h >> 3) & 15u;
        d.setFlags = false;
        if (op == 3) { d.op = h & 0x80u ? Op::Blx : Op::Bx; d.text = std::string(d.op == Op::Blx ? "blx " : "bx ") + registerName(d.rm); return d; }
        d.op = op == 0 ? Op::Add : op == 1 ? Op::Cmp : Op::Mov;
        mnemonic = op == 0 ? "add" : op == 1 ? "cmp" : "mov";
        d.setFlags = op == 1;
    } else if ((h & 0xf800u) == 0x4800u) {
        d.op = Op::Load; d.rd = (h >> 8) & 7u; d.rn = 15; d.immediateOperand = true; d.immediate = (h & 0xffu) * 4;
    } else if ((h & 0xf000u) == 0x5000u) {
        const unsigned op = (h >> 9) & 7u;
        d.op = op < 3 ? Op::Store : Op::Load;
        static const unsigned widths[] = {4, 2, 1, 1, 4, 2, 1, 2};
        d.width = widths[op]; d.signExtend = op == 3 || op == 7;
    } else if ((h & 0xe000u) == 0x6000u || (h & 0xf000u) == 0x8000u) {
        d.op = (h & 0x800u) ? Op::Load : Op::Store;
        d.width = (h & 0xf000u) == 0x8000u ? 2u : (h & 0x1000u) ? 1u : 4u;
        d.immediateOperand = true; d.immediate = ((h >> 6) & 31u) * d.width;
    } else if ((h & 0xf000u) == 0x9000u) {
        d.op = h & 0x800u ? Op::Load : Op::Store; d.rd = (h >> 8) & 7u; d.rn = 13;
        d.immediateOperand = true; d.immediate = (h & 0xffu) * 4;
    } else if ((h & 0xf000u) == 0xa000u) {
        d.op = Op::Adr; d.rd = (h >> 8) & 7u; d.rn = h & 0x800u ? 13u : 15u; d.immediate = (h & 0xffu) * 4;
        d.text = "add " + registerName(d.rd) + ", " + registerName(d.rn) + ", " + imm(d.immediate); return d;
    } else if ((h & 0xff00u) == 0xb000u) {
        d.op = Op::AddSp; d.immediate = (h & 0x7fu) * 4; d.signExtend = h & 0x80u;
        d.text = std::string(d.signExtend ? "sub sp, " : "add sp, ") + imm(d.immediate); return d;
    } else if ((h & 0xf500u) == 0xb100u && memory.description().chip == Chip::RP2350) {
        d.op = Op::Cbz; d.rn = h & 7u; d.signExtend = h & 0x800u;
        d.immediate = address + 4 + ((h >> 3) & 31u) * 2 + ((h >> 9) & 1u) * 64;
        d.text = std::string(d.signExtend ? "cbnz " : "cbz ") + registerName(d.rn) + ", " + hex(d.immediate); return d;
    } else if ((h & 0xff00u) == 0xb200u) {
        static const Op ops[] = {Op::Sxth, Op::Sxtb, Op::Uxth, Op::Uxtb};
        static const char* names[] = {"sxth", "sxtb", "uxth", "uxtb"};
        const unsigned op = (h >> 6) & 3u;
        d.op = ops[op]; d.rm = d.rn; d.text = std::string(names[op]) + " " + registerName(d.rd) + ", " + registerName(d.rm); return d;
    } else if ((h & 0xf600u) == 0xb400u) {
        d.op = h & 0x800u ? Op::Pop : Op::Push; d.immediate = h & 0xffu;
        if (h & 0x100u) d.immediate |= d.op == Op::Push ? 1u << 14 : 1u << 15;
        d.text = std::string(d.op == Op::Push ? "push " : "pop ") + regs(d.immediate); return d;
    } else if ((h & 0xffefu) == 0xb662u) {
        d.op = Op::Cps; d.immediate = (h >> 4) & 1u; d.text = d.immediate ? "cpsid i" : "cpsie i"; return d;
    } else if ((h & 0xff00u) == 0xba00u) {
        const unsigned op = (h >> 6) & 3u;
        if (op != 2) { d.op = op == 0 ? Op::Rev : op == 1 ? Op::Rev16 : Op::Revsh; d.rm = d.rn; d.text = std::string(op == 0 ? "rev " : op == 1 ? "rev16 " : "revsh ") + registerName(d.rd) + ", " + registerName(d.rm); return d; }
    } else if ((h & 0xff00u) == 0xbe00u) {
        d.op = Op::Bkpt; d.immediate = h & 0xffu; d.text = "bkpt " + imm(d.immediate); return d;
    } else if ((h & 0xff00u) == 0xbf00u && !(h & 15u)) {
        switch ((h >> 4) & 15u) {
        case 0: d.op = Op::Nop; d.text = "nop"; break;
        case 1: d.op = Op::Nop; d.text = "yield"; break;
        case 2: d.op = Op::Wfe; d.text = "wfe"; break;
        case 3: d.op = Op::Wfi; d.text = "wfi"; break;
        case 4: d.op = Op::Sev; d.text = "sev"; break;
        default: d.text = "hint non pris en charge"; break;
        }
        return d;
    } else if ((h & 0xf000u) == 0xc000u) {
        d.op = h & 0x800u ? Op::Ldm : Op::Stm; d.rn = (h >> 8) & 7u; d.immediate = h & 0xffu;
        d.text = std::string(d.op == Op::Ldm ? "ldmia " : "stmia ") + registerName(d.rn) + "!, " + regs(d.immediate); return d;
    } else if ((h & 0xf000u) == 0xd000u) {
        d.condition = (h >> 8) & 15u; d.immediate = h & 0xffu;
        if (d.condition == 15) { d.op = Op::Svc; d.text = "svc " + imm(d.immediate); }
        else if (d.condition == 14) { d.op = Op::Udf; d.text = "udf " + imm(d.immediate); }
        else {
            static const char* names[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"};
            d.op = Op::CondBranch; d.immediate = address + 4 + extend((h & 0xffu) << 1, 9);
            d.text = std::string("b") + names[d.condition] + " " + hex(d.immediate);
        }
        return d;
    } else if ((h & 0xf800u) == 0xe000u) {
        d.op = Op::Branch; d.immediate = address + 4 + extend((h & 0x7ffu) << 1, 12);
        d.text = "b " + hex(d.immediate); return d;
    }
    if (d.op == Op::Load || d.op == Op::Store) {
        mnemonic = d.op == Op::Load ? "ldr" : "str";
        if (d.signExtend) mnemonic += "s";
        if (d.width == 1) mnemonic += "b";
        if (d.width == 2) mnemonic += "h";
        d.text = mnemonic + " " + registerName(d.rd) + ", [" + registerName(d.rn) + ", " + (d.immediateOperand ? imm(d.immediate) : registerName(d.rm)) + "]";
    } else if (d.op == Op::Unsupported) {
        d.text = ".hword " + hex(h, 4) + " ; non pris en charge";
    } else {
        d.text = mnemonic + " " + registerName(d.rd) + ", ";
        if (d.op != Op::Mov && d.op != Op::Cmp && d.op != Op::Mvn && d.op != Op::Tst && d.op != Op::Cmn && d.op != Op::Rsb)
            d.text += registerName(d.rn) + ", ";
        d.text += d.immediateOperand ? imm(d.immediate) : registerName(d.rm);
        if (d.op == Op::Rsb) d.text += ", #0";
    }
    return d;
}
}
