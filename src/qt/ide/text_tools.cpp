#include "text_tools.hpp"
#include "code_editor.hpp"
#include "../choice_box.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QSplitter>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace picoide {
namespace {
class ToolPages : public QStackedWidget {
public:
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(); }
};
constexpr int resultLimit = 2000;

QLabel* statusLabel(const char* name) {
    auto* label = new QLabel;
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QTreeWidget* resultTree(const char* name, const QStringList& headers) {
    auto* tree = new QTreeWidget;
    tree->setObjectName(name);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->setHeaderLabels(headers);
    tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    tree->setColumnWidth(0, 130);
    return tree;
}

QString changeExcerpt(const QString& text, int pos) {
    const int start = qMax(0, pos - 100);
    const auto excerpt = text.mid(start, 1150);
    return (start ? QString("…\n") : QString()) + excerpt +
           (start + excerpt.size() < text.size() ? QString("\n…") : QString());
}

void selectRange(CodeEditor& editor, TextRange range) {
    QTextCursor cursor(editor.document());
    const int limit = editor.document()->characterCount() - 1;
    cursor.setPosition(qBound(0, range.start, limit));
    cursor.setPosition(qBound(0, range.end(), limit), QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.ensureCursorVisible();
}

void replaceRange(CodeEditor& editor, TextRange range, const QString& value) {
    QTextCursor cursor(editor.document());
    cursor.beginEditBlock();
    cursor.setPosition(range.start);
    cursor.setPosition(range.end(), QTextCursor::KeepAnchor);
    cursor.insertText(value);
    cursor.endEditBlock();
    editor.setTextCursor(cursor);
}
}

TextTools::TextTools(QWidget* parent) : QWidget(parent), scanner_(this) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    setStyleSheet("QPushButton { padding:3px 6px; } QTabBar::tab { padding:4px 10px; }");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    rail_ = new QTabBar;
    rail_->setObjectName("ideTextToolsRail");
    rail_->setShape(QTabBar::RoundedNorth);
    rail_->setExpanding(false);
    rail_->addTab("Recherche");
    rail_->addTab("TODO");
    rail_->setTabToolTip(Search, "Recherche et remplacement");
    rail_->setTabToolTip(Todo, "Tâches signalées dans le projet");
    auto* railRow = new QHBoxLayout;
    railRow->addWidget(rail_);
    searchStatus_ = statusLabel("ideSearchStatus");
    railRow->addWidget(searchStatus_, 1);
    searchStatus_->hide();
    layout->addLayout(railRow);
    pages_ = new ToolPages;
    pages_->setObjectName("ideTextToolsPanel");
    pages_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    pages_->addWidget(buildSearch());
    pages_->addWidget(buildTodo());
    layout->addWidget(pages_);
    pages_->hide();
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), pages_);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &TextTools::closePanel);
    connect(rail_, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index < 0) return;
        if (!pages_->isHidden() && pages_->currentIndex() == index) closePanel();
        else setPage(static_cast<Page>(index));
    });
    refresh_.setSingleShot(true);
    refresh_.setInterval(170);
    connect(&refresh_, &QTimer::timeout, this, &TextTools::refreshPanel);
    scanner_.file = [this](const QString& path) { scanFile(path); };
    scanner_.finished = [this] { finishScan(); };
}

QWidget* TextTools::buildSearch() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 5, 6, 5);
    auto* top = new QHBoxLayout;
    scope_ = new ChoiceBox;
    scope_->setObjectName("ideSearchScope");
    scope_->addItems({"Fichier actif", "Tout le projet"});
    top->addWidget(scope_);
    query_ = new QLineEdit;
    query_->setObjectName("ideSearchQuery");
    query_->setPlaceholderText("Rechercher…");
    query_->setAccessibleName("Motif à rechercher");
    query_->setClearButtonEnabled(true);
    top->addWidget(query_, 1);
    regex_ = new QCheckBox("Regex");
    regex_->setObjectName("ideSearchRegex");
    regex_->setToolTip("Utiliser une expression régulière");
    matchCase_ = new QCheckBox("Casse");
    matchCase_->setObjectName("ideSearchCase");
    matchCase_->setToolTip("Respecter les majuscules et minuscules");
    top->addWidget(regex_);
    top->addWidget(matchCase_);
    previous_ = new QPushButton("↑");
    previous_->setObjectName("ideSearchPrevious");
    previous_->setToolTip("Résultat précédent (Maj+F3)");
    previous_->setAccessibleName("Résultat précédent");
    next_ = new QPushButton("↓");
    next_->setObjectName("ideSearchNext");
    next_->setToolTip("Résultat suivant (F3)");
    next_->setAccessibleName("Résultat suivant");
    auto* refresh = new QPushButton("↻");
    refresh->setObjectName("ideSearchRefresh");
    refresh->setToolTip("Actualiser les résultats");
    refresh->setAccessibleName(refresh->toolTip());
    auto* showReplace = new QPushButton("Remplacer…");
    auto* close = new QPushButton("×");
    close->setToolTip("Fermer la recherche");
    close->setAccessibleName(close->toolTip());
    top->addWidget(previous_);
    top->addWidget(next_);
    top->addWidget(refresh);
    top->addWidget(showReplace);
    top->addWidget(close);
    layout->addLayout(top);

    replaceRow_ = new QWidget;
    auto* replace = new QHBoxLayout(replaceRow_);
    replace->setContentsMargins(0, 0, 0, 0);
    operation_ = new ChoiceBox;
    operation_->setObjectName("ideSearchOperation");
    operation_->addItem("Remplacer le motif", static_cast<int>(TextOperation::Replace));
    operation_->addItem("Insérer avant le motif", static_cast<int>(TextOperation::BeforeMatch));
    operation_->addItem("Insérer après le motif", static_cast<int>(TextOperation::AfterMatch));
    operation_->addItem("Ajouter en tête de fichier", static_cast<int>(TextOperation::AtStart));
    operation_->addItem("Ajouter en fin de fichier", static_cast<int>(TextOperation::AtEnd));
    replace->addWidget(operation_);
    replacement_ = new QPlainTextEdit;
    replacement_->setObjectName("ideSearchReplacement");
    replacement_->setAccessibleName("Texte à écrire");
    replacement_->setPlaceholderText("Texte à écrire");
    replacement_->setFixedHeight(replacement_->fontMetrics().lineSpacing() * 2 + 10);
    replacement_->setToolTip("Le remplacement accepte plusieurs lignes, ainsi que les groupes capturés en mode regex.");
    replace->addWidget(replacement_, 1);
    replaceOne_ = new QPushButton("Remplacer");
    replaceOne_->setObjectName("ideSearchReplace");
    replaceAll_ = new QPushButton("Tout remplacer");
    replaceAll_->setObjectName("ideSearchReplaceAll");
    replace->addWidget(replaceOne_);
    replace->addWidget(replaceAll_);
    layout->addWidget(replaceRow_);
    replaceRow_->hide();
    connect(showReplace, &QPushButton::clicked, this, [this] {
        replaceRow_->setVisible(replaceRow_->isHidden());
        pages_->updateGeometry();
        if (!replaceRow_->isHidden()) replacement_->setFocus();
    });
    searchResults_ = resultTree("ideSearchResults", {"Fichier", "Occurrence"});
    searchResults_->setMaximumHeight(122);
    layout->addWidget(searchResults_);

    connect(close, &QPushButton::clicked, this, &TextTools::closePanel);
    connect(query_, &QLineEdit::textChanged, this, [this] { scheduleRefresh(); });
    connect(query_, &QLineEdit::returnPressed, this, [this] { navigate(true); });
    connect(scope_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { scheduleRefresh(); });
    connect(regex_, &QCheckBox::toggled, this, [this] { scheduleRefresh(); });
    connect(matchCase_, &QCheckBox::toggled, this, [this] { scheduleRefresh(); });
    const auto editParametersChanged = [this] {
        if (scanner_.active() && scan_ == Scan::Preview) refreshPanel();
        else updateActions();
    };
    connect(operation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, editParametersChanged);
    connect(replacement_, &QPlainTextEdit::textChanged, this, editParametersChanged);
    connect(refresh, &QPushButton::clicked, this, &TextTools::refreshPanel);
    connect(previous_, &QPushButton::clicked, this, [this] { navigate(false); });
    connect(next_, &QPushButton::clicked, this, [this] { navigate(true); });
    connect(replaceOne_, &QPushButton::clicked, this, &TextTools::replaceCurrent);
    connect(replaceAll_, &QPushButton::clicked, this, &TextTools::replaceEverything);
    connect(searchResults_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const auto index = item->data(0, Qt::UserRole).toUInt();
        if (index < results_.size()) reveal(results_[index]);
    });
    return panel;
}

QWidget* TextTools::buildTodo() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 5, 6, 5);
    auto* top = new QHBoxLayout;
    todoKind_ = new ChoiceBox;
    todoKind_->setObjectName("ideTodoKind");
    todoKind_->addItems({"Tous les marqueurs", "TODO", "FIXME", "HACK", "XXX", "BUG", "TEST", "DEPRECATED"});
    top->addWidget(todoKind_, 1);
    auto* refresh = new QPushButton("Actualiser");
    refresh->setObjectName("ideTodoRefresh");
    top->addWidget(refresh);
    auto* close = new QPushButton("Fermer");
    top->addWidget(close);
    layout->addLayout(top);

    todoQuery_ = new QLineEdit;
    todoQuery_->setObjectName("ideTodoQuery");
    todoQuery_->setPlaceholderText("Filtrer par texte ou fichier");
    todoQuery_->setAccessibleName("Filtrer les tâches");
    todoQuery_->setClearButtonEnabled(true);
    layout->addWidget(todoQuery_);
    doxygen_ = new QCheckBox("Toutes les balises Doxygen");
    doxygen_->setObjectName("ideTodoDoxygen");
    layout->addWidget(doxygen_);
    todoStatus_ = statusLabel("ideTodoStatus");
    layout->addWidget(todoStatus_);
    todoResults_ = resultTree("ideTodoResults", {"Marqueur", "Fichier", "Commentaire"});
    todoResults_->setColumnWidth(0, 78);
    todoResults_->setColumnWidth(1, 118);
    todoResults_->setMaximumHeight(140);
    layout->addWidget(todoResults_);

    connect(close, &QPushButton::clicked, this, &TextTools::closePanel);
    connect(refresh, &QPushButton::clicked, this, &TextTools::refreshPanel);
    connect(todoKind_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TextTools::renderTodo);
    connect(todoQuery_, &QLineEdit::textChanged, this, &TextTools::renderTodo);
    connect(doxygen_, &QCheckBox::toggled, this, [this](bool enabled) {
        todoKind_->setEnabled(!enabled);
        todoResults_->headerItem()->setText(0, enabled ? "Balise" : "Marqueur");
        refreshPanel();
    });
    connect(todoQuery_, &QLineEdit::returnPressed, this, [this] {
        if (auto* first = todoResults_->topLevelItem(0)) reveal(todos_[first->data(0, Qt::UserRole).toUInt()]);
    });
    connect(todoResults_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const auto index = item->data(0, Qt::UserRole).toUInt();
        if (index < todos_.size()) reveal(todos_[index]);
    });
    return panel;
}

void TextTools::setPage(Page page) {
    rail_->setCurrentIndex(page);
    pages_->setCurrentIndex(page);
    pages_->show();
    searchStatus_->setVisible(page == Search);
    pages_->updateGeometry();
    refreshPanel();
}

void TextTools::closePanel() {
    if (pages_->isHidden()) return;
    pages_->hide();
    searchStatus_->hide();
    refresh_.stop();
    scanner_.cancel();
    if (editor_) {
        editor_->setSearchHighlights({});
        editor_->setFocus();
    }
}

void TextTools::showSearch(bool project, bool replace) {
    {
        const QSignalBlocker blocked(scope_);
        scope_->setCurrentIndex(project ? 1 : 0);
    }
    if (editor_) {
        const auto selected = editor_->textCursor().selectedText();
        if (query_->text().isEmpty() && !selected.contains(QChar::ParagraphSeparator))
            query_->setText(selected);
    }
    replaceRow_->setVisible(replace);
    setPage(Search);
    if (replace && !query_->text().isEmpty()) replacement_->setFocus();
    else { query_->setFocus(); query_->selectAll(); }
}

void TextTools::showTodo(bool documentation) {
    doxygen_->setChecked(documentation);
    setPage(Todo);
    todoQuery_->setFocus();
    todoQuery_->selectAll();
}

void TextTools::setEditor(CodeEditor* editor) {
    if (editor_ == editor) return;
    if (editor_) editor_->setSearchHighlights({});
    disconnect(editorContents_);
    editor_ = editor;
    if (editor_) editorContents_ = connect(editor_, &QPlainTextEdit::textChanged, this, [this] { projectChanged(); });
    if (!pages_->isHidden() && pages_->currentIndex() == Search) {
        collectLocal();
        updateActions();
    }
}

void TextTools::setProject(const QString& root) {
    root_ = root;
    results_.clear();
    todos_.clear();
    searchResults_->clear();
    todoResults_->clear();
    local_.clear();
    edits_.clear();
    if (editor_) editor_->setSearchHighlights({});
    refreshPanel();
}

void TextTools::projectChanged() {
    if (!pages_->isHidden()) scheduleRefresh();
}

void TextTools::scheduleRefresh() {
    scanner_.cancel();
    if (pages_->isHidden()) return;
    refresh_.start();
    updateActions();
}

bool TextTools::searchInProject() const { return scope_->currentIndex() == 1; }

TextOperation TextTools::operation() const {
    return static_cast<TextOperation>(operation_->currentData().toInt());
}

void TextTools::refreshPanel() {
    refresh_.stop();
    scanner_.cancel();
    if (pages_->isHidden()) return;
    if (pages_->currentIndex() == Todo) {
        if (editor_) editor_->setSearchHighlights({});
        startScan(doxygen_->isChecked() ? Scan::Documentation : Scan::Todo);
        return;
    }
    collectLocal();
    if (searchInProject()) startScan(Scan::Search);
    updateActions();
}

void TextTools::collectLocal() {
    local_.clear();
    localIndex_ = -1;
    const auto expression = searchExpression(query_->text(), regex_->isChecked(), matchCase_->isChecked());
    if (editor_ && !query_->text().isEmpty() && expression.isValid()) {
        auto matches = expression.globalMatch(editor_->sourceText());
        while (matches.hasNext()) {
            const auto match = matches.next();
            local_.push_back({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength())});
        }
    }
    if (editor_) editor_->setSearchHighlights(local_);
    if (searchInProject()) return;
    if (!expression.isValid()) searchStatus_->setText("Regex invalide · " + expression.errorString());
    else if (!editor_) searchStatus_->setText("Ouvrir un fichier pour rechercher.");
    else if (query_->text().isEmpty()) searchStatus_->clear();
    else searchStatus_->setText(QString("%1 occurrence(s)").arg(local_.size()));
}

void TextTools::startScan(Scan mode) {
    scan_ = mode;
    total_ = 0;
    oversized_ = 0;
    if (mode == Scan::Todo || mode == Scan::Documentation) {
        todoResults_->clear();
        todos_.clear();
        todoStatus_->setText(root_.isEmpty() ? "Ouvrir un projet pour lancer la recherche." :
            mode == Scan::Documentation ? "Recherche des balises Doxygen…" : "Recherche des tâches…");
    } else {
        expression_ = searchExpression(query_->text(), regex_->isChecked(), matchCase_->isChecked());
        useRegex_ = regex_->isChecked();
        operationForScan_ = operation();
        replacementForScan_ = replacement_->toPlainText();
        edits_.clear();
        if (mode == Scan::Search) {
            results_.clear();
            searchResults_->clear();
        }
        if (root_.isEmpty()) { searchStatus_->setText("Ouvrir un projet pour rechercher."); return; }
        if (mode == Scan::Search || needsPattern(operationForScan_)) {
            if (query_->text().isEmpty()) { searchStatus_->clear(); return; }
            if (!expression_.isValid()) { searchStatus_->setText("Regex invalide · " + expression_.errorString()); return; }
        }
        searchStatus_->setText(mode == Scan::Preview ? "Préparation de l’aperçu…" : "Recherche dans le projet…");
    }
    if (!root_.isEmpty()) scanner_.start(root_);
}

void TextTools::scanFile(const QString& path) {
    QString text;
    if (!readFile || !readFile(path, text)) return;
    if (scan_ == Scan::Preview) {
        TextEdit edit;
        edit.path = path;
        edit.before = text;
        if (!transformText(edit, expression_, replacementForScan_, useRegex_ && needsPattern(operationForScan_), operationForScan_)) {
            ++oversized_;
        } else if (edit.after != edit.before) {
            edits_.push_back(std::move(edit));
        }
        return;
    }

    // Position the next result relative to the previous one. For TODO scans, sort
    // because unlabelled Markdown tasks are reported after explicit markers.
    std::vector<std::pair<TextRange, QString>> found;
    const auto collect = [&](TextRange range, const QString& label) { found.emplace_back(range, label); };
    if (scan_ == Scan::Todo) {
        findTodo(path, text, collect);
        std::sort(found.begin(), found.end(), [](const std::pair<TextRange, QString>& a,
                                                const std::pair<TextRange, QString>& b) { return a.first.start < b.first.start; });
    } else if (scan_ == Scan::Documentation) {
        findDoxygen(path, text, collect);
    } else {
        auto matches = expression_.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            ++total_;
            if (results_.size() + found.size() < resultLimit)
                found.emplace_back(TextRange{static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength())}, QString());
        }
    }
    auto& destination = scan_ == Scan::Search ? results_ : todos_;
    int line = 1;
    int lineStart = 0;
    int lineEnd = text.indexOf('\n');
    for (const auto& item : found) {
        while (lineEnd >= 0 && lineEnd < item.first.start) {
            ++line;
            lineStart = lineEnd + 1;
            lineEnd = text.indexOf('\n', lineStart);
        }
        const int end = lineEnd < 0 ? static_cast<int>(text.size()) : lineEnd;
        const int start = qMax(lineStart, item.first.start - 70);
        destination.push_back({path, item.second, text.mid(start, qMin(200, end - start)).trimmed(), item.first, line});
    }
}

void TextTools::finishScan() {
    if (scan_ == Scan::Search) {
        renderResults();
        searchStatus_->setText(QString("%1 occurrence(s)%2").arg(total_)
            .arg(total_ > resultLimit ? QString(" · %1 résultats affichés").arg(resultLimit) : QString()));
    } else if (scan_ == Scan::Todo || scan_ == Scan::Documentation) {
        renderTodo();
    } else {
        previewEdits();
    }
    updateActions();
}

void TextTools::renderResults() {
    searchResults_->clear();
    for (size_t i = 0; i < results_.size(); ++i) {
        const auto& result = results_[i];
        const auto location = QDir(root_).relativeFilePath(result.path) + ":" + QString::number(result.line);
        auto* item = new QTreeWidgetItem(searchResults_, {location, result.excerpt});
        item->setToolTip(0, location);
        item->setToolTip(1, result.excerpt);
        item->setData(0, Qt::UserRole, static_cast<uint>(i));
    }
}

void TextTools::renderTodo() {
    todoResults_->clear();
    const auto filter = todoQuery_->text();
    int visible = 0;
    for (size_t i = 0; i < todos_.size(); ++i) {
        const auto& result = todos_[i];
        const auto path = QDir(root_).relativeFilePath(result.path);
        if (!doxygen_->isChecked() && todoKind_->currentIndex() && result.label != todoKind_->currentText()) continue;
        if (!result.label.contains(filter, Qt::CaseInsensitive) &&
            !path.contains(filter, Qt::CaseInsensitive) && !result.excerpt.contains(filter, Qt::CaseInsensitive)) continue;
        if (++visible > resultLimit) continue;
        const auto location = path + ":" + QString::number(result.line);
        auto* item = new QTreeWidgetItem(todoResults_, {result.label, location, result.excerpt});
        item->setToolTip(1, location);
        item->setToolTip(2, result.excerpt);
        item->setData(0, Qt::UserRole, static_cast<uint>(i));
    }
    if (root_.isEmpty()) todoStatus_->setText("Ouvrir un projet pour lancer la recherche.");
    else if (!scanner_.active()) todoStatus_->setText((doxygen_->isChecked() ? QString("%1 balise(s)%2") :
                                                                    QString("%1 tâche(s)%2")).arg(visible)
        .arg(visible > resultLimit ? QString(" · %1 résultats affichés").arg(resultLimit) : QString()));
}

void TextTools::updateActions() {
    const bool project = searchInProject();
    const bool stable = !refresh_.isActive() && !scanner_.active();
    searchResults_->setVisible(project);
    searchResults_->setEnabled(stable);
    const bool hasResults = stable && (project ? !results_.empty() : !local_.empty());
    previous_->setEnabled(hasResults);
    next_->setEnabled(hasResults);
    const auto op = operation();
    const bool motif = needsPattern(op);
    replaceOne_->setVisible(!project && motif);
    replaceOne_->setText(op == TextOperation::Replace ? "Remplacer" : "Insérer");
    replaceOne_->setEnabled(hasResults && editor_ && !editor_->isReadOnly());
    replaceAll_->setText(project ? "Aperçu…" : !motif ? "Ajouter" : op == TextOperation::Replace ? "Tout remplacer" : "Tout insérer");
    replaceAll_->setEnabled(stable && (motif ? hasResults :
        !replacement_->document()->isEmpty() && (project ? !root_.isEmpty() : static_cast<bool>(editor_))));
}

int TextTools::nextLocalIndex(bool next) const {
    const auto cursor = editor_->textCursor();
    const int size = static_cast<int>(local_.size());
    if (localIndex_ >= 0) {
        const auto& last = local_[static_cast<size_t>(localIndex_)];
        if (last.start == cursor.selectionStart() && last.end() == cursor.selectionEnd())
            return (localIndex_ + (next ? 1 : size - 1)) % size;
    }
    const auto it = std::lower_bound(local_.begin(), local_.end(), next ? cursor.selectionEnd() : cursor.selectionStart(),
        [](const TextRange& match, int pos) { return match.start < pos; });
    const auto index = static_cast<int>(it - local_.begin());
    return (index + (next ? 0 : size - 1)) % size;
}

void TextTools::selectLocal(int index) {
    localIndex_ = index;
    selectRange(*editor_, local_[static_cast<size_t>(index)]);
    searchStatus_->setText(QString("Occurrence %1 sur %2").arg(index + 1).arg(local_.size()));
}

void TextTools::navigate(bool next) {
    if (pages_->isHidden() || pages_->currentIndex() != Search) { showSearch(false, false); }
    if (refresh_.isActive()) refreshPanel();
    if (searchInProject()) {
        const int size = searchResults_->topLevelItemCount();
        if (!size || scanner_.active()) return;
        const int index = searchResults_->indexOfTopLevelItem(searchResults_->currentItem());
        auto* item = searchResults_->topLevelItem(index < 0 ? (next ? 0 : size - 1) :
                                                           (index + (next ? 1 : size - 1)) % size);
        searchResults_->setCurrentItem(item);
        reveal(results_[item->data(0, Qt::UserRole).toUInt()]);
    } else if (editor_ && !local_.empty()) {
        selectLocal(nextLocalIndex(next));
    }
}

void TextTools::reveal(const Result& result) {
    if (!openFile) return;
    const auto range = result.range;
    if (auto* editor = openFile(result.path)) selectRange(*editor, range);
}

void TextTools::replaceCurrent() {
    if (!editor_ || editor_->isReadOnly()) return;
    if (refresh_.isActive()) refreshPanel();
    if (local_.empty() || !needsPattern(operation())) return;
    const auto cursor = editor_->textCursor();
    const auto selected = [&](const TextRange& match) {
        return match.start == cursor.selectionStart() && match.end() == cursor.selectionEnd();
    };
    if (localIndex_ < 0 || !selected(local_[static_cast<size_t>(localIndex_)])) {
        const auto exact = cursor.hasSelection() ? std::find_if(local_.begin(), local_.end(), selected) : local_.end();
        selectLocal(exact == local_.end() ? nextLocalIndex(true) : static_cast<int>(exact - local_.begin()));
    }
    const auto range = local_[static_cast<size_t>(localIndex_)];
    const auto expression = searchExpression(query_->text(), regex_->isChecked(), matchCase_->isChecked());
    auto matches = expression.globalMatch(editor_->sourceText(), range.start);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() != range.start) break;
        if (match.capturedLength() != range.length) continue;
        const auto value = replaceMatch(match, replacement_->toPlainText(), regex_->isChecked(), operation());
        if (value != match.captured()) {
            replaceRange(*editor_, range, value);
            refreshPanel();
        }
        navigate(true);
        return;
    }
}

void TextTools::replaceEverything() {
    if (searchInProject()) {
        refresh_.stop();
        startScan(Scan::Preview);
        updateActions();
        return;
    }
    if (!editor_ || editor_->isReadOnly()) return;
    const auto op = operation();
    const auto expression = searchExpression(query_->text(), regex_->isChecked(), matchCase_->isChecked());
    if (needsPattern(op) && (query_->text().isEmpty() || !expression.isValid())) return;
    TextEdit edit;
    edit.before = editor_->sourceText();
    if (!transformText(edit, expression, replacement_->toPlainText(), regex_->isChecked() && needsPattern(op), op)) {
        searchStatus_->setText("Le résultat dépasserait la limite de 2 Mio.");
        return;
    }
    if (edit.before != edit.after) editor_->applySearchEdit(edit);
    refreshPanel();
    searchStatus_->setText(edit.before == edit.after ? QString("Aucune modification.") :
        QString("%1 modification(s) · Ctrl+Z pour annuler").arg(edit.count));
}

void TextTools::previewEdits() {
    if (edits_.empty()) {
        searchStatus_->setText(oversized_ ? "Les résultats dépassent la limite de 2 Mio par fichier." : "Aucune modification.");
        return;
    }
    const auto edits = std::move(edits_);
    QDialog dialog(this);
    dialog.setObjectName("ideSearchPreviewDialog");
    dialog.setWindowTitle("Aperçu des modifications du projet");
    dialog.resize(820, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = statusLabel("ideSearchPreviewSummary");
    summary->setText(QString("%1 fichier(s) à modifier. Les changements seront ouverts dans l’éditeur. Enregistrer les fichiers pour les écrire sur le disque.%2")
        .arg(edits.size()).arg(oversized_ ? QString(" %1 fichier(s) dépasseraient 2 Mio et sont écartés.").arg(oversized_) : QString()));
    layout->addWidget(summary);
    auto* tree = resultTree("ideSearchPreviewFiles", {"Fichier", "Occurrences"});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(tree, 1);
    auto* panes = new QSplitter;
    auto addPane = [&](const QString& label) {
        auto* wrapper = new QWidget;
        auto* content = new QVBoxLayout(wrapper);
        content->setContentsMargins(0, 0, 0, 0);
        content->addWidget(new QLabel(label));
        auto* text = new QPlainTextEdit;
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        content->addWidget(text);
        panes->addWidget(wrapper);
        return text;
    };
    auto* before = addPane("Avant");
    auto* after = addPane("Après");
    layout->addWidget(panes, 2);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName("ideSearchPreviewApply");
    apply->setText("Appliquer aux fichiers cochés");
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    for (size_t i = 0; i < edits.size(); ++i) {
        const auto& edit = edits[i];
        auto* item = new QTreeWidgetItem(tree, {QDir(root_).relativeFilePath(edit.path), QString::number(edit.count)});
        item->setData(0, Qt::UserRole, static_cast<uint>(i));
        item->setCheckState(0, Qt::Checked);
    }
    connect(tree, &QTreeWidget::itemChanged, &dialog, [tree, apply] {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->checkState(0) == Qt::Checked) { apply->setEnabled(true); return; }
        apply->setEnabled(false);
    });
    connect(tree, &QTreeWidget::currentItemChanged, &dialog, [&](QTreeWidgetItem* item) {
        if (!item) return;
        const auto& edit = edits[item->data(0, Qt::UserRole).toUInt()];
        const auto first = std::mismatch(edit.before.begin(), edit.before.begin() + qMin(edit.before.size(), edit.after.size()), edit.after.begin());
        const auto pos = static_cast<int>(first.first - edit.before.begin());
        before->setPlainText(changeExcerpt(edit.before, pos));
        after->setPlainText(changeExcerpt(edit.after, pos));
    });
    tree->setCurrentItem(tree->topLevelItem(0));
    // The preview owns its snapshots until acceptance, even if an editor changes nearby.
    refresh_.stop();
    const auto accepted = dialog.exec() == QDialog::Accepted;
    if (!accepted) { searchStatus_->setText("Aperçu fermé."); return; }
    std::vector<TextEdit> selected;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->checkState(0) == Qt::Checked) selected.push_back(edits[static_cast<size_t>(i)]);
    QString message;
    if (applyEdits && applyEdits(selected, message)) {
        refreshPanel();
    } else {
        searchStatus_->setText(message);
    }
}

}
