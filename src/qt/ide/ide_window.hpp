#pragma once

#include "code_editor.hpp"
#include "project.hpp"
#include "../source_map.hpp"
#include <QMainWindow>
#include <QPointer>
#include <memory>

class MainWindow;
class QAction;
class QComboBox;
class QFileSystemModel;
class QLabel;
class QPlainTextEdit;
class QHideEvent;
class QShowEvent;
class QSplitter;
class QTableView;
class QTabWidget;
class QDialog;
class QTreeWidget;
class QTreeView;

namespace picoide {

class TextTools;
class AssemblyModel;

class IdeWindow : public QMainWindow {
public:
    explicit IdeWindow(MainWindow* debugger = nullptr, picodbg::Board board = picodbg::Board::Pico);
    ~IdeWindow() override;
    void prepareElf(const QString& path) { startupElf_ = path; }
    void prepareDemo() { startupDemo_ = true; }
    bool openProject(const QString& directory);
    void restoreSessionProject();
    const QString& projectDirectory() const { return root_; }
    bool openSource(const QString& path, bool focus = true, bool activate = true, bool externalDefinition = false);
    void debuggerState(bool ready, bool canPause, bool canStep, bool debuggerVisible, bool inspectAssembly,
                       picodbg::Board board, const QString& description);
    void debuggerFeedback(const QString& message, bool error = false);
    void debuggerSourceState(const picosource::Location& current, const picosource::Location& selected,
                             const picosource::SourceMap& map, bool hasPc, uint32_t pc,
                             bool force, bool showSelection);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Source {
        QString path;
        QByteArray diskText;
        CodeEditor* editor;
        QTimer* sync;
        QTabWidget* group = nullptr;
        QPointer<QDialog> detached;
        std::vector<Problem> diagnostics;
        unsigned completionSequence = 0;
        unsigned definitionSequence = 0;
    };
    enum class Task { Configure, Build, Debug, Board };
    QPointer<MainWindow> debugger_;
    std::unique_ptr<MainWindow> ownedDebugger_;
    Tools tools_;
    LanguageService language_;
    Build build_;
    QString root_, settingsKey_, buildDirectory_, languageContext_;
    QString startupElf_;
    bool startupDemo_ = false;
    QString buildLine_;
    std::vector<Problem> buildProblems_;
    std::vector<std::unique_ptr<Source>> sources_;
    QTreeView* tree_;
    QFileSystemModel* files_;
    QTabWidget* editors_;
    QTabWidget* secondaryEditors_;
    QTabWidget* activeGroup_ = nullptr;
    QPointer<CodeEditor> activeEditor_;
    QSplitter* editorSplit_;
    TextTools* textTools_;
    QSplitter* codeSplit_;
    QWidget* assemblyPane_;
    QTableView* assembly_;
    AssemblyModel* assemblyModel_;
    QLabel* assemblyStatus_;
    QTabWidget* bottom_;
    QPlainTextEdit* output_;
    QTreeWidget* problems_;
    QLabel* projectLabel_;
    QLabel* analysisStatus_;
    QLabel* targetStatus_;
    QLabel* linkStatus_;
    QComboBox* board_;
    QComboBox* target_;
    QAction* compile_;
    QAction* configure_;
    QAction* debug_;
    QAction* boardRun_;
    QAction* pause_;
    QAction* step_;
    QAction* stopDebugging_;
    QAction* followSource_;
    QAction* goToAssembly_;
    QAction* showAssembly_;
    QAction* cancel_;
    QAction* newProject_;
    QAction* openProject_;
    QAction* closeProject_;
    QAction* newFile_;
    QAction* newFolder_;
    QAction* rename_;
    QAction* trash_;
    QAction* save_;
    QAction* saveAll_;
    QAction* generateDocs_;
    bool hardwareReady_ = false;
    bool emulationTemplate_ = false;
    picodbg::Board hardwareBoard_ = picodbg::Board::Pico;
    Task task_ = Task::Configure;
    picosource::Location executingSource_, selectedSource_;
    QDateTime debugElfTimestamp_;
    bool syncingSource_ = false;
    bool inspectAssembly_ = false;
    bool assemblyDirty_ = true;
    QList<int> assemblySizes_;
    bool hasAssemblyPc_ = false;
    uint32_t assemblyPc_ = 0;

    void setupUi();
    MainWindow& openDebugger();
    void newProject();
    void pickProject();
    bool allowCloseSources();
    void resetProject();
    void setProjectBoard(picodbg::Board board);
    picodbg::Board projectBoard() const;
    void refreshActions();
    bool belongsToProject(const QString& path) const;
    QString selectedFolder() const;
    void createEntry(bool directory);
    void renameSource();
    void trashSource();
    Source* findSource(const QString& path);
    Source* sourceForWidget(QWidget* widget);
    bool readSearchSource(const QString& path, QString& text);
    bool applyProjectTextEdits(const std::vector<TextEdit>& edits, QString& error);
    void highlightDebugLines(Source& source);
    void updateAssemblyVisibility();
    void refreshAssembly(const picosource::SourceMap& map, bool hasPc, uint32_t pc, bool revealSelection);
    void revealDebugLocation(const picosource::Location& location);
    bool sourceMatchesElf(const Source& source) const;
    void selectSource(bool navigate);
    void closeSource(Source& source);
    void configureEditorGroup(QTabWidget& group);
    void addEditorTab(Source& source, QTabWidget& group);
    void activateEditor(CodeEditor* editor, bool focus = false);
    void moveEditor(Source& source, QTabWidget& group);
    void detachEditor(Source& source);
    void removeEditorView(Source& source);
    void syncEditorGroups();
    bool save(Source& source);
    bool saveAll();
    void configureTools();
    void restartLanguage(bool force = false);
    void requestCompletion(Source& source);
    void requestDefinition(Source& source, int line, int column);
    void receiveDiagnostics(const QString& path, const QJsonArray& diagnostics);
    void refreshProblems();
    void build(Task task);
    void appendBuildOutput(const QString& text);
    void considerBuildLine(const QString& line);
    void buildFinished(bool success, const QString& message, const std::vector<Artifact>& artifacts);
    void launch(const std::vector<Artifact>& artifacts);
    void warn(const QString& message);
};

}
