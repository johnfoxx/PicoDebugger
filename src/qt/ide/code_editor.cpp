#include "code_editor.hpp"
#include "../source_map.hpp"
#include "../window_theme.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QCursor>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QScrollBar>
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
        while (offset < line.size() || (offset == 0 && previousBlockState() > 0)) {
            if (offset == 0 && previousBlockState() > 0) {
                const int end = line.indexOf("*/");
                highlightComment(line.left(end < 0 ? line.size() : end + 2), 0, previousBlockState() == 2);
                if (end < 0) { setCurrentBlockState(previousBlockState()); return; }
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
                const auto comment = line.mid(match.capturedStart(), offset - match.capturedStart());
                const bool documentation = isDoxygenComment(comment);
                highlightComment(comment, match.capturedStart(), documentation);
                if (end < 0) { setCurrentBlockState(documentation ? 2 : 1); return; }
                continue;
            }
            if (token.startsWith("//")) {
                highlightComment(token, match.capturedStart(), isDoxygenComment(token));
                break;
            }
            if (!token.isEmpty()) color = picoThemeColor("#add7a2");
            else if (!match.captured(2).isEmpty()) color = picoThemeColor("#83cbde");
            else if (!match.captured(3).isEmpty()) color = picoThemeColor("#f5c47e");
            else color = picoThemeColor("#d5adf4");
            setFormat(match.capturedStart(), match.capturedLength(), color);
            if (offset >= line.size()) break;
        }
    }
private:
    void highlightComment(const QString& comment, int offset, bool documentation) {
        setFormat(offset, comment.size(), picoThemeColor("#8b9db1"));
        if (!documentation) return;
        auto tags = doxygenCommand().globalMatch(comment);
        while (tags.hasNext()) {
            const auto tag = tags.next();
            QTextCharFormat format;
            format.setForeground(picoThemeColor("#83cbde"));
            format.setFontWeight(QFont::DemiBold);
            setFormat(offset + tag.capturedStart(), tag.capturedLength(), format);
        }
    }
};

int positionIn(const QTextBlock& block, int column) {
    return block.position() + std::max(0, std::min(column, block.length() - 1));
}
}

CodeEditor::CodeEditor(const Language& language, QWidget* parent)
    : QPlainTextEdit(parent), gutter_(new Gutter(this)), completer_(new QCompleter(this)),
      suggestions_(new QStandardItemModel(this)), cComments_(language.highlight == Highlight::C) {
    setObjectName("ideEditor");
    if (language.highlight == Highlight::C) highlighter_ = new CHighlighter(document());
    auto codingFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codingFont.setStyleHint(QFont::Monospace);
    codingFont.setFixedPitch(true);
    setFont(codingFont);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(' ') * 4);
    viewport()->setMouseTracking(true);
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
        if (dy && !searchHighlights_.empty()) decorate();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { decorate(); gutter_->update(); });
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        if (clickedDefinition_.isNull() && hoveredDefinition_.isNull()) return;
        definitionFeedback_.stop();
        clickedDefinition_ = {};
        setHoveredDefinition({});
        decorate();
    });
    definitionFeedback_.setSingleShot(true);
    connect(&definitionFeedback_, &QTimer::timeout, this, [this] {
        auto symbol = clickedDefinition_;
        clickedDefinition_ = {};
        decorate();
        symbol.setPosition(symbol.selectionStart());
        if (definitionRequested) definitionRequested(symbol.blockNumber(), symbol.positionInBlock());
    });
    updateGutter();
    decorate();
}

int CodeEditor::gutterWidth() const {
    return 27 + fontMetrics().horizontalAdvance('9') * QString::number(qMax(10, blockCount())).size();
}

void CodeEditor::updateGutter() {
    if (viewportMargins().left() != gutterWidth()) setViewportMargins(gutterWidth(), 0, 0, 0);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    gutter_->setGeometry(area.left(), area.top(), gutterWidth(), area.height());
    if (!searchHighlights_.empty()) decorate();
}

void CodeEditor::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() != QEvent::PaletteChange) return;
    if (highlighter_) highlighter_->rehighlight();
    decorate();
    gutter_->update();
}

void CodeEditor::paintGutter(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
    auto block = firstVisibleBlock();
    const int activeLine = textCursor().blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && top + height >= event->rect().top()) {
            const int line = block.blockNumber() + 1;
            if (line == executionLine_) {
                const int center = top + height / 2;
                painter.setPen(Qt::NoPen);
                painter.setBrush(picoThemeColor("#8ce4d6"));
                painter.drawPolygon(QPolygon({{5, center - 5}, {14, center}, {5, center + 5}}));
            }
            painter.setPen(line == executionLine_ ? picoThemeColor("#8ce4d6") :
                block.blockNumber() == activeLine ? palette().color(QPalette::Text) : picoThemeColor("#a8afb5"));
            painter.drawText(17, top, gutter_->width() - 24, fontMetrics().height(), Qt::AlignRight, QString::number(line));
        }
        block = block.next();
        top += height;
    }
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Control) setHoveredDefinition(symbolAt(viewport()->mapFromGlobal(QCursor::pos())));
    else if (event->modifiers() != Qt::ControlModifier) setHoveredDefinition({});
    if (isReadOnly()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    const auto key = event->key();
    if ((event->modifiers() & Qt::ControlModifier) &&
        (key == Qt::Key_Slash || (key == Qt::Key_C && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)))) {
        toggleLineComment();
        return;
    }
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

void CodeEditor::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Control) setHoveredDefinition({});
    QPlainTextEdit::keyReleaseEvent(event);
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

QString CodeEditor::sourceText() const {
    auto text = document()->toRawText();
    text.replace(QChar::ParagraphSeparator, '\n');
    text.replace(QChar::LineSeparator, '\n');
    return text;
}

void CodeEditor::setSearchHighlights(std::vector<TextRange> ranges) {
    searchHighlights_ = std::move(ranges);
    decorate();
}

void CodeEditor::applySearchEdit(const TextEdit& edit) {
    if (edit.before == edit.after) return;
    const int beforeSize = static_cast<int>(edit.before.size());
    const int afterSize = static_cast<int>(edit.after.size());
    const int common = std::min(beforeSize, afterSize);
    int start = 0;
    while (start < common && edit.before.at(start) == edit.after.at(start)) ++start;
    if (start && edit.before.at(start - 1).isHighSurrogate()) --start;
    int beforeEnd = beforeSize;
    int afterEnd = afterSize;
    while (beforeEnd > start && afterEnd > start &&
           edit.before.at(beforeEnd - 1) == edit.after.at(afterEnd - 1)) {
        --beforeEnd;
        --afterEnd;
    }
    if (beforeEnd < beforeSize && edit.before.at(beforeEnd).isLowSurrogate()) {
        ++beforeEnd;
        ++afterEnd;
    }
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(beforeEnd, QTextCursor::KeepAnchor);
    cursor.insertText(edit.after.mid(start, afterEnd - start));
    cursor.endEditBlock();
}

void CodeEditor::decorate() {
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection active;
    active.cursor = textCursor();
    active.cursor.clearSelection();
    active.format.setBackground(palette().color(QPalette::AlternateBase));
    active.format.setProperty(QTextFormat::FullWidthSelection, true);
    selections.push_back(active);
    const auto debugLine = [&](int line, const QString& color) {
        if (line <= 0) return;
        const auto block = document()->findBlockByNumber(line - 1);
        if (!block.isValid()) return;
        QTextEdit::ExtraSelection highlight;
        highlight.cursor = QTextCursor(block);
        highlight.format.setBackground(QColor(color));
        highlight.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.push_back(highlight);
    };
    debugLine(selectionLine_, picosource::selectionColor());
    debugLine(executionLine_, picosource::executionColor());
    if (!searchHighlights_.empty()) {
        const int start = firstVisibleBlock().position();
        const auto bottom = cursorForPosition(QPoint(0, viewport()->height())).block();
        const int end = bottom.position() + bottom.length();
        const auto first = std::lower_bound(searchHighlights_.begin(), searchHighlights_.end(), start,
            [](const TextRange& range, int position) { return range.end() < position; });
        const int documentEnd = document()->characterCount() - 1;
        for (auto it = first; it != searchHighlights_.end() && it->start <= end; ++it) {
            if (it->end() > documentEnd) break;
            QTextEdit::ExtraSelection highlight;
            highlight.cursor = QTextCursor(document());
            highlight.cursor.setPosition(it->start);
            highlight.cursor.setPosition(it->end(), QTextCursor::KeepAnchor);
            if (!it->length) {
                if (highlight.cursor.atBlockEnd()) highlight.cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
                else highlight.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            }
            highlight.format.setBackground(picoThemeColor("#655021"));
            selections.push_back(highlight);
        }
    }
    for (const auto& problem : problems_) {
        QTextEdit::ExtraSelection issue;
        issue.cursor = rangeCursor(problem.line, problem.column, problem.endLine, problem.endColumn);
        if (issue.cursor.isNull()) continue;
        if (!issue.cursor.hasSelection()) issue.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        issue.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        issue.format.setUnderlineColor(problem.severity == 1 ? picoThemeColor("#ff9490") : picoThemeColor("#f1ce80"));
        selections.push_back(issue);
    }
    const bool clicked = !clickedDefinition_.isNull();
    const auto& link = clicked ? clickedDefinition_ : hoveredDefinition_;
    if (!link.isNull()) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = link;
        const auto color = picoThemeColor(clicked ? "#8ce4d6" : "#83cbde");
        selection.format.setForeground(color);
        selection.format.setUnderlineColor(color);
        selection.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        if (clicked) selection.format.setBackground(picoThemeColor("#285168"));
        selections.push_back(selection);
    }
    setExtraSelections(selections);
}

bool CodeEditor::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave) setHoveredDefinition({});
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

void CodeEditor::focusInEvent(QFocusEvent* event) {
    QPlainTextEdit::focusInEvent(event);
    if (focused) focused();
}

void CodeEditor::focusOutEvent(QFocusEvent* event) {
    setHoveredDefinition({});
    QPlainTextEdit::focusOutEvent(event);
}

QTextCursor CodeEditor::symbolAt(const QPoint& position) const {
    if (!definitionRequested || !viewport()->rect().contains(position)) return {};
    const auto point = cursorForPosition(position);
    const auto block = point.block();
    static const QRegularExpression identifier("\\b[A-Za-z_][A-Za-z_0-9]*\\b");
    auto matches = identifier.globalMatch(block.text());
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (point.positionInBlock() < match.capturedStart()) break;
        if (point.positionInBlock() > match.capturedEnd()) continue;
        QTextCursor symbol(block);
        symbol.setPosition(block.position() + match.capturedStart());
        const auto start = cursorRect(symbol);
        symbol.setPosition(block.position() + match.capturedEnd(), QTextCursor::KeepAnchor);
        if (position.y() >= start.top() && position.y() <= start.bottom() &&
            position.x() >= start.left() && position.x() < cursorRect(symbol).left()) return symbol;
    }
    return {};
}

void CodeEditor::setHoveredDefinition(const QTextCursor& symbol) {
    if (hoveredDefinition_ == symbol && hoveredDefinition_.anchor() == symbol.anchor()) return;
    hoveredDefinition_ = symbol;
    viewport()->setCursor(symbol.isNull() ? Qt::IBeamCursor : Qt::PointingHandCursor);
    decorate();
}

void CodeEditor::mouseMoveEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseMoveEvent(event);
    setHoveredDefinition(event->modifiers() == Qt::ControlModifier && event->buttons() == Qt::NoButton
        ? symbolAt(event->pos()) : QTextCursor());
}

void CodeEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
        const auto symbol = symbolAt(event->pos());
        if (!symbol.isNull()) {
            clickedDefinition_ = symbol;
            setHoveredDefinition(symbol);
            decorate();
            definitionFeedback_.start(120);
            event->accept();
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::toggleLineComment() {
    if (isReadOnly() || !cComments_) return;
    const auto selection = textCursor();
    const auto first = document()->findBlock(selection.selectionStart());
    const auto last = document()->findBlock(selection.hasSelection() ? selection.selectionEnd() - 1 : selection.position());
    QVector<int> positions;
    bool allCommented = true;
    static const QRegularExpression indent("^[ \\t]*");
    for (auto block = first; block.isValid(); block = block.next()) {
        const auto line = block.text();
        const auto start = indent.match(line).capturedLength();
        if (start < line.size() || !selection.hasSelection()) {
            positions.push_back(block.position() + start);
            allCommented = allCommented && line.mid(start).startsWith("//");
        }
        if (block == last) break;
    }
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (auto it = positions.crbegin(); it != positions.crend(); ++it) {
        edit.setPosition(*it);
        if (allCommented) {
            edit.setPosition(*it + (document()->characterAt(*it + 2) == ' ' ? 3 : 2), QTextCursor::KeepAnchor);
            edit.removeSelectedText();
        } else {
            edit.insertText("// ");
        }
    }
    edit.endEditBlock();
    if (!selection.hasSelection() && !positions.isEmpty()) {
        edit.setPosition(positions.front() + (allCommented ? 0 : 3));
        setTextCursor(edit);
    }
}

void CodeEditor::revealDebugLine(int line) {
    if (line <= 0) return;
    const int visibleLines = qMax(1, viewport()->height() / fontMetrics().lineSpacing());
    auto* bar = verticalScrollBar();
    if (line - 1 < bar->value() || line - 1 >= bar->value() + visibleLines)
        bar->setValue(qMax(0, line - 1 - visibleLines / 2));
}

void CodeEditor::setDebugLines(int executionLine, int selectionLine) {
    if (executionLine_ == executionLine && selectionLine_ == selectionLine) return;
    executionLine_ = executionLine;
    selectionLine_ = selectionLine;
    decorate();
    gutter_->update();
}
}
