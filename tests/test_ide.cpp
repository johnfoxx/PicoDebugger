#include "ide/ide_window.hpp"
#include "main_window.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QTreeWidget>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T* get(QObject& parent, const char* name) {
    auto* child = parent.findChild<T*>(name);
    check(child != nullptr, name);
    return child;
}

void waitFor(const std::function<bool()>& condition, const char* message) {
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (condition()) return;
        QThread::msleep(5);
    } while (timer.elapsed() < 16000);
    throw std::runtime_error(message);
}

void write(const QString& path, const QByteArray& content) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(), "Write fixture");
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("PicoDebuggerTests");
    QApplication::setApplicationName("IdeTests");
    try {
        check(argc == 2, "Expected clangd fixture path");
        QTemporaryDir temp(QDir::tempPath() + "/pico IDE.XXXXXX");
        check(temp.isValid(), "Workspace fixture");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        QSettings().setValue("ide/clangd", QString::fromLocal8Bit(argv[1]));

        MainWindow debugger;
        debugger.show();
        get<QAction>(debugger, "openIdeAction")->trigger();
        picoide::IdeWindow* ide = nullptr;
        for (auto* window : QApplication::topLevelWidgets())
            if (window->objectName() == "picoIdeWindow") ide = dynamic_cast<picoide::IdeWindow*>(window);
        check(ide && ide->isVisible() && ide->isWindow(), "Debugger shows independent IDE window");
        debugger.close();
        check(ide->isVisible() && !ide->parentWidget(), "IDE stays open when the debugger window closes");
        get<QAction>(*ide, "ideShowDebugger")->trigger();
        check(debugger.isVisible(), "IDE can reopen the same debugger");

        bool wizardAccepted = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
            if (!dialog) return;
            auto* name = dialog->findChild<QLineEdit*>("ideProjectName");
            auto* parent = dialog->findChild<QLineEdit*>("ideProjectParent");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (name && parent && buttons) {
                name->setText("clignotement");
                parent->setText(temp.path());
                wizardAccepted = buttons->button(QDialogButtonBox::Ok)->isEnabled();
                buttons->button(QDialogButtonBox::Ok)->click();
            } else dialog->reject();
        });
        get<QAction>(*ide, "ideNewProject")->trigger();
        const QDir project(temp.filePath("clignotement"));
        check(wizardAccepted && QFile::exists(project.filePath("CMakeLists.txt")) &&
              QFile::exists(project.filePath("startup/startup.c")), "Wizard creates a complete project");

        auto* tabs = get<QTabWidget>(*ide, "ideEditors");
        auto* editor = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
        check(editor, "New project opens its C source in the editor");
        const QString original = editor->toPlainText();
        editor->appendPlainText("PICO_IDE_BAD");
        auto* problems = get<QTreeWidget>(*ide, "ideProblems");
        waitFor([&] { return problems->topLevelItemCount() == 1; }, "LSP diagnostics appear in the project");
        check(problems->topLevelItem(0)->text(2).contains("PICO_IDE_BAD"), "LSP diagnostics identify the problem");
        editor->appendPlainText("PICO_IDE_AUT");
        editor->moveCursor(QTextCursor::End);
        get<QAction>(*ide, "ideCompletion")->trigger();
        auto* completion = editor->findChild<QCompleter*>();
        check(completion, "Qt completion popup present");
        waitFor([&] { return completion->popup()->isVisible(); }, "Completion response opens suggestions");
        const auto suggestion = completion->completionModel()->index(0, 0);
        QMetaObject::invokeMethod(completion, "activated", Q_ARG(QModelIndex, suggestion));
        completion->popup()->hide();
        check(editor->toPlainText().endsWith("PICO_IDE_AUTOCOMPLETE"), "Completion replaces the typed prefix");
        editor->setPlainText(original + "\n/* IDE save */\n");
        editor->document()->setModified(true);
        get<QAction>(*ide, "ideSave")->trigger();
        QFile source(project.filePath("src/main.c"));
        check(source.open(QIODevice::ReadOnly) && source.readAll().contains("IDE save"), "Source edits saved to disk");

        auto* log = get<QPlainTextEdit>(*ide, "ideBuildOutput");
        const bool canCompileArm = !picoide::Tools::detect().gcc.isEmpty();
        if (!canCompileArm) {
            check(QDir(temp.path()).mkdir("host"), "Host fallback directory");
            const QDir host(temp.filePath("host"));
            write(host.filePath("CMakeLists.txt"), "cmake_minimum_required(VERSION 3.16)\nproject(host C)\nadd_executable(host main.c)\n");
            write(host.filePath("main.c"), "int main(void) { return 0; }\n");
            check(ide->openProject(host.path()), "Import existing CMake project");
        }
        editor = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
        check(editor, "An imported project opens its source");
        const auto validText = editor->toPlainText();
        editor->appendPlainText("int ide_invalid = ;");
        get<QAction>(*ide, "ideBuild")->trigger();
        waitFor([&] { return log->toPlainText().contains("La compilation a échoué."); }, "Compiler errors are reported");
        check(problems->topLevelItemCount() > 0, "A real compiler error appears in Problems");
        editor->setPlainText(validText);
        editor->document()->setModified(true);
        get<QAction>(*ide, canCompileArm ? "ideDebug" : "ideBuild")->trigger();
        waitFor([&] { return log->toPlainText().contains("Compilation réussie.") || log->toPlainText().contains("Erreur ·"); },
                "IDE compilation finishes");
        if (!log->toPlainText().contains("Compilation réussie.")) throw std::runtime_error(log->toPlainText().toStdString());
        check(get<QComboBox>(*ide, "ideTarget")->count() == 2, "CMake File API supplies executable target");
        if (canCompileArm) {
            check(log->toPlainText().contains("ELF chargé à l'arrêt") && get<QAction>(debugger, "stepAction")->isEnabled(),
                  "A native ARM build opens in the existing debugger");
            auto* board = get<QComboBox>(*ide, "ideBoard");
            board->setCurrentIndex(board->findData(static_cast<int>(picodbg::Board::Pico2)));
            get<QAction>(*ide, "ideRun")->trigger();
            waitFor([&] { return log->toPlainText().contains("ELF lancé dans l'émulateur."); }, "Run compiles and resumes emulator");
            check(debugger.selectedBoard() == picodbg::Board::Pico2, "The debugger adopts the project's build board");
        }
        if (!qgetenv("PICODBG_IDE_SCREENSHOT").isEmpty())
            check(ide->grab().save(QString::fromLocal8Bit(qgetenv("PICODBG_IDE_SCREENSHOT"))), "Capture IDE");
        const auto sdk = picoide::createProject(temp.path(), "carte", picodbg::Board::Pico2W, picoide::Template::Sdk);
        check(ide->openProject(sdk) && get<QComboBox>(*ide, "ideBoard")->currentData().toInt() ==
              static_cast<int>(picodbg::Board::Pico2W), "The SDK project template retains its build board");
        ide->close();
        debugger.close();
        std::cout << "IDE project, LSP, build and shared debugger verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
