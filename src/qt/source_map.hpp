#pragma once

#include <QDateTime>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace picosource {

struct Location {
    QString path;
    int line = 0;
    bool valid() const { return line > 0 && !path.isEmpty(); }
    bool operator==(const Location& other) const { return path == other.path && line == other.line; }
    bool operator!=(const Location& other) const { return !(*this == other); }
};

QString executionColor();
QString selectionColor();

struct AssemblyInstruction {
    uint32_t address;
    QString text;
    Location source;
};
using AssemblyListing = std::vector<AssemblyInstruction>;

class SourceMap : public QObject {
public:
    explicit SourceMap(QObject* parent);
    ~SourceMap() override;
    void index(const QString& path, const QString& compiler);
    QString status() const { return status_; }
    bool hasLocations() const { return !sourceAddresses_.empty(); }
    QDateTime elfTimestamp() const { return timestamp_; }
    Location location(uint32_t address) const;
    bool addressFor(const Location& location, uint32_t& address) const;
    const std::vector<uint32_t>& assemblyAddresses(const Location& location) const;
    std::shared_ptr<const AssemblyListing> assembly() const { return instructions_; }
    QString assemblyStatus() const { return assemblyStatus_; }
    std::function<void()> updated;

private:
    enum Stage { Idle, Decode, Resolve, Assembly };
    struct Range {
        uint32_t end = 0;
        Location source;
    };
    Stage stage_ = Idle;
    QProcess process_;
    QTimer deadline_;
    QString path_, objdump_, addr2line_, status_, assemblyStatus_;
    QDateTime timestamp_;
    QByteArray output_, addresses_;
    std::map<uint32_t, Range> ranges_;
    std::map<std::pair<QString, int>, uint32_t> sourceAddresses_;
    std::shared_ptr<const AssemblyListing> instructions_;
    std::map<std::pair<QString, int>, std::vector<uint32_t>> assemblyAddresses_;
    void stop(bool preserveLines = false);
    void readOutput();
    void finished(int code, QProcess::ExitStatus status);
    void decode();
    void resolve();
    void startAssembly(const QString& sourceStatus);
    void readAssembly();
    void failIndex(const QString& message);
    void report(const QString& message);
};

}
