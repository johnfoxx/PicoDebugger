#pragma once

#include "language_service.hpp"
#include <QPlainTextEdit>
#include <vector>

class QCompleter;
class QStandardItemModel;

namespace picoide {

struct Problem {
    QString path;
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    int severity = 1;
    QString message;
};

class CodeEditor : public QPlainTextEdit {
public:
    CodeEditor(const Language& language, QWidget* parent);
    void showCompletions(const QJsonArray& items);
    void showProblems(const std::vector<Problem>& problems);
    void goTo(int line, int column);
    void revealDebugLine(int line);
    void setDebugLines(int executionLine, int selectionLine);
    std::function<void()> completionRequested;
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    QWidget* gutter_;
    QCompleter* completer_;
    QStandardItemModel* suggestions_;
    std::vector<Problem> problems_;
    int executionLine_ = 0;
    int selectionLine_ = 0;
    void updateGutter();
    void applyCompletion(const QJsonObject& item);
    void decorate();
    QTextCursor rangeCursor(int line, int column, int endLine, int endColumn) const;
};

}
