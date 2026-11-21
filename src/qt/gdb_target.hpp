#pragma once
#include "picodbg/target.hpp"
#include "gdb_mi.hpp"
#include <QMap>
#include <QSet>
#include <map>

namespace picodbg {
struct ProbeOptions {
    QString gdb = "gdb-multiarch";
    bool launchOpenOcd = true;
    QString openOcd = "openocd", scripts, host = "127.0.0.1";
    unsigned port = 3333, speed = 1000;
    std::array<QString, 2> coreThreads; // Optional explicit mapping for external servers.
};
class GdbTarget : public QObject, public DebugTarget {
public:
    explicit GdbTarget(Board board, QObject* parent = nullptr);
    ~GdbTarget() override;
    void connectProbe(const ProbeOptions& options);
    void disconnectProbe();
    void poll();
    void refreshHardware();
    void download(bool ramEntry);
    bool connected() const { return connected_; }
    bool connecting() const { return connecting_; }
    QString connectionText() const;
    QString errorText() const { return error_; }
    QString elfPath() const { return elfPath_; }
    bool hardware() const override { return true; }
    bool busy() const override { return busy_ || connecting_; }
    bool executing() const override { return running_; }
    bool loaded() const override { return connected_ && verified_; }
    bool terminalStop() const override { return !loaded() || executionBlocked_; }
    uint64_t revision() const override { return revision_; }
    const BoardDescription& board() const override { return board_; }
    const ChipDescription& description() const override { return description_; }
    void load(const std::string& path) override;
    void loadImage(const ElfImage&) override;
    void reset() override;
    void startCore(unsigned core, uint32_t entry, uint32_t stack) override;
    Stop step(unsigned core) override;
    Stop run(size_t) override;
    void pause() override;
    Stop runFor(uint64_t, size_t = 1000) override;
    void advanceTime(uint64_t) override;
    uint64_t virtualTime() const override { return 0; }
    size_t injectUart(unsigned, const std::string&) override;
    std::string uartOutput(unsigned) const override { return {}; }
    bool registerAvailable(unsigned core, unsigned index) const override;
    uint32_t peek(uint32_t address, unsigned size, unsigned core = 0) const override;
    Instruction disassemble(uint32_t address, unsigned core) override;
    void readRegisterOnce(uint32_t address, unsigned core) const override;
    void addBreakpoint(uint32_t address) override;
    void removeBreakpoint(uint32_t address) override;
    void addWatchpoint(uint32_t address, uint32_t length, bool read, bool write) override;
    void removeWatchpoint(size_t index) override;
    void clearWatchpoints() override;
    const std::array<std::map<std::string, std::string>, 2>& extraRegisters() const { return extras_; }
    std::function<void(const QString&)> logMessage;
private:
    BoardDescription board_;
    ChipDescription description_;
    std::set<uint32_t> automaticRegisters_;
    ProbeOptions options_;
    mutable GdbMi mi_;
    QProcess server_;
    QTimer serverDeadline_, haltDeadline_;
    QByteArray serverOutput_;
    bool connected_ = false, connecting_ = false, verified_ = false, busy_ = false;
    bool running_ = false, snapshotNeeded_ = false, disconnecting_ = false, stoppingServer_ = false;
    bool gdbStarted_ = false, executionBlocked_ = false;
    QString error_, activity_, elfPath_;
    QByteArray elfHash_;
    mutable uint64_t revision_ = 0;
    uint64_t generation_ = 0;
    QStringList registerNames_;
    std::array<QString, 2> threads_;
    std::array<std::array<bool, 21>, 2> valid_{};
    std::array<std::map<std::string, std::string>, 2> extras_;
    struct Read { QByteArray bytes; QString error; bool pending = true; };
    mutable std::map<std::pair<unsigned, uint32_t>, Read> reads_;
    std::map<std::pair<unsigned, uint32_t>, Instruction> instructions_;
    std::map<std::pair<unsigned, uint32_t>, QString> disassemblyRequests_;
    std::map<uint32_t, QString> breakpointIds_;
    std::vector<QString> watchIds_;
    void launchGdb();
    void initialize();
    void notification(const MiRecord& record);
    void fail(const QString& message, bool lost = false);
    bool success(const MiRecord& record);
    void commands(QStringList commands, std::function<void()> done, int timeout = 8000);
    void requireStopped() const;
    void begin(const QString& activity);
    void invalidate();
    void snapshot();
    void readThreads();
    void readCpu(unsigned core);
    void finishSnapshot();
    void completeDisconnect();
    void stopServer();
    bool safeRegister(uint32_t address) const;
    bool ordinaryMemory(uint32_t address, unsigned size) const;
    void requestMemory(uint32_t address, unsigned length, unsigned core) const;
    QString threadOption(unsigned core) const;
    QString registerWrite(unsigned core, unsigned index, uint32_t value) const;
    QString monitor(const QString& command) const;
    static QByteArray memoryBytes(const MiRecord& record, uint32_t address, unsigned length);
};
}
