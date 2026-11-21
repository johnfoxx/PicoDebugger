#include "gdb_target.hpp"
#include "picodbg/peripherals.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace picodbg {
namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString hx(uint32_t n) { return q(hex(n)); }
uint32_t number(const QString& text) {
    bool ok = false; const auto n = text.toULongLong(&ok, 0);
    if (!ok || n > 0xffffffffull) throw Error(ErrorKind::Unsupported, "Valeur GDB 32 bits invalide : " + text.toStdString());
    return static_cast<uint32_t>(n);
}
QByteArray digest(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw Error(ErrorKind::InvalidImage, "ELF inaccessible");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) throw Error(ErrorKind::InvalidImage, "Lecture ELF incomplète");
    return hash.result();
}
int registerIndex(QString name) {
    name = name.toLower();
    for (int i = 0; i < 16; ++i) if (name == q(registerName(static_cast<unsigned>(i))) || name == "r" + QString::number(i)) return i;
    if (name == "xpsr" || name == "cpsr") return 16;
    const QStringList extra = {"msp", "psp", "control", "primask"};
    const int at = extra.indexOf(name); return at < 0 ? -1 : at + 17;
}
void unavailable(const char* text) { throw Error(ErrorKind::Unsupported, text); }
}
GdbTarget::GdbTarget(Board board, QObject* parent) : QObject(parent), board_(describeBoard(board)), description_(describe(board_.chip)) {
    for (const auto& device : peripheralCatalog(board_.chip)) for (const auto& reg : device.registers) {
        if (reg.access == "WO") continue;
        const bool safe = device.name == "PWM" || device.name == "RESETS" ||
            (device.name == "WATCHDOG" && reg.name.find("SCRATCH") != std::string::npos) ||
            ((device.name == "UART0" || device.name == "UART1") && reg.name != "UARTDR") ||
            ((device.name == "SPI0" || device.name == "SPI1") && reg.name != "SSPDR") ||
            ((device.name == "TIMER" || device.name == "TIMER0" || device.name == "TIMER1") && reg.name != "TIMELR");
        if (safe) automaticRegisters_.insert(reg.address);
    }
    mi_.started = [this] { initialize(); };
    mi_.notification = [this](const MiRecord& r) { notification(r); };
    mi_.failed = [this](const QString& message) { fail(message, true); };
    mi_.output = [this](const QString& text) { if (logMessage && !text.trimmed().isEmpty()) logMessage(text.trimmed()); };
    serverDeadline_.setSingleShot(true);
    haltDeadline_.setSingleShot(true);
    connect(&haltDeadline_, &QTimer::timeout, this, [this] { fail("Arrêt matériel non confirmé par le serveur", true); });
    connect(&serverDeadline_, &QTimer::timeout, this, [this] { fail("OpenOCD n'a pas ouvert son port GDB dans le délai prévu", true); });
    server_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&server_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = server_.readAllStandardOutput(); serverOutput_ += bytes;
        if (serverOutput_.size() > 16384) serverOutput_ = serverOutput_.right(16384);
        if (logMessage) logMessage(QString::fromUtf8(bytes).trimmed().left(4000));
        const QByteArray marker = "Listening on port " + QByteArray::number(options_.port) + " for gdb connections";
        if (connecting_ && !gdbStarted_ && serverOutput_.contains(marker)) { serverDeadline_.stop(); launchGdb(); }
    });
    connect(&server_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!stoppingServer_) fail("OpenOCD : " + server_.errorString(), true);
    });
    connect(&server_, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        if (!stoppingServer_) fail(QString("OpenOCD s'est arrêté (code %1)").arg(code), true);
    });
}
GdbTarget::~GdbTarget() { mi_.stop(); stopServer(); }
void GdbTarget::stopServer() {
    stoppingServer_ = true; serverDeadline_.stop();
    if (server_.state() != QProcess::NotRunning) { server_.kill(); server_.waitForFinished(1000); }
}
QString GdbTarget::monitor(const QString& command) const { return "-interpreter-exec console " + miQuote("monitor " + command); }
void GdbTarget::connectProbe(const ProbeOptions& options) {
    if (connected_ || connecting_) throw Error(ErrorKind::InvalidArgument, "Déconnecter la cible avant de changer la connexion");
    if (!options.port || options.port > 65535 || options.speed < 1 || options.speed > 10000 ||
        !QRegularExpression("^[a-zA-Z0-9_.:-]+$").match(options.host).hasMatch())
        throw Error(ErrorKind::InvalidArgument, "Hôte, port ou vitesse SWD invalide");
    for (const auto& id : options.coreThreads)
        if (!id.isEmpty() && !QRegularExpression("^[0-9]+$").match(id).hasMatch())
            throw Error(ErrorKind::InvalidArgument, "Identifiant de thread GDB invalide");
    if (!options.coreThreads[0].isEmpty() && options.coreThreads[0] == options.coreThreads[1])
        throw Error(ErrorKind::InvalidArgument, "Les deux cœurs doivent correspondre à des threads distincts");
    // Each connection starts a new GDB process with no executable or symbols.
    elfPath_.clear();
    ++symbolRevision_;
    elfHash_.clear();
    image = ElfImage();
    options_ = options; error_.clear(); connecting_ = busy_ = true; gdbStarted_ = false; executionBlocked_ = false;
    activity_ = "Connexion…"; ++revision_; invalidate();
    if (!options_.launchOpenOcd) { launchGdb(); return; }
    options_.host = "127.0.0.1"; stoppingServer_ = false; serverOutput_.clear();
    QStringList args;
    if (!options_.scripts.isEmpty()) args << "-s" << options_.scripts;
    args << "-c" << "bindto 127.0.0.1" << "-c" << "tcl_port disabled" << "-c" << "telnet_port disabled"
         << "-c" << QString("gdb_port %1").arg(options_.port)
         << "-f" << "interface/cmsis-dap.cfg" << "-f" << (board_.chip == Chip::RP2040 ? "target/rp2040.cfg" : "target/rp2350.cfg")
         << "-c" << QString("adapter speed %1").arg(options_.speed);
    server_.setProgram(options_.openOcd); server_.setArguments(args);
    serverDeadline_.start(15000); server_.start();
}
void GdbTarget::launchGdb() { gdbStarted_ = true; mi_.start(options_.gdb); }
void GdbTarget::initialize() {
    QString endpoint = options_.host;
    if (endpoint.contains(':') && !endpoint.startsWith('[')) endpoint = "[" + endpoint + "]";
    endpoint += ":" + QString::number(options_.port);
    // GDB passes the endpoint to the remote CLI verbatim, including any quotes.
    commands({"-gdb-set pagination off", "-gdb-set confirm off", "-gdb-set mi-async on", "-gdb-set non-stop off",
              "-gdb-set remotetimeout 5", "-gdb-set mem inaccessible-by-default off", "-gdb-set architecture auto",
              "-target-select extended-remote " + endpoint, monitor("halt")}, [this] {
        mi_.command("-data-read-memory-bytes 0x40000000 4", [this](const MiRecord& r) {
            if (!success(r)) return;
            try {
                const auto bytes = memoryBytes(r, 0x40000000, 4);
                uint32_t id = 0; for (unsigned i = 0; i < 4; ++i) id |= uint32_t(static_cast<unsigned char>(bytes[static_cast<int>(i)])) << (8 * i);
                const uint32_t expected = board_.chip == Chip::RP2040 ? 0x2927u : 0x4927u;
                if ((id & 0x0fffffffu) != expected) { fail("Puce connectée incompatible avec la carte sélectionnée (CHIP_ID=" + hx(id) + ")", true); return; }
            } catch (const std::exception& e) { fail(QString::fromUtf8(e.what()), true); return; }
            mi_.command("-data-list-register-names", [this](const MiRecord& names) {
                if (!success(names)) return;
                registerNames_.clear();
                for (const auto& value : names.data.get("register-names").items) registerNames_ << value.text;
                const bool hasArmStatus = registerNames_.contains("xpsr", Qt::CaseInsensitive) ||
                                          registerNames_.contains("cpsr", Qt::CaseInsensitive);
                if (!hasArmStatus || !registerNames_.contains("pc", Qt::CaseInsensitive) || !registerNames_.contains("sp", Qt::CaseInsensitive)) {
                    fail("Le serveur ne fournit pas les registres ARM attendus", true); return;
                }
                connected_ = verified_ = true; connecting_ = busy_ = false; running_ = false;
                stop.kind = StopKind::Ready; stop.message = "Cible réelle arrêtée ; puce " + std::string(board_.chip == Chip::RP2040 ? "RP2040" : "RP2350") + " vérifiée";
                snapshotNeeded_ = true; ++revision_;
            });
        });
    });
}
void GdbTarget::fail(const QString& message, bool lost) {
    snapshotNeeded_ = connected_ && activity_ != "Lecture des registres CPU…";
    error_ = message; activity_.clear(); busy_ = false; disconnecting_ = false;
    if (lost || connecting_) {
        haltDeadline_.stop(); mi_.stop(); stopServer(); connected_ = verified_ = connecting_ = running_ = disconnecting_ = false;
        snapshotNeeded_ = false;
        invalidate(); breakpoints.clear(); breakpointIds_.clear(); watchpoints.clear(); watchIds_.clear();
    }
    stop.kind = StopKind::Fault; stop.message = message.toStdString(); ++revision_;
    if (logMessage) logMessage("Erreur cible : " + message);
}
bool GdbTarget::success(const MiRecord& r) {
    if (!r.error()) return true;
    fail(r.data.get("msg").text); return false;
}
void GdbTarget::commands(QStringList sequence, std::function<void()> done, int timeout) {
    if (sequence.isEmpty()) { done(); return; }
    const auto command = sequence.takeFirst();
    mi_.command(command, [this, sequence, done, timeout](const MiRecord& r) {
        if (success(r)) commands(sequence, done, timeout);
    }, timeout);
}
void GdbTarget::requireStopped() const {
    if (!loaded() || busy() || running_ || disconnecting_) throw Error(ErrorKind::InvalidArgument, "Cible indisponible : connecter, attendre l'opération et mettre en pause");
}
void GdbTarget::invalidate() {
    ++generation_; reads_.clear(); instructions_.clear(); disassemblyRequests_.clear(); valid_ = {};
    for (auto& extra : extras_) extra.clear();
    mi_.cancelInspections(); ++revision_;
}
void GdbTarget::begin(const QString& activity) {
    requireStopped(); busy_ = true; error_.clear(); activity_ = activity; invalidate();
}
QString GdbTarget::connectionText() const {
    if (connecting_) return "Connexion en cours";
    if (!connected_) return error_.isEmpty() ? "Déconnectée" : "Connexion perdue / erreur : " + error_;
    const QString identity = board_.chip == Chip::RP2040 ? "RP2040 vérifié" : "RP2350 ARM vérifié";
    return identity + " · " + (busy_ ? activity_ : running_ ? QString("En exécution") : QString("Connectée · arrêtée")) +
        (error_.isEmpty() ? QString() : " · " + error_);
}
void GdbTarget::notification(const MiRecord& r) {
    if (r.kind == '+' && r.klass == "download") { activity_ = "Téléversement…"; ++revision_; return; }
    if (r.kind == '=' && (r.klass == "thread-group-exited" || r.klass == "thread-group-removed") && connected_ && !disconnecting_) {
        fail("Le serveur a fermé la cible", true); return;
    }
    if (r.kind != '*') return;
    if (r.klass == "running") { running_ = true; ++revision_; }
    if (r.klass == "stopped") {
        haltDeadline_.stop();
        running_ = false; snapshotNeeded_ = true; invalidate();
        const auto reason = r.data.get("reason").text;
        if (reason.startsWith("exited")) { fail("Fin de la session cible : " + reason, true); return; }
        stop.kind = reason.contains("watchpoint") ? StopKind::Watchpoint : reason == "breakpoint-hit" ? StopKind::Breakpoint :
                    reason == "end-stepping-range" ? StopKind::Step : reason == "signal-received" && r.data.get("signal-name").text != "SIGINT" ? StopKind::Fault : StopKind::User;
        stop.message = ("Arrêt matériel : " + reason + " " + r.data.get("signal-name").text).trimmed().toStdString();
        for (unsigned i = 0; i < 2; ++i) if (threads_[i] == r.data.get("thread-id").text) stop.core = i;
        if (logMessage) logMessage(q(stop.message));
    }
}
void GdbTarget::poll() {
    if (!connected_ || busy_ || running_) return;
    if (disconnecting_) { completeDisconnect(); return; }
    if (snapshotNeeded_) snapshot();
}
void GdbTarget::snapshot() {
    snapshotNeeded_ = false; busy_ = true; activity_ = "Lecture des registres CPU…"; ++revision_;
    // Monitor reset/halt does not always invalidate GDB's register cache.
    commands({"-interpreter-exec console \"maintenance flush register-cache\""}, [this] { readThreads(); });
}
void GdbTarget::readThreads() {
    mi_.command("-thread-info", [this](const MiRecord& r) {
        if (!success(r)) return;
        threads_ = {}; cpus = {}; valid_ = {};
        for (const auto& thread : r.data.get("threads").items) {
            const QString id = thread.get("id").text;
            if (!QRegularExpression("^[0-9]+$").match(id).hasMatch()) continue;
            int core = -1;
            for (unsigned c = 0; c < 2; ++c) if (!options_.coreThreads[c].isEmpty() && options_.coreThreads[c] == id) core = static_cast<int>(c);
            if (core < 0 && options_.coreThreads[0].isEmpty() && options_.coreThreads[1].isEmpty()) {
                // With qThreadExtraInfo, OpenOCD reports the core name in details.
                const auto labels = thread.get("name").text + " " + thread.get("target-id").text + " " + thread.get("details").text;
                const auto match = QRegularExpression("(?:^|[ .:(])(?:core|cm)([01])(?:$|[ ,:)])").match(labels);
                if (match.hasMatch()) core = match.captured(1).toInt();
                else if (thread.get("core").text == "0" || thread.get("core").text == "1") core = thread.get("core").text.toInt();
            }
            if (core >= 0 && core < 2) {
                if (!threads_[static_cast<unsigned>(core)].isEmpty()) { fail("Plusieurs threads correspondent au même cœur ; configurer le serveur en hwthread", true); return; }
                threads_[static_cast<unsigned>(core)] = id;
            }
        }
        if (threads_[0].isEmpty() && threads_[1].isEmpty()) { fail("Cœurs non identifiés : utiliser OpenOCD hwthread ou renseigner les threads GDB dans la connexion", true); return; }
        readCpu(0);
    });
}
QString GdbTarget::threadOption(unsigned core) const {
    if (core > 1 || threads_[core].isEmpty()) throw Error(ErrorKind::Unsupported, "Cœur non exposé par le serveur GDB");
    return " --thread " + threads_[core];
}
QString GdbTarget::registerWrite(unsigned core, unsigned index, uint32_t value) const {
    if (!registerAvailable(core, index)) throw Error(ErrorKind::Unsupported, "Registre requis pour le démarrage non exposé par GDB");
    for (int n = 0; n < registerNames_.size(); ++n)
        if (registerIndex(registerNames_[n]) == static_cast<int>(index))
            return "-data-write-register-values" + threadOption(core) + " x " + QString::number(n) + " " + hx(value);
    throw Error(ErrorKind::Unsupported, "Registre GDB inconnu");
}
void GdbTarget::readCpu(unsigned core) {
    if (core == 2) { finishSnapshot(); return; }
    extras_[core].clear();
    if (threads_[core].isEmpty()) { readCpu(core + 1); return; }
    mi_.command("-data-list-register-values" + threadOption(core) + " x", [this, core](const MiRecord& r) {
        if (r.error()) {
            if (logMessage) logMessage("Registres du cœur indisponibles : " + r.data.get("msg").text);
            readCpu(core + 1); return;
        }
        for (const auto& value : r.data.get("register-values").items) {
            bool ok; const int n = value.get("number").text.toInt(&ok);
            if (!ok || n < 0 || n >= registerNames_.size()) continue;
            const QString name = registerNames_[n], text = value.get("value").text;
            const int index = registerIndex(name);
            if (index < 0) { if (!name.isEmpty()) extras_[core][name.toStdString()] = text.toStdString(); continue; }
            try {
                const uint32_t raw = number(text); valid_[core][static_cast<unsigned>(index)] = true;
                auto& cpu = cpus[core];
                if (index < 16) cpu.r[static_cast<unsigned>(index)] = raw;
                else { uint32_t* values[] = {&cpu.xpsr, &cpu.msp, &cpu.psp, &cpu.control, &cpu.primask}; *values[index - 16] = raw; }
            } catch (const Error&) { /* Missing target registers remain explicitly unavailable. */ }
        }
        cpus[core].held = !valid_[core][15]; readCpu(core + 1);
    });
}
void GdbTarget::finishSnapshot() {
    busy_ = false; activity_.clear(); stop.address = cpus[stop.core].r[15]; ++revision_;
}
bool GdbTarget::registerAvailable(unsigned core, unsigned index) const {
    return loaded() && !running_ && !busy_ && core < 2 && index < 21 && valid_[core][index];
}
void GdbTarget::refreshHardware() { requireStopped(); invalidate(); snapshotNeeded_ = true; }
void GdbTarget::load(const std::string& path) {
    requireStopped();
    const QString absolute = QFileInfo(q(path)).absoluteFilePath();
    if (absolute.contains('\n') || absolute.contains('\r'))
        throw Error(ErrorKind::InvalidArgument, "Le chemin de l'ELF contient un saut de ligne incompatible avec GDB");
    const ElfImage candidate = ElfImage::read(path);
    const QByteArray hash = digest(absolute);
    // This MI command uses the CLI's filename parser, which expects literal UTF-8.
    QString escaped = absolute;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    begin("Chargement des symboles…");
    mi_.command("-file-exec-and-symbols \"" + escaped + "\"", [this, candidate, absolute, hash](const MiRecord& r) {
        if (!success(r)) {
            // GDB may have changed its executable before reporting a symbol error.
            elfPath_.clear();
            ++symbolRevision_;
            elfHash_.clear();
            image = ElfImage();
            return;
        }
        image = candidate; elfPath_ = absolute; elfHash_ = hash; busy_ = false; snapshotNeeded_ = true;
        ++symbolRevision_;
        stop.message = "Symboles ELF chargés ; la mémoire de la carte n'a pas été modifiée"; ++revision_;
        if (logMessage) logMessage(q(stop.message));
    });
}
void GdbTarget::loadImage(const ElfImage&) { unavailable("L'exemple intégré est réservé à l'émulation ; sélectionner un ELF destiné au matériel"); }
void GdbTarget::download(bool ramEntry) {
    requireStopped();
    if (elfPath_.isEmpty() || digest(elfPath_) != elfHash_) throw Error(ErrorKind::InvalidImage, "Ouvrir un ELF à jour avant le téléversement");
    if (threads_[0].isEmpty()) unavailable("Le téléversement requiert le cœur 0 exposé par OpenOCD");
    bool flash = false;
    for (const auto& segment : image.segments) {
        if (segment.bytes.empty()) continue;
        const uint64_t end = uint64_t(segment.address) + segment.bytes.size();
        const bool inRam = segment.address >= 0x20000000u && end <= 0x20000000ull + description_.ramSize;
        const bool inFlash = segment.address >= 0x10000000u && end <= 0x10000000ull + board_.flashSize;
        if (!inRam && !inFlash) throw Error(ErrorKind::InvalidImage, "Segment ELF hors SRAM/Flash de la carte sélectionnée");
        flash = flash || inFlash;
    }
    if (!ramEntry && !flash) throw Error(ErrorKind::InvalidImage, "Cet ELF est en RAM ; choisir le démarrage RAM pour préserver son chargement");
    QStringList sequence = {monitor("reset halt"), "-target-download"};
    if (ramEntry) {
        uint32_t sp = 0;
        if (!image.hasVectors || (image.vectorAddress & 255u))
            throw Error(ErrorKind::InvalidImage, "L'entrée RAM requiert une table de vecteurs ELF alignée sur 256 octets");
        uint32_t entry = 0;
        for (const auto& segment : image.segments) {
            if (segment.bytes.empty()) continue;
            if (segment.address < 0x20000000 || uint64_t(segment.address) + segment.bytes.size() > 0x20000000ull + description_.ramSize)
                throw Error(ErrorKind::InvalidImage, "Le démarrage RAM exige un ELF entièrement chargé en SRAM");
            if (image.vectorAddress >= segment.address && uint64_t(image.vectorAddress) + 8 <= uint64_t(segment.address) + segment.bytes.size()) {
                const size_t off = image.vectorAddress - segment.address;
                for (unsigned i = 0; i < 4; ++i) { sp |= uint32_t(segment.bytes[off + i]) << (8 * i); entry |= uint32_t(segment.bytes[off + 4 + i]) << (8 * i); }
            }
        }
        if (!sp || !(entry & 1u) || (sp & 3u) || sp <= 0x20000000u || sp > 0x20000000u + description_.ramSize ||
            entry < 0x20000000u || uint64_t(entry & ~1u) >= 0x20000000ull + description_.ramSize)
            throw Error(ErrorKind::InvalidImage, "Vecteurs RAM SP/PC invalides");
        QByteArray vectorBytes;
        for (unsigned i = 0; i < 4; ++i) vectorBytes += static_cast<char>(image.vectorAddress >> (8 * i));
        sequence << "-thread-select " + threads_[0]
                 << registerWrite(0, 13, sp) << registerWrite(0, 15, entry & ~1u) << registerWrite(0, 16, 0x01000000)
                 << "-data-write-memory-bytes 0xe000ed08 " + QString::fromLatin1(vectorBytes.toHex());
    } else sequence << monitor("reset halt");
    begin("Téléversement ELF — mémoire cible modifiée…");
    executionBlocked_ = true;
    commands(sequence, [this] {
        busy_ = false; running_ = false; snapshotNeeded_ = true; executionBlocked_ = false; ++revision_;
        stop.kind = StopKind::Ready; stop.message = "ELF téléversé ; carte arrêtée, F5 pour exécuter";
        if (logMessage) logMessage(q(stop.message));
    }, 120000);
}
void GdbTarget::reset() {
    begin("Reset matériel…");
    commands({monitor("reset halt")}, [this] { busy_ = running_ = false; snapshotNeeded_ = true; stop.kind = StopKind::Ready; stop.message = "Reset matériel, cible arrêtée"; ++revision_; });
}
Stop GdbTarget::run(size_t) {
    if (executionBlocked_) unavailable("Téléversement incomplet : réussir un nouveau téléversement avant de reprendre");
    if (!registerAvailable(0, 15) && !registerAvailable(1, 15)) unavailable("Aucun cœur arrêté avec PC disponible");
    begin("Reprise…"); running_ = true;
    mi_.command("-exec-continue --all", [this](const MiRecord& r) {
        busy_ = false;
        if (r.error()) { running_ = false; fail(r.data.get("msg").text); snapshotNeeded_ = true; }
        ++revision_;
    }); return stop;
}
Stop GdbTarget::step(unsigned core) {
    if (executionBlocked_) unavailable("Téléversement incomplet : réussir un nouveau téléversement avant de reprendre");
    const auto thread = threadOption(core); begin("Pas instruction…"); running_ = true; stop.core = core;
    mi_.command("-exec-step-instruction" + thread, [this](const MiRecord& r) {
        busy_ = false;
        if (r.error()) { running_ = false; fail(r.data.get("msg").text); snapshotNeeded_ = true; }
        ++revision_;
    }); return stop;
}
void GdbTarget::pause() {
    if (!connected_ || !running_ || busy_) return;
    busy_ = true; activity_ = "Interruption…"; ++revision_;
    haltDeadline_.start(10000);
    mi_.command("-exec-interrupt --all", [this](const MiRecord& r) { busy_ = false; success(r); ++revision_; });
}
void GdbTarget::startCore(unsigned core, uint32_t entry, uint32_t stack) {
    requireStopped();
    if (!(entry & 1u) || (stack & 3u) || stack <= 0x20000000u || stack > 0x20000000u + description_.ramSize)
        throw Error(ErrorKind::InvalidArgument, "Entrée Thumb ou pile SRAM invalide");
    const QStringList sequence = {registerWrite(core, 13, stack), registerWrite(core, 15, entry & ~1u), registerWrite(core, 16, 0x01000000)};
    begin("Modification PC/SP du cœur arrêté…");
    commands(sequence, [this] { busy_ = false; snapshotNeeded_ = true; ++revision_; });
}
void GdbTarget::disconnectProbe() {
    if (!connected_) { mi_.stop(); stopServer(); connecting_ = busy_ = false; ++revision_; return; }
    if (busy_) throw Error(ErrorKind::InvalidArgument, "Attendre la fin de l'opération avant de déconnecter");
    error_.clear();
    disconnecting_ = true;
    if (running_) pause(); else completeDisconnect();
}
void GdbTarget::completeDisconnect() {
    busy_ = true; activity_ = "Déconnexion…";
    QStringList ids;
    for (const auto& point : breakpointIds_) ids << point.second;
    for (const auto& id : watchIds_) ids << id;
    QStringList sequence;
    if (!ids.isEmpty()) sequence << "-break-delete " + ids.join(' ');
    sequence << "-target-disconnect";
    commands(sequence, [this] {
        mi_.stop(); stopServer(); connected_ = verified_ = busy_ = running_ = disconnecting_ = false;
        invalidate(); breakpoints.clear(); breakpointIds_.clear(); watchpoints.clear(); watchIds_.clear();
        stop.message = "Sonde déconnectée"; ++revision_;
    });
}
Stop GdbTarget::runFor(uint64_t, size_t) { unavailable("Le temps virtuel est réservé à l'émulation"); return stop; }
void GdbTarget::advanceTime(uint64_t) { unavailable("Le temps virtuel est réservé à l'émulation"); }
size_t GdbTarget::injectUart(unsigned, const std::string&) { unavailable("SWD ne transporte pas l'UART ; utiliser la liaison série de la sonde"); return 0; }
bool GdbTarget::ordinaryMemory(uint32_t address, unsigned size) const {
    const auto* region = regionAt(description_, address);
    return region && region->permissions != "MMIO" && uint64_t(address) + size <= region->end;
}
bool GdbTarget::safeRegister(uint32_t address) const {
    // Explicitly reviewed registers only. Descriptions of unmodelled blocks do
    // not certify that reading them is free of side effects on real silicon.
    if (address == 0xe000ed00 || address == 0xe000ed08 || address == 0xe000e014 || address == 0xe000e018 ||
        address == 0xe000e100 || address == 0xe000e104 || address == 0xe000e200 || address == 0xe000e204 ||
        address == 0xd0000000 || address == 0xd0000010 || address == (board_.chip == Chip::RP2040 ? 0xd0000020u : 0xd0000030u)) return true;
    return automaticRegisters_.count(address) != 0;
}
QByteArray GdbTarget::memoryBytes(const MiRecord& r, uint32_t address, unsigned length) {
    if (length > 4096) throw Error(ErrorKind::InvalidArgument, "Lecture GDB trop grande");
    QByteArray out(static_cast<int>(length), 0), seen(static_cast<int>(length), 0);
    for (const auto& block : r.data.get("memory").items) {
        const uint32_t start = number(block.get("begin").text);
        const auto hexText = block.get("contents").text;
        if (hexText.size() % 2 || !QRegularExpression("^[0-9a-fA-F]*$").match(hexText).hasMatch())
            throw Error(ErrorKind::Unsupported, "Octets GDB invalides");
        const auto bytes = QByteArray::fromHex(hexText.toLatin1());
        if (start < address || uint64_t(start) + static_cast<unsigned>(bytes.size()) > uint64_t(address) + length)
            throw Error(ErrorKind::Unsupported, "Plage de réponse GDB incohérente");
        for (int i = 0; i < bytes.size(); ++i) {
            const int off = static_cast<int>(start - address) + i;
            if (seen[off]) throw Error(ErrorKind::Unsupported, "Réponse mémoire GDB chevauchante");
            out[off] = bytes[i]; seen[off] = 1;
        }
    }
    if (seen.contains(char(0))) throw Error(ErrorKind::Unsupported, "Lecture mémoire GDB incomplète");
    return out;
}
void GdbTarget::requestMemory(uint32_t address, unsigned length, unsigned core) const {
    const auto key = std::make_pair(core, address); const uint64_t generation = generation_;
    reads_[key] = Read();
    mi_.command("-data-read-memory-bytes" + threadOption(core) + " " + hx(address) + " " + QString::number(length),
        [this, key, generation, address, length](const MiRecord& r) {
            if (generation != generation_ || !connected_) return;
            auto& read = reads_[key]; read.pending = false;
            if (r.error()) read.error = r.data.get("msg").text;
            else try { read.bytes = memoryBytes(r, address, length); }
                 catch (const std::exception& e) { read.error = QString::fromUtf8(e.what()); }
            ++revision_;
        }, 8000, true);
}
uint32_t GdbTarget::peek(uint32_t address, unsigned size, unsigned core) const {
    if (!loaded() || busy_ || running_) unavailable("Lecture matérielle indisponible pendant l'exécution ou une opération");
    if ((size != 1 && size != 2 && size != 4) || uint64_t(address) + size > 0x100000000ull)
        throw Error(ErrorKind::InvalidArgument, "Plage de lecture invalide");
    threadOption(core);
    const bool memory = ordinaryMemory(address, size);
    const uint32_t base = memory ? address & ~63u : address & ~3u;
    if (uint64_t(address) + size > uint64_t(base) + (memory ? 64u : 4u)) {
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i) value |= peek(address + i, 1, core) << (8 * i);
        return value;
    }
    const auto key = std::make_pair(core, base);
    auto it = reads_.find(key);
    if (it == reads_.end()) {
        if (!memory && !safeRegister(base)) unavailable("Lecture explicite requise : ce registre peut modifier l'état matériel");
        unsigned length = 4;
        if (memory) {
            const auto* region = regionAt(description_, base);
            if (!region || region->permissions == "MMIO") unavailable("Plage mémoire indisponible");
            length = static_cast<unsigned>(std::min<uint64_t>(64, region->end - base));
        }
        requestMemory(base, length, core); unavailable("Lecture matérielle en cours…");
    }
    const auto& read = it->second;
    if (read.pending) unavailable("Lecture matérielle en cours…");
    if (!read.error.isEmpty()) throw Error(ErrorKind::Unsupported, read.error.toStdString());
    if (uint64_t(address - base) + size > static_cast<unsigned>(read.bytes.size())) unavailable("Lecture matérielle incomplète");
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i) value |= uint32_t(static_cast<unsigned char>(read.bytes[static_cast<int>(address - base + i)])) << (8 * i);
    return value;
}
void GdbTarget::readRegisterOnce(uint32_t address, unsigned core) const {
    requireStopped(); if (address & 3u) throw Error(ErrorKind::InvalidArgument, "Registre matériel non aligné");
    if (ordinaryMemory(address, 4)) throw Error(ErrorKind::InvalidArgument, "Cette adresse est en mémoire ordinaire ; utiliser Lire ou Relire l'état matériel");
    requestMemory(address, 4, core);
}
Instruction GdbTarget::disassemble(uint32_t address, unsigned core) {
    if (!loaded() || busy_ || running_) unavailable("Désassemblage matériel indisponible pendant l'exécution ou une opération");
    threadOption(core);
    const auto key = std::make_pair(core, address);
    const auto found = instructions_.find(key); if (found != instructions_.end()) return found->second;
    auto request = disassemblyRequests_.find(key);
    if (request == disassemblyRequests_.end()) {
        const uint64_t end = std::min<uint64_t>(uint64_t(address) + 128, 0xffffffffull);
        const auto generation = generation_; disassemblyRequests_[key] = "Désassemblage GDB en cours…";
        mi_.command("-data-disassemble" + threadOption(core) + " -s " + hx(address) + " -e " + hx(static_cast<uint32_t>(end)) + " -- 2",
            [this, key, generation](const MiRecord& r) {
                if (generation != generation_ || !connected_) return;
                if (r.error()) disassemblyRequests_[key] = r.data.get("msg").text;
                else {
                    try {
                        for (const auto& line : r.data.get("asm_insns").items) {
                            Instruction instruction; instruction.address = number(line.get("address").text);
                            QString encoded = line.get("opcodes").text; encoded.remove(' '); encoded.remove('\t');
                            if ((encoded.size() != 4 && encoded.size() != 8) || !QRegularExpression("^[0-9a-fA-F]+$").match(encoded).hasMatch()) continue;
                            const auto bytes = QByteArray::fromHex(encoded.toLatin1()); instruction.size = static_cast<unsigned>(bytes.size());
                            for (unsigned i = 0; i < instruction.size; ++i) instruction.raw |= uint32_t(static_cast<unsigned char>(bytes[static_cast<int>(i)])) << (8 * i);
                            instruction.text = line.get("inst").text.toStdString(); instructions_[{key.first, instruction.address}] = instruction;
                        }
                        disassemblyRequests_[key] = "Aucune instruction fournie par GDB à cette adresse";
                    } catch (const std::exception& e) { disassemblyRequests_[key] = QString::fromUtf8(e.what()); }
                }
                ++revision_;
            }, 8000, true);
        unavailable("Désassemblage GDB en cours…");
    }
    throw Error(ErrorKind::Unsupported, request->second.toStdString());
}
void GdbTarget::addBreakpoint(uint32_t address) {
    requireStopped(); address &= ~1u; if (breakpoints.count(address)) return;
    begin("Insertion du breakpoint matériel…");
    mi_.command("-break-insert -h *" + hx(address), [this, address](const MiRecord& r) {
        if (!success(r)) { snapshotNeeded_ = true; return; }
        const auto id = r.data.get("bkpt").get("number").text;
        if (!QRegularExpression("^[0-9]+$").match(id).hasMatch()) { fail("Identifiant de breakpoint GDB invalide", true); return; }
        breakpointIds_[address] = id; breakpoints.insert(address); busy_ = false; snapshotNeeded_ = true; ++revision_;
    });
}
void GdbTarget::removeBreakpoint(uint32_t address) {
    requireStopped(); address &= ~1u; if (!breakpointIds_.count(address)) return;
    const QString id = breakpointIds_.at(address); begin("Suppression du breakpoint…");
    commands({"-break-delete " + id}, [this, address] { breakpointIds_.erase(address); breakpoints.erase(address); busy_ = false; snapshotNeeded_ = true; ++revision_; });
}
void GdbTarget::addWatchpoint(uint32_t address, uint32_t length, bool read, bool write) {
    if ((length != 1 && length != 2 && length != 4) || (address % length) || (!read && !write) || uint64_t(address) + length > 0x100000000ull)
        throw Error(ErrorKind::InvalidArgument, "Watchpoint matériel : 1, 2 ou 4 octets alignés requis ; ressources limitées par la puce");
    begin("Insertion du watchpoint matériel…");
    const QString type = length == 1 ? "unsigned char" : length == 2 ? "unsigned short" : "unsigned int";
    const QString expr = "*(" + type + "*)" + hx(address);
    mi_.command("-break-watch " + QString(read ? (write ? "-a " : "-r ") : "") + miQuote(expr),
        [this, address, length, read, write](const MiRecord& r) {
            if (!success(r)) { snapshotNeeded_ = true; return; }
            QString id;
            for (const auto& key : {QString("wpt"), QString("hw-rwpt"), QString("hw-awpt")})
                if (!r.data.get(key).get("number").text.isEmpty()) id = r.data.get(key).get("number").text;
            if (!QRegularExpression("^[0-9]+$").match(id).hasMatch()) { fail("Identifiant de watchpoint GDB invalide", true); return; }
            mi_.command("-break-info " + id, [this, id, address, length, read, write](const MiRecord& info) {
                bool hardware = false;
                for (const auto& entry : info.data.get("BreakpointTable").get("body").items) {
                    const auto type = entry.get("type").text;
                    hardware = hardware || type == "hw watchpoint" || type == "read watchpoint" || type == "acc watchpoint";
                }
                if (info.error() || !hardware) {
                    mi_.command("-break-delete " + id, [this](const MiRecord& removed) {
                        if (!success(removed)) return;
                        fail("Watchpoint matériel indisponible ; aucun remplacement logiciel n'a été activé"); snapshotNeeded_ = true;
                    }); return;
                }
                watchIds_.push_back(id); watchpoints.push_back({address, length, read, write});
                busy_ = false; snapshotNeeded_ = true; ++revision_;
            });
        });
}
void GdbTarget::removeWatchpoint(size_t index) {
    requireStopped(); if (index >= watchIds_.size()) throw Error(ErrorKind::InvalidArgument, "Watchpoint inconnu");
    const QString id = watchIds_[index]; begin("Suppression du watchpoint…");
    commands({"-break-delete " + id}, [this, index] {
        watchIds_.erase(watchIds_.begin() + static_cast<ptrdiff_t>(index));
        watchpoints.erase(watchpoints.begin() + static_cast<ptrdiff_t>(index));
        busy_ = false; snapshotNeeded_ = true; ++revision_;
    });
}
void GdbTarget::clearWatchpoints() {
    requireStopped(); if (watchIds_.empty()) return;
    QStringList ids; for (const auto& id : watchIds_) ids << id;
    begin("Suppression des watchpoints…");
    commands({"-break-delete " + ids.join(' ')}, [this] { watchIds_.clear(); watchpoints.clear(); busy_ = false; snapshotNeeded_ = true; ++revision_; });
}
}
