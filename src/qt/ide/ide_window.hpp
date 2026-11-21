#pragma once

#include "code_editor.hpp"
#include "project.hpp"
#include <QMainWindow>
#include <QPointer>
#include <memory>

class MainWindow;
class QAction;
class QComboBox;
class QFileSystemModel;
class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;
class QTreeView;

namespace picoide {

class IdeWindow : public QMainWindow {
public:
    explicit IdeWindow(MainWindow* debugger);
    bool openProject(const QString& directory);
    bool openSource(const QString& path);
    void debuggerState(bool ready, picodbg::Board board, const QString& description);
    void debuggerFeedback(const QString& message, bool error = false);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Source {
        QString path;
        QByteArray diskText;
        CodeEditor* editor;
        QTimer* sync;
        std::vector<Problem> diagnostics;
        unsigned completionSequence = 0;
    };
    enum class Task { Configure, Build, Debug, Run, Board };
    QPointer<MainWindow> debugger_;
    Tools tools_;
    LanguageService language_;
    Build build_;
    QString root_, settingsKey_, buildDirectory_, languageContext_;
    QString buildLine_;
    std::vector<Problem> buildProblems_;
    std::vector<std::unique_ptr<Source>> sources_;
    QTreeView* tree_;
    QFileSystemModel* files_;
    QTabWidget* editors_;
    QTabWidget* bottom_;
    QPlainTextEdit* output_;
    QTreeWidget* problems_;
    QLabel* projectLabel_;
    QLabel* analysisStatus_;
    QLabel* targetStatus_;
    QComboBox* board_;
    QComboBox* target_;
    QAction* compile_;
    QAction* configure_;
    QAction* run_;
    QAction* debug_;
    QAction* boardRun_;
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
    bool hardwareReady_ = false;
    bool emulationTemplate_ = false;
    picodbg::Board hardwareBoard_ = picodbg::Board::Pico;
    Task task_ = Task::Configure;

    void setupUi();
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
    void closeSource(int index);
    bool save(Source& source);
    bool saveAll();
    void configureTools();
    void restartLanguage(bool force = false);
    void requestCompletion(Source& source);
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
