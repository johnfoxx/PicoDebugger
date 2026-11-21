#include "code_editor.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolTip>
#include <algorithm>

namespace picoide {
namespace {
class Gutter : public QWidget {
public:
    explicit Gutter(CodeEditor* editor) : QWidget(editor), editor_(editor) {}
protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintGutter(event); }
private:
    CodeEditor* editor_;
};

class CHighlighter : public QSyntaxHighlighter {
public:
    explicit CHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
protected:
    void highlightBlock(const QString& line) override {
        static const QRegularExpression tokens(
            R"c((//.*|/\*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(^\s*#\s*\w+)|\b(0[xX][a-fA-F0-9]+|[0-9]+(?:\.[0-9]+)?)(?:[uUlLfF]*)\b|\b(auto|break|case|char|const|continue|default|do|double|else|enum|extern|float|for|goto|if|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while|_Alignas|_Alignof|_Atomic|_Bool|_Generic|_Noreturn|_Static_assert|_Thread_local|bool|true|false|NULL|u?int(?:8|16|32|64)_t|size_t)\b)c");
        setCurrentBlockState(0);
        int offset = 0;
        while (offset < line.size() || previousBlockState() == 1) {
            if (offset == 0 && previousBlockState() == 1) {
                const int end = line.indexOf("*/");
                setFormat(0, end < 0 ? line.size() : end + 2, QColor("#8b9db1"));
                if (end < 0) { setCurrentBlockState(1); return; }
                offset = end + 2;
            }
            const auto match = tokens.match(line, offset);
            if (!match.hasMatch()) break;
            offset = match.capturedEnd();
            const auto token = match.captured(1);
            QColor color;
            if (token == "/*") {
                const int end = line.indexOf("*/", offset);
                offset = end < 0 ? line.size() : end + 2;
                setFormat(match.capturedStart(), offset - match.capturedStart(), QColor("#8b9db1"));
                if (end < 0) { setCurrentBlockState(1); return; }
                continue;
            }
            if (token.startsWith("//")) color = QColor("#8b9db1");
            else if (!token.isEmpty()) color = QColor("#add7a2");
            else if (!match.captured(2).isEmpty()) color = QColor("#83cbde");
            else if (!match.captured(3).isEmpty()) color = QColor("#f5c47e");
            else color = QColor("#d5adf4");
            setFormat(match.capturedStart(), match.capturedLength(), color);
            if (offset >= line.size()) break;
        }
    }
};

int positionIn(const QTextBlock& block, int column) {
    return block.position() + std::max(0, std::min(column, block.length() - 1));
}
}

CodeEditor::CodeEditor(const Language& language, QWidget* parent)
    : QPlainTextEdit(parent), gutter_(new Gutter(this)), completer_(new QCompleter(this)),
      suggestions_(new QStandardItemModel(this)) {
    setObjectName("ideEditor");
    if (language.highlight == Highlight::C) new CHighlighter(document());
    auto codingFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codingFont.setStyleHint(QFont::Monospace);
    codingFont.setFixedPitch(true);
    setFont(codingFont);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(' ') * 4);
    setMouseTracking(true);
    completer_->setWidget(this);
    completer_->setModel(suggestions_);
    completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    connect(completer_, QOverload<const QModelIndex&>::of(&QCompleter::activated), this, [this](const QModelIndex& index) {
        applyCompletion(index.data(Qt::UserRole).toJsonObject());
    });
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutter(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy) gutter_->scroll(0, dy);
        else gutter_->update(0, rect.y(), gutter_->width(), rect.height());
        if (rect.contains(viewport()->rect())) updateGutter();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { decorate(); gutter_->update(); });
    updateGutter();
    decorate();
}

int CodeEditor::gutterWidth() const {
    return 17 + fontMetrics().horizontalAdvance('9') * QString::number(qMax(10, blockCount())).size();
}

void CodeEditor::updateGutter() {
    if (viewportMargins().left() != gutterWidth()) setViewportMargins(gutterWidth(), 0, 0, 0);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    gutter_->setGeometry(area.left(), area.top(), gutterWidth(), area.height());
}

void CodeEditor::paintGutter(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), QColor("#192635"));
    auto block = firstVisibleBlock();
    const int activeLine = textCursor().blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && top + height >= event->rect().top()) {
            painter.setPen(block.blockNumber() == activeLine ? QColor("#ecf4fc") : QColor("#8cabc1"));
            painter.drawText(0, top, gutter_->width() - 7, fontMetrics().height(),
                             Qt::AlignRight, QString::number(block.blockNumber() + 1));
        }
        block = block.next();
        top += height;
    }
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    const auto key = event->key();
    if (completer_->popup()->isVisible()) {
        switch (key) {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Escape: case Qt::Key_Tab:
        case Qt::Key_Backtab: event->ignore(); return;
        default: completer_->popup()->hide(); break;
        }
    }
    if (event->modifiers() == Qt::ControlModifier && key == Qt::Key_Space) {
        if (completionRequested) completionRequested();
        return;
    }
    if (event->modifiers() == Qt::NoModifier && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
        auto cursor = textCursor();
        const auto before = cursor.block().text().left(cursor.positionInBlock());
        const auto indentation = QRegularExpression("^\\s*").match(before).captured();
        cursor.insertText("\n" + indentation + (before.trimmed().endsWith('{') ? QString(4, ' ') : QString()));
        setTextCursor(cursor);
        return;
    }
    if (event->modifiers() == Qt::NoModifier && key == Qt::Key_Tab && !textCursor().hasSelection()) {
        insertPlainText(QString(4 - textCursor().positionInBlock() % 4, ' '));
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
    if (completionRequested && (event->text() == "." || event->text() == ">")) {
        const auto cursor = textCursor();
        if (cursor.block().text().left(cursor.positionInBlock()).endsWith("->") || event->text() == ".")
            completionRequested();
    }
}

void CodeEditor::showCompletions(const QJsonArray& items) {
    suggestions_->clear();
    for (const auto& value : items) {
        auto item = value.toObject();
        if (item.value("insertTextFormat").toInt(1) != 1) continue;
        const auto label = item.value("label").toString();
        if (label.isEmpty()) continue;
        auto* row = new QStandardItem(label);
        row->setData(item, Qt::UserRole);
        row->setToolTip(item.value("detail").toString());
        suggestions_->appendRow(row);
    }
    if (!suggestions_->rowCount()) return;
    completer_->setCompletionPrefix(QString());
    auto area = cursorRect();
    area.setWidth(qBound(230, completer_->popup()->sizeHintForColumn(0) + 30, 520));
    completer_->complete(area);
    completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
}

QTextCursor CodeEditor::rangeCursor(int line, int column, int endLine, int endColumn) const {
    const auto start = document()->findBlockByNumber(line);
    const auto end = document()->findBlockByNumber(endLine);
    if (!start.isValid() || !end.isValid()) return {};
    QTextCursor cursor(start);
    cursor.setPosition(positionIn(start, column));
    cursor.setPosition(qMax(cursor.position(), positionIn(end, endColumn)), QTextCursor::KeepAnchor);
    return cursor;
}

void CodeEditor::applyCompletion(const QJsonObject& item) {
    const auto edit = item.value("textEdit").toObject();
    const auto range = edit.value("range").toObject();
    auto cursor = textCursor();
    if (!range.isEmpty()) {
        const auto start = range.value("start").toObject(), end = range.value("end").toObject();
        cursor = rangeCursor(start.value("line").toInt(-1), start.value("character").toInt(),
                             end.value("line").toInt(-1), end.value("character").toInt());
        if (cursor.isNull()) return;
    } else {
        const auto prefix = cursor.block().text().left(cursor.positionInBlock());
        const auto identifier = QRegularExpression("[A-Za-z0-9_]+$").match(prefix);
        if (identifier.hasMatch())
            cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, identifier.capturedLength());
    }
    const auto text = edit.value("newText").toString(item.value("insertText").toString(item.value("label").toString().trimmed()));
    cursor.insertText(text);
    setTextCursor(cursor);
}

void CodeEditor::showProblems(const std::vector<Problem>& problems) {
    problems_ = problems;
    decorate();
}

void CodeEditor::decorate() {
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection active;
    active.cursor = textCursor();
    active.cursor.clearSelection();
    active.format.setBackground(QColor("#1c2b3b"));
    active.format.setProperty(QTextFormat::FullWidthSelection, true);
    selections.push_back(active);
    for (const auto& problem : problems_) {
        QTextEdit::ExtraSelection issue;
        issue.cursor = rangeCursor(problem.line, problem.column, problem.endLine, problem.endColumn);
        if (issue.cursor.isNull()) continue;
        if (!issue.cursor.hasSelection()) issue.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        issue.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        issue.format.setUnderlineColor(problem.severity == 1 ? QColor("#ff9490") : QColor("#f1ce80"));
        selections.push_back(issue);
    }
    setExtraSelections(selections);
}

bool CodeEditor::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const int line = cursorForPosition(help->pos()).blockNumber();
        QStringList messages;
        for (const auto& issue : problems_)
            if (line >= issue.line && line <= issue.endLine) messages << issue.message;
        if (!messages.isEmpty()) {
            QToolTip::showText(help->globalPos(), "<p>" + messages.join("\n").toHtmlEscaped().replace("\n", "<br>") + "</p>", this);
            return true;
        }
    }
    return QPlainTextEdit::viewportEvent(event);
}

void CodeEditor::goTo(int line, int column) {
    const auto cursor = rangeCursor(line, column, line, column);
    if (!cursor.isNull()) {
        setTextCursor(cursor);
        centerCursor();
    }
    setFocus();
}
}
