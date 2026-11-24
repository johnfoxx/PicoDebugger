#include "ide/ide_window.hpp"
#include "ide/code_editor.hpp"
#include "ide/git_panel.hpp"
#include "ide/text_search.hpp"
#include "main_window.hpp"
#include "picodbg/elf.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QImage>
#include <QHeaderView>
#include <QMouseEvent>
#include <QLineEdit>
#include <QLabel>
#include <QColor>
#include <QColorDialog>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
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
          get<QWidget>(ide, "ideTextToolsRail")->isVisible(), "Find opens the search bar below the editor");
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
    get<QAction>(ide, "ideReplace")->trigger();
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
    get<QAction>(ide, "ideReplaceProject")->trigger();
    count(3);
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

void verifyProjectSessions(const QString& first, const QString& second) {
    check(QDir().mkpath(second), "Second project directory");
    write(QDir(second).filePath("CMakeLists.txt"), "cmake_minimum_required(VERSION 3.16)\nproject(second C)\n");
    {
        picoide::IdeWindow ide;
        ide.restoreSessionProject();
        check(ide.projectDirectory() == first && !debuggerWindow(), "Restore the previous IDE project without starting the debugger");
        ide.show();
        auto* show = get<QAction>(ide, "ideShowDebugger");
        show->trigger();
        auto* debugger = debuggerWindow();
        check(debugger, "Open the project debugger");
        auto* core = get<QComboBox>(*debugger, "coreSelector");
        auto* memory = get<QDockWidget>(*debugger, "memoryDock");
        auto* peripherals = get<QDockWidget>(*debugger, "peripheralDock");
        auto* tools = get<QTabWidget>(*debugger, "inspectionTabs");
        auto* monitor = get<QTabWidget>(*debugger, "monitorTabs");
        auto* workspace = get<QTabWidget>(*debugger, "inspectionWorkspace");
        auto* cataloguePage = get<QWidget>(*debugger, "peripheralCatalogPage");
        auto* catalogue = get<QTreeWidget>(*cataloguePage, "peripheralCatalog");
        const auto openPeripheral = [&](const QString& device) {
            const auto items = catalogue->findItems(device, Qt::MatchExactly, 0);
            check(!items.empty(), "Device is present in the catalogue");
            catalogue->setCurrentItem(items.front());
            get<QPushButton>(*cataloguePage, "openDeviceWindowButton")->click();
            return get<QWidget>(*debugger, ("deviceInspection_" + device).toUtf8().constData());
        };
        check(core->currentIndex() == 0 && !memory->isHidden(), "A new layout uses the Core 0 overview");
        core->setCurrentIndex(1);
        memory->hide();
        peripherals->setFloating(true);
        tools->setCurrentIndex(2);
        monitor->setCurrentIndex(1);
        auto* uart = openPeripheral("UART0");
        get<QLineEdit>(*uart, "registerFilter")->setText("UARTFR");
        get<QTabWidget>(*uart, "uartTabs")->setCurrentIndex(1);
        get<QPushButton>(*uart, "detachInspectionButton")->click();
        workspace->setCurrentWidget(cataloguePage);
        openPeripheral("SIO");
        workspace->setCurrentWidget(cataloguePage);
        auto* gpio = openPeripheral("IO_BANK0");
        get<QLineEdit>(*gpio, "registerFilter")->setText("GPIO25");
        auto* gpioRegisters = get<QTableWidget>(*gpio, "peripheralsTable");
        const auto gpioControl = gpioRegisters->findItems("GPIO25_CTRL", Qt::MatchExactly);
        check(!gpioControl.isEmpty(), "GPIO25 control is listed");
        gpioRegisters->setCurrentItem(gpioControl.front());
        get<QPushButton>(*gpio, "registerDetailsButton")->click();
        QApplication::processEvents();

        check(ide.openProject(second), "Switch to a project without a saved layout");
        check(core->currentIndex() == 0 && !memory->isHidden() && !peripherals->isFloating(),
              "A new project gets the initial arrangement instead of the first project's layout");
        check(workspace->count() == 1 && !debugger->findChild<QDialog*>("deviceWindow_UART0"),
              "Peripheral tabs and detached windows stay with their own project");
        check(QFileInfo(QDir(first).filePath(".picodbg/debugger-layout.ini")).isFile(), "Switching projects saves the outgoing layout");
        core->setCurrentIndex(2);
        auto* timer = openPeripheral("TIMER");
        get<QLineEdit>(*timer, "registerFilter")->setText("ALARM1");
        peripherals->hide();
        check(debugger->close(), "Closing the debugger saves the second project");

        check(ide.openProject(first), "Return to the first project while the debugger is closed");
        show->trigger();
        check(core->currentIndex() == 1 && memory->isHidden() && peripherals->isFloating() &&
              tools->currentIndex() == 2 && monitor->currentIndex() == 1, "Restore floating and hidden panels, selected tabs and core");
        auto* restoredGpio = get<QWidget>(*debugger, "deviceInspection_IO_BANK0");
        auto* restoredSio = get<QWidget>(*debugger, "deviceInspection_SIO");
        auto* restoredUartWindow = get<QDialog>(*debugger, "deviceWindow_UART0");
        auto* restoredUart = get<QWidget>(*restoredUartWindow, "deviceInspection_UART0");
        auto* restoredRegisters = get<QTableWidget>(*restoredGpio, "peripheralsTable");
        check(workspace->currentWidget() == restoredGpio && workspace->indexOf(restoredSio) < workspace->indexOf(restoredGpio) &&
              get<QLineEdit>(*restoredGpio, "registerFilter")->text() == "GPIO25" &&
              restoredRegisters->currentItem() && restoredRegisters->currentItem()->text() == "GPIO25_CTRL" &&
              get<QPushButton>(*restoredGpio, "registerDetailsButton")->isChecked(), "Restore tab order, active peripheral and register filter");
        check(get<QComboBox>(*restoredUart, "inspectionCore")->currentIndex() == 1, "Restore the detached UART core");
        check(get<QTabWidget>(*restoredUart, "uartTabs")->currentIndex() == 1, "Restore the detached UART terminal tab");
        check(get<QLineEdit>(*restoredUart, "registerFilter")->text() == "UARTFR", "Restore the detached UART register filter");
        auto* backgroundRows = get<QTableWidget>(*restoredSio, "peripheralsTable");
        check(backgroundRows->rowCount() == 0, "Background peripheral tabs build their register list lazily");
        workspace->setCurrentWidget(restoredSio);
        check(backgroundRows->rowCount() > 0, "The background tab loads its registers when selected");
        auto* overview = get<QAction>(*debugger, "overviewAction");
        check(overview->shortcuts().contains(QKeySequence("Ctrl+Shift+O")), "The overview has the Ctrl+Shift+O shortcut");
        overview->trigger();
        check(core->currentIndex() == 0 && !memory->isHidden() && !peripherals->isFloating() && tools->currentIndex() == 0,
              "The overview restores the original panels and Core 0");

        check(ide.openProject(second), "Return to the second project");
        check(core->currentIndex() == 2 && peripherals->isHidden(), "Each project restores its own layout");
        core->setCurrentIndex(1);
        check(ide.close() && debugger->isVisible(), "Closing the IDE saves the open project while leaving the debugger visible");
    }
    {
        picoide::IdeWindow ide;
        ide.restoreSessionProject();
        check(ide.projectDirectory() == second, "A new IDE instance reopens the last project");
        ide.show();
        get<QAction>(ide, "ideShowDebugger")->trigger();
        auto* debugger = debuggerWindow();
        check(debugger && get<QComboBox>(*debugger, "coreSelector")->currentIndex() == 1 &&
              get<QDockWidget>(*debugger, "peripheralDock")->isHidden(), "Restore the last layout across IDE instances");
        auto* timer = get<QWidget>(*debugger, "deviceInspection_TIMER");
        check(get<QTabWidget>(*debugger, "inspectionWorkspace")->currentWidget() == timer &&
              get<QLineEdit>(*timer, "registerFilter")->text() == "ALARM1", "Restore the active peripheral across IDE instances");
        get<QAction>(ide, "ideCloseProject")->trigger();
        check(ide.projectDirectory().isEmpty() && ide.close(), "Close the IDE without an open project");
    }
    picoide::IdeWindow empty;
    empty.restoreSessionProject();
    check(empty.projectDirectory().isEmpty(), "An IDE closed without a project next starts on the welcome page");
    check(QSettings().value("ide/lastProject").toString() == second, "The manual recent-project action retains its own history");
}

bool hasDebugLine(picoide::CodeEditor& editor, const QString& color) {
    for (const auto& selection : editor.extraSelections())
        if (selection.format.background().color() == QColor(color)) return true;
    return false;
}

void verifySourceMapping(picoide::IdeWindow& ide, MainWindow& debugger, const QDir& project, const picodbg::ElfImage& image) {
    auto* assembly = get<QTableWidget>(debugger, "assemblyTable");
    auto* ideAssembly = get<QTableView>(ide, "ideAssemblyTable");
    auto* model = ideAssembly->model();
    const auto text = [model](int row, int column) { return model->index(row, column).data().toString(); };
    const auto pcInIde = [&] {
        for (int row = 0; row < model->rowCount(); ++row)
            if (text(row, 0) == "▶") return text(row, 1);
        return QString();
    };
    waitFor([&] { return ideAssembly->isVisible() && ideAssembly->width() > 120 && !pcInIde().isEmpty(); },
            "IDE shows PC beside the C editor");
    const auto mainAddress = QString::fromStdString(picodbg::hex(image.symbols.at("main") & ~1u));
    QModelIndex mainInstruction;
    QModelIndex pcInstruction;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (text(row, 1) == mainAddress) mainInstruction = model->index(row, 2);
        if (text(row, 0) == "▶") pcInstruction = model->index(row, 1);
    }
    check(mainInstruction.isValid() && mainAddress != pcInIde(), "Full listing includes main while the PC is in startup");
    check(ideAssembly->visualRect(pcInstruction).intersects(ideAssembly->viewport()->rect()), "The initial PC is visible in the full listing");
    const QPersistentModelIndex persistentMain(mainInstruction);
    const int instructionCount = model->rowCount();
    auto* scroll = ideAssembly->verticalScrollBar();
    scroll->setValue(scroll->maximum());
    check(scroll->value() > 0, "The entire assembly listing can be scrolled");
    const int scrolled = scroll->value();
    debugger.refreshIdeSource();
    check(scroll->value() == scrolled, "A refresh with the same selection and PC preserves assembly scroll");
    auto* step = get<QAction>(ide, "ideStepInstruction");
    check(step->isEnabled(), "IDE can step a loaded target");
    const auto* registers = get<QTableWidget>(debugger, "registersTable");
    const auto address = registers->item(15, 1)->text();
    step->trigger();
    check(registers->item(15, 1)->text() != address, "IDE single-steps the same debugger");
    check(pcInIde() == registers->item(15, 1)->text(), "IDE assembly arrow updates at instruction granularity");
    check(persistentMain.isValid() && model->rowCount() == instructionCount, "Stepping updates the highlights without replacing the listing");
    for (int row = 0; row < model->rowCount(); ++row) {
        if (text(row, 0) == "▶")
            check(model->index(row, 2).data(Qt::BackgroundRole).value<QBrush>().color() == QColor(picosource::executionColor()),
                  "The current instruction keeps the execution colour");
    }
    check(ide.openSource(project.filePath("src/main.c")), "Open C file for source-to-assembly mapping");
    auto* tabs = get<QTabWidget>(ide, "ideEditors");
    auto* editor = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
    check(editor, "Editor for mapped C file");
    const auto counter = editor->document()->find("++counter");
    check(!counter.isNull(), "Find the source statement");
    const QString location = "main.c:" + QString::number(counter.blockNumber() + 1);
    editor->setTextCursor(counter);
    int selectedRows = 0;
    QModelIndex firstSelected;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto instruction = model->index(row, 2);
        if (text(row, 3) == location && instruction.data(Qt::BackgroundRole).value<QBrush>().color() == QColor(picosource::selectionColor())) {
            ++selectedRows;
            if (!firstSelected.isValid()) firstSelected = instruction;
        }
    }
    check(selectedRows > 1 && !pcInIde().isEmpty() && model->rowCount() == instructionCount,
          "Selecting C highlights its instructions within the full listing alongside the current PC");
    scroll->setValue(0);
    get<QAction>(ide, "ideGoToAssembly")->trigger();
    check(ideAssembly->visualRect(firstSelected).intersects(ideAssembly->viewport()->rect()), "Explicit C navigation brings its instructions into view");
    check(hasDebugLine(*editor, picosource::selectionColor()), "The IDE uses the selection color for the C line");
    auto* showDebuggerAssembly = get<QAction>(debugger, "showDebuggerAssemblyAction");
    check(!assembly->isVisible() && !showDebuggerAssembly->isEnabled(), "Visible IDE frees the central assembler slot");
    ide.hide();
    QApplication::processEvents();
    check(assembly->isVisible() && showDebuggerAssembly->isEnabled(), "The debugger restores its stand-alone assembler when the IDE closes");
    check(assembly->item(0, 5) && assembly->item(0, 5)->text() == location &&
          assembly->item(0, 5)->background().color() == QColor(picosource::selectionColor()),
          "Stand-alone assembler preserves source navigation and its color");
    check(ide.openSource(project.filePath("CMakeLists.txt")), "Switch to another editor tab");
    QMetaObject::invokeMethod(assembly, "cellClicked", Q_ARG(int, 0), Q_ARG(int, 5));
    check(ide.isVisible() && !assembly->isVisible() && tabs->currentWidget() == editor &&
          hasDebugLine(*editor, picosource::selectionColor()), "Assembler source link restores IDE and navigates to the line");
    auto* showIdeAssembly = get<QAction>(ide, "ideShowAssembly");
    showIdeAssembly->trigger();
    check(!ideAssembly->isVisible(), "Assembly pane can be hidden to edit in the full width");
    showIdeAssembly->trigger();
    check(ideAssembly->isVisible() && !pcInIde().isEmpty(), "Restoring the pane shows the current PC");
}

void verifyStackVariables(MainWindow& debugger, const QString& compiler, const QDir& temporary) {
    const auto source = temporary.filePath("stack_variables.c");
    QFile file(source);
    check(file.open(QIODevice::WriteOnly), "Create DWARF stack fixture");
    file.write("int probe(short amount, unsigned char flag) {\n"
               "    __asm__ volatile (\"\" ::: \"lr\");\n"
               "    volatile unsigned char bytes[15];\n"
               "    bytes[0] = 1; bytes[1] = 2; bytes[14] = 90;\n"
               "    long result = amount + flag + bytes[0];\n"
               "    return result;\n"
               "}\n"
               "void entry(void) { (void)probe(22, 3); for (;;) {} }\n");
    file.close();
    const auto elf = temporary.filePath("stack_variables.elf");
    QProcess gcc;
    gcc.start(compiler, {"-mcpu=cortex-m0plus", "-mthumb", "-O0", "-g", "-ffreestanding", "-nostdlib",
                         "-Wl,-Ttext=0x10000200,--entry=entry", "-o", elf, source});
    check(gcc.waitForFinished(5000) && gcc.exitCode() == 0, "Compile a real ARM ELF with typed arguments and locals");
    check(debugger.loadFile(elf), "Load stack fixture");
    auto* core = get<QComboBox>(debugger, "coreSelector");
    core->setCurrentIndex(0);
    auto* stack = get<QTreeWidget>(debugger, "stackCore0");
    auto* step = get<QAction>(debugger, "stepAction");
    for (int i = 0; i < 16 && !stack->topLevelItem(0)->text(0).contains("probe"); ++i) step->trigger();
    check(stack->topLevelItem(0)->text(0).contains("probe"), "The selected core entered the fixture function");
    auto* root = stack->topLevelItem(0);
    QMetaObject::invokeMethod(stack, "itemDoubleClicked", Q_ARG(QTreeWidgetItem*, root), Q_ARG(int, 0));
    auto* inspector = get<QDialog>(debugger, "stackInspector");
    auto* memory = get<QTreeWidget>(*inspector, "stackInspectorMemory");
    auto* fields = get<QTreeWidget>(*inspector, "stackInspectorFields");
    auto* context = get<QLabel>(*inspector, "stackInspectorContext");
    check(inspector->isWindow() && inspector->isVisible() && context->text().contains("Cœur 0") &&
        context->text().contains("#0 probe"), "A double click opens the exact frame in a detached window");
    waitFor([&] { return context->text().contains("CFA"); }, "Inspector displays indexed frame rules");
    const auto findField = [fields](const QString& name) {
        for (int i = 0; i < fields->topLevelItemCount(); ++i)
            if (fields->topLevelItem(i)->text(2) == name) return fields->topLevelItem(i);
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    auto* registers = get<QTableWidget>(debugger, "registersTable");
    const auto oldFp = registers->item(7, 1)->text();
    check(registers->item(0, 1)->text() == "0x00000016" && registers->item(1, 1)->text() == "0x00000003",
        "At function entry GCC passes the two arguments in r0 and r1");
    check(findField("amount") && findField("amount")->text(5) != "0",
        "A fixed DWARF slot below SP is not shown as an argument with value zero");
    check(stack->header()->sectionResizeMode(1) == QHeaderView::Fixed && memory->header()->sectionResizeMode(1) == QHeaderView::Fixed &&
          memory->header()->sectionResizeMode(3) == QHeaderView::Stretch, "The marker column keeps its width while ASCII gets the remaining space");
    step->trigger();
    auto* saved = findField("r7 sauvegardé");
    check(saved && saved->text(0) == "S7" && saved->text(5).compare(oldFp, Qt::CaseInsensitive) == 0,
        "Immediately after PUSH, the saved r7 value is the caller's register, not the current CFA register");
    bool sawUnwritten = false;
    bool sawFp = false;
    for (int i = 0; i < 35; ++i) {
        const auto* amount = findField("amount");
        if (amount && amount->text(5).contains("Copie en pile non observée")) sawUnwritten = true;
        const auto* fp = findField("Ancien pointeur de trame (r7)");
        if (fp && fp->text(0) == "FP" && fp->text(5).compare(oldFp, Qt::CaseInsensitive) == 0) sawFp = true;
        const auto* bytes = findField("bytes");
        if (amount && amount->text(5) == "22" && bytes && bytes->childCount() == 15 &&
            bytes->child(0)->text(5) == "1" && bytes->child(14)->text(5) == "90") break;
        step->trigger();
    }
    auto* amount = findField("amount");
    auto* array = findField("bytes");
    check(amount && amount->text(0) == "a1" && amount->text(3).contains("2 o") && amount->text(5) == "22",
        "Decoded argument has its label, type, width and value");
    check(array && array->text(0).startsWith('l') && array->text(3).contains("15 o") && array->childCount() == 15,
        "The decoded table exposes a 15-byte local with its elements");
    check(sawUnwritten && sawFp, "A reserved spill slot stays unavailable until written and the CFI identifies FP once established");
    const uint32_t sp = registers->item(13, 1)->text().toUInt(nullptr, 16);
    const uint32_t first = sp + array->text(1).mid(3).toUInt(nullptr, 16);
    check(array->checkState(6) == Qt::Unchecked && array->background(0).style() == Qt::NoBrush,
        "Detailed field colors are opt-in");
    array->setCheckState(6, Qt::Checked);
    check(findField("bytes")->background(0).style() != Qt::NoBrush, "Checking a field gives it a color");
    array = findField("bytes");
    fields->setCurrentItem(array, 2);
    auto* colorButton = get<QPushButton>(*inspector, "stackFieldColorButton");
    check(colorButton->isEnabled(), "A memory field can be assigned a custom color");
    const QColor custom("#7dd7ce");
    bool accepted = false;
    QTimer chooseColor;
    chooseColor.setSingleShot(true);
    QObject::connect(&chooseColor, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->setCurrentColor(custom);
        dialog->accept();
        accepted = true;
    });
    chooseColor.start(0);
    colorButton->click();
    check(accepted && findField("bytes")->background(0).color() == custom, "Changing the field color updates the detailed table");
    array = findField("bytes");
    const auto arrayColor = array->background(0).color();
    unsigned colored = 0;
    for (int i = 0; i < memory->topLevelItemCount(); ++i) {
        const auto* row = memory->topLevelItem(i);
        const auto address = row->text(0).toUInt(nullptr, 16);
        check((address & 3u) == 0 && row->text(2).size() == 11 && row->text(3).size() == 4, "Rows show aligned words, four hex bytes and four ASCII characters");
        const auto colors = row->data(0, Qt::UserRole + 2).toList();
        for (int b = 0; b < colors.size(); ++b) {
            const bool belongsToArray = address + unsigned(b) >= first && address + unsigned(b) < first + 15;
            if (colors[b].value<QColor>() == arrayColor) ++colored;
            check((colors[b].value<QColor>() == arrayColor) == belongsToArray, "Each array byte and only those bytes have its band");
        }
    }
    check(colored == 15, "The color spans exactly fifteen bytes across word boundaries");
    const auto mouse = [](QTreeWidget* tree, QTreeWidgetItem* item, int column, int x, QEvent::Type type) {
        tree->scrollToItem(item);
        const QPoint point(tree->header()->sectionViewportPosition(column) + x, tree->visualItemRect(item).center().y());
        QMouseEvent event(type, QPointF(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(tree->viewport(), &event);
    };
    const auto memoryRow = [](QTreeWidget* tree, QTreeWidgetItem* frame, uint32_t address) {
        const int count = frame ? frame->childCount() : tree->topLevelItemCount();
        for (int i = 0; i < count; ++i) {
            auto* row = frame ? frame->child(i) : tree->topLevelItem(i);
            if (row->text(0).toUInt(nullptr, 16) == (address & ~uint32_t(3))) return row;
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    const int previousHeight = stack->maximumHeight();
    stack->setMaximumHeight(105);
    waitFor([&] {
        for (int i = 0; i < root->childCount(); ++i)
            if (root->child(i)->text(0).startsWith("…")) return true;
        return false;
    }, "A compact summary indicates the words omitted between SP and the saved registers");
    for (const auto& name : {"Ancien pointeur de trame (r7)", "Adresse de retour sauvegardée"}) {
        const auto* field = findField(name);
        check(field && field->text(1).startsWith("+0x"), "CFI provides the address of the saved register");
        const auto address = sp + field->text(1).mid(3).toUInt(nullptr, 16);
        const auto* summary = memoryRow(stack, root, address);
        const auto* detailed = memoryRow(memory, nullptr, address);
        check(summary && detailed && summary->text(1).split(' ').contains(field->text(0)) &&
            summary->text(2) == detailed->text(2) && summary->text(3) == detailed->text(3),
            "Compact summary keeps each saved FP and RET at the real address with the same hex and ASCII bytes");
    }
    stack->setMaximumHeight(previousHeight);
    waitFor([&] { return memoryRow(stack, root, first); }, "Restore the expanded summary");
    QMetaObject::invokeMethod(fields, "itemClicked", Q_ARG(QTreeWidgetItem*, array), Q_ARG(int, 2));
    unsigned selected = 0;
    for (int i = 0; i < memory->topLevelItemCount(); ++i) {
        const auto* row = memory->topLevelItem(i);
        const auto mask = row->data(2, Qt::UserRole + 3).toUInt();
        check(mask == row->data(3, Qt::UserRole + 3).toUInt(), "Hex and ASCII share the same selection mask");
        for (unsigned bit = 0; bit < 4; ++bit) selected += (mask >> bit) & 1u;
    }
    check(selected == 15, "Selecting the decoded array selects its full span");
    auto* raw = memoryRow(memory, nullptr, first + 14);
    check(raw, "The last array byte has a raw row");
    const auto digit = memory->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    mouse(memory, raw, 3, 4 + int((first + 14) & 3u) * digit, QEvent::MouseButtonPress);
    check(findField("bytes")->background(0).color() == inspector->palette().highlight().color(),
        "Clicking one ASCII character keeps the full decoded variable selected");
    const auto edit = [&](const QString& input, const std::function<void()>& open) {
        bool written = false;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* value = dialog->findChild<QLineEdit*>("stackWriteValue");
            if (value) {
                value->setText(input);
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                written = !dialog->isVisible();
            }
            if (!written) dialog->reject();
        });
        timer.start(0);
        open();
        check(written, "The paused stack editor accepts this value");
    };
    auto* element = findField("bytes")->child(14);
    check(element->text(4) == "0x5a", "The raw stack column always shows the hexadecimal byte initially");
    QMetaObject::invokeMethod(fields, "itemClicked", Q_ARG(QTreeWidgetItem*, element), Q_ARG(int, 4));
    waitFor([&] { return findField("bytes")->child(14)->text(4) == "0b1011010"; }, "A raw stack click shows binary without leading zeros");
    edit("0b01011000", [&] {
        QMetaObject::invokeMethod(fields, "itemDoubleClicked", Q_ARG(QTreeWidgetItem*, findField("bytes")->child(14)), Q_ARG(int, 4));
    });
    check(findField("bytes")->child(14)->text(5) == "88", "Editing binary refreshes the decoded value");
    edit("Y", [&] { mouse(memory, raw, 3, 4 + int((first + 14) & 3u) * digit, QEvent::MouseButtonDblClick); });
    check(findField("bytes")->child(14)->text(5) == "89" && raw->text(3).at(int((first + 14) & 3u)) == 'Y',
        "An ASCII edit updates the hex and decoded representations");
    auto* compact = memoryRow(stack, stack->topLevelItem(0), first);
    check(compact, "Summary contains the beginning of the array");
    int markerX = 4;
    bool markerFound = false;
    for (const auto& value : compact->data(1, Qt::UserRole + 5).toList()) {
        const auto label = value.toList().front().toString();
        if (label == findField("bytes")->text(0)) { markerFound = true; break; }
        markerX += stack->fontMetrics().horizontalAdvance(label) + 5;
    }
    check(markerFound, "Summary shows the variable's short label in the first row");
    QMetaObject::invokeMethod(fields, "itemClicked", Q_ARG(QTreeWidgetItem*, findField("amount")), Q_ARG(int, 2));
    mouse(stack, compact, 1, markerX, QEvent::MouseButtonPress);
    check(findField("bytes")->background(0).color() == inspector->palette().highlight().color(),
        "Clicking a summary label selects the same variable in the inspector");
    for (const auto& color : compact->data(2, Qt::UserRole + 2).toList())
        check(!color.value<QColor>().isValid(), "Checked zone colors never bleed into the summary");
    const auto summaryByte = (stack->columnWidth(2) - 6) / 4;
    edit("09", [&] { mouse(stack, compact, 2, 4 + int(first & 3u) * summaryByte, QEvent::MouseButtonDblClick); });
    check(findField("bytes")->child(0)->text(5) == "9" && findField("bytes")->child(1)->text(5) == "2",
        "A hex edit in the summary updates the inspector and preserves adjacent bytes");
    const auto byteIndex = int(first & 3u);
    const auto changedHex = compact->data(2, Qt::UserRole + 8).toList()[byteIndex].toLongLong();
    const auto changedAscii = compact->data(3, Qt::UserRole + 8).toList()[byteIndex].toLongLong();
    check(changedHex > QDateTime::currentMSecsSinceEpoch() && changedHex == changedAscii &&
          findField("bytes")->child(0)->data(5, Qt::UserRole + 8).toLongLong() == changedHex,
          "A recent edit underlines the same byte in hex, ASCII and the decoded table");
    const auto* inspected = memoryRow(memory, nullptr, first);
    check(inspected && inspected->data(2, Qt::UserRole + 8).toList()[byteIndex].toLongLong() == changedHex &&
        inspected->data(3, Qt::UserRole + 8).toList()[byteIndex].toLongLong() == changedHex,
        "The detailed hex and ASCII highlight uses the same deadline as the decoded field");
    auto* currentArray = findField("bytes");
    currentArray->setCheckState(6, Qt::Unchecked);
    check(!memoryRow(memory, nullptr, first)->data(2, Qt::UserRole + 2).toList()[byteIndex].value<QColor>().isValid(),
        "Unchecking removes the pinned color while the manual selection stays");
    currentArray->setCheckState(6, Qt::Checked);
    check(memoryRow(memory, nullptr, first)->data(2, Qt::UserRole + 2).toList()[byteIndex].value<QColor>() == custom,
        "Rechecking restores the same manually chosen color");
    edit("-7", [&] { QMetaObject::invokeMethod(fields, "itemDoubleClicked", Q_ARG(QTreeWidgetItem*, findField("amount")), Q_ARG(int, 5)); });
    check(findField("amount")->text(5) == "-7", "A typed edit updates the signed short");
    for (int i = 0; i < 45 && !findField("Valeur de retour"); ++i) step->trigger();
    auto* result = findField("Valeur de retour");
    check(result && result->text(5) == "5", "Inspector follows the live activation and identifies its result");
    edit("-12", [&] { QMetaObject::invokeMethod(fields, "itemDoubleClicked", Q_ARG(QTreeWidgetItem*, result), Q_ARG(int, 5)); });
    step->trigger();
    check(registers->item(0, 1)->text() == "0xfffffff4", "The new result is delivered to the caller");
    check(!memory->isEnabled(), "A completed frame cannot write stale memory");
    inspector->close();
    core->setCurrentIndex(2);
}

void verifyRegisterLocationLists(const QString& compiler, const QDir& temporary) {
    const auto source = temporary.filePath("stack_loclists.c");
    QFile file(source);
    check(file.open(QIODevice::WriteOnly), "Create optimized DWARF fixture");
    file.write("volatile int observed;\n"
               "__attribute__((noinline)) void sink(int x) { observed = x; }\n"
               "__attribute__((noinline)) int probe(int a,int b,int c,int d,int e) {\n"
               "    int x = a+b+c+d+e; sink(x); return x+1;\n"
               "}\n"
               "void entry(void) { (void)probe(11,22,33,44,55); for (;;) {} }\n");
    file.close();
    for (const auto version : {QString("-gdwarf-4"), QString("-gdwarf-5")}) {
        const auto elf = temporary.filePath("loclists" + version + ".elf");
        QProcess gcc;
        gcc.start(compiler, {"-mcpu=cortex-m0plus", "-mthumb", "-Og", version, "-fno-omit-frame-pointer", "-ffreestanding",
                            "-nostdlib", "-Wl,-Ttext=0x10000200,-Tbss=0x20000000,--entry=entry", "-o", elf, source});
        check(gcc.waitForFinished(5000) && gcc.exitCode() == 0, "Compile an ELF with address-dependent DWARF locations");
        picodbg::Debugger target;
        target.load(elf.toStdString());
        const auto entry = target.image.symbols.at("probe") & ~1u;
        for (int step = 0; step < 30 && target.cpus[0].r[15] != entry; ++step) target.step(0);
        check(target.cpus[0].r[15] == entry, "Step into optimized function");
        StackDebugInfo info(nullptr);
        info.index(elf, compiler);
        waitFor([&] { return info.status().isEmpty(); }, "Read location list and CFI");
        const auto frame = info.frame(target, 0, entry);
        const auto named = [](const FrameDetails& details, const QString& name) {
            return std::find_if(details.fields.cbegin(), details.fields.cend(), [&](const FrameField& field) { return field.name == name; });
        };
        for (int i = 0; i < 4; ++i) {
            const auto field = named(frame, QString(QChar('a' + i)));
            check(field != frame.fields.cend() && field->reg == i && !field->inMemory &&
                  frameValue(target, 0, *field) == QString::number((i + 1) * 11), "A current DWARF location range resolves the argument to the real CPU register");
        }
        const auto fifth = named(frame, "e");
        check(fifth != frame.fields.cend() && fifth->inMemory && fifth->address == target.cpus[0].r[13] &&
              frameValue(target, 0, *fifth) == "55", "The incoming stack argument is valid before any store in the callee");
        bool multipleSaves = false, registerExpired = false;
        for (int i = 0; i < 18 && target.image.functionAt(target.cpus[0].r[15]) == "probe"; ++i) {
            target.step(0);
            if (target.image.functionAt(target.cpus[0].r[15]) != "probe") break;
            const auto current = info.frame(target, 0, target.cpus[0].r[15]);
            const auto savedCount = std::count_if(current.fields.cbegin(), current.fields.cend(), [](const FrameField& field) {
                return field.savedRegister >= 0 && field.inMemory;
            });
            multipleSaves |= savedCount >= 3;
            const auto field = named(current, "a");
            registerExpired |= field != current.fields.cend() && field->reg < 0 && !field->inMemory && !field->editable;
        }
        check(multipleSaves, "Several saved CFI registers coexist in the current frame");
        check(registerExpired, "A register is no longer offered when the location expression at PC is unsupported");
    }
}

void verifyAssemblyWithoutDwarf(picoide::IdeWindow& ide, MainWindow& debugger,
                                const QString& elf, const QString& stripped) {
    const auto compiler = picoide::Tools::detect().gcc;
    const auto gcc = compiler.lastIndexOf("gcc");
    const auto objcopy = gcc < 0 ? QString() : compiler.left(gcc) + "objcopy" + compiler.mid(gcc + 3);
    QProcess strip;
    strip.start(QFileInfo(objcopy).isExecutable() ? objcopy : QString("arm-none-eabi-objcopy"),
                {"--strip-debug", elf, stripped});
    check(strip.waitForFinished(5000) && strip.exitStatus() == QProcess::NormalExit && strip.exitCode() == 0,
          "Create an ARM executable without DWARF");
    auto* model = get<QTableView>(ide, "ideAssemblyTable")->model();
    const int instructionCount = model->rowCount();
    check(debugger.loadFile(stripped), "Open the stripped executable");
    waitFor([&] { return model->rowCount() == instructionCount && model->index(0, 3).data().toString().isEmpty(); },
            "The full assembly is available even without DWARF line mappings");
    bool pcMarked = false;
    for (int row = 0; row < model->rowCount(); ++row)
        pcMarked |= model->index(row, 0).data().toString() == "▶";
    check(pcMarked, "The PC is marked in an ELF without source lines");
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
        verifyProjectSessions(standaloneRoot, temp.filePath("second-project"));
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
        check(ide->isVisible() && ide->isMaximized() && !ide->parentWidget(), "IDE opens maximized and stays open when the debugger closes");
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
        waitFor([&] { return get<QLabel>(*ide, "ideAnalysisStatus")->text().endsWith(" prêt"); },
                "Language server is ready for Ctrl+click");
        editor->setPlainText("int PICO_IDE_DEFINITION;\nvoid probe(void) { PICO_IDE_DEFINITION = 1; }\n");
        editor->goTo(1, 24);
        const auto point = editor->cursorRect().center();
        QMouseEvent definitionHover(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
        QApplication::sendEvent(editor->viewport(), &definitionHover);
        const auto hover = editor->extraSelections().back();
        check(editor->viewport()->cursor().shape() == Qt::PointingHandCursor &&
              hover.cursor.selectedText() == "PICO_IDE_DEFINITION" &&
              hover.format.underlineStyle() == QTextCharFormat::SingleUnderline, "Ctrl hover underlines the identifier");
        QMouseEvent definitionClick(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QApplication::sendEvent(editor->viewport(), &definitionClick);
        const auto clicked = editor->extraSelections().back();
        check(clicked.cursor.selectedText() == "PICO_IDE_DEFINITION" && clicked.format.foreground() != hover.format.foreground(),
              "Ctrl click gives immediate feedback with a different link color");
        QKeyEvent controlRelease(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
        QApplication::sendEvent(editor, &controlRelease);
        check(editor->viewport()->cursor().shape() == Qt::IBeamCursor, "Releasing Ctrl restores the editor cursor");
        waitFor([&] { return editor->textCursor().blockNumber() == 0 && editor->textCursor().positionInBlock() == 4; },
                "Ctrl+click requests and reveals the LSP definition");
        editor->selectAll();
        QKeyEvent comment(QEvent::KeyPress, Qt::Key_Slash, Qt::ControlModifier);
        QApplication::sendEvent(editor, &comment);
        check(editor->toPlainText().startsWith("// int") && editor->toPlainText().contains("\n// void"),
              "The comment shortcut operates on the selected lines");
        editor->selectAll();
        QApplication::sendEvent(editor, &comment);
        check(editor->toPlainText().startsWith("int") && editor->toPlainText().contains("\nvoid"), "The same shortcut removes the comments");
        const auto testFile = project.filePath("editor-split-test.h");
        write(testFile, "#define EDITOR_SPLIT_TEST 1\n");
        check(ide->openSource(testFile), "Open a second file in the IDE");
        auto* secondEditor = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
        check(secondEditor && secondEditor != editor, "The second editor has its own document");
        secondEditor->appendPlainText("// unsaved");
        get<QAction>(*ide, "ideSplitEditor")->trigger();
        auto* right = get<QTabWidget>(*ide, "ideSecondaryEditors");
        check(right->isVisible() && right->currentWidget() == secondEditor && tabs->currentWidget() == editor,
              "Both files are visible in side-by-side tabs");
        get<QAction>(*ide, "ideDetachEditor")->trigger();
        auto* detached = get<QDialog>(*ide, "ideDetachedEditor");
        check(detached->isWindow() && detached->isVisible() && detached->isAncestorOf(secondEditor),
              "The second editor detaches into a new window");
        get<QPushButton>(*detached, "ideReattachEditor")->click();
        check(right->currentWidget() == secondEditor && secondEditor->document()->isModified() &&
              secondEditor->toPlainText().contains("// unsaved"), "Reattachment preserves the editor and unsaved text");
        secondEditor->undo();
        check(ide->openSource(project.filePath("src/main.c")), "Focus the main source again");
        editor->setPlainText(original);
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
            const auto elfPath = QDir(build).filePath("clignotement.elf");
            const auto elf = picodbg::ElfImage::read(elfPath.toStdString());
            check(elf.hasVectors && elf.vectorAddress == 0x20000000 && elf.symbols.count("counter"),
                  "The generated executable has RAM vectors and a shared counter symbol");
            verifySourceMapping(*ide, debugger, project, elf);
            verifyRegisterLocationLists(picoide::Tools::detect().gcc, QDir(temp.path()));
            verifyStackVariables(debugger, picoide::Tools::detect().gcc, QDir(temp.path()));
            check(debugger.loadFile(elfPath), "Restore the project after the stack inspection fixture");
            waitFor([&] { return get<QTableView>(*ide, "ideAssemblyTable")->model()->rowCount() > 0; }, "Restore IDE assembly listing");
            verifyAssemblyWithoutDwarf(*ide, debugger, elfPath, temp.filePath("sans-dwarf.elf"));
            auto* board = get<QComboBox>(*ide, "ideBoard");
            board->setCurrentIndex(board->findData(static_cast<int>(picodbg::Board::Pico2)));
            auto* launch = get<QAction>(*ide, "ideDebug");
            check(launch->shortcuts().contains(QKeySequence(Qt::Key_F5)), "F5 starts a paused IDE debug session");
            launch->trigger();
            waitFor([&] { return log->toPlainText().contains("ELF chargé à l'arrêt"); }, "Default launch compiles and loads paused");
            check(debugger.selectedBoard() == picodbg::Board::Pico2, "The debugger adopts the project's build board");
            auto* pause = get<QAction>(*ide, "idePause");
            auto* ideAssembly = get<QTableView>(*ide, "ideAssemblyTable");
            check(!pause->isEnabled() && ideAssembly->isVisible(), "Default launch leaves execution stopped with both languages visible");
            get<QAction>(debugger, "runAction")->trigger();
            check(pause->isEnabled() && get<QLabel>(*ide, "ideTargetStatus")->text().contains("Simulation en cours"),
                  "IDE reports and can pause simulation");
            check(!ideAssembly->isVisible(), "Running uses the full-width editor");
            waitFor([&] {
                auto* current = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
                return current && current->toPlainText().contains("++counter") &&
                       hasDebugLine(*current, picosource::executionColor());
            }, "IDE follows the actual C line while the compiler's ELF runs");
            pause->trigger();
            check(!pause->isEnabled() && get<QLabel>(*ide, "ideTargetStatus")->text().contains("Simulation arrêtée"),
                  "Pause in IDE stops the shared emulator");
            check(ideAssembly->isVisible(), "Paused session restores the split layout");
            waitFor([&] { return ideAssembly->model()->rowCount() > 0; }, "Paused session shows the ELF instructions");
            auto* focusedSource = dynamic_cast<picoide::CodeEditor*>(tabs->currentWidget());
            check(focusedSource && focusedSource->toPlainText().contains("++counter") &&
                  hasDebugLine(*focusedSource, picosource::executionColor()), "IDE follows and marks the actual C line of the PC");
            auto* expression = get<QLineEdit>(debugger, "consoleExpression");
            expression->setText("print read32(counter)");
            QMetaObject::invokeMethod(expression, "returnPressed");
            const auto output = get<QPlainTextEdit>(debugger, "consoleView")->toPlainText();
            check(output.contains("read32(counter) = 0x") && !output.contains("read32(counter) = 0x00000000"),
                  "The compiled counter really advanced in the emulator");
            auto* leaveDebugging = get<QAction>(*ide, "ideStopDebugging");
            const auto checkDevelopment = [&] {
                debugger.refreshIdeSource();
                check(ide->isVisible() && !debugger.isVisible() && !ideAssembly->isVisible() &&
                    !leaveDebugging->isEnabled() && !get<QAction>(*ide, "ideStepInstruction")->isEnabled(),
                    "Leaving the debugger returns the IDE to the full-width development layout");
                check(tabs->currentWidget() == focusedSource &&
                    !hasDebugLine(*focusedSource, picosource::executionColor()) &&
                    !hasDebugLine(*focusedSource, picosource::selectionColor()),
                    "The IDE keeps the open source and clears both debug line colors");
            };
            focusedSource->setTextCursor(focusedSource->document()->find("++counter"));
            get<QAction>(*ide, "ideGoToAssembly")->trigger();
            check(leaveDebugging->isEnabled() && hasDebugLine(*focusedSource, picosource::selectionColor()),
                "The IDE can leave debug mode while a source line is selected");
            leaveDebugging->trigger();
            checkDevelopment();
            get<QAction>(*ide, "ideShowDebugger")->trigger();
            check(ideAssembly->isVisible() && leaveDebugging->isEnabled() &&
                hasDebugLine(*focusedSource, picosource::executionColor()),
                "Reopening the debugger restores the live PC and assembly");
            auto* run = get<QAction>(debugger, "runAction");
            run->trigger();
            check(pause->isEnabled(), "The simulation is running before closing the debugger window");
            check(debugger.close(), "The debugger window closes while the simulation runs");
            checkDevelopment();
            check(run->isEnabled() && get<QLabel>(*ide, "ideTargetStatus")->text().contains("Simulation arrêtée"),
                "Closing the debugger stops the simulation");
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
