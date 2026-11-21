#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include <deque>
#include <functional>
#include <vector>

namespace picodbg {
struct MiValue {
    QString name, text;
    std::vector<MiValue> items;
    const MiValue& get(const QString& key) const;
};
struct MiRecord {
    qint64 token = -1;
    char kind = 0;
    QString klass;
    MiValue data;
    bool error() const { return kind == '^' && klass == "error"; }
};
MiRecord parseMi(const QByteArray& line);
QString miQuote(const QString& value);

// One outstanding token at a time; execution notifications are independent.
// All I/O is asynchronous on the owning Qt thread, including downloads.
class GdbMi : public QObject {
public:
    using Reply = std::function<void(const MiRecord&)>;
    explicit GdbMi(QObject* parent = nullptr);
    ~GdbMi();
    void start(const QString& executable);
    void stop();
    void command(const QString& text, Reply reply, int timeoutMs = 8000, bool inspection = false);
    void cancelInspections();
    bool active() const { return process_.state() != QProcess::NotRunning; }
    std::function<void()> started;
    std::function<void(const MiRecord&)> notification;
    std::function<void(const QString&)> failed;
    std::function<void(const QString&)> output;
private:
    struct Command { QString text; Reply reply; int timeout; bool inspection; };
    QProcess process_;
    QTimer deadline_;
    QByteArray buffer_;
    std::deque<Command> queue_;
    Command current_;
    qint64 nextToken_ = 0, pending_ = -1;
    bool stopping_ = false;
    void pump();
    void consume();
    void fail(const QString& message);
};
}
