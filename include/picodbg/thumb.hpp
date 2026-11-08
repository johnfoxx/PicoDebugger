#pragma once
#include "picodbg/memory.hpp"

namespace picodbg {
enum class Op {
    Unsupported, Lsl, Lsr, Asr, Add, Sub, Mov, Cmp, And, Eor, Adc, Sbc,
    Ror, Tst, Rsb, Cmn, Orr, Mul, Bic, Mvn, Bx, Blx, Bl,
    Load, Store, Adr, AddSp, Push, Pop, Stm, Ldm,
    Sxtb, Sxth, Uxtb, Uxth, Rev, Rev16, Revsh,
    Branch, CondBranch, Svc, Bkpt, Udf, Nop, Wfi, Wfe, Sev,
    Cps, Mrs, Msr, Barrier, Movw, Movt, Cbz
};
struct Instruction {
    Op op = Op::Unsupported;
    uint32_t address = 0, raw = 0, immediate = 0;
    unsigned size = 2, rd = 0, rn = 0, rm = 0;
    unsigned width = 4, condition = 14;
    bool immediateOperand = false, setFlags = true, signExtend = false;
    std::string text;
};
Instruction decode(Memory& memory, uint32_t address, unsigned core, bool inspect = true);
std::string registerName(unsigned index);
}
