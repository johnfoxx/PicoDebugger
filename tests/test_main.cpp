#include "picodbg/debugger.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace picodbg;
namespace {
unsigned checks = 0;
void check(bool ok, const std::string& message) { ++checks; if (!ok) throw std::runtime_error(message); }
void throws(const std::function<void()>& body, ErrorKind kind) {
    ++checks;
    try { body(); } catch (const Error& e) { if (e.kind == kind) return; throw; }
    throw std::runtime_error("Expected exception");
}
void instruction(Memory& memory, uint16_t first, uint16_t second = 0) {
    memory.load(0x20000100, {uint8_t(first), uint8_t(first >> 8), uint8_t(second), uint8_t(second >> 8)});
}
void memoryTests() {
    Memory m;
    m.write(0x20000000, 0x12345678, 4);
    check(m.read(0x20000000, 1) == 0x78, "Little endian");
    check(m.read(0x20000002, 2) == 0x1234, "Halfword");
    check(m.peek(0x20000001, 2) == 0x3456, "Inspection unaligned bytes");
    throws([&] { m.read(0x20000001, 4); }, ErrorKind::Alignment);
    throws([&] { m.write(0x20042000, 0, 4); }, ErrorKind::BusFault);
    throws([&] { m.read(0x40054000, 4); }, ErrorKind::Unsupported);
    throws([&] { m.read(0x60000000, 4); }, ErrorKind::BusFault);
    check(m.read(0xd0000000, 4, 0) == 0 && m.read(0xd0000000, 4, 1) == 1, "CPUID independent");
    m.write(0x21010000, 0xaabbccdd, 4);
    check(m.read(0x20000004, 4) == 0xaabbccdd, "SRAM non-striped bank1 aliases striped word1");
    m.write(0x21000004, 0x11223344, 4);
    check(m.read(0x20000010, 4) == 0x11223344, "SRAM bank0 second word");
    m.write(0xd0000054, 123, 4, 0);
    check(m.peek(0xd0000058, 4, 1) == 123 && m.peek(0xd0000058, 4, 1) == 123, "FIFO inspection is pure");
    check(m.read(0xd0000058, 4, 1) == 123 && m.read(0xd0000050, 4, 1) == 2, "FIFO consumption");
    check(m.peek(0xd0000100, 4) == 1 && m.read(0xd0000100, 4) == 1 && m.read(0xd0000100, 4, 1) == 0, "Atomic spinlock, pure peek");
    m.write(0xd0000100, 0, 4, 1);
    check(m.read(0xd0000100, 4) == 1, "Spinlock release");
    for (unsigned i = 0; i < 9; ++i) m.write(0xd0000054, i, 4);
    check(m.peek(0xd0000050, 4) == 4, "FIFO overflow and not ready");
    m.write(0xd0000050, 4, 4);
    check(m.peek(0xd0000050, 4) == 0, "FIFO W1C");
    Memory pico2(Chip::RP2350);
    pico2.write(0x20081ffc, 0xfeedbeef, 4);
    check(pico2.read(0x20081ffc, 4, 1) == 0xfeedbeef, "RP2350 RAM size/shared");
    check(pico2.read(0xe000e010, 4) == 0, "RP2350 SysTick reset state");
    throws([&] { pico2.read(0xe000ed28, 4); }, ErrorKind::Unsupported); // Fault status remains unmodelled.
}
void arithmeticTests() {
    Memory m;
    Cpu c;
    const auto run = [&](uint16_t code, uint32_t r0, uint32_t r1, uint32_t flags = 0x01000000) {
        c.reset(0x20000101, 0x20041000); c.r[0] = r0; c.r[1] = r1; c.xpsr = flags;
        instruction(m, code); execute(c, m, 0);
    };
    run(0x1840, 0xffffffff, 1); // adds r0,r0,r1
    check(c.r[0] == 0 && (c.xpsr & 0xf0000000) == 0x60000000, "ADD unsigned carry/zero");
    run(0x1840, 0x7fffffff, 1);
    check(c.r[0] == 0x80000000 && (c.xpsr & 0xf0000000) == 0x90000000, "ADD signed overflow");
    run(0x1a40, 0, 1); // subs r0,r0,r1
    check(c.r[0] == 0xffffffff && (c.xpsr & 0xf0000000) == 0x80000000, "SUB borrow");
    run(0x1a40, 0x80000000, 1);
    check(c.r[0] == 0x7fffffff && (c.xpsr & 0xf0000000) == 0x30000000, "SUB signed overflow/carry");
    run(0x4148, 0xffffffff, 0, 0x21000000); // adcs r0,r1
    check(c.r[0] == 0 && (c.xpsr & 0x60000000) == 0x60000000, "ADC includes carry");
    run(0x4188, 0, 0); // sbcs r0,r1
    check(c.r[0] == 0xffffffff, "SBC subtracts inverted carry");
    run(0x4248, 0, 0x80000000); // rsbs r0,r1,#0
    check(c.r[0] == 0x80000000 && (c.xpsr & 0x10000000), "NEG min int overflow");
    run(0x4348, 0xffffffff, 2);
    check(c.r[0] == 0xfffffffe, "MUL wraps unsigned");
    run(0xba08, 0, 0x12345678);
    check(c.r[0] == 0x78563412, "REV");
    run(0xb248, 0, 0x80);
    check(c.r[0] == 0xffffff80, "SXTB");
    run(0x4687, 0x20000201, 0); // mov pc,r0
    check(c.r[15] == 0x20000200, "MOV PC Thumb alignment");
    for (uint32_t shift : {0u, 1u, 31u, 32u, 33u, 255u, 256u}) {
        run(0x4088, 0x80000001, shift, 0x21000000); // lsls r0,r1
        const auto amount = shift & 0xffu;
        check(c.r[0] == (amount < 32 ? uint32_t(uint64_t(0x80000001) << amount) : 0u), "LSL edge shift");
        run(0x4108, 0x80000000, shift); // asrs r0,r1
        const uint32_t expected = amount == 0 ? 0x80000000u : amount >= 32 ? 0xffffffffu : 0xffffffffu << (31u - amount);
        check(c.r[0] == expected, "ASR edge shift");
    }
    run(0x0808, 0, 0x80000000); // lsrs r0,r1,#32
    check(c.r[0] == 0 && (c.xpsr & 0x20000000), "LSR immediate zero encodes 32");
    for (unsigned flags = 0; flags < 16; ++flags) {
        c.xpsr = (flags << 28) | 0x01000000;
        check(c.condition(0) == bool(flags & 4), "EQ predicate");
        check(c.condition(8) == (bool(flags & 2) && !bool(flags & 4)), "HI predicate");
        check(c.condition(10) == (bool(flags & 8) == bool(flags & 1)), "GE predicate");
    }
}
void controlTests() {
    Memory m; Cpu c; c.reset(0x20000101, 0x20041000);
    instruction(m, 0xf000, 0xf802); // BL PC+4+4
    execute(c, m, 0);
    check(c.r[15] == 0x20000108 && c.r[14] == 0x20000105, "32-bit BL / LR");
    instruction(m, 0xb503); c.r[15] = 0x20000100; c.r[0] = 11; c.r[1] = 22;
    execute(c, m, 0); // push {r0,r1,lr}
    check(c.r[13] == 0x20040ff4 && m.peek(c.r[13], 4) == 11 && m.peek(c.r[13]+4, 4) == 22, "PUSH layout");
    instruction(m, 0xbd03); c.r[15] = 0x20000100; execute(c, m, 0);
    check(c.r[15] == 0x20000104 && c.r[13] == 0x20041000, "POP PC/SP");
    instruction(m, 0x4708); c.r[15] = 0x20000100; c.r[1] = 0x20000200;
    throws([&] { execute(c, m, 0); }, ErrorKind::BusFault);
    instruction(m, 0xf3ef, 0x8008); c.r[15] = 0x20000100; execute(c, m, 0);
    check(c.r[0] == c.msp, "MRS MSP");
    instruction(m, 0xf380, 0x8809); c.r[0] = 0x20040000; c.r[15] = 0x20000100; execute(c, m, 0);
    check(c.psp == 0x20040000, "MSR PSP");
    instruction(m, 0xf380, 0x8814); c.r[0] = 2; c.r[15] = 0x20000100; execute(c, m, 0);
    check(c.r[13] == c.psp && c.control == 2, "CONTROL SPSEL");
    instruction(m, 0xbf18); c.r[15] = 0x20000100;
    throws([&] { execute(c, m, 0); }, ErrorKind::Unsupported); // IT is not silently ignored.
}
void loadStoreTests() {
    Memory m; Cpu c;
    const auto run = [&](uint16_t code) { instruction(m, code); c.r[15] = 0x20000100; execute(c, m, 0); };
    c.reset(0x20000101, 0x20041000); c.r[1] = 0x20001000; c.r[2] = 4;
    m.write(0x20001000, 0xff80abcd, 4); m.write(0x20001004, 0x80000080, 4);
    run(0x6808); check(c.r[0] == 0xff80abcd, "LDR immediate");
    run(0x7808); check(c.r[0] == 0xcd, "LDRB zero extend");
    run(0x8808); check(c.r[0] == 0xabcd, "LDRH zero extend");
    run(0x5688); check(c.r[0] == 0xffffff80, "LDRSB register offset");
    c.r[0] = 0x12345678; run(0x7008);
    check(m.peek(0x20001000, 4) == 0xff80ab78, "STRB preserves neighbors");
    run(0x8008); check(m.peek(0x20001000, 4) == 0xff805678, "STRH preserves neighbors");
    c.r[0] = 11; c.r[1] = 22; c.r[2] = 0x20002000;
    run(0xc203); check(c.r[2] == 0x20002008 && m.peek(0x20002004, 4) == 22, "STMIA writeback");
    c.r[2] = 0x20002000; c.r[0] = c.r[1] = 0;
    run(0xca03); check(c.r[0] == 11 && c.r[1] == 22 && c.r[2] == 0x20002008, "LDMIA writeback");
}
void exceptionTests() {
    Debugger d; d.loadImage(demoImage());
    d.cpus[0].r[13] -= 4; d.cpus[0].syncSp();
    const auto sp = d.cpus[0].r[13];
    d.memory.load(0x10000200, {0x00, 0xdf}); // svc #0
    check(d.step(0).kind == StopKind::Step && (d.cpus[0].xpsr & 0x1ff) == 11, "SVC entry");
    check(!(d.cpus[0].r[13] & 7u) && (d.memory.peek(d.cpus[0].r[13] + 28, 4) & 0x200u), "Exception stack alignment flag");
    d.step(0); d.step(0);
    check(d.cpus[0].r[13] == sp && d.cpus[0].r[15] == 0x10000202, "SVC restores alignment and next PC");
    d.reset();
    d.memory.write(0xe000e014, 2, 4); d.memory.write(0xe000e010, 7, 4);
    d.step(0); d.step(0); d.step(0);
    check(d.memory.system(0).systickPending, "SysTick pending after virtual reload/decrements");
    const auto flag = d.memory.peek(0xe000e010, 4);
    check((flag & 0x10000) && d.memory.peek(0xe000e010, 4) == flag, "SysTick peek does not clear COUNTFLAG");
    d.memory.read(0xe000e010, 4); check(!(d.memory.peek(0xe000e010, 4) & 0x10000), "CPU read clears COUNTFLAG");
    d.step(0); check((d.cpus[0].xpsr & 0x1ff) == 15, "SysTick vector");
    d.reset();
    d.memory.load(0x10000240, {0x00, 0xde}); d.memory.load(0x10000200, {0x00, 0xde});
    d.step(0); d.step(0);
    check(d.terminalStop() && d.cpus[0].lockup, "HardFault inside HardFault locks up");
    d.reset(); d.memory.load(0x10000200, {0x20, 0xbf}); // WFE
    d.step(0); check(d.cpus[0].sleeping, "WFE sleeps without event");
    d.startCore(1, 0x10000221, 0x20042000); d.memory.load(0x10000220, {0x40, 0xbf}); // SEV
    d.step(1); d.step(0); check(!d.cpus[0].sleeping && !d.cpus[0].event, "SEV wakes WFE across cores");
    d.memory.write(0xd0000054, 42, 4, 0); d.memory.tick(1);
    check(d.memory.system(1).pending & (1u << 16), "FIFO level drives SIO core1 IRQ");
    Debugger m33(Chip::RP2350); m33.loadImage(demoImage()); m33.memory.load(0x10000200, {0x00, 0xde});
    check(m33.step(0).kind == StopKind::Unsupported, "M33 exception coverage is explicit");
}
void decoderTests() {
    Memory m;
    // Exhaustive *decoder robustness*, not an architectural conformance proof.
    // Every possible first halfword must produce a bounded, printable result.
    for (uint32_t word = 0; word <= 0xffff; ++word) {
        instruction(m, static_cast<uint16_t>(word), 0);
        const auto d = decode(m, 0x20000100, 0);
        check((d.size == 2 || d.size == 4) && !d.text.empty(), "Decoder totality");
    }
    Memory m33(Chip::RP2350); Cpu c; c.reset(0x20000101, 0x20041000);
    instruction(m33, 0xf241, 0x2034); // movw r0,#0x1234
    execute(c, m33, 0); check(c.r[0] == 0x1234, "M33 subset MOVW");
    c.r[15] = 0x20000100; instruction(m33, 0xf6ca, 0x30cd); // movt r0,#0xabcd
    execute(c, m33, 0); check(c.r[0] == 0xabcd1234, "M33 subset MOVT");
}
void debuggerTests() {
    Debugger d; d.loadImage(demoImage());
    check(d.cpus[0].instructions == 0 && d.cpus[1].held, "Load never starts execution");
    check(d.evaluate("(0xffffffff + 1) | 7") == 7, "Expression uint32 wrap");
    check(d.evaluate("2 + 3 * 4") == 14, "Expression precedence");
    check(d.evaluate("$pc") == 0x10000200, "Register expression");
    throws([&] { d.evaluate("read32(1"); }, ErrorKind::InvalidArgument);
    throws([&] { d.evaluate("1 << 32"); }, ErrorKind::InvalidArgument);
    throws([&] { d.evaluate("1 / 0"); }, ErrorKind::InvalidArgument);
    d.breakpoints.insert(0x10000204);
    check(d.run(100).kind == StopKind::Breakpoint && d.cpus[0].r[0] == 0, "Breakpoint before instruction");
    check(d.run(100).kind == StopKind::Breakpoint && d.cpus[0].r[0] == 1, "Continue skips only once and re-hits loop");
    d.breakpoints.clear(); d.reset();
    d.addWatchpoint(0x20000002, 1, false, true);
    check(d.run(100).kind == StopKind::Watchpoint && d.memory.peek(0x20000000, 4) == 1, "Overlapping watchpoint after store");
    d.clearWatchpoints(); d.reset();
    d.startCore(1, 0x10000221, 0x20042000);
    d.run(1000);
    check(d.cpus[0].instructions > 0 && d.cpus[1].instructions > 0, "Both cores scheduled");
    check(d.memory.peek(0x20000000, 4) == 5 && d.memory.peek(0x20000004, 4) == 10, "Shared memory / independent contexts");
    d.reset(); check(d.memory.peek(0x20000000, 4) == 0 && d.cpus[0].r[0] == 0, "Restart restores RAM/registers");
    d.memory.load(0x10000200, {0x00, 0xde}); // UDF
    check(d.step(0).kind == StopKind::Fault && (d.cpus[0].xpsr & 0x1ff) == 3, "UDF enters HardFault");
    check(d.memory.peek(d.cpus[0].r[13] + 24, 4) == 0x10000200, "Fault frame contains faulting PC");
    d.step(0); d.step(0); // handler BKPT then BX LR
    check((d.cpus[0].xpsr & 0x1ff) == 0 && d.cpus[0].r[15] == 0x10000200, "Exception return restores thread/PC");
    d.reset(); d.memory.load(0x10000200, {0x18, 0xbf});
    check(d.step(0).kind == StopKind::Unsupported && d.terminalStop() && d.cpus[0].r[15] == 0x10000200, "Unsupported freezes at instruction");
    const auto before = d.revision(); d.step(0); check(d.revision() == before, "Cannot run beyond incomplete semantics");
    d.reset();
    const auto pc = d.cpus[0].r[15];
    auto bad = demoImage(); bad.entry = 0; bad.hasVectors = false;
    throws([&] { d.loadImage(bad); }, ErrorKind::InvalidImage);
    check(d.cpus[0].r[15] == pc, "Bad image leaves session intact");
}
std::vector<uint8_t> elfFixture() {
    std::vector<uint8_t> bytes(0x104, 0);
    const auto put = [&](size_t at, uint32_t value, unsigned size) { for (unsigned i = 0; i < size; ++i) bytes.at(at + i) = uint8_t(value >> (8 * i)); };
    put(0, 0x464c457f, 4); bytes[4] = bytes[5] = bytes[6] = 1;
    put(16, 2, 2); put(18, 40, 2); put(20, 1, 4); put(24, 0x10000201, 4);
    put(28, 52, 4); put(40, 52, 2); put(42, 32, 2); put(44, 1, 2);
    put(52, 1, 4); put(56, 0x100, 4); put(60, 0x10000200, 4); put(64, 0x10000200, 4);
    put(68, 4, 4); put(72, 4, 4); put(76, 5, 4); put(80, 4, 4);
    put(0x100, 0xbe00202a, 4); return bytes;
}
void elfTests() {
    const auto bytes = elfFixture();
    Debugger d; d.loadImage(ElfImage::parse(bytes));
    d.step(0); check(d.cpus[0].r[0] == 42, "ELF32 ARM loads and executes");
    check(d.step(0).kind == StopKind::Breakpoint, "ELF BKPT stop");
    for (size_t n : {size_t(0), size_t(51), size_t(60), size_t(0x102)}) {
        std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>(n));
        throws([&] { ElfImage::parse(truncated); }, ErrorKind::InvalidImage);
    }
    auto corrupt = bytes; corrupt[5] = 2;
    throws([&] { ElfImage::parse(corrupt); }, ErrorKind::InvalidImage);
    corrupt = bytes; corrupt[18] = 243;
    throws([&] { ElfImage::parse(corrupt); }, ErrorKind::InvalidImage);
    corrupt = bytes; corrupt[56] = corrupt[57] = corrupt[58] = corrupt[59] = 0xff;
    throws([&] { ElfImage::parse(corrupt); }, ErrorKind::InvalidImage);
}
}
int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> suites = {
        {"memory", memoryTests}, {"arithmetic", arithmeticTests}, {"control", controlTests},
        {"load_store", loadStoreTests}, {"exceptions", exceptionTests}, {"decoder", decoderTests},
        {"debugger", debuggerTests}, {"elf", elfTests}
    };
    try {
        if (argc == 2) suites.at(argv[1])(); else for (const auto& suite : suites) suite.second();
        std::cout << checks << " checks passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n"; return 1; }
}
