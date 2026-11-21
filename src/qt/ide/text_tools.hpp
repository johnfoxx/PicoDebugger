#pragma once

#include "text_search.hpp"
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace picoide {

class CodeEditor;

class TextTools : public QWidget {
public:
    explicit TextTools(QWidget* workspace, QWidget* parent = nullptr);
    void setEditor(CodeEditor* editor);
    void setProject(const QString& root);
    void projectChanged();
    void showSearch(bool project, bool replace);
    void showTodo(bool documentation = false);
    void navigate(bool next);

    std::function<bool(const QString&, QString&)> readFile;
    std::function<CodeEditor*(const QString&)> openFile;
    std::function<bool(const std::vector<TextEdit>&, QString&)> applyEdits;

private:
    struct Result {
        QString path, label, excerpt;
        TextRange range;
        int line;
    };
    enum Page { Search, Todo };
    enum class Scan { Search, Todo, Documentation, Preview };
    QSplitter* splitter_;
    QTabBar* rail_;
    QStackedWidget* pages_;
    QLineEdit* query_;
    QComboBox* scope_;
    QCheckBox* regex_;
    QCheckBox* matchCase_;
    QComboBox* operation_;
    QPlainTextEdit* replacement_;
    QPushButton* previous_;
    QPushButton* next_;
    QPushButton* replaceOne_;
    QPushButton* replaceAll_;
    QLabel* searchStatus_;
    QTreeWidget* searchResults_;
    QLineEdit* todoQuery_;
    QComboBox* todoKind_;
    QCheckBox* doxygen_;
    QLabel* todoStatus_;
    QTreeWidget* todoResults_;

    QTimer refresh_;
    ProjectTextScan scanner_;
    QString root_;
    QPointer<CodeEditor> editor_;
    QMetaObject::Connection editorContents_;
    std::vector<TextRange> local_;
    int localIndex_ = -1;
    int panelWidth_ = 330;
    Scan scan_ = Scan::Search;
    QRegularExpression expression_;
    bool useRegex_ = false;
    TextOperation operationForScan_ = TextOperation::Replace;
    QString replacementForScan_;
    std::vector<Result> results_, todos_;
    std::vector<TextEdit> edits_;
    int total_ = 0, oversized_ = 0;

    QWidget* buildSearch();
    QWidget* buildTodo();
    void setPage(Page page);
    void closePanel();
    void scheduleRefresh();
    void refreshPanel();
    bool searchInProject() const;
    TextOperation operation() const;
    void collectLocal();
    void startScan(Scan mode);
    void scanFile(const QString& path);
    void finishScan();
    void updateActions();
    void renderResults();
    void renderTodo();
    void reveal(const Result& result);
    void selectLocal(int index);
    int nextLocalIndex(bool next) const;
    void replaceCurrent();
    void replaceEverything();
    void previewEdits();
};

}
