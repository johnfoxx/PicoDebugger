#include "gdb_mi.hpp"
#include <stdexcept>

namespace picodbg {
const MiValue& MiValue::get(const QString& key) const {
    for (const auto& item : items) if (item.name == key) return item;
    static const MiValue empty;
    return empty;
}
namespace {
class Parser {
public:
    explicit Parser(const QByteArray& input) : s(input) {}
    const QByteArray& s;
    int at = 0;
    char peek() const { return at < s.size() ? s[at] : '\0'; }
    char take() { if (at >= s.size()) throw std::runtime_error("Truncated GDB/MI record"); return s[at++]; }
    QString word() {
        const int start = at;
        while (peek() && peek() != '=' && peek() != ',' && peek() != '}' && peek() != ']') ++at;
        return QString::fromUtf8(s.mid(start, at - start));
    }
    QString string() {
        if (take() != '"') throw std::runtime_error("Expected MI string");
        QByteArray out;
        while (peek() != '"') {
            char c = take();
            if (c == '\\') {
                c = take();
                if (c >= '0' && c <= '7') {
                    unsigned n = static_cast<unsigned>(c - '0');
                    for (int i = 0; i < 2 && peek() >= '0' && peek() <= '7'; ++i) n = n * 8 + static_cast<unsigned>(take() - '0');
                    c = static_cast<char>(n);
                } else {
                    switch (c) {
                    case 'n': c = '\n'; break; case 'r': c = '\r'; break; case 't': c = '\t'; break;
                    case 'b': c = '\b'; break; case 'f': c = '\f'; break; case 'v': c = '\v'; break;
                    case 'a': c = '\a'; break; case '\\': case '"': break;
                    default: throw std::runtime_error("Invalid MI string escape");
                    }
                }
            }
            out += c;
        }
        take(); return QString::fromUtf8(out);
    }
    MiValue value(unsigned depth = 0) {
        if (depth > 32) throw std::runtime_error("GDB/MI nesting limit");
        MiValue v;
        if (peek() == '"') v.text = string();
        else if (peek() == '{' || peek() == '[') {
            const char end = take() == '{' ? '}' : ']';
            while (peek() != end) {
                v.items.push_back(result(depth + 1));
                if (peek() != ',') break;
                take();
            }
            if (take() != end) throw std::runtime_error("Invalid MI container");
        } else throw std::runtime_error("Invalid MI value");
        return v;
    }
    MiValue result(unsigned depth = 0) {
        if (peek() == '"' || peek() == '{' || peek() == '[') return value(depth);
        const QString name = word();
        if (name.isEmpty() || take() != '=') throw std::runtime_error("Invalid MI result");
        MiValue v = value(depth); v.name = name; return v;
    }
};
}
MiRecord parseMi(const QByteArray& input) {
    const QByteArray line = input.trimmed();
    MiRecord r;
    if (line.isEmpty() || line == "(gdb)") return r;
    Parser p(line);
    const int start = p.at;
    while (p.peek() >= '0' && p.peek() <= '9') ++p.at;
    if (p.at > start) { bool ok; r.token = line.left(p.at).toLongLong(&ok); if (!ok) throw std::runtime_error("Invalid MI token"); }
    r.kind = p.take();
    if (r.kind == '~' || r.kind == '@' || r.kind == '&') r.data.text = p.string();
    else if (r.kind == '^' || r.kind == '*' || r.kind == '+' || r.kind == '=') {
        r.klass = p.word();
        while (p.peek() == ',') { p.take(); r.data.items.push_back(p.result()); }
    } else throw std::runtime_error("Unexpected GDB/MI output");
    if (p.at != line.size()) throw std::runtime_error("Trailing GDB/MI data");
    return r;
}
QString miQuote(const QString& value) {
    QByteArray result("\"");
    for (unsigned char c : value.toUtf8()) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32 || c >= 127) {
            result += '\\'; result += static_cast<char>('0' + ((c >> 6) & 7));
            result += static_cast<char>('0' + ((c >> 3) & 7)); result += static_cast<char>('0' + (c & 7));
        } else result += static_cast<char>(c);
    }
    result += '"'; return QString::fromLatin1(result);
}
GdbMi::GdbMi(QObject* parent) : QObject(parent) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { fail("Délai GDB dépassé : " + current_.text.left(100)); });
    connect(&process_, &QProcess::started, this, [this] { deadline_.stop(); if (started) started(); pump(); });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { consume(); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        const QString text = QString::fromUtf8(process_.readAllStandardError()); if (output) output(text.left(4000));
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!stopping_) fail("Processus GDB : " + process_.errorString());
    });
    connect(&process_, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) { if (!stopping_) fail(QString("GDB s'est arrêté (code %1)").arg(code)); });
}
GdbMi::~GdbMi() { stop(); }
void GdbMi::start(const QString& executable) {
    stop(); stopping_ = false; buffer_.clear();
    process_.setProgram(executable);
    // Never execute .gdbinit or auto-loaded target scripts from an ELF.
    process_.setArguments({"--quiet", "--nx", "--nh", "--interpreter=mi2", "-iex", "set auto-load off"});
    deadline_.start(8000); process_.start();
}
void GdbMi::stop() {
    stopping_ = true; deadline_.stop(); queue_.clear(); pending_ = -1; current_.reply = {};
    if (active()) { process_.kill(); process_.waitForFinished(1000); }
    buffer_.clear();
}
void GdbMi::fail(const QString& message) {
    if (stopping_) return;
    stop(); if (failed) failed(message);
}
void GdbMi::command(const QString& text, Reply reply, int timeout, bool inspection) {
    if (text.contains('\n') || text.contains('\r')) throw std::runtime_error("Multiline MI command rejected");
    if (queue_.size() >= 1024) {
        // A slow probe must not turn a GUI refresh into an uncaught exception.
        MiRecord error; error.kind = '^'; error.klass = "error";
        MiValue message; message.name = "msg"; message.text = "Trop de requêtes GDB en attente ; relire après leur traitement";
        error.data.items.push_back(message);
        if (reply) reply(error);
        return;
    }
    queue_.push_back({text, std::move(reply), timeout, inspection}); pump();
}
void GdbMi::cancelInspections() {
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->inspection) it = queue_.erase(it); else ++it;
    }
}
void GdbMi::pump() {
    if (stopping_ || process_.state() != QProcess::Running || pending_ >= 0 || queue_.empty()) return;
    current_ = std::move(queue_.front()); queue_.pop_front(); pending_ = ++nextToken_;
    process_.write(QByteArray::number(pending_) + current_.text.toUtf8() + '\n');
    deadline_.start(current_.timeout);
}
void GdbMi::consume() {
    buffer_ += process_.readAllStandardOutput();
    if (buffer_.size() > 4 * 1024 * 1024) { fail("Réponse GDB trop volumineuse"); return; }
    while (!stopping_) {
        const int end = buffer_.indexOf('\n'); if (end < 0) break;
        const QByteArray line = buffer_.left(end); buffer_.remove(0, end + 1);
        MiRecord r;
        try { r = parseMi(line); }
        catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); return; }
        if (!r.kind) continue;
        if (r.kind == '^') {
            if (r.token != pending_) { fail("Réponse GDB sans requête correspondante"); return; }
            deadline_.stop(); pending_ = -1;
            const auto reply = std::move(current_.reply); current_.reply = {};
            if (reply) reply(r);
            pump();
        } else if (r.kind == '~' || r.kind == '@' || r.kind == '&') {
            if (output) output(r.data.text.left(4000));
        } else if (notification) notification(r);
    }
}
}
