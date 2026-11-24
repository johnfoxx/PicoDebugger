#pragma once

#include "language_service.hpp"
#include "text_search.hpp"
#include <QPlainTextEdit>
#include <QTimer>
#include <vector>

class QCompleter;
class QStandardItemModel;
class QSyntaxHighlighter;

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
    QString sourceText() const;
    void setSearchHighlights(std::vector<TextRange> ranges);
    void applySearchEdit(const TextEdit& edit);
    void toggleLineComment();
    std::function<void()> completionRequested;
    std::function<void(int, int)> definitionRequested;
    std::function<void()> focused;
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    QWidget* gutter_;
    QCompleter* completer_;
    QStandardItemModel* suggestions_;
    QSyntaxHighlighter* highlighter_ = nullptr;
    std::vector<Problem> problems_;
    std::vector<TextRange> searchHighlights_;
    QTextCursor hoveredDefinition_, clickedDefinition_;
    QTimer definitionFeedback_;
    int executionLine_ = 0;
    int selectionLine_ = 0;
    bool cComments_ = false;
    void updateGutter();
    void applyCompletion(const QJsonObject& item);
    void decorate();
    QTextCursor symbolAt(const QPoint& position) const;
    void setHoveredDefinition(const QTextCursor& symbol);
    QTextCursor rangeCursor(int line, int column, int endLine, int endColumn) const;
};

}
