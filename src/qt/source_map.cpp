#include "source_map.hpp"
#include "window_theme.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <algorithm>

namespace picosource {
namespace {
constexpr int outputLimit = 32 * 1024 * 1024;
constexpr int assemblyOutputLimit = 96 * 1024 * 1024;
constexpr size_t maxRanges = 200000;

QString binutil(const QString& compiler, const QString& name) {
    QStringList candidates{"arm-none-eabi-" + name, name};
    if (!compiler.isEmpty()) {
        const QFileInfo info(compiler);
        const auto file = info.fileName();
        const auto gcc = file.indexOf("gcc");
        if (gcc >= 0) {
            const auto suffix = file.endsWith(".exe") ? QString(".exe") : QString();
            candidates.prepend(info.dir().filePath(file.left(gcc) + name + suffix));
        }
    }
    for (const auto& candidate : candidates) {
        const auto path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty()) return path;
    }
    return {};
}
}

QString executionColor() { return picoThemeColor("#234d57").name(); }
QString selectionColor() { return picoThemeColor("#514070").name(); }

SourceMap::SourceMap(QObject* parent) : QObject(parent) {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    environment.remove("DEBUGINFOD_URLS");
    process_.setProcessEnvironment(environment);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        failIndex(stage_ == Assembly ? "Désassemblage de l'ELF interrompu par le délai."
                                     : "Lecture des lignes DWARF interrompue par le délai.");
    });
    connect(&process_, &QProcess::started, this, [this] {
        if (stage_ == Resolve) process_.write(addresses_);
        process_.closeWriteChannel();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            failIndex("Outil Binutils indisponible pour cet ELF.");
        }
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &SourceMap::finished);
}

SourceMap::~SourceMap() {
    stop();
}

void SourceMap::stop(bool preserveLines) {
    stage_ = Idle;
    deadline_.stop();
    if (process_.state() != QProcess::NotRunning) {
        const QSignalBlocker signalsBlocked(&process_);
        process_.kill();
        process_.waitForFinished(500);
    }
    if (process_.isOpen()) {
        process_.readAllStandardOutput();
        process_.readAllStandardError();
    }
    output_.clear();
    addresses_.clear();
    if (!preserveLines) {
        ranges_.clear();
        sourceAddresses_.clear();
    }
    instructions_.reset();
    assemblyAddresses_.clear();
}

void SourceMap::index(const QString& file, const QString& compiler) {
    const QFileInfo info(file);
    const auto path = file.isEmpty() ? QString() : info.absoluteFilePath();
    if (path.isEmpty() && path_.isEmpty() && !status_.isEmpty()) return;
    stop();
    path_ = path;
    timestamp_ = info.lastModified();
    assemblyStatus_.clear();
    if (path_.isEmpty()) { report("Ouvrir un ELF compilé avec les informations de débogage (-g)."); return; }
    objdump_ = binutil(compiler, "objdump");
    addr2line_ = binutil(compiler, "addr2line");
    if (objdump_.isEmpty()) {
        assemblyStatus_ = "Installer Binutils ARM pour désassembler l'ELF.";
        report("Installer Binutils ARM pour lire les lignes DWARF de l'ELF.");
        return;
    }
    if (addr2line_.isEmpty()) {
        startAssembly("addr2line indisponible pour associer les lignes source.");
        return;
    }
    stage_ = Decode;
    status_ = "Lecture des lignes DWARF…";
    assemblyStatus_ = "Préparation du désassemblage…";
    deadline_.start(15000);
    process_.start(objdump_, {"--wide", "--dwarf=decodedline,no-follow-links", path_});
}

void SourceMap::readOutput() {
    output_ += process_.readAllStandardOutput();
    if (output_.size() > (stage_ == Assembly ? assemblyOutputLimit : outputLimit)) {
        failIndex(stage_ == Assembly ? "Désassemblage de l'ELF trop volumineux."
                                     : "Table de lignes DWARF trop volumineuse.");
    }
}

void SourceMap::finished(int code, QProcess::ExitStatus state) {
    const auto completed = stage_;
    if (completed == Idle) return;
    readOutput();
    if (stage_ != completed) return;
    if (code != 0 || state != QProcess::NormalExit) {
        failIndex(stage_ == Assembly ? "Binutils n'a pas pu désassembler l'ELF."
                                     : "Binutils n'a pas pu lire les lignes DWARF.");
        return;
    }
    if (stage_ == Decode) decode();
    else if (stage_ == Resolve) resolve();
    else readAssembly();
}

void SourceMap::decode() {
    // End-of-sequence rows bound a range. Multiple rows at the same address
    // have no width; addr2line chooses the source for that instruction.
    static const QRegularExpression row(R"(^.+?\s+(\d+|-)\s+(0x[0-9a-fA-F]+)(?:\s+.*)?$)");
    uint32_t previous = 0;
    bool havePrevious = false;
    for (const auto& line : output_.split('\n')) {
        const auto value = QString::fromLatin1(line).trimmed();
        const auto match = row.match(value);
        if (!match.hasMatch()) {
            if (value.startsWith("CU:") || value.startsWith("Contents of ")) havePrevious = false;
            continue;
        }
        bool ok = false;
        const auto address = match.captured(2).toUInt(&ok, 16);
        if (!ok) { havePrevious = false; continue; }
        if (havePrevious && address > previous) ranges_[previous].end = address;
        if (ranges_.size() > maxRanges) {
            stop();
            startAssembly("Trop de plages de lignes DWARF.");
            return;
        }
        previous = address;
        havePrevious = match.captured(1) != "-";
    }
    if (ranges_.empty()) {
        startAssembly("Cet ELF ne fournit pas de lignes DWARF exploitables. Compiler avec -g.");
        return;
    }
    output_.clear();
    for (const auto& range : ranges_) addresses_ += "0x" + QByteArray::number(range.first, 16) + '\n';
    stage_ = Resolve;
    process_.start(addr2line_, {"-a", "-e", path_});
}

void SourceMap::resolve() {
    static const QRegularExpression source(R"(^(.+):(\d+)(?:\s+\(discriminator\s+\d+\))?$)");
    const auto lines = output_.split('\n');
    std::map<QString, QString> paths;
    const auto directory = QFileInfo(path_).dir();
    for (int i = 0; i + 1 < lines.size(); i += 2) {
        bool ok = false;
        const auto address = lines.at(i).trimmed().toUInt(&ok, 16);
        auto range = ranges_.find(address);
        if (!ok || range == ranges_.end()) continue;
        const auto match = source.match(QString::fromLocal8Bit(lines.at(i + 1)).trimmed());
        if (!match.hasMatch() || match.captured(1) == "??") continue;
        const auto line = match.captured(2).toInt();
        if (line <= 0) continue;
        const auto name = match.captured(1);
        auto path = paths.find(name);
        if (path == paths.end()) {
            const QFileInfo info(directory.absoluteFilePath(name));
            const auto exact = info.canonicalFilePath();
            path = paths.emplace(name, exact.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : exact).first;
        }
        range->second.source.path = path->second;
        range->second.source.line = line;
        sourceAddresses_.emplace(std::make_pair(path->second, line), address);
    }
    startAssembly(sourceAddresses_.empty() ? "Aucune ligne source n'a pu être associée à l'ELF."
                                          : "Lignes DWARF chargées");
}

void SourceMap::startAssembly(const QString& sourceStatus) {
    output_.clear();
    addresses_.clear();
    stage_ = Assembly;
    assemblyStatus_ = "Désassemblage de l'ELF…";
    deadline_.start(15000);
    process_.start(objdump_, {"--disassemble", "--disassemble-zeroes", "--wide", "--no-show-raw-insn", path_});
    report(sourceStatus);
}

void SourceMap::failIndex(const QString& message) {
    // The debugger can still use the DWARF ranges if only static disassembly failed.
    const bool assemblyFailed = stage_ == Assembly;
    stop(assemblyFailed);
    if (!assemblyFailed) {
        startAssembly(message);
        return;
    }
    assemblyStatus_ = message;
    if (updated) updated();
}

void SourceMap::readAssembly() {
    static const QRegularExpression row(R"(^\s*([0-9a-fA-F]+):\s+(\S.*)$)");
    auto instructions = std::make_shared<AssemblyListing>();
    for (const auto& raw : output_.split('\n')) {
        const auto match = row.match(QString::fromLocal8Bit(raw));
        if (!match.hasMatch()) continue;
        bool ok = false;
        const auto address = match.captured(1).toUInt(&ok, 16);
        auto instruction = match.captured(2).trimmed();
        if (!ok || instruction.startsWith('.')) continue;
        instruction.replace('\t', ' ');
        instructions->push_back({address, std::move(instruction), {}});
    }
    const auto byAddress = [](const AssemblyInstruction& a, const AssemblyInstruction& b) { return a.address < b.address; };
    if (!std::is_sorted(instructions->begin(), instructions->end(), byAddress))
        std::stable_sort(instructions->begin(), instructions->end(), byAddress);
    instructions->erase(std::unique(instructions->begin(), instructions->end(), [](const AssemblyInstruction& a, const AssemblyInstruction& b) {
        return a.address == b.address;
    }), instructions->end());
    for (auto& instruction : *instructions) {
        instruction.source = location(instruction.address);
        if (instruction.source.valid())
            assemblyAddresses_[{instruction.source.path, instruction.source.line}].push_back(instruction.address);
    }
    stage_ = Idle;
    deadline_.stop();
    output_.clear();
    assemblyStatus_ = instructions->empty() ? "Aucune instruction trouvée dans l'ELF." : QString();
    instructions_ = std::move(instructions);
    if (updated) updated();
}

Location SourceMap::location(uint32_t address) const {
    address &= ~1u;
    auto range = ranges_.upper_bound(address);
    if (range == ranges_.begin()) return {};
    --range;
    return address < range->second.end ? range->second.source : Location();
}

bool SourceMap::addressFor(const Location& source, uint32_t& address) const {
    auto found = sourceAddresses_.find({source.path, source.line});
    if (found == sourceAddresses_.end()) return false;
    address = found->second;
    return true;
}

const std::vector<uint32_t>& SourceMap::assemblyAddresses(const Location& source) const {
    static const std::vector<uint32_t> empty;
    const auto found = assemblyAddresses_.find({source.path, source.line});
    return found == assemblyAddresses_.end() ? empty : found->second;
}

void SourceMap::report(const QString& message) {
    status_ = message;
    if (updated) updated();
}

}
