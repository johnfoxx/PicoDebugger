#pragma once

#include <QRegularExpression>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

class QDirIterator;

namespace picoide {

enum class TextOperation { Replace, BeforeMatch, AfterMatch, AtStart, AtEnd };

struct TextRange {
    int start;
    int length;
    int end() const { return start + length; }
};

struct TextEdit {
    QString path, before, after;
    int count = 0;
};

QRegularExpression searchExpression(const QString& query, bool regex, bool matchCase);
bool needsPattern(TextOperation operation);
QString replacementFor(const QRegularExpressionMatch& match, const QString& text, bool regex);
QString replaceMatch(const QRegularExpressionMatch& match, const QString& text, bool regex, TextOperation operation);
bool transformText(TextEdit& edit, const QRegularExpression& expression, const QString& replacement,
                   bool regex, TextOperation operation);

void findTodo(const QString& path, const QString& text,
              const std::function<void(TextRange, const QString&)>& found);
bool isDoxygenComment(const QString& text);
const QRegularExpression& doxygenCommand();
void findDoxygen(const QString& path, const QString& text,
                 const std::function<void(TextRange, const QString&)>& found);

class ProjectTextScan : public QObject {
public:
    explicit ProjectTextScan(QObject* parent);
    ~ProjectTextScan() override;
    void start(const QString& root);
    void cancel();
    bool active() const { return timer_.isActive(); }
    std::function<void(const QString&)> file;
    std::function<void()> finished;

private:
    QTimer timer_;
    std::vector<std::unique_ptr<QDirIterator>> directories_;
    void addDirectory(const QString& path);
    void advance();
};

}
