#include "language_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QUrl>

namespace picoide {
namespace {
QString uri(const QString& path) { return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded); }
QJsonObject textDocument(const QString& path) { return {{"uri", uri(path)}}; }
constexpr int maxMessageBytes = 16 * 1024 * 1024;
}

Language languageForFile(const QString& path) {
    const auto extension = QFileInfo(path).suffix();
    if (extension == "c" || extension == "h") return {"c", Highlight::C};
    return {};
}

LanguageServer clangdServer(const QString& executable, const QString& buildDirectory, const QString& compiler) {
    LanguageServer server{"clangd", executable, {"--clang-tidy", "--log=error", "--header-insertion=never"}};
    if (QFileInfo(QDir(buildDirectory).filePath("compile_commands.json")).isFile())
        server.arguments << "--compile-commands-dir=" + buildDirectory;
    const QRegularExpression pattern("[,*?\\[\\]]");
    if (!compiler.isEmpty() && !compiler.contains(pattern)) {
        const auto canonical = QFileInfo(compiler).canonicalFilePath();
        auto allow = compiler;
        if (!canonical.isEmpty() && canonical != compiler && !canonical.contains(pattern))
            allow += "," + canonical;
        server.arguments << "--query-driver=" + allow;
    }
    return server;
}

LanguageService::LanguageService(QObject* parent) : QObject(parent) {
    initializeDeadline_.setSingleShot(true);
    connect(&initializeDeadline_, &QTimer::timeout, this, [this] { fail("Délai de démarrage dépassé · " + name_); });
    connect(&process_, &QProcess::started, this, [this] { initialize(); });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { consume(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail("Impossible de démarrer " + name_ + " · " + process_.errorString());
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { fail(name_ + " s'est arrêté."); });
}

LanguageService::~LanguageService() {
    process_.disconnect(this);
    stop();
}

void LanguageService::stop() {
    initializeDeadline_.stop();
    ready_ = false;
    pending_.clear();
    buffer_.clear();
    for (auto& document : documents_) document.opened = false;
    if (process_.state() != QProcess::NotRunning) {
        const QSignalBlocker blocker(&process_);
        process_.kill();
        process_.waitForFinished(1000);
        process_.readAllStandardOutput();
        process_.readAllStandardError();
    }
}

void LanguageService::start(const LanguageServer& server, const QString& root) {
    stop();
    root_ = root;
    name_ = server.name;
    if (server.executable.isEmpty()) {
        if (status) status(name_.isEmpty() ? QString() : name_ + " introuvable · Outils du projet");
        return;
    }
    if (status) status("Démarrage · " + name_);
    process_.setWorkingDirectory(root);
    initializeDeadline_.start(10000);
    process_.start(server.executable, server.arguments);
}

void LanguageService::initialize() {
    const QJsonObject completionItem{{"snippetSupport", false}, {"insertReplaceSupport", false}};
    const QJsonObject capabilities{
        {"general", QJsonObject{{"positionEncodings", QJsonArray{"utf-16"}}}},
        {"textDocument", QJsonObject{{"completion", QJsonObject{{"completionItem", completionItem}}},
                                     {"publishDiagnostics", QJsonObject{{"versionSupport", true}}}}}
    };
    request("initialize", {{"processId", double(QCoreApplication::applicationPid())},
                           {"rootUri", uri(root_)}, {"capabilities", capabilities}},
        [this](const QJsonObject& response) {
            if (!response.value("result").isObject()) {
                fail(name_ + " n'a pas accepté l'initialisation.");
                return;
            }
            initializeDeadline_.stop();
            ready_ = true;
            notify("initialized", {});
            for (auto it = documents_.begin(); it != documents_.end(); ++it) didOpen(it.key(), it.value());
            if (status) status(name_ + " prêt");
        });
}

void LanguageService::open(const QString& path, const Language& language, const QString& text) {
    if (language.id.isEmpty()) return;
    close(path);
    auto& document = documents_[path];
    document.language = language;
    document.text = text;
    document.version = 1;
    if (ready_) didOpen(path, document);
}

void LanguageService::didOpen(const QString& path, Document& document) {
    auto item = textDocument(path);
    item["languageId"] = document.language.id;
    item["version"] = document.version;
    item["text"] = document.text;
    notify("textDocument/didOpen", {{"textDocument", item}});
    document.opened = true;
}

void LanguageService::change(const QString& path, const QString& text) {
    auto it = documents_.find(path);
    if (it == documents_.end() || it->text == text) return;
    it->text = text;
    auto item = textDocument(path);
    item["version"] = ++it->version;
    if (it->opened)
        notify("textDocument/didChange", {{"textDocument", item},
                                         {"contentChanges", QJsonArray{QJsonObject{{"text", text}}}}});
}

void LanguageService::save(const QString& path) {
    if (documents_.value(path).opened) notify("textDocument/didSave", {{"textDocument", textDocument(path)}});
}

void LanguageService::close(const QString& path) {
    if (documents_.take(path).opened) notify("textDocument/didClose", {{"textDocument", textDocument(path)}});
}

void LanguageService::complete(const QString& path, const QString& text, int line, int column,
                               std::function<void(const QJsonArray&)> receive) {
    if (!documents_.value(path).opened || pending_.size() >= 32) return;
    change(path, text);
    request("textDocument/completion", {{"textDocument", textDocument(path)},
                {"position", QJsonObject{{"line", line}, {"character", column}}},
                {"context", QJsonObject{{"triggerKind", 1}}}},
        [receive](const QJsonObject& response) {
            const auto result = response.value("result");
            receive(result.isArray() ? result.toArray() : result.toObject().value("items").toArray());
        });
}

void LanguageService::notify(const QString& method, const QJsonObject& params) {
    send({{"method", method}, {"params", params}});
}
void LanguageService::definition(const QString& path, const QString& text, int line, int column,
                                 std::function<void(const QJsonArray&)> receive) {
    if (!documents_.value(path).opened || pending_.size() >= 32) { receive({}); return; }
    change(path, text);
    request("textDocument/definition", {{"textDocument", textDocument(path)},
                {"position", QJsonObject{{"line", line}, {"character", column}}}},
        [receive](const QJsonObject& response) {
            const auto result = response.value("result");
            receive(result.isArray() ? result.toArray() : result.isObject() ? QJsonArray{result} : QJsonArray{});
        });
}

void LanguageService::request(const QString& method, const QJsonObject& params, Reply receive) {
    const int id = ++nextId_;
    pending_.insert(id, std::move(receive));
    send({{"id", id}, {"method", method}, {"params", params}});
}

void LanguageService::send(const QJsonObject& value) {
    auto message = value;
    message["jsonrpc"] = "2.0";
    const auto body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    process_.write("Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
}

void LanguageService::consume() {
    buffer_ += process_.readAllStandardOutput();
    while (!buffer_.isEmpty()) {
        const int end = buffer_.indexOf("\r\n\r\n");
        if (end < 0) {
            if (buffer_.size() > 4096) fail("En-tête LSP invalide.");
            return;
        }
        int length = -1;
        for (const auto& line : buffer_.left(end).split('\n')) {
            if (!line.toLower().startsWith("content-length:")) continue;
            bool valid;
            length = line.mid(15).trimmed().toInt(&valid);
            if (!valid) length = -1;
            break;
        }
        if (length < 0 || length > maxMessageBytes) {
            fail("Taille de message LSP invalide.");
            return;
        }
        if (buffer_.size() - end - 4 < length) return;
        auto message = QJsonDocument::fromJson(buffer_.mid(end + 4, length));
        buffer_.remove(0, end + length + 4);
        if (!message.isObject()) {
            fail("Message JSON LSP invalide.");
            return;
        }
        dispatch(message.object());
    }
}

void LanguageService::dispatch(const QJsonObject& message) {
    const auto method = message.value("method").toString();
    if (method.isEmpty()) {
        const auto reply = pending_.take(message.value("id").toInt());
        if (reply) reply(message);
    } else if (message.contains("id")) {
        send({{"id", message.value("id")}, {"error", QJsonObject{{"code", -32601}, {"message", "Method not supported"}}}});
    } else if (method == "textDocument/publishDiagnostics") {
        const auto params = message.value("params").toObject();
        const auto path = QUrl(params.value("uri").toString()).toLocalFile();
        const auto document = documents_.constFind(path);
        if (document != documents_.end() && document->opened && diagnostics &&
            params.value("version").toInt(document->version) == document->version)
            diagnostics(path, params.value("diagnostics").toArray());
    }
}

void LanguageService::fail(const QString& message) {
    stop();
    if (status) status(message);
}

}
