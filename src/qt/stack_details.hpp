#pragma once
#include "picodbg/target.hpp"
#include <QHash>
#include <QProcess>
#include <QTimer>
#include <QVector>
#include <functional>
#include <map>

struct FrameType {
    enum Kind { Unknown, Unsigned, Signed, Pointer, Float, Boolean };
    QString name;
    Kind kind = Unknown;
    unsigned size = 0;
    unsigned count = 1;
    bool readOnly = false;
    bool isArray = false;
};

struct FrameField {
    QString name, role, rendered, expression;
    FrameType type;
    uint32_t address = 0;
    int reg = -1;
    bool inMemory = false;
    bool editable = false;
    int savedRegister = -1;
    bool savedFramePointer = false;
};

struct FrameDetails {
    QVector<FrameField> fields;
    bool hasBase = false;
    uint32_t base = 0;
    FrameType result;
};

FrameType frameType(QString name);
QString frameValue(const picodbg::DebugTarget& target, unsigned core, const FrameField& field);
uint64_t parseFrameValue(const QString& text, const FrameType& type);
bool stackMemory(const picodbg::DebugTarget& target, uint32_t address, unsigned size);

class StackDebugInfo : public QObject {
public:
    explicit StackDebugInfo(QObject* parent);
    ~StackDebugInfo() override;
    void index(const QString& path, const QString& compiler);
    FrameDetails frame(const picodbg::DebugTarget& target, unsigned core, uint32_t pc,
                       const picodbg::StackFrame* caller = nullptr) const;
    QString status() const { return status_; }
    std::function<void()> updated;
private:
    struct Variable {
        QString name, role, location;
        FrameType type;
        uint32_t begin, end;
        int64_t locationList;
    };
    struct Function {
        uint32_t end;
        QString base;
        FrameType result;
        QVector<Variable> variables;
    };
    struct SavedSlot { unsigned reg; int offset; bool isReturn; };
    struct Rule {
        uint32_t end;
        unsigned reg;
        int offset;
        QVector<SavedSlot> saved;
    };
    struct Location { uint32_t begin, end; QString expression; };
    QProcess process_;
    QTimer deadline_;
    QByteArray output_;
    QString status_;
    std::map<uint32_t, Function> functions_;
    std::map<uint32_t, Rule> rules_;
    std::map<uint32_t, QVector<Location>> locations_;
    void stop();
    void readOutput();
    void parse();
};
