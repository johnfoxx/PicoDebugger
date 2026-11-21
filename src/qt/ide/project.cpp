#include "project.hpp"
#include "toolchain_catalog.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <stdexcept>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

static void loadIdeTemplates() {
    Q_INIT_RESOURCE(ide_templates);
}

namespace picoide {
namespace {
void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

QString executable(const QString& value) {
    if (value.isEmpty()) return {};
    return QStandardPaths::findExecutable(value);
}

QString configuredTool(const QSettings& settings, const QString& key, const QStringList& candidates) {
    const QString value = settings.value(key).toString().trimmed();
    if (!value.isEmpty()) return executable(value);
    for (const auto& candidate : candidates) {
        const auto found = executable(candidate);
        if (!found.isEmpty()) return found;
    }
    return {};
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail("Impossible de lire " + path);
    const auto data = file.readAll();
    if (file.error() != QFileDevice::NoError) fail("Lecture incomplète de " + path);
    return data;
}

void copyTemplate(const QDir& destination, const QString& path, const QByteArray& name = {},
                  const QByteArray& board = {}) {
    const QString filePath = destination.filePath(path);
    if (!destination.mkpath(QFileInfo(filePath).absolutePath())) fail("Impossible de créer " + filePath);
    auto contents = readFile(":/ide/templates/" + path);
    contents.replace("@PROJECT_NAME@", name);
    contents.replace("@BOARD_NAME@", board);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        fail("Impossible d'écrire " + filePath);
}

QJsonObject replyObject(const QDir& reply, const QString& name) {
    if (name.isEmpty() || QFileInfo(name).fileName() != name) fail("Réponse File API CMake invalide");
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(readFile(reply.filePath(name)), &error);
    if (!document.isObject()) fail("Réponse File API CMake illisible : " + error.errorString());
    return document.object();
}

std::vector<Artifact> readArtifacts(const QString& directory) {
    const QDir build(directory);
    const QDir reply(build.filePath(".cmake/api/v1/reply"));
    const auto indices = reply.entryList({"index-*.json"}, QDir::Files, QDir::Name | QDir::Reversed);
    if (indices.isEmpty()) fail("CMake n'a pas produit de réponse File API. CMake 3.16 ou plus récent est requis.");
    const auto index = replyObject(reply, indices.first());
    const auto reference = index.value("reply").toObject().value("codemodel-v2").toObject();
    const auto model = replyObject(reply, reference.value("jsonFile").toString());
    const auto configs = model.value("configurations").toArray();
    if (configs.isEmpty()) fail("Le projet CMake n'expose aucune configuration.");
    auto config = configs.at(0).toObject();
    for (const auto& entry : configs) {
        if (entry.toObject().value("name").toString() == "Debug") {
            config = entry.toObject();
            break;
        }
    }
    std::vector<Artifact> artifacts;
    for (const auto& entry : config.value("targets").toArray()) {
        const auto target = replyObject(reply, entry.toObject().value("jsonFile").toString());
        if (target.value("type").toString() != "EXECUTABLE") continue;
        for (const auto& artifact : target.value("artifacts").toArray()) {
            const auto path = artifact.toObject().value("path").toString();
            if (!path.isEmpty())
                artifacts.push_back({target.value("name").toString(), build.absoluteFilePath(path)});
        }
    }
    std::sort(artifacts.begin(), artifacts.end(), [](const Artifact& a, const Artifact& b) {
        return a.target < b.target;
    });
    return artifacts;
}
}

Tools Tools::detect() {
    const QSettings settings;
    const auto managed = installedTools();
    const auto local = [&managed](const char* name) { return managed.value(name).toString(); };
    Tools tools;
    tools.cmake = configuredTool(settings, "ide/cmake", {local("cmake"), "cmake"});
    const auto sdkToolchain = QString::fromLocal8Bit(qgetenv("PICO_TOOLCHAIN_PATH"));
    tools.gcc = configuredTool(settings, "ide/gcc", {
        local("gcc"),
        sdkToolchain.isEmpty() ? QString() : QDir(sdkToolchain).filePath("bin/arm-none-eabi-gcc"),
        "arm-none-eabi-gcc"
    });
    tools.clangd = configuredTool(settings, "ide/clangd", {local("clangd"), "clangd"});
    tools.gdb = configuredTool(settings, "probe/gdb", {local("gdb"), "gdb-multiarch", "arm-none-eabi-gdb"});
    tools.openocd = configuredTool(settings, "probe/openocd", {local("openocd"), "openocd"});
    if (!tools.openocd.isEmpty() && tools.openocd == local("openocd"))
        tools.openocdScripts = local("openocdScripts");
    tools.ninja = executable(local("ninja"));
    tools.picotool = executable(local("picotool"));
    tools.pioasm = executable(local("pioasm"));
    const auto configured = settings.value("ide/sdk").toString().trimmed();
    const auto paths = configured.isEmpty()
        ? QStringList{local("sdk"), QString::fromLocal8Bit(qgetenv("PICO_SDK_PATH")),
                      QDir::home().filePath("pico/pico-sdk"), QDir::home().filePath("pico-sdk")}
        : QStringList{configured};
    for (const auto& path : paths) {
        if (!path.isEmpty() && QFileInfo(QDir(path).filePath("external/pico_sdk_import.cmake")).isFile()) {
            tools.sdk = QFileInfo(path).absoluteFilePath();
            break;
        }
    }
    return tools;
}

QProcessEnvironment Tools::buildEnvironment() const {
    auto env = QProcessEnvironment::systemEnvironment();
    if (!gcc.isEmpty()) {
        const auto bin = QFileInfo(gcc).absolutePath();
        env.insert("PATH", bin + QDir::listSeparator() + env.value("PATH"));
        env.insert("PICO_TOOLCHAIN_PATH", QFileInfo(bin).absolutePath());
    }
    return env;
}

QString boardName(picodbg::Board board) {
    return QString::fromStdString(picodbg::describeBoard(board).id);
}

bool readProjectText(const QString& path, QByteArray& bytes) {
    constexpr qint64 sizeLimit = 2 * 1024 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > sizeLimit) return false;
    bytes = file.read(sizeLimit + 1);
    return bytes.size() <= sizeLimit && file.error() == QFileDevice::NoError && !bytes.contains('\0') &&
           QString::fromUtf8(bytes).toUtf8() == (bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes);
}

QString projectText(const QByteArray& bytes) {
    auto text = QString::fromUtf8(bytes);
    // Match the editor's paragraph representation for unopened files.
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace(QChar(0x2028), '\n');
    text.replace(QChar(0x2029), '\n');
    return text;
}

QString projectBuildDirectory(const QString& root, picodbg::Board board, const Tools& tools) {
    QByteArray identity;
    for (const auto& path : {tools.cmake, tools.gcc, tools.sdk, tools.ninja, tools.picotool, tools.pioasm}) {
        identity += path.toUtf8();
        identity += '\0';
    }
    const auto hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(10);
    return QDir(root).filePath(".picodbg/build/" + boardName(board) + "-" + QString::fromLatin1(hash));
}

QString createProject(const QString& parent, const QString& name, picodbg::Board board, Template kind) {
    if (!QRegularExpression("^[a-zA-Z][a-zA-Z0-9_-]{0,63}$").match(name).hasMatch())
        fail("Utiliser un nom de 1 à 64 lettres, chiffres, tirets ou soulignements, commençant par une lettre.");
    QDir folder(parent);
    if (parent.isEmpty() || !folder.exists()) fail("Le dossier parent est introuvable.");
    const auto root = folder.filePath(name);
    if (QFileInfo(root).exists() || QFileInfo(root).isSymLink()) fail("Ce nom existe déjà dans le dossier parent.");
    static const bool resourcesReady = [] { loadIdeTemplates(); return true; }();
    Q_UNUSED(resourcesReady);
    QTemporaryDir staging(folder.filePath(".pico-ide-XXXXXX"));
    if (!staging.isValid()) fail("Impossible de créer le projet dans ce dossier.");
    const QDir destination(staging.path());
    const auto boardId = boardName(board).toUtf8();
    const auto projectName = name.toUtf8();
    const auto prefix = kind == Template::Sdk ? QString("sdk/") : QString("bare-metal/");
    // The resource prefix selects the template. Published files live at the project root.
    const QDir tree(destination.filePath(prefix));
    if (!destination.mkpath(prefix)) fail("Impossible de préparer le projet.");
    for (const auto& path : {"CMakeLists.txt", "src/main.c"}) copyTemplate(destination, prefix + path, projectName, boardId);
    if (kind == Template::BareMetal) {
        for (const auto& path : {"cmake/arm-none-eabi.cmake", "startup/ram.ld", "startup/startup.c",
                                 "startup/memory_init.h", "startup/memory_init.c"})
            copyTemplate(destination, prefix + path);
        if (picodbg::describeBoard(board).chip == picodbg::Chip::RP2350)
            copyTemplate(destination, prefix + "include/rp2350_regs.h");
    }
    copyTemplate(tree, ".gitignore");
    copyTemplate(tree, ".clang-tidy");
    const QJsonObject defaults{{"board", boardName(board)}, {"language", "c"},
                              {"template", kind == Template::BareMetal ? "bare-metal" : "sdk"}};
    const auto metadata = QJsonDocument(defaults).toJson();
    QSaveFile projectFile(tree.filePath(".picodbg-project.json"));
    if (!projectFile.open(QIODevice::WriteOnly) || projectFile.write(metadata) != metadata.size() || !projectFile.commit())
        fail("Impossible d'écrire les réglages du projet.");
    if (!folder.rename(tree.absolutePath(), root)) fail("Impossible de créer le dossier " + root);
    return QFileInfo(root).absoluteFilePath();
}

Build::Process::Process() {
#if defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
}

#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void Build::Process::setupChildProcess() {
    ::setpgid(0, 0);
}
#endif

void Build::Process::stopTree(bool force) {
#ifdef Q_OS_UNIX
    const auto pid = static_cast<pid_t>(processId());
    if (pid > 0 && ::getpgid(pid) == pid && ::kill(-pid, force ? SIGKILL : SIGTERM) == 0) return;
#endif
    if (force) kill();
    else terminate();
}

Build::Build(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false, process_.errorString());
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) { stopped(code, status); });
}

Build::~Build() {
    process_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) {
        process_.stopTree(true);
        process_.waitForFinished(1500);
    }
}

void Build::start(const QString& root, const QString& directory, picodbg::Board board,
                  const Tools& tools, const QString& target, bool configureOnly) {
    if (busy()) fail("Une compilation est déjà en cours.");
    if (tools.cmake.isEmpty()) fail("CMake est introuvable. Vérifier les outils du projet.");
    const QDir build(directory);
    const QString queryDir = ".cmake/api/v1/query";
    if (!build.mkpath(queryDir)) fail("Impossible de créer le dossier de compilation.");
    QFile query(build.filePath(queryDir + "/codemodel-v2"));
    if (!query.open(QIODevice::WriteOnly)) fail("Impossible d'interroger la File API de CMake.");
    query.close();
    target_ = target;
    directory_ = directory;
    tool_ = tools.cmake;
    configureOnly_ = configureOnly;
    cancelled_ = false;
    artifacts_.clear();
    outputBuffer_.clear();
    process_.setWorkingDirectory(root);
    process_.setProcessEnvironment(tools.buildEnvironment());
    QStringList args{"-S", root, "-B", directory, "-DCMAKE_BUILD_TYPE=Debug",
                     "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DPICO_BOARD=" + boardName(board)};
    if (!tools.sdk.isEmpty()) args << "-DPICO_SDK_PATH=" + tools.sdk;
    if (!tools.ninja.isEmpty()) args << "-G" << "Ninja" << "-DCMAKE_MAKE_PROGRAM=" + tools.ninja;
    if (!tools.picotool.isEmpty()) args << "-Dpicotool_DIR=" + QFileInfo(tools.picotool).absolutePath();
    if (!tools.pioasm.isEmpty()) args << "-Dpioasm_DIR=" + QFileInfo(tools.pioasm).absolutePath();
    stage_ = Configure;
    run(args);
}

void Build::run(const QStringList& args) {
    if (output) output("\n" + tool_ + "  " + args.join("  ") + "\n\n");
    process_.start(tool_, args, QIODevice::ReadOnly);
}

void Build::cancel() {
    if (!busy() || cancelled_) return;
    cancelled_ = true;
    process_.stopTree(false);
    QTimer::singleShot(1500, this, [this] {
        if (busy() && cancelled_ && process_.state() != QProcess::NotRunning) process_.stopTree(true);
    });
}

void Build::readOutput(bool flush) {
    outputBuffer_ += process_.readAllStandardOutput();
    const auto length = flush || outputBuffer_.size() > 65536
        ? outputBuffer_.size() : outputBuffer_.lastIndexOf('\n') + 1;
    if (length > 0) {
        const auto text = QString::fromLocal8Bit(outputBuffer_.left(length));
        outputBuffer_.remove(0, length);
        if (output) output(text);
    }
}

void Build::stopped(int code, QProcess::ExitStatus status) {
    readOutput(true);
    if (cancelled_) { complete(false, "Compilation annulée."); return; }
    if (status != QProcess::NormalExit || code != 0) {
        complete(false, stage_ == Configure ? "La configuration CMake a échoué." : "La compilation a échoué.");
        return;
    }
    try {
        artifacts_ = readArtifacts(directory_);
        if (stage_ == Configure && !configureOnly_) {
            if (!target_.isEmpty() && std::none_of(artifacts_.begin(), artifacts_.end(),
                                                 [this](const Artifact& a) { return a.target == target_; }))
                fail("Cette cible n'existe plus. Sélectionner une autre cible.");
            QStringList arguments{"--build", directory_, "--config", "Debug", "--parallel", "2"};
            if (!target_.isEmpty()) arguments << "--target" << target_;
            stage_ = Compile;
            run(arguments);
            return;
        }
        complete(true, configureOnly_ ? "Projet configuré." : "Compilation réussie.");
    } catch (const std::exception& error) {
        complete(false, QString::fromUtf8(error.what()));
    }
}

void Build::complete(bool success, const QString& message) {
    if (!busy()) return;
    stage_ = Idle;
    if (finished) finished(success, message, artifacts_);
}
}
