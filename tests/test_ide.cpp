#include "ide/ide_window.hpp"
#include "ide/code_editor.hpp"
#include "ide/git_panel.hpp"
#include "ide/text_search.hpp"
#include "main_window.hpp"
#include "picodbg/elf.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QImage>
#include <QLineEdit>
#include <QLabel>
#include <QColor>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QThread>
#include <QTreeWidget>
#include <QTreeView>
#include <algorithm>
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

void verifyTextTools(const QDir& project) {
    const auto main = project.filePath("main.c");
    const auto other = project.filePath("other.c");
    const QByteArray primary = "const char *pattern = \"a.*\";\n// todo alpha\nint old_1;\nint old_2;\n"
                               "const char *literal = \"todo ignored\";\n";
    const QByteArray secondary = "\xef\xbb\xbf// FixMe beta\xc2\xa0note\r\nint old_3;\r\n";
    check(project.mkpath(".picodbg/build"), "Generated project directory fixture");
    write(project.filePath("CMakeLists.txt"), "cmake_minimum_required(VERSION 3.16)\nproject(text C)\n");
    write(main, primary);
    write(other, secondary);
    write(project.filePath("tasks.md"), "- [ ] documenter les exemples\n- [ ] todo relire le guide\n");
    write(project.filePath(".picodbg/build/generated.c"), "// TODO generated\nint old_4;\n");

    picoide::IdeWindow ide;
    ide.show();
    check(ide.openProject(project.path()) && ide.openSource(main), "Open project for text tools");
    auto* tabs = get<QTabWidget>(ide, "ideEditors");
    auto* editor = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
    check(editor != nullptr, "Text tools operate on the active code editor");
    get<QAction>(ide, "ideFind")->trigger();
    auto* query = get<QLineEdit>(ide, "ideSearchQuery");
    auto* status = get<QLabel>(ide, "ideSearchStatus");
    auto* regex = get<QCheckBox>(ide, "ideSearchRegex");
    auto* replacement = get<QPlainTextEdit>(ide, "ideSearchReplacement");
    auto* replaceAll = get<QPushButton>(ide, "ideSearchReplaceAll");
    const auto next = [&] { get<QAction>(ide, "ideFindNext")->trigger(); };
    auto count = [&](int expected) {
        waitFor([&] { return status->text().startsWith(QString::number(expected) + " occurrence"); },
                "Search occurrence count");
    };
    query->setText("a.*");
    count(1);
    check(get<QWidget>(ide, "ideTextToolsPanel")->isVisible() &&
          get<QWidget>(ide, "ideTextToolsRail")->isVisible(), "Find opens the right pane");
    auto highlights = editor->extraSelections();
    check(std::any_of(highlights.begin(), highlights.end(), [](const QTextEdit::ExtraSelection& entry) {
        return entry.cursor.selectedText() == "a.*";
    }), "Literal search highlights the match");

    regex->setChecked(true);
    query->setText("(");
    waitFor([&] { return status->text().contains("Regex invalide"); }, "Invalid regular expression is reported");
    check(!replaceAll->isEnabled(), "Invalid regular expression disables replacement");
    query->setText("old_(\\d)");
    count(2);
    editor->moveCursor(QTextCursor::Start);
    next();
    check(editor->textCursor().selectedText() == "old_1", "Find next selects the first match");
    next();
    check(editor->textCursor().selectedText() == "old_2", "Find next advances to the second match");
    get<QAction>(ide, "ideFindPrevious")->trigger();
    check(editor->textCursor().selectedText() == "old_1", "Find previous returns to the first match");
    replacement->setPlainText("new_$1");
    get<QPushButton>(ide, "ideSearchReplace")->click();
    check(editor->sourceText().contains("int new_1;\nint old_2;"), "Replace expands the selected regex capture");
    replaceAll->click();
    check(editor->sourceText().contains("int new_1;\nint new_2;"), "Replace all applies to the rest of the active file");
    editor->undo();
    editor->undo();
    check(editor->sourceText() == QString::fromUtf8(primary), "Local operations are independently undoable");

    get<QAction>(ide, "ideFindProject")->trigger();
    count(3);
    auto* results = get<QTreeWidget>(ide, "ideSearchResults");
    check(results->topLevelItemCount() == 3, "Project search includes unopened files and skips generated output");

    auto preview = [&](const std::function<bool(QTreeWidget&)>& inspect) {
        bool accepted = false;
        QTimer watcher;
        QObject::connect(&watcher, &QTimer::timeout, &ide, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "ideSearchPreviewDialog") return;
            watcher.stop();
            auto* files = dialog->findChild<QTreeWidget*>("ideSearchPreviewFiles");
            auto* apply = dialog->findChild<QPushButton*>("ideSearchPreviewApply");
            accepted = files && apply && inspect(*files) && apply->isEnabled();
            if (accepted) apply->click();
            else dialog->reject();
        });
        watcher.start(10);
        check(replaceAll->isEnabled(), "Project operation can be previewed");
        replaceAll->click();
        waitFor([&] { return accepted || !watcher.isActive(); }, "Project preview opens");
        check(accepted, "Project preview contains the expected changes");
    };
    preview([&](QTreeWidget& files) {
        write(other, secondary + "// Changed outside the IDE\r\n");
        return files.topLevelItemCount() == 2;
    });
    check(status->text().contains("Aucune modification appliquée") && editor->sourceText() == QString::fromUtf8(primary),
          "A stale preview cannot partially apply its text changes");
    write(other, secondary);
    preview([&](QTreeWidget& files) { return files.topLevelItemCount() == 2; });
    check(editor->sourceText().contains("int new_1;\nint new_2;"), "Project replace changes multiple matches");
    QFile unopenedPreviously(other);
    check(unopenedPreviously.open(QIODevice::ReadOnly) && unopenedPreviously.readAll() == secondary,
          "Project changes stay in the editor until saved");
    unopenedPreviously.close();
    editor->undo();
    check(editor->sourceText() == QString::fromUtf8(primary), "One undo rolls back the project operation for a file");
    get<QAction>(ide, "ideSaveAll")->trigger();
    auto expected = secondary;
    expected.replace("old_3", "new_3");
    check(unopenedPreviously.open(QIODevice::ReadOnly) && unopenedPreviously.readAll() == expected,
          "Saving preserves the BOM, CRLF line endings and nonbreaking space");
    unopenedPreviously.close();

    auto* operation = get<QComboBox>(ide, "ideSearchOperation");
    operation->setCurrentIndex(operation->findData(static_cast<int>(picoide::TextOperation::AtStart)));
    query->clear();
    replacement->setPlainText("// Entête\n");
    waitFor([&] { return replaceAll->isEnabled(); }, "Project insertion needs no motif");
    preview([](QTreeWidget& files) {
        if (files.topLevelItemCount() != 4) return false;
        for (int i = 0; i < files.topLevelItemCount(); ++i) {
            auto* file = files.topLevelItem(i);
            if (file->text(0) != "main.c") file->setCheckState(0, Qt::Unchecked);
        }
        return true;
    });
    check(editor->sourceText().startsWith("// Entête\n"), "Project insertion applies to the checked file");
    editor->undo();

    get<QAction>(ide, "ideShowTodo")->trigger();
    auto* todos = get<QTreeWidget>(ide, "ideTodoResults");
    waitFor([&] { return todos->topLevelItemCount() == 4; },
            "TODO recognizes markers regardless of case and counts tagged Markdown tasks once");
    auto* todoQuery = get<QLineEdit>(ide, "ideTodoQuery");
    auto* kind = get<QComboBox>(ide, "ideTodoKind");
    kind->setCurrentText("TODO");
    todoQuery->setText("alpha");
    check(todos->topLevelItemCount() == 1, "The TODO category includes lowercase markers");
    todoQuery->setText("beta");
    check(todos->topLevelItemCount() == 0, "TODO filters by marker and text together");
    kind->setCurrentText("FIXME");
    check(todos->topLevelItemCount() == 1, "Filter finds the FIXME in another source file");
    todos->itemActivated(todos->topLevelItem(0), 0);
    auto* opened = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
    check(opened && opened != editor && opened->textCursor().selectedText() == "FixMe",
          "Activating a TODO result opens its exact source location");
}

void verifyDoxygenTools(const QDir& project) {
    check(project.mkpath(".picodbg/bin") && project.mkpath("docs"), "Documentation test project");
    write(project.filePath("CMakeLists.txt"), "cmake_minimum_required(VERSION 3.16)\nproject(docs C)\n");
    write(project.filePath("main.c"),
          "/** @brief Read a value.\n * \\param[in] value Sample number.\n * @todo Calibrate.\n"
          " * \\test Exercise the timeout.\n * @deprecated Use the new API.\n"
          " * @xrefitem bug \"Anomalie\" \"Anomalies\" Check overflow.\n"
          " * @custom_tag User alias.\n * \\f$ x\\f$\n */\n"
          "void sample(int value);\nconst char *text = \"@param noise\";\n");
    write(project.filePath("docs/page.dox"), "/*! @page intro Introduction\nExample. */\n");
    picoide::IdeWindow ide;
    ide.show();
    check(ide.openProject(project.path()), "Open documented project");
    get<QAction>(ide, "ideShowTodo")->trigger();
    auto* tree = get<QTreeWidget>(ide, "ideTodoResults");
    waitFor([&] { return tree->topLevelItemCount() == 4; }, "Doxygen task markers appear once");
    const auto hasLabel = [&](const QString& label) {
        return !tree->findItems(label, Qt::MatchExactly, 0).isEmpty();
    };
    check(hasLabel("TODO") && hasLabel("TEST") && hasLabel("DEPRECATED") && hasLabel("BUG"),
          "Doxygen task categories include predefined xrefitem keys");
    get<QAction>(ide, "ideFindDoxygen")->trigger();
    check(get<QCheckBox>(ide, "ideTodoDoxygen")->isChecked(), "Documentation action opens tag search");
    waitFor([&] { return tree->topLevelItemCount() == 10; }, "Tag index includes C and standalone Doxygen files");
    check(hasLabel("@page") && hasLabel("@custom_tag") && hasLabel("\\f$"),
          "Tag index includes aliases and special commands");
    get<QLineEdit>(ide, "ideTodoQuery")->setText("param");
    check(tree->topLevelItemCount() == 1 && hasLabel("\\param"), "Tag filter skips C string literals");
    tree->itemActivated(tree->topLevelItem(0), 0);
    auto* editor = dynamic_cast<picoide::CodeEditor*>(get<QTabWidget>(ide, "ideEditors")->currentWidget());
    check(editor && editor->textCursor().selectedText() == "\\param", "Tag result links to the command");
    const auto boldTag = [editor] {
        const auto formats = editor->textCursor().block().layout()->formats();
        for (const auto& range : formats)
            if (range.format.fontWeight() == QFont::DemiBold) return range.format.foreground().color();
        return QColor();
    };
    check(boldTag().isValid(), "Doxygen commands are colored inside multiline doc blocks");
    const auto dark = boldTag();
    get<QAction>(ide, "picoThemeLight")->trigger();
    waitFor([&] { return boldTag().isValid() && boldTag() != dark; }, "Doxygen syntax changes color with the theme");
    get<QAction>(ide, "picoThemeDark")->trigger();

#if defined(Q_OS_UNIX)
    const auto executable = project.filePath(".picodbg/bin/doxygen");
    write(executable, "#!/bin/sh\n"
          "case \"$1\" in\n"
          "  -) grep -q '^OUTPUT_DIRECTORY = .picodbg/doxygen$' || exit 1\n"
          "     mkdir -p .picodbg/doxygen/html && printf 'docs' > .picodbg/doxygen/html/index.html ;;\n"
          "  docs/Doxyfile) cat \"$1\" || exit 1 ;;\n"
          "  *) exit 1 ;;\n"
          "esac\n"
          "printf '\\n%s\\n' 'Doxygen fixture'\n");
    check(QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
          "Make the documentation fixture executable");
    const auto path = qgetenv("PATH");
    qputenv("PATH", QFileInfo(executable).absolutePath().toUtf8() + ':' + path);
    const auto generate = [&](const QString& expectedLog) {
        bool launched = false;
        bool succeeded = false;
        QElapsedTimer timeout;
        QTimer timer;
        timeout.start();
        QObject::connect(&timer, &QTimer::timeout, &ide, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "ideDoxygenDialog") return;
            auto* start = dialog->findChild<QPushButton*>("ideDoxygenStart");
            auto* status = dialog->findChild<QLabel*>("ideDoxygenStatus");
            auto* log = dialog->findChild<QPlainTextEdit*>("ideDoxygenLog");
            if (!start || !status || !log || timeout.elapsed() > 3000) { dialog->reject(); return; }
            if (!launched) { launched = true; start->click(); }
            if (status->text().contains("Génération terminée")) {
                succeeded = log->toPlainText().contains(expectedLog) && log->toPlainText().contains("Doxygen fixture");
                dialog->accept();
            }
        });
        timer.start(10);
        get<QAction>(ide, "ideGenerateDoxygen")->trigger();
        check(succeeded, "IDE launches Doxygen and displays command output");
    };
    generate("Doxygen fixture");
    check(project.exists(".picodbg/doxygen/html/index.html"), "Default configuration produces an isolated HTML entry point");
    write(project.filePath("docs/Doxyfile"), "PROJECT_NAME = existing project\n");
    generate("PROJECT_NAME = existing project");
    qputenv("PATH", path);
#endif
}

void verifyTheme(picoide::IdeWindow& ide, MainWindow& debugger) {
    auto* light = get<QAction>(ide, "picoThemeLight");
    auto* dark = get<QAction>(debugger, "picoThemeDark");
    check(dark->isChecked(), "Dark theme is selected initially");
    light->trigger();
    const auto isLight = [](QWidget& widget) {
        return widget.palette().color(QPalette::Base).lightness() > 220 &&
               widget.palette().color(QPalette::Text).lightness() < 100;
    };
    check(isLight(ide) && isLight(debugger), "Switching from the IDE updates the debugger too");
    check(get<QAction>(debugger, "picoThemeLight")->isChecked() &&
          QSettings().value("appearance/theme").toString() == "light", "Theme menu and saved preference are synchronized");
    picoide::IdeWindow newlyOpened;
    check(isLight(newlyOpened) && get<QAction>(newlyOpened, "picoThemeLight")->isChecked(),
          "Further windows start with the selected theme");
    dark->trigger();
    check(!isLight(ide) && !isLight(debugger) && !isLight(newlyOpened) &&
          get<QAction>(ide, "picoThemeDark")->isChecked(), "Switching from the debugger restores all dark windows");
}

void verifyGit(QWidget& parent, const QString& path) {
    const auto git = QStandardPaths::findExecutable("git");
    if (git.isEmpty()) return;
    picoide::GitPanel panel(path, git, &parent);
    auto* init = get<QPushButton>(panel, "gitInit");
    waitFor([&] { return init->isEnabled(); }, "Git detects non-repository project");
    init->click();
    auto* changes = get<QTreeWidget>(panel, "gitChanges");
    waitFor([&] { return changes->topLevelItemCount() > 0; }, "Git populates project changes");
    QTreeWidgetItem* cmake = nullptr;
    for (int i = 0; i < changes->topLevelItemCount(); ++i)
        if (changes->topLevelItem(i)->text(2) == "CMakeLists.txt") cmake = changes->topLevelItem(i);
    check(cmake, "Git lists new CMake project file");
    changes->setCurrentItem(cmake);
    auto* stage = get<QPushButton>(panel, "gitStage");
    waitFor([&] { return stage->isEnabled(); }, "Git can stage selected project file");
    stage->click();
    waitFor([&] {
        for (int i = 0; i < changes->topLevelItemCount(); ++i)
            if (changes->topLevelItem(i)->text(2) == "CMakeLists.txt" &&
                changes->topLevelItem(i)->text(0) == "A") return true;
        return false;
    }, "Git refreshes the staged state");
}

void verifyLocalToolsMenu(picoide::IdeWindow& ide) {
    bool loaded = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* packages = dialog->findChild<QTreeWidget*>("ideLocalPackages");
        loaded = packages && packages->topLevelItemCount() > 0;
        dialog->reject();
    });
    get<QAction>(ide, "ideInstallLocalTools")->trigger();
    check(loaded, "Local installation reads the packaged catalogue");
}

MainWindow* debuggerWindow() {
    for (auto* window : QApplication::topLevelWidgets())
        if (auto* debugger = dynamic_cast<MainWindow*>(window)) return debugger;
    return nullptr;
}

void verifyIdeFirst(const QString& path) {
    picoide::IdeWindow ide;
    ide.show();
    check(!debuggerWindow(), "IDE starts without instantiating a debugger");
    check(ide.openProject(path), "IDE-first can open a project independently");
    check(!debuggerWindow(), "Opening a project leaves the debugger uninitialized");
    auto* tree = get<QTreeView>(ide, "ideProjectTree");
    auto* model = qobject_cast<QFileSystemModel*>(tree->model());
    const auto source = QDir(path).filePath("main.c");
    waitFor([&] { return model->index(source).isValid() && tree->visualRect(model->index(source)).isValid(); },
            "Project file becomes visible in the tree");
    tree->setCurrentIndex(model->index(source));
    check(ide.openSource(source), "Opening the selected file moves focus to its editor");
    QApplication::processEvents();
    const auto row = tree->visualRect(tree->currentIndex());
    const auto pixels = tree->viewport()->grab().toImage();
    const QPoint sample(qRound((row.right() - 8) * pixels.devicePixelRatio()),
                        qRound(row.center().y() * pixels.devicePixelRatio()));
    check(pixels.rect().contains(sample), "Selected project row is within the captured viewport");
    const auto background = pixels.pixelColor(sample);
    check(background.value() < 130, "The selected project row remains dark when the editor has focus");
    get<QAction>(ide, "ideBuild")->trigger();
    auto* output = get<QPlainTextEdit>(ide, "ideBuildOutput");
    waitFor([&] { return output->toPlainText().contains("Compilation réussie."); }, "IDE-first builds before debugging");
    check(!debuggerWindow(), "Building a project leaves the debugger uninitialized");
    get<QAction>(ide, "ideShowDebugger")->trigger();
    auto* debugger = debuggerWindow();
    check(debugger && debugger->isVisible(), "Opening the debugger from the IDE creates its window");
    get<QAction>(*debugger, "openIdeAction")->trigger();
    check(ide.isVisible(), "IDE-first debugger reuses the original IDE");
    debugger->close();
    ide.close();
}

bool hasDebugLine(picoide::CodeEditor& editor, const QString& color) {
    for (const auto& selection : editor.extraSelections())
        if (selection.format.background().color() == QColor(color)) return true;
    return false;
}

void verifySourceMapping(picoide::IdeWindow& ide, MainWindow& debugger, const QDir& project) {
    auto* assembly = get<QTableWidget>(debugger, "assemblyTable");
    auto* ideAssembly = get<QTableWidget>(ide, "ideAssemblyTable");
    auto* preview = get<QLabel>(debugger, "debugSourceStatus");
    waitFor([&] { return preview->text().contains("startup.c:"); }, "DWARF index locates startup source");
    const auto pcInIde = [&] {
        for (int row = 0; row < ideAssembly->rowCount(); ++row)
            if (ideAssembly->item(row, 0)->text() == "▶") return ideAssembly->item(row, 1)->text();
        return QString();
    };
    waitFor([&] { return ideAssembly->isVisible() && ideAssembly->width() > 120 && !pcInIde().isEmpty(); },
            "IDE shows PC beside the C editor");
    auto* step = get<QAction>(ide, "ideStepInstruction");
    check(step->isEnabled(), "IDE can step a loaded target");
    const auto address = assembly->item(0, 1)->text();
    step->trigger();
    check(assembly->item(0, 1)->text() != address, "IDE single-steps the same debugger");
    check(pcInIde() == assembly->item(0, 1)->text(), "IDE assembly arrow updates at instruction granularity");
    check(ide.openSource(project.filePath("src/main.c")), "Open C file for source-to-assembly mapping");
    auto* tabs = get<QTabWidget>(ide, "ideEditors");
    auto* editor = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
    check(editor, "Editor for mapped C file");
    const auto counter = editor->document()->find("++counter");
    check(!counter.isNull(), "Find the source statement");
    const QString location = "main.c:" + QString::number(counter.blockNumber() + 1);
    editor->setTextCursor(counter);
    int selectedRows = 0;
    for (int row = 0; row < ideAssembly->rowCount(); ++row)
        if (ideAssembly->item(row, 3)->text() == location &&
            ideAssembly->item(row, 2)->background().color() == QColor(picosource::selectionColor())) ++selectedRows;
    check(selectedRows > 1 && !pcInIde().isEmpty(), "Selecting C highlights its instructions alongside the current PC");
    get<QAction>(ide, "ideGoToAssembly")->trigger();
    check(assembly->item(0, 5) && assembly->item(0, 5)->text() == location, "C line jumps to matching assembly");
    check(assembly->item(0, 5)->background().color() == QColor(picosource::selectionColor()) &&
          hasDebugLine(*editor, picosource::selectionColor()), "Both windows use the selection color for the C line");
    check(ide.openSource(project.filePath("CMakeLists.txt")), "Switch to another editor tab");
    QMetaObject::invokeMethod(assembly, "cellClicked", Q_ARG(int, 0), Q_ARG(int, 5));
    check(tabs->currentWidget() == editor && preview->text().endsWith(location),
          "Assembly source cell opens the matching line in the IDE and previews it");
    auto* showDebuggerAssembly = get<QAction>(debugger, "showDebuggerAssemblyAction");
    showDebuggerAssembly->trigger();
    check(!assembly->isVisible() && ideAssembly->isVisible(), "Debugger layout can yield space to the peripheral views");
    get<QAction>(debugger, "codeViewAction")->trigger();
    check(assembly->isVisible(), "Code view restores the debugger assembly");
    auto* showIdeAssembly = get<QAction>(ide, "ideShowAssembly");
    showIdeAssembly->trigger();
    check(!ideAssembly->isVisible(), "Assembly pane can be hidden to edit in the full width");
    showIdeAssembly->trigger();
    check(ideAssembly->isVisible() && !pcInIde().isEmpty(), "Restoring the pane shows the current PC");
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
        qputenv("XDG_CONFIG_HOME", temp.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", temp.filePath("data").toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        QSettings().setValue("ide/clangd", QString::fromLocal8Bit(argv[1]));
        check(QDir(temp.path()).mkdir("ide-first"), "IDE-first project fixture");
        const auto standaloneRoot = temp.filePath("ide-first");
        write(QDir(standaloneRoot).filePath("CMakeLists.txt"),
              "cmake_minimum_required(VERSION 3.16)\nproject(standalone C)\nadd_executable(standalone main.c)\n");
        write(QDir(standaloneRoot).filePath("main.c"), "int main(void) { return 0; }\n");
        verifyIdeFirst(standaloneRoot);
        verifyTextTools(QDir(temp.filePath("text-tools")));
        verifyDoxygenTools(QDir(temp.filePath("documentation")));

        MainWindow debugger;
        debugger.show();
        get<QAction>(debugger, "openIdeAction")->trigger();
        picoide::IdeWindow* ide = nullptr;
        for (auto* window : QApplication::topLevelWidgets())
            if (window->objectName() == "picoIdeWindow") ide = dynamic_cast<picoide::IdeWindow*>(window);
        check(ide && ide->isVisible() && ide->isWindow(), "Debugger shows independent IDE window");
        check(get<QLabel>(debugger, "ideLinkStatus")->text() == "IDE relié", "Debugger displays IDE connection");
        debugger.close();
        check(ide->isVisible() && !ide->parentWidget(), "IDE stays open when the debugger window closes");
        check(get<QLabel>(*ide, "ideDebuggerLink")->text() == "Débogueur fermé", "IDE observes debugger closure");
        get<QAction>(*ide, "ideShowDebugger")->trigger();
        check(debugger.isVisible(), "IDE can reopen the same debugger");
        check(get<QLabel>(*ide, "ideDebuggerLink")->text() == "Débogueur relié", "IDE displays debugger connection");
        verifyTheme(*ide, debugger);
        verifyLocalToolsMenu(*ide);

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
              QFile::exists(project.filePath("startup/ram.ld")), "Wizard creates a dual-target RAM project");
        check(!QFile::exists(project.filePath("include/rp2350_regs.h")), "The RP2040 template excludes the RP2350 header");

        for (const auto board : {picodbg::Board::Pico2, picodbg::Board::Pico2W}) {
            const auto created = picoide::createProject(temp.path(), picoide::boardName(board),
                                                        board, picoide::Template::BareMetal);
            QFile header(QDir(created).filePath("include/rp2350_regs.h"));
            QFile packaged(":/ide/templates/bare-metal/include/rp2350_regs.h");
            check(header.open(QIODevice::ReadOnly) && packaged.open(QIODevice::ReadOnly) &&
                  header.readAll() == packaged.readAll(), "Bare-metal Pico 2 profiles copy the packaged register header");
        }

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
            const auto build = picoide::projectBuildDirectory(project.path(), picodbg::Board::Pico, picoide::Tools::detect());
            const auto elf = picodbg::ElfImage::read(QDir(build).filePath("clignotement.elf").toStdString());
            check(elf.hasVectors && elf.vectorAddress == 0x20000000 && elf.symbols.count("counter"),
                  "The generated executable has RAM vectors and a shared counter symbol");
            verifySourceMapping(*ide, debugger, project);
            auto* board = get<QComboBox>(*ide, "ideBoard");
            board->setCurrentIndex(board->findData(static_cast<int>(picodbg::Board::Pico2)));
            auto* launch = get<QAction>(*ide, "ideDebug");
            check(launch->shortcuts().contains(QKeySequence(Qt::Key_F5)), "F5 starts a paused IDE debug session");
            launch->trigger();
            waitFor([&] { return log->toPlainText().contains("ELF chargé à l'arrêt"); }, "Default launch compiles and loads paused");
            check(debugger.selectedBoard() == picodbg::Board::Pico2, "The debugger adopts the project's build board");
            auto* pause = get<QAction>(*ide, "idePause");
            auto* ideAssembly = get<QTableWidget>(*ide, "ideAssemblyTable");
            check(!pause->isEnabled() && ideAssembly->isVisible(), "Default launch leaves execution stopped with both languages visible");
            get<QAction>(debugger, "runAction")->trigger();
            check(pause->isEnabled() && get<QLabel>(*ide, "ideTargetStatus")->text().contains("Simulation en cours"),
                  "IDE reports and can pause simulation");
            check(!ideAssembly->isVisible(), "Running uses the full-width editor");
            auto* preview = get<QLabel>(debugger, "debugSourceStatus");
            try {
                waitFor([&] {
                    auto* current = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
                    return preview->text().contains("main.c:") && current &&
                           current->toPlainText().contains("++counter") &&
                           hasDebugLine(*current, picosource::executionColor());
                }, "IDE follows the actual C line while the compiler's ELF runs");
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string(error.what()) + " · " + preview->text().toStdString());
            }
            pause->trigger();
            check(!pause->isEnabled() && get<QLabel>(*ide, "ideTargetStatus")->text().contains("Simulation arrêtée"),
                  "Pause in IDE stops the shared emulator");
            check(ideAssembly->isVisible(), "Paused session restores the split layout");
            waitFor([&] { return ideAssembly->rowCount() > 0; }, "Paused session shows the ELF instructions");
            auto* focusedSource = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
            check(focusedSource && focusedSource->toPlainText().contains("++counter") &&
                  hasDebugLine(*focusedSource, picosource::executionColor()), "IDE follows and marks the actual C line of the PC");
            auto* expression = get<QLineEdit>(debugger, "consoleExpression");
            expression->setText("print read32(counter)");
            QMetaObject::invokeMethod(expression, "returnPressed");
            const auto output = get<QPlainTextEdit>(debugger, "consoleView")->toPlainText();
            check(output.contains("read32(counter) = 0x") && !output.contains("read32(counter) = 0x00000000"),
                  "The compiled counter really advanced in the emulator");
        }
        if (!qgetenv("PICODBG_IDE_SCREENSHOT").isEmpty())
            check(ide->grab().save(QString::fromLocal8Bit(qgetenv("PICODBG_IDE_SCREENSHOT"))), "Capture IDE");
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
            if (!dialog) return;
            auto* board = dialog->findChild<QComboBox*>("ideNewProjectBoard");
            auto* environment = dialog->findChild<QComboBox*>("ideNewProjectEnvironment");
            auto* name = dialog->findChild<QLineEdit*>("ideProjectName");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!board || !environment || !name || !buttons) { dialog->reject(); return; }
            board->setCurrentIndex(board->findData(static_cast<int>(picodbg::Board::Pico2W)));
            environment->setCurrentIndex(environment->findData(static_cast<int>(picoide::Template::Sdk)));
            name->setText("carte");
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        get<QAction>(*ide, "ideNewProject")->trigger();
        const QDir sdk(temp.filePath("carte"));
        QFile cmake(sdk.filePath("CMakeLists.txt"));
        check(cmake.open(QIODevice::ReadOnly) && cmake.readAll().contains("pico_sdk_init()") &&
              !sdk.exists("include/rp2350_regs.h") && !sdk.exists("startup/ram.ld") &&
              get<QComboBox>(*ide, "ideBoard")->currentData().toInt() == static_cast<int>(picodbg::Board::Pico2W),
              "Wizard selects the SDK environment independently from the Pico 2 W board");
        verifyGit(*ide, project.path());
        ide->close();
        debugger.close();
        std::cout << "IDE project, LSP, build and shared debugger verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
