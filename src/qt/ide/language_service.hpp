#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <functional>

namespace picoide {

enum class Highlight { Plain, C };
struct Language {
    Language(QString name = {}, Highlight syntax = Highlight::Plain) : id(std::move(name)), highlight(syntax) {}
    QString id;
    Highlight highlight = Highlight::Plain;
};
Language languageForFile(const QString& path);

struct LanguageServer {
    QString name, executable;
    QStringList arguments;
};
LanguageServer clangdServer(const QString& executable, const QString& buildDirectory, const QString& compiler);

class LanguageService : public QObject {
public:
    explicit LanguageService(QObject* parent);
    ~LanguageService() override;
    void start(const LanguageServer& server, const QString& root);
    void open(const QString& path, const Language& language, const QString& text);
    void change(const QString& path, const QString& text);
    void save(const QString& path);
    void close(const QString& path);
    void complete(const QString& path, const QString& text, int line, int column,
                  std::function<void(const QJsonArray&)> receive);
    void definition(const QString& path, const QString& text, int line, int column,
                    std::function<void(const QJsonArray&)> receive);
    bool ready() const { return ready_; }
    std::function<void(const QString&)> status;
    std::function<void(const QString&, const QJsonArray&)> diagnostics;

private:
    struct Document {
        Language language;
        QString text;
        int version = 1;
        bool opened = false;
    };
    using Reply = std::function<void(const QJsonObject&)>;
    QProcess process_;
    QTimer initializeDeadline_;
    QHash<QString, Document> documents_;
    QHash<int, Reply> pending_;
    QByteArray buffer_;
    QString root_, name_;
    int nextId_ = 0;
    bool ready_ = false;
    void stop();
    void initialize();
    void send(const QJsonObject& message);
    void notify(const QString& method, const QJsonObject& params);
    void request(const QString& method, const QJsonObject& params, Reply receive);
    void didOpen(const QString& path, Document& document);
    void consume();
    void dispatch(const QJsonObject& message);
    void fail(const QString& message);
};

}
