#include "ide_window.hpp"
#include "../main_window.hpp"
#include "../choice_box.hpp"
#include "../window_policy.hpp"
#include "../window_theme.hpp"
#include "picodbg/elf.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QColor>
#include <QMenu>
#include <QTabBar>
#include <array>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QHideEvent>
#include <QHeaderView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include "git_panel.hpp"
#include "doxygen_dialog.hpp"
#include "text_tools.hpp"
#include "toolchain_catalog.hpp"
#include <QStandardPaths>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

namespace picoide {
namespace {
const std::array<picodbg::Board, 4> boards{
    picodbg::Board::Pico, picodbg::Board::PicoW, picodbg::Board::Pico2, picodbg::Board::Pico2W
};
QString label(picodbg::Board board) { return QString::fromStdString(picodbg::describeBoard(board).name); }
void populateBoards(QComboBox& box) {
    for (const auto board : boards) box.addItem(label(board), static_cast<int>(board));
}
QString level(int severity) {
    if (severity == 1) return "Erreur";
    if (severity == 2) return "Avertissement";
    return "Information";
}
QString projectSettingsKey(const QString& root) {
    return "ide/projects/" + QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}

IdeWindow::IdeWindow(MainWindow* debugger, picodbg::Board board)
    : debugger_(debugger), tools_(Tools::detect()), language_(this), build_(this) {
    setObjectName("picoIdeWindow");
    setWindowTitle("PicoDebugger — IDE C");
    resize(1180, 800);
    applyPicoTheme(*this);
    setupUi();
    setProjectBoard(board);
    auto geometry = QSettings().value("ide/geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    build_.output = [this](const QString& text) { appendBuildOutput(text); };
    build_.finished = [this](bool success, const QString& message, const std::vector<Artifact>& artifacts) {
        buildFinished(success, message, artifacts);
    };
    language_.status = [this](const QString& text) {
        analysisStatus_->setText(text);
        if (!language_.ready()) {
            for (auto& source : sources_) {
                source->diagnostics.clear();
                source->editor->showProblems({});
            }
            refreshProblems();
        }
    };
    language_.diagnostics = [this](const QString& path, const QJsonArray& items) { receiveDiagnostics(path, items); };
    statusBar()->showMessage("Créer ou ouvrir un projet CMake");
    refreshActions();
}

IdeWindow::~IdeWindow() {
    if (ownedDebugger_) ownedDebugger_->attachIde(nullptr);
}

MainWindow& IdeWindow::openDebugger() {
    if (!debugger_) {
        ownedDebugger_.reset(new MainWindow);
        debugger_ = ownedDebugger_.get();
        debugger_->selectBoard(projectBoard());
        debugger_->attachIde(this);
    }
    showInspectionWindow(debugger_);
    if (startupDemo_) {
        startupDemo_ = false;
        debugger_->loadDemo();
    } else if (!startupElf_.isEmpty()) {
        const auto path = startupElf_;
        startupElf_.clear();
        debugger_->loadFile(path);
    }
    return *debugger_;
}

void IdeWindow::setupUi() {
    auto* file = menuBar()->addMenu("Projet");
    auto* edit = menuBar()->addMenu("Édition");
    auto* buildMenu = menuBar()->addMenu("Compilation");
    auto* debugMenu = menuBar()->addMenu("Débogage");
    auto* views = menuBar()->addMenu("Vues");
    addPicoThemeMenu(*views, *this, [this] { refreshProblems(); });
    auto* documentation = menuBar()->addMenu("Documentation");
    auto* help = menuBar()->addMenu("Aide");
    auto* toolbar = addToolBar("Projet et lancement");
    toolbar->setObjectName("ideToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(16, 16));
    const auto action = [this](QMenu* menu, const QString& text, const char* id, const QKeySequence& shortcut,
                               const std::function<void()>& invoke) {
        auto* item = menu->addAction(text);
        item->setObjectName(id);
        item->setShortcut(shortcut);
        connect(item, &QAction::triggered, this, invoke);
        return item;
    };
    newProject_ = action(file, "Nouveau projet…", "ideNewProject", QKeySequence("Ctrl+Shift+N"), [this] { newProject(); });
    openProject_ = action(file, "Ouvrir un projet…", "ideOpenProject", QKeySequence::Open, [this] { pickProject(); });
    action(file, "Rouvrir le dernier projet", "ideRecentProject", {}, [this] {
        const auto path = QSettings().value("ide/lastProject").toString();
        if (path.isEmpty()) statusBar()->showMessage("Aucun projet récent", 4000);
        else openProject(path);
    });
    closeProject_ = action(file, "Fermer le projet", "ideCloseProject", {}, [this] {
        if (!build_.busy() && allowCloseSources()) resetProject();
    });
    action(file, "Git…", "ideGit", {}, [this] {
        if (root_.isEmpty()) { statusBar()->showMessage("Ouvrir un projet pour utiliser Git.", 4000); return; }
        if (!saveAll()) return;
        const auto setting = QSettings().value("ide/git", "git").toString();
        GitPanel panel(root_, QStandardPaths::findExecutable(setting), this);
        panel.exec();
    });
    file->addSeparator();
    newFile_ = action(file, "Nouveau fichier…", "ideNewFile", QKeySequence::New, [this] { createEntry(false); });
    newFolder_ = action(file, "Nouveau dossier…", "ideNewFolder", {}, [this] { createEntry(true); });
    rename_ = action(file, "Renommer le fichier…", "ideRenameFile", QKeySequence(Qt::Key_F2), [this] { renameSource(); });
    trash_ = action(file, "Mettre le fichier à la corbeille…", "ideTrashFile", {}, [this] { trashSource(); });
    save_ = action(file, "Enregistrer", "ideSave", QKeySequence::Save, [this] {
        if (auto* source = sourceForWidget(editors_->currentWidget())) save(*source);
    });
    saveAll_ = action(file, "Tout enregistrer", "ideSaveAll", QKeySequence("Ctrl+Shift+S"), [this] { saveAll(); });
    const auto current = [this](void (QPlainTextEdit::*operation)()) {
        if (auto* source = sourceForWidget(editors_->currentWidget())) (source->editor->*operation)();
    };
    action(edit, "Annuler", "ideUndo", QKeySequence::Undo, [=] { current(&QPlainTextEdit::undo); });
    action(edit, "Rétablir", "ideRedo", QKeySequence::Redo, [=] { current(&QPlainTextEdit::redo); });
    edit->addSeparator();
    action(edit, "Rechercher dans le fichier…", "ideFind", QKeySequence::Find,
           [this] { textTools_->showSearch(false, false); });
    action(edit, "Résultat suivant", "ideFindNext", QKeySequence(Qt::Key_F3),
           [this] { textTools_->navigate(true); });
    action(edit, "Résultat précédent", "ideFindPrevious", QKeySequence("Shift+F3"),
           [this] { textTools_->navigate(false); });
    action(edit, "Remplacer dans le fichier…", "ideReplace", QKeySequence("Ctrl+H"),
           [this] { textTools_->showSearch(false, true); });
    action(edit, "Rechercher dans le projet…", "ideFindProject", QKeySequence("Ctrl+Shift+F"),
           [this] { textTools_->showSearch(true, false); });
    action(edit, "Modifier les fichiers du projet…", "ideReplaceProject", QKeySequence("Ctrl+Shift+H"),
           [this] { textTools_->showSearch(true, true); });
    action(file, "Tâches TODO…", "ideShowTodo", QKeySequence("Ctrl+Shift+T"),
           [this] { textTools_->showTodo(); });
    action(documentation, "Rechercher les balises Doxygen…", "ideFindDoxygen", {},
           [this] { textTools_->showTodo(true); });
    generateDocs_ = action(documentation, "Générer avec Doxygen…", "ideGenerateDoxygen", {}, [this] {
        if (!root_.isEmpty()) generateDoxygen(root_, this, [this] { return saveAll(); });
    });
    edit->addSeparator();
    action(edit, "Autocomplétion", "ideCompletion", {}, [this] {
        if (auto* source = sourceForWidget(editors_->currentWidget())) requestCompletion(*source);
    })->setToolTip("Ctrl+Espace dans l'éditeur");
    configure_ = action(buildMenu, "Configurer CMake", "ideConfigure", {}, [this] { build(Task::Configure); });
    compile_ = action(buildMenu, "Compiler", "ideBuild", QKeySequence("Ctrl+B"), [this] { build(Task::Build); });
    debug_ = action(buildMenu, "Compiler et déboguer en émulation", "ideDebug", {}, [this] { build(Task::Debug); });
    debug_->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence("Ctrl+F5")});
    debug_->setToolTip("Compiler et charger à l'arrêt en émulation (F5)");
    boardRun_ = action(buildMenu, "Téléverser et déboguer sur la carte…", "ideRunBoard", QKeySequence("Ctrl+Shift+F5"), [this] { build(Task::Board); });
    pause_ = action(buildMenu, "Mettre l'exécution en pause", "idePause", QKeySequence("Shift+F5"), [this] {
        if (debugger_) debugger_->pauseFromIde();
    });
    pause_->setEnabled(false);
    step_ = action(debugMenu, "Pas instruction", "ideStepInstruction", QKeySequence(Qt::Key_F10), [this] {
        if (debugger_) debugger_->stepFromIde();
    });
    step_->setEnabled(false);
    followSource_ = action(debugMenu, "Suivre le PC dans le C", "ideFollowSource", {}, [this] {
        if (followSource_->isChecked()) revealDebugLocation(executingSource_);
    });
    followSource_->setCheckable(true);
    followSource_->setChecked(true);
    followSource_->setToolTip("Turquoise pour la ligne du PC, violet pour la ligne sélectionnée.");
    goToAssembly_ = action(debugMenu, "Voir l'assembleur de cette ligne", "ideGoToAssembly", QKeySequence("Ctrl+Alt+A"),
                            [this] { selectSource(true); });
    showAssembly_ = action(debugMenu, "Afficher l'assembleur à l'arrêt", "ideShowAssembly", {}, [this] {
        updateAssemblyVisibility();
        if (debugger_) debugger_->refreshIdeSource();
    });
    showAssembly_->setCheckable(true);
    showAssembly_->setChecked(true);
    cancel_ = action(buildMenu, "Annuler la compilation", "ideCancel", {}, [this] { build_.cancel(); });
    action(buildMenu, "Outils du projet…", "ideToolchains", {}, [this] { configureTools(); });
    action(buildMenu, "Installer les dépendances localement…", "ideInstallLocalTools", {}, [this] {
        if (build_.busy() || !manageLocalTools(this)) return;
        tools_ = Tools::detect();
        if (!root_.isEmpty()) {
            buildDirectory_ = projectBuildDirectory(root_, projectBoard(), tools_);
            restartLanguage(true);
        }
        statusBar()->showMessage("Outils locaux redétectés", 4000);
    });
    action(help, "Installation des outils…", "ideInstallGuide", {}, [this] { configureTools(); });
    auto* debugger = action(debugMenu, "Afficher le débogueur", "ideShowDebugger", QKeySequence("Ctrl+Shift+D"), [this] {
        openDebugger();
    });
    openProject_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openProject_->setIconText("Ouvrir");
    compile_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    debug_->setIconText("Déboguer");
    boardRun_->setIconText("Carte");
    step_->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    step_->setIconText("Pas");
    pause_->setIconText("Pause");
    cancel_->setIconText("Annuler");
    debugger->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    debugger->setIconText("Débogueur");
    toolbar->addAction(openProject_);
    toolbar->addAction(save_);
    toolbar->addSeparator();
    board_ = new ChoiceBox;
    board_->setObjectName("ideBoard"); populateBoards(*board_);
    board_->setToolTip("Carte de compilation du projet");
    target_ = new ChoiceBox;
    target_->setObjectName("ideTarget");
    target_->setToolTip("Exécutable CMake à compiler et lancer");
    target_->addItem("Toutes les cibles", QString());
    toolbar->addWidget(board_); toolbar->addWidget(target_);
    toolbar->addSeparator(); toolbar->addAction(compile_); toolbar->addAction(debug_);
    toolbar->addAction(boardRun_); toolbar->addAction(step_); toolbar->addAction(pause_); toolbar->addAction(cancel_);
    toolbar->addSeparator(); toolbar->addAction(debugger);
    connect(board_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (root_.isEmpty()) return;
        QSettings().setValue(settingsKey_ + "/board", boardName(projectBoard()));
        buildDirectory_ = projectBuildDirectory(root_, projectBoard(), tools_);
        const QSignalBlocker blocked(target_);
        target_->clear(); target_->addItem("Toutes les cibles", QString());
        buildProblems_.clear(); refreshProblems();
        restartLanguage(); refreshActions();
    });
    connect(target_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (!root_.isEmpty()) QSettings().setValue(settingsKey_ + "/target", target_->currentData());
    });

    auto* rootPane = new QSplitter(Qt::Vertical);
    auto* workspace = new QSplitter;
    auto* sidebar = new QWidget;
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(7, 7, 7, 5);
    projectLabel_ = new QLabel("Projet");
    projectLabel_->setObjectName("ideProjectTitle");
    projectLabel_->setTextFormat(Qt::PlainText);
    sidebarLayout->addWidget(projectLabel_);
    tree_ = new QTreeView;
    tree_->setObjectName("ideProjectTree");
    files_ = new QFileSystemModel(this);
    files_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    tree_->setModel(files_);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    for (int column = 1; column < files_->columnCount(); ++column) tree_->hideColumn(column);
    tree_->setAlternatingRowColors(true);
    connect(tree_, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (!files_->isDir(index)) openSource(files_->filePath(index));
    });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { refreshActions(); });
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const auto item = tree_->indexAt(pos);
        if (item.isValid()) tree_->setCurrentIndex(item);
        QMenu context(this);
        context.addAction(newFile_); context.addAction(newFolder_); context.addAction(rename_); context.addAction(trash_);
        context.exec(tree_->viewport()->mapToGlobal(pos));
    });
    sidebarLayout->addWidget(tree_, 1);
    workspace->addWidget(sidebar);

    editors_ = new QTabWidget;
    editors_->setObjectName("ideEditors");
    editors_->setMovable(true);
    auto* welcome = new QWidget;
    auto* welcomeLayout = new QVBoxLayout(welcome);
    welcomeLayout->setContentsMargins(32, 38, 32, 30);
    auto* heading = new QLabel("Programmer une Pico en C");
    heading->setObjectName("sectionTitle");
    auto* intro = new QLabel("Ouvre un dossier contenant CMakeLists.txt ou crée un projet à partir d'un exemple.");
    intro->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    auto* create = new QPushButton("Nouveau projet…");
    auto* open = new QPushButton("Ouvrir un projet…");
    buttons->addWidget(create); buttons->addWidget(open); buttons->addStretch();
    connect(create, &QPushButton::clicked, newProject_, &QAction::trigger);
    connect(open, &QPushButton::clicked, openProject_, &QAction::trigger);
    welcomeLayout->addWidget(heading); welcomeLayout->addWidget(intro);
    welcomeLayout->addLayout(buttons); welcomeLayout->addStretch();
    editors_->addTab(welcome, "Accueil");
    codeSplit_ = new QSplitter;
    codeSplit_->addWidget(editors_);
    assemblyPane_ = new QWidget;
    assemblyPane_->setObjectName("ideAssemblyPane");
    auto* assemblyLayout = new QVBoxLayout(assemblyPane_);
    assemblyLayout->setContentsMargins(4, 3, 0, 0);
    auto* assemblyTitle = new QLabel("Assembleur de l’ELF");
    assemblyTitle->setObjectName("sectionTitle");
    assemblyLayout->addWidget(assemblyTitle);
    assembly_ = new QTableWidget(0, 4);
    assembly_->setObjectName("ideAssemblyTable");
    assembly_->setHorizontalHeaderLabels({QString(), "Adresse", "Instruction", "Source C"});
    assembly_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    assembly_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    assembly_->setSelectionMode(QAbstractItemView::NoSelection);
    assembly_->setAlternatingRowColors(true);
    assembly_->setShowGrid(false);
    assembly_->verticalHeader()->hide();
    assembly_->verticalHeader()->setDefaultSectionSize(24);
    assembly_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    assembly_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    assembly_->setColumnWidth(3, 102);
    assemblyLayout->addWidget(assembly_, 1);
    assemblyStatus_ = new QLabel;
    assemblyStatus_->setObjectName("ideAssemblyStatus");
    assemblyStatus_->setTextFormat(Qt::PlainText);
    assemblyStatus_->setWordWrap(true);
    assemblyLayout->addWidget(assemblyStatus_);
    codeSplit_->addWidget(assemblyPane_);
    codeSplit_->setChildrenCollapsible(false);
    assemblyPane_->hide();
    workspace->addWidget(codeSplit_);
    workspace->setStretchFactor(1, 1);
    workspace->setSizes({245, 900});
    textTools_ = new TextTools(workspace);
    rootPane->addWidget(textTools_);
    textTools_->readFile = [this](const QString& path, QString& text) { return readSearchSource(path, text); };
    textTools_->openFile = [this](const QString& path) -> CodeEditor* {
        if (!openSource(path)) return nullptr;
        return dynamic_cast<CodeEditor*>(editors_->currentWidget());
    };
    textTools_->applyEdits = [this](const std::vector<TextEdit>& edits, QString& error) {
        return applyProjectTextEdits(edits, error);
    };
    connect(editors_, &QTabWidget::currentChanged, this, [this] {
        textTools_->setEditor(dynamic_cast<CodeEditor*>(editors_->currentWidget()));
        refreshActions();
        selectSource(false);
    });

    bottom_ = new QTabWidget;
    output_ = new QPlainTextEdit;
    output_->setObjectName("ideBuildOutput");
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(3500);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    problems_ = new QTreeWidget;
    problems_->setObjectName("ideProblems");
    problems_->setRootIsDecorated(false);
    problems_->setAlternatingRowColors(true);
    problems_->setHeaderLabels({"Niveau", "Fichier", "Message"});
    problems_->setColumnWidth(0, 115);
    problems_->setColumnWidth(1, 190);
    connect(problems_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const auto path = item->data(0, Qt::UserRole).toString();
        if (openSource(path))
            if (auto* source = findSource(path))
                source->editor->goTo(item->data(1, Qt::UserRole).toInt(), item->data(2, Qt::UserRole).toInt());
    });
    bottom_->addTab(output_, "Compilation");
    bottom_->addTab(problems_, "Problèmes");
    rootPane->addWidget(bottom_);
    rootPane->setStretchFactor(0, 1); rootPane->setSizes({595, 185});
    setCentralWidget(rootPane);
    analysisStatus_ = new QLabel;
    analysisStatus_->setObjectName("ideAnalysisStatus");
    targetStatus_ = new QLabel("Carte déconnectée");
    targetStatus_->setObjectName("ideTargetStatus");
    targetStatus_->setTextFormat(Qt::PlainText);
    linkStatus_ = new QLabel("Débogueur fermé");
    linkStatus_->setObjectName("ideDebuggerLink");
    statusBar()->addPermanentWidget(analysisStatus_);
    statusBar()->addPermanentWidget(linkStatus_);
    statusBar()->addPermanentWidget(targetStatus_);
}

void IdeWindow::newProject() {
    if (build_.busy()) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Nouveau projet C");
    QFormLayout layout(&dialog);
    QLineEdit name;
    name.setObjectName("ideProjectName");
    name.setPlaceholderText("mon_firmware");
    QLineEdit folder(QSettings().value("ide/newProjectParent", QDir::homePath()).toString());
    folder.setObjectName("ideProjectParent");
    auto* browse = new QPushButton("Parcourir…");
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(&folder, 1); folderRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getExistingDirectory(&dialog, "Dossier parent du projet", folder.text());
        if (!path.isEmpty()) folder.setText(path);
    });
    ChoiceBox card;
    card.setObjectName("ideNewProjectBoard");
    populateBoards(card);
    card.setCurrentIndex(card.findData(static_cast<int>(projectBoard())));
    ChoiceBox type;
    type.setObjectName("ideNewProjectEnvironment");
    type.addItem("Bare metal · sans Pico SDK", static_cast<int>(Template::BareMetal));
    type.addItem("Avec Pico SDK", static_cast<int>(Template::Sdk));
    QLabel explanation;
    explanation.setWordWrap(true);
    const auto explain = [&] {
        if (static_cast<Template>(type.currentData().toInt()) == Template::Sdk) {
            explanation.setText("Le Pico SDK fournit le démarrage et les bibliothèques. L'exemple fait clignoter la LED de la carte.");
            return;
        }
        const auto board = static_cast<picodbg::Board>(card.currentData().toInt());
        QString text = "Le projet fournit son démarrage C et son script de lien. Le compteur se lance en RAM dans l'émulateur ou sur la carte via SWD.";
        if (picodbg::describeBoard(board).chip == picodbg::Chip::RP2350)
            text += " Les adresses et registres sont dans include/rp2350_regs.h.";
        explanation.setText(text);
    };
    connect(&type, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, explain);
    connect(&card, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, explain);
    explain();
    layout.addRow("Nom", &name); layout.addRow("Dossier parent", folderRow);
    layout.addRow("Carte", &card); layout.addRow("Environnement", &type); layout.addRow(&explanation);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    const auto validate = [&] { buttons.button(QDialogButtonBox::Ok)->setEnabled(!name.text().trimmed().isEmpty() && !folder.text().trimmed().isEmpty()); };
    connect(&name, &QLineEdit::textChanged, &dialog, validate);
    connect(&folder, &QLineEdit::textChanged, &dialog, validate);
    validate();
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !allowCloseSources()) return;
    try {
        const auto board = static_cast<picodbg::Board>(card.currentData().toInt());
        const auto path = createProject(folder.text().trimmed(), name.text().trimmed(), board,
                                        static_cast<Template>(type.currentData().toInt()));
        QSettings().setValue("ide/newProjectParent", folder.text().trimmed());
        QSettings().setValue(projectSettingsKey(path) + "/board", boardName(board));
        resetProject();
        openProject(path);
        openSource(QDir(path).filePath("src/main.c"));
    } catch (const std::exception& error) {
        warn(QString::fromUtf8(error.what()));
    }
}

void IdeWindow::pickProject() {
    const auto path = QFileDialog::getExistingDirectory(this, "Ouvrir un projet CMake",
        root_.isEmpty() ? QSettings().value("ide/lastProject", QDir::homePath()).toString() : root_);
    if (!path.isEmpty()) openProject(path);
}

bool IdeWindow::openProject(const QString& directory) {
    const auto path = QFileInfo(directory).canonicalFilePath();
    if (path.isEmpty() || !QFileInfo(QDir(path).filePath("CMakeLists.txt")).isFile()) {
        warn("Choisir le dossier qui contient le CMakeLists.txt du projet.");
        return false;
    }
    if (path == root_) return true;
    if (build_.busy() || !allowCloseSources()) return false;
    resetProject();
    root_ = path;
    textTools_->setProject(root_);
    settingsKey_ = projectSettingsKey(root_);
    QSettings settings;
    settings.setValue("ide/lastProject", root_);
    auto card = projectBoard();
    QFile metadata(QDir(root_).filePath(".picodbg-project.json"));
    QJsonObject defaults;
    if (metadata.open(QIODevice::ReadOnly) && metadata.size() <= 16384)
        defaults = QJsonDocument::fromJson(metadata.readAll()).object();
    emulationTemplate_ = defaults.value("template").toString() == "emulator";
    const auto savedBoard = settings.value(settingsKey_ + "/board", defaults.value("board").toString()).toString();
    if (!savedBoard.isEmpty()) picodbg::parseBoard(savedBoard.toStdString(), card);
    setProjectBoard(card);
    buildDirectory_ = projectBuildDirectory(root_, card, tools_);
    const auto title = QFileInfo(root_).fileName();
    projectLabel_->setText(title);
    projectLabel_->setToolTip(root_);
    setWindowTitle(title + " — PicoDebugger IDE");
    tree_->setRootIndex(files_->setRootPath(root_));
    tree_->setEnabled(true);
    restartLanguage();
    statusBar()->showMessage("Projet ouvert · compiler ou choisir Configurer CMake");
    refreshActions();
    for (const auto& entry : {"src/main.c", "main.c", "CMakeLists.txt"}) {
        const auto file = QDir(root_).filePath(entry);
        if (QFileInfo(file).isFile() && openSource(file)) break;
    }
    if (followSource_->isChecked()) revealDebugLocation(executingSource_);
    return true;
}

void IdeWindow::resetProject() {
    const QSignalBlocker blockedEditors(editors_);
    textTools_->setEditor(nullptr);
    textTools_->setProject({});
    for (const auto& source : sources_) {
        language_.close(source->path);
        editors_->removeTab(editors_->indexOf(source->editor));
        delete source->editor;
    }
    sources_.clear();
    language_.start({}, {});
    analysisStatus_->clear();
    languageContext_.clear();
    emulationTemplate_ = false;
    root_.clear();
    settingsKey_.clear();
    buildDirectory_.clear();
    buildProblems_.clear();
    projectLabel_->setText("Projet");
    projectLabel_->setToolTip({});
    setWindowTitle("PicoDebugger — IDE C");
    tree_->setRootIndex({});
    tree_->setEnabled(false);
    const QSignalBlocker blocked(target_);
    target_->clear(); target_->addItem("Toutes les cibles", QString());
    output_->clear();
    refreshProblems();
    refreshActions();
    selectSource(false);
}

void IdeWindow::setProjectBoard(picodbg::Board card) {
    const QSignalBlocker blocked(board_);
    board_->setCurrentIndex(board_->findData(static_cast<int>(card)));
}

picodbg::Board IdeWindow::projectBoard() const {
    return static_cast<picodbg::Board>(board_->currentData().toInt());
}

bool IdeWindow::belongsToProject(const QString& path) const {
    if (root_.isEmpty() || path.isEmpty()) return false;
    const auto canonical = QFileInfo(path).canonicalFilePath();
    return canonical == root_ || canonical.startsWith(root_ + '/');
}

QString IdeWindow::selectedFolder() const {
    const auto selected = tree_->currentIndex();
    if (selected.isValid()) {
        QFileInfo file(files_->filePath(selected));
        const auto folder = file.isDir() ? file.absoluteFilePath() : file.absolutePath();
        if (belongsToProject(folder)) return folder;
    }
    const auto source = QDir(root_).filePath("src");
    return QFileInfo(source).isDir() && belongsToProject(source) ? source : root_;
}

void IdeWindow::createEntry(bool directory) {
    if (root_.isEmpty() || build_.busy()) return;
    const auto base = selectedFolder();
    bool ok;
    const auto name = QInputDialog::getText(this, directory ? "Nouveau dossier" : "Nouveau fichier",
        "Nom dans " + QDir(root_).relativeFilePath(base), QLineEdit::Normal, directory ? QString() : "nouveau.c", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (name == "." || name == ".." || name.contains('/') || name.contains('\\')) {
        warn("Saisir un nom simple dans le dossier sélectionné."); return;
    }
    const auto path = QDir(base).filePath(name);
    QFile file(path);
    const bool created = directory ? QDir(base).mkdir(name) : file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
    if (!created) { warn("Impossible de créer " + path + ". Vérifier si ce nom existe déjà."); return; }
    if (!directory) { file.close(); openSource(path); }
    textTools_->projectChanged();
}

void IdeWindow::renameSource() {
    const auto oldPath = files_->filePath(tree_->currentIndex());
    if (build_.busy() || !belongsToProject(oldPath) || !QFileInfo(oldPath).isFile()) return;
    const QFileInfo old(oldPath);
    bool ok;
    const auto name = QInputDialog::getText(this, "Renommer le fichier", "Nouveau nom", QLineEdit::Normal, old.fileName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == old.fileName()) return;
    if (name == "CMakeLists.txt" || old.fileName() == "CMakeLists.txt" || name == "." || name == ".." ||
        name.contains('/') || name.contains('\\')) { warn("Saisir un nom de fichier valide sans modifier CMakeLists.txt."); return; }
    auto* source = findSource(oldPath);
    if (source && !save(*source)) return;
    const auto next = QDir(old.absolutePath()).filePath(name);
    if (!QFile::rename(oldPath, next)) { warn("Le fichier n'a pas pu être renommé."); return; }
    if (source) {
        closeSource(editors_->indexOf(source->editor));
        openSource(next);
    }
    textTools_->projectChanged();
    statusBar()->showMessage("Fichier renommé · vérifier ses références dans le projet", 6000);
}

void IdeWindow::trashSource() {
    const auto path = files_->filePath(tree_->currentIndex());
    if (build_.busy() || !belongsToProject(path) || !QFileInfo(path).isFile() ||
        path == QDir(root_).filePath("CMakeLists.txt")) return;
    if (QMessageBox::question(this, "Mettre à la corbeille",
            "Mettre " + QFileInfo(path).fileName() + " à la corbeille ?",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    auto* source = findSource(path);
    if (source && !save(*source)) return;
    if (!QFile::moveToTrash(path)) { warn("Impossible de mettre ce fichier à la corbeille."); return; }
    if (source) closeSource(editors_->indexOf(source->editor));
    textTools_->projectChanged();
    statusBar()->showMessage("Fichier placé dans la corbeille · vérifier ses références dans le projet", 6000);
}

bool IdeWindow::openSource(const QString& path, bool focus, bool activate) {
    if (!belongsToProject(path)) return false;
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto selectFile = [this, &canonical] {
        const auto index = files_->index(canonical);
        if (index.isValid()) {
            tree_->setCurrentIndex(index);
            tree_->scrollTo(index);
        }
    };
    if (auto* existing = findSource(canonical)) {
        if (activate) {
            editors_->setCurrentWidget(existing->editor);
            selectFile();
            if (focus) existing->editor->setFocus();
        }
        return true;
    }
    QByteArray contents;
    if (!readProjectText(canonical, contents)) { warn("Impossible d'ouvrir ce fichier. L'éditeur accepte les fichiers texte UTF-8 jusqu'à 2 Mio."); return false; }
    const auto language = languageForFile(canonical);
    auto* editor = new CodeEditor(language, editors_);
    auto source = std::unique_ptr<Source>(new Source);
    source->path = canonical;
    source->diskText = contents;
    source->editor = editor;
    source->sync = new QTimer(editor);
    source->sync->setSingleShot(true);
    source->sync->setInterval(220);
    auto* handle = source.get();
    editor->setPlainText(projectText(contents));
    editor->document()->setModified(false);
    connect(source->sync, &QTimer::timeout, editor, [this, handle] {
        language_.change(handle->path, handle->editor->toPlainText());
    });
    connect(editor, &QPlainTextEdit::textChanged, editor, [this, handle] {
        handle->sync->start();
        highlightDebugLines(*handle);
        if (!handle->diagnostics.empty()) {
            handle->diagnostics.clear();
            handle->editor->showProblems({});
            refreshProblems();
        }
    });
    connect(editor->document(), &QTextDocument::modificationChanged, editor, [this, handle](bool modified) {
        const int index = editors_->indexOf(handle->editor);
        if (index >= 0) editors_->setTabText(index, QFileInfo(handle->path).fileName() + (modified ? " •" : ""));
        highlightDebugLines(*handle);
        if (editors_->currentWidget() == handle->editor) selectSource(false);
    });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, editor, [this, editor] {
        if (editors_->currentWidget() == editor) selectSource(false);
    });
    if (!language.id.isEmpty()) editor->completionRequested = [this, handle] { requestCompletion(*handle); };
    language_.open(canonical, language, editor->toPlainText());
    sources_.push_back(std::move(source));
    const auto index = editors_->addTab(editor, QFileInfo(canonical).fileName());
    editors_->setTabToolTip(index, canonical);
    auto* close = new QToolButton(editors_->tabBar());
    close->setObjectName("ideCloseTab");
    close->setText("×");
    close->setAccessibleName("Fermer " + QFileInfo(canonical).fileName());
    close->setToolTip(close->accessibleName());
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QToolButton::clicked, this, [this, editor] { closeSource(editors_->indexOf(editor)); });
    editors_->tabBar()->setTabButton(index, QTabBar::RightSide, close);
    highlightDebugLines(*handle);
    if (activate) {
        editors_->setCurrentIndex(index);
        selectFile();
        if (focus) editor->setFocus();
    }
    return true;
}

IdeWindow::Source* IdeWindow::findSource(const QString& path) {
    for (const auto& source : sources_) if (source->path == path) return source.get();
    return nullptr;
}

IdeWindow::Source* IdeWindow::sourceForWidget(QWidget* widget) {
    for (const auto& source : sources_) if (source->editor == widget) return source.get();
    return nullptr;
}

bool IdeWindow::readSearchSource(const QString& path, QString& text) {
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || !belongsToProject(path) || info.canonicalFilePath() != path)
        return false;
    if (const auto* source = findSource(path)) {
        text = source->editor->sourceText();
        return true;
    }
    QByteArray bytes;
    if (!readProjectText(path, bytes)) return false;
    text = projectText(bytes);
    return true;
}

bool IdeWindow::applyProjectTextEdits(const std::vector<TextEdit>& edits, QString& error) {
    const auto changed = [&](const QString& path) {
        error = "Fichier modifié ou indisponible depuis l’aperçu · " + QDir(root_).relativeFilePath(path) +
                ". Actualiser l’aperçu. Aucune modification appliquée.";
        return false;
    };
    for (const auto& edit : edits) {
        QString current;
        if (!readSearchSource(edit.path, current) || current != edit.before) return changed(edit.path);
        if (const auto* source = findSource(edit.path)) {
            QByteArray disk;
            if (source->editor->isReadOnly() || !readProjectText(edit.path, disk) || disk != source->diskText)
                return changed(edit.path);
        }
    }
    std::vector<CodeEditor*> targets;
    for (const auto& edit : edits) {
        if (!openSource(edit.path, false, false)) return changed(edit.path);
        auto* source = findSource(edit.path);
        if (!source || source->editor->sourceText() != edit.before) return changed(edit.path);
        targets.push_back(source->editor);
    }
    // All selected snapshots still match. Apply one undo block per document.
    for (size_t i = 0; i < edits.size(); ++i) targets[i]->applySearchEdit(edits[i]);
    refreshActions();
    statusBar()->showMessage(QString("%1 fichier(s) modifié(s) · Tout enregistrer pour écrire sur le disque").arg(edits.size()), 6500);
    return true;
}

void IdeWindow::closeSource(int index) {
    auto* source = sourceForWidget(editors_->widget(index));
    if (!source) return;
    if (source->editor->document()->isModified()) {
        const auto response = QMessageBox::question(this, "Modifications en attente", "Enregistrer " + QFileInfo(source->path).fileName() + " ?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (response == QMessageBox::Cancel || (response == QMessageBox::Save && !save(*source))) return;
    }
    language_.close(source->path);
    auto* editor = source->editor;
    editors_->removeTab(index);
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
        [source](const std::unique_ptr<Source>& item) { return item.get() == source; }), sources_.end());
    delete editor;
    refreshProblems();
    refreshActions();
}

bool IdeWindow::save(Source& source) {
    if (!source.editor->document()->isModified()) return true;
    QByteArray disk;
    if (!belongsToProject(source.path) || !readProjectText(source.path, disk) || disk != source.diskText) {
        warn("Le fichier a changé sur le disque. Les modifications de l'éditeur sont conservées dans l'onglet : " + source.path);
        return false;
    }
    auto text = source.editor->sourceText();
    if (source.diskText.contains("\r\n")) text.replace("\n", "\r\n");
    else if (source.diskText.contains('\r') && !source.diskText.contains('\n')) text.replace('\n', '\r');
    auto bytes = text.toUtf8();
    if (source.diskText.startsWith("\xef\xbb\xbf")) bytes.prepend("\xef\xbb\xbf");
    QSaveFile file(source.path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        warn("Impossible d'enregistrer " + source.path + " · " + file.errorString()); return false;
    }
    source.diskText = bytes;
    source.editor->document()->setModified(false);
    language_.change(source.path, source.editor->toPlainText());
    language_.save(source.path);
    statusBar()->showMessage("Enregistré · " + QFileInfo(source.path).fileName(), 3500);
    return true;
}

bool IdeWindow::saveAll() {
    for (auto& source : sources_) if (!save(*source)) return false;
    return true;
}

bool IdeWindow::allowCloseSources() {
    if (std::none_of(sources_.begin(), sources_.end(), [](const std::unique_ptr<Source>& source) {
        return source->editor->document()->isModified();
    })) return true;
    const auto response = QMessageBox::question(this, "Modifications en attente", "Enregistrer les fichiers modifiés du projet ?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return response == QMessageBox::Discard || (response == QMessageBox::Save && saveAll());
}

void IdeWindow::closeEvent(QCloseEvent* event) {
    if (build_.busy()) {
        statusBar()->showMessage("Attendre ou annuler la compilation avant de fermer.", 5000);
        event->ignore();
        return;
    }
    if (!allowCloseSources()) { event->ignore(); return; }
    QSettings().setValue("ide/geometry", saveGeometry());
    resetProject();
    QMainWindow::closeEvent(event);
}

void IdeWindow::configureTools() {
    if (build_.busy()) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Outils du projet");
    dialog.resize(695, 400);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel("Les champs vides utilisent la détection automatique. Indique un chemin si un outil est installé ailleurs.");
    intro->setWordWrap(true); layout->addWidget(intro);
    auto* fields = new QFormLayout;
    QSettings settings;
    const Tools detected = Tools::detect();
    struct Entry { const char* title; const char* key; QString found; bool folder; QLineEdit* field; };
    Entry entries[] = {
        {"CMake", "ide/cmake", detected.cmake, false},
        {"Git", "ide/git", QStandardPaths::findExecutable("git"), false},
        {"Compilateur ARM GCC", "ide/gcc", detected.gcc, false},
        {"Pico SDK", "ide/sdk", detected.sdk, true},
        {"clangd", "ide/clangd", detected.clangd, false},
        {"GDB ARM pour SWD", "probe/gdb", detected.gdb, false},
        {"OpenOCD pour SWD", "probe/openocd", detected.openocd, false}
    };
    for (auto& entry : entries) {
        auto* row = new QHBoxLayout;
        auto* field = new QLineEdit(settings.value(entry.key).toString());
        entry.field = field;
        field->setPlaceholderText(entry.found.isEmpty() ? "Introuvable" : entry.found);
        auto* browse = new QPushButton("Parcourir…");
        row->addWidget(field, 1); row->addWidget(browse);
        fields->addRow(entry.title, row);
        const bool folder = entry.folder;
        connect(browse, &QPushButton::clicked, &dialog, [&, field, folder] {
            const auto path = folder
                ? QFileDialog::getExistingDirectory(&dialog, "Choisir le dossier du SDK", field->text())
                : QFileDialog::getOpenFileName(&dialog, "Choisir l'exécutable", field->text());
            if (!path.isEmpty()) field->setText(path);
        });
    }
    layout->addLayout(fields);
    auto* links = new QLabel(dependencyLinks());
    links->setTextFormat(Qt::RichText);
    links->setWordWrap(true); links->setOpenExternalLinks(true); layout->addWidget(links);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons.addButton("Utiliser la détection automatique", QDialogButtonBox::ResetRole);
    connect(&buttons, &QDialogButtonBox::clicked, &dialog, [&](QAbstractButton* button) {
        if (buttons.buttonRole(button) == QDialogButtonBox::ResetRole)
            for (const auto& entry : entries) entry.field->clear();
    });
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(&buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    for (const auto& entry : entries) {
        const auto path = entry.field->text().trimmed();
        if (path.isEmpty()) settings.remove(entry.key);
        else settings.setValue(entry.key, path);
    }
    tools_ = Tools::detect();
    if (!root_.isEmpty()) {
        buildDirectory_ = projectBuildDirectory(root_, projectBoard(), tools_);
        restartLanguage(true);
    }
    statusBar()->showMessage("Outils détectés à nouveau", 4000);
}

void IdeWindow::restartLanguage(bool force) {
    if (root_.isEmpty()) return;
    QFile database(QDir(buildDirectory_).filePath("compile_commands.json"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool hasDatabase = database.open(QIODevice::ReadOnly) && hash.addData(&database);
    const auto context = root_ + '\n' + buildDirectory_ + '\n' + tools_.clangd +
                         (hasDatabase ? QString::fromLatin1(hash.result().toHex()) : QString());
    if (!force && context == languageContext_) return;
    languageContext_ = context;
    language_.start(clangdServer(tools_.clangd, buildDirectory_, tools_.gcc), root_);
}

void IdeWindow::requestCompletion(Source& source) {
    if (languageForFile(source.path).id.isEmpty()) return;
    if (!language_.ready()) {
        statusBar()->showMessage("Autocomplétion disponible avec clangd", 4000);
        return;
    }
    const auto cursor = source.editor->textCursor();
    const int revision = source.editor->document()->revision();
    const int position = cursor.position();
    const auto path = source.path;
    const unsigned sequence = ++source.completionSequence;
    QPointer<CodeEditor> editor(source.editor);
    language_.complete(path, source.editor->toPlainText(), cursor.blockNumber(), cursor.positionInBlock(),
        [this, editor, path, sequence, revision, position](const QJsonArray& items) {
            const auto* source = findSource(path);
            if (editor && source && source->completionSequence == sequence && editors_->currentWidget() == editor &&
                editor->document()->revision() == revision && editor->textCursor().position() == position)
                editor->showCompletions(items);
        });
}

void IdeWindow::receiveDiagnostics(const QString& path, const QJsonArray& items) {
    auto* source = findSource(path);
    if (!source || source->sync->isActive()) return;
    source->diagnostics.clear();
    for (const auto& value : items) {
        const auto item = value.toObject();
        const auto range = item.value("range").toObject();
        const auto begin = range.value("start").toObject(), end = range.value("end").toObject();
        if (begin.isEmpty() || end.isEmpty()) continue;
        Problem problem;
        problem.path = path;
        problem.line = begin.value("line").toInt();
        problem.column = begin.value("character").toInt();
        problem.endLine = end.value("line").toInt();
        problem.endColumn = end.value("character").toInt();
        problem.severity = item.value("severity").toInt(2);
        problem.message = item.value("message").toString();
        source->diagnostics.push_back(problem);
    }
    source->editor->showProblems(source->diagnostics);
    refreshProblems();
}

void IdeWindow::refreshProblems() {
    problems_->clear();
    const auto append = [this](const Problem& issue) {
        const auto file = QDir(root_).relativeFilePath(issue.path);
        auto* row = new QTreeWidgetItem(problems_, {level(issue.severity), file + ":" + QString::number(issue.line + 1), issue.message});
        row->setForeground(0, issue.severity == 1 ? picoThemeColor("#ff9490") : picoThemeColor("#f1ce80"));
        row->setToolTip(2, issue.message);
        row->setData(0, Qt::UserRole, issue.path);
        row->setData(1, Qt::UserRole, issue.line);
        row->setData(2, Qt::UserRole, issue.column);
    };
    for (const auto& issue : buildProblems_) append(issue);
    for (const auto& source : sources_) for (const auto& issue : source->diagnostics) append(issue);
    const int count = problems_->topLevelItemCount();
    bottom_->setTabText(1, count ? "Problèmes (" + QString::number(count) + ")" : "Problèmes");
}

void IdeWindow::build(Task task) {
    if (root_.isEmpty() || build_.busy() || !saveAll()) return;
    task_ = task;
    buildProblems_.clear(); buildLine_.clear(); refreshProblems();
    output_->clear();
    bottom_->setCurrentIndex(0);
    try {
        const auto selected = target_->currentData().toString();
        statusBar()->showMessage(task == Task::Configure ? "Configuration CMake…" : "Compilation…");
        build_.start(root_, buildDirectory_, projectBoard(), tools_, selected, task == Task::Configure);
    } catch (const std::exception& error) {
        debuggerFeedback(QString::fromUtf8(error.what()), true);
    }
    refreshActions();
}

void IdeWindow::appendBuildOutput(const QString& text) {
    auto tail = output_->textCursor();
    tail.movePosition(QTextCursor::End);
    tail.insertText(text);
    output_->setTextCursor(tail);
    output_->ensureCursorVisible();
    buildLine_ += text;
    int from = 0;
    for (int end = buildLine_.indexOf('\n'); end >= 0; end = buildLine_.indexOf('\n', from)) {
        considerBuildLine(buildLine_.mid(from, end - from).trimmed());
        from = end + 1;
    }
    if (from > 0) buildLine_.remove(0, from);
    if (buildLine_.size() > 16384) buildLine_.clear();
}

void IdeWindow::considerBuildLine(const QString& line) {
    if (buildProblems_.size() >= 500) return;
    static const QRegularExpression diagnostic(R"(^(.+?):(\d+)(?::(\d+))?:\s+(fatal error|error|warning|note):\s+(.+)$)");
    const auto match = diagnostic.match(line);
    if (!match.hasMatch()) return;
    auto info = QFileInfo(QDir(root_).absoluteFilePath(match.captured(1)));
    if (!info.isFile()) info = QFileInfo(QDir(buildDirectory_).absoluteFilePath(match.captured(1)));
    if (!belongsToProject(info.absoluteFilePath())) return;
    Problem problem;
    problem.path = info.canonicalFilePath();
    problem.line = qMax(0, match.captured(2).toInt() - 1);
    problem.column = qMax(0, match.captured(3).toInt() - 1);
    problem.endLine = problem.line;
    problem.endColumn = problem.column + 1;
    const auto type = match.captured(4);
    problem.severity = type == "note" ? 3 : type == "warning" ? 2 : 1;
    problem.message = match.captured(5);
    buildProblems_.push_back(problem);
}

void IdeWindow::buildFinished(bool success, const QString& message, const std::vector<Artifact>& artifacts) {
    if (!buildLine_.isEmpty()) considerBuildLine(buildLine_);
    buildLine_.clear();
    if (success || !artifacts.empty()) {
        const auto selected = QSettings().value(settingsKey_ + "/target").toString();
        const QSignalBlocker blocked(target_);
        target_->clear();
        target_->addItem("Toutes les cibles", QString());
        for (const auto& item : artifacts)
            if (target_->findData(item.target) < 0) target_->addItem(item.target, item.target);
        target_->setCurrentIndex(qMax(0, target_->findData(selected)));
    }
    restartLanguage();
    refreshProblems();
    refreshActions();
    debuggerFeedback(message, !success);
    if (!success || task_ == Task::Configure || task_ == Task::Build) return;
    if (std::any_of(sources_.begin(), sources_.end(), [](const std::unique_ptr<Source>& source) {
        return source->editor->document()->isModified();
    })) {
        debuggerFeedback("Des sources ont été modifiées pendant la compilation. Relancer pour utiliser les nouveaux fichiers.", true);
        return;
    }
    launch(artifacts);
}

void IdeWindow::launch(const std::vector<Artifact>& artifacts) {
    const auto selected = target_->currentData().toString();
    QStringList choices, paths;
    for (const auto& artifact : artifacts) {
        if (!selected.isEmpty() && selected != artifact.target) continue;
        try {
            // Imports may also build host tools. The debugger only accepts ELF32 ARM.
            picodbg::ElfImage::read(artifact.path.toStdString());
            choices << artifact.target + " — " + QFileInfo(artifact.path).fileName();
            paths << artifact.path;
        } catch (const std::exception&) {}
    }
    if (paths.isEmpty()) {
        debuggerFeedback("La cible compilée ne produit pas d'ELF ARM compatible avec PicoDebugger.", true);
        return;
    }
    int index = 0;
    if (paths.size() > 1) {
        bool accepted;
        const auto choice = QInputDialog::getItem(this, "Choisir l'ELF à lancer", "Cible", choices, 0, false, &accepted);
        if (!accepted) return;
        index = choices.indexOf(choice);
    }
    startupElf_.clear();
    startupDemo_ = false;
    openDebugger().launchProjectElf(paths.at(index), projectBoard(), task_ == Task::Board, false);
}

void IdeWindow::refreshActions() {
    const bool busy = build_.busy();
    const bool hasProject = !root_.isEmpty();
    const bool ready = hasProject && !busy;
    newProject_->setEnabled(!busy); openProject_->setEnabled(!busy);
    closeProject_->setEnabled(ready); newFile_->setEnabled(ready); newFolder_->setEnabled(ready);
    const auto selected = files_->filePath(tree_->currentIndex());
    const bool editableFile = ready && selected != QDir(root_).filePath("CMakeLists.txt") &&
                              belongsToProject(selected) && QFileInfo(selected).isFile();
    rename_->setEnabled(editableFile); trash_->setEnabled(editableFile);
    compile_->setEnabled(ready); configure_->setEnabled(ready); debug_->setEnabled(ready);
    generateDocs_->setEnabled(ready);
    boardRun_->setEnabled(ready && !emulationTemplate_ && hardwareReady_ && hardwareBoard_ == projectBoard());
    boardRun_->setToolTip(emulationTemplate_ ? "Le modèle d'émulation produit un ELF pour le simulateur."
        : hardwareReady_ && hardwareBoard_ != projectBoard()
        ? "La carte connectée diffère du profil de compilation."
        : hardwareReady_ ? "Compiler, téléverser et laisser la carte arrêtée"
                         : "Connecter et arrêter la carte dans le débogueur");
    cancel_->setEnabled(busy);
    cancel_->setVisible(busy);
    board_->setEnabled(ready); target_->setEnabled(ready);
    save_->setEnabled(hasProject && sourceForWidget(editors_->currentWidget()));
    saveAll_->setEnabled(hasProject && !sources_.empty());
    goToAssembly_->setEnabled(debugger_ && sourceForWidget(editors_->currentWidget()));
}

void IdeWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (debugger_) debugger_->syncIdeState();
    if (followSource_->isChecked()) revealDebugLocation(executingSource_);
}

void IdeWindow::hideEvent(QHideEvent* event) {
    QMainWindow::hideEvent(event);
    if (debugger_) debugger_->syncIdeState();
}

void IdeWindow::debuggerState(bool ready, bool canPause, bool canStep, bool debuggerVisible, bool inspectAssembly,
                              picodbg::Board card, const QString& description) {
    pause_->setEnabled(canPause);
    step_->setEnabled(canStep);
    if (inspectAssembly_ != inspectAssembly) {
        inspectAssembly_ = inspectAssembly;
        updateAssemblyVisibility();
    }
    linkStatus_->setText(debuggerVisible ? "Débogueur relié" : "Débogueur fermé");
    if (hardwareReady_ == ready && hardwareBoard_ == card && targetStatus_->text() == description) return;
    hardwareReady_ = ready;
    hardwareBoard_ = card;
    targetStatus_->setText(description);
    refreshActions();
}

void IdeWindow::debuggerFeedback(const QString& message, bool error) {
    appendBuildOutput((error ? "Erreur · " : QString()) + message + "\n");
    statusBar()->showMessage(message, 6500);
    if (error) bottom_->setCurrentIndex(0);
}

void IdeWindow::warn(const QString& message) {
    QMessageBox::warning(this, "PicoDebugger IDE", message);
}

void IdeWindow::debuggerSourceState(const picosource::Location& current, const picosource::Location& selected,
                                    const picosource::SourceMap& map, bool hasPc, uint32_t pc,
                                    bool force, bool showSelection) {
    const auto timestamp = map.elfTimestamp();
    const bool lineChanged = executingSource_ != current || debugElfTimestamp_ != timestamp;
    const bool selectionChanged = selectedSource_ != selected;
    executingSource_ = current;
    selectedSource_ = selected;
    debugElfTimestamp_ = timestamp;
    if (lineChanged || selectionChanged)
        for (auto& source : sources_) highlightDebugLines(*source);
    if (showSelection) revealDebugLocation(selectedSource_);
    else if (lineChanged && followSource_->isChecked()) revealDebugLocation(executingSource_);
    if (!assemblyPane_->isHidden() &&
        (force || lineChanged || selectionChanged || assemblyDirty_ || hasPc != hasAssemblyPc_ || pc != assemblyPc_))
        refreshAssembly(map, hasPc, pc);
}

void IdeWindow::updateAssemblyVisibility() {
    const bool show = inspectAssembly_ && showAssembly_->isChecked();
    if (show == !assemblyPane_->isHidden()) return;
    if (!show) {
        const auto sizes = codeSplit_->sizes();
        if (sizes.at(1) > 0) assemblySizes_ = sizes;
    }
    assemblyPane_->setVisible(show);
    if (show) {
        const int width = codeSplit_->width();
        const int codeWidth = width * 56 / 100;
        codeSplit_->setSizes(assemblySizes_.isEmpty() ? QList<int>{codeWidth, width - codeWidth} : assemblySizes_);
    }
    assemblyDirty_ = true;
}

void IdeWindow::refreshAssembly(const picosource::SourceMap& map, bool hasPc, uint32_t pc) {
    hasAssemblyPc_ = hasPc;
    assemblyPc_ = pc;
    assemblyDirty_ = false;
    std::set<uint32_t> rows;
    constexpr size_t maxInstructionsPerLine = 1024;
    bool truncated = false;
    for (const auto& location : {executingSource_, selectedSource_}) {
        const auto& addresses = map.assemblyAddresses(location);
        const auto count = std::min(addresses.size(), maxInstructionsPerLine);
        rows.insert(addresses.begin(), addresses.begin() + count);
        truncated |= addresses.size() > count;
    }
    if (hasPc && !map.instruction(pc).isEmpty()) rows.insert(pc);
    assembly_->setRowCount(static_cast<int>(rows.size()));
    int row = 0, pcRow = -1, selectionRow = -1;
    for (const auto address : rows) {
        const auto location = map.location(address);
        const bool atPc = hasPc && pc == address;
        const bool executing = atPc || (location.valid() && location == executingSource_);
        const bool selected = location.valid() && location == selectedSource_;
        const QColor color(executing ? picosource::executionColor() : picosource::selectionColor());
        const QStringList columns{atPc ? "▶" : QString(), QString::fromStdString(picodbg::hex(address)),
            map.instruction(address), location.valid() ? QFileInfo(location.path).fileName() + ":" + QString::number(location.line) : QString()};
        for (int column = 0; column < columns.size(); ++column) {
            auto* cell = new QTableWidgetItem(columns.at(column));
            cell->setToolTip(columns.at(column));
            if (executing || selected) cell->setBackground(color);
            assembly_->setItem(row, column, cell);
        }
        if (atPc) pcRow = row;
        if (selected && selectionRow < 0) selectionRow = row;
        ++row;
    }
    if (selectionRow >= 0) assembly_->scrollToItem(assembly_->item(selectionRow, 1));
    else if (pcRow >= 0) assembly_->scrollToItem(assembly_->item(pcRow, 1));
    QString status = map.assemblyStatus();
    if (status.isEmpty()) {
        if (!map.hasLocations()) status = map.status();
        else if (truncated) status = "Affichage limité aux 1 024 premières instructions par ligne.";
        else if (selectedSource_.valid() && selectionRow < 0) status = "Aucune instruction associée à la ligne sélectionnée.";
        else if (rows.empty()) status = "Sélectionner une ligne C pour voir ses instructions.";
        else status = "▶ PC · turquoise  |  Sélection · violet";
    }
    assemblyStatus_->setText(status);
}

bool IdeWindow::sourceMatchesElf(const Source& source) const {
    if (!debugElfTimestamp_.isValid() || source.editor->document()->isModified()) return false;
    const QFileInfo file(source.path);
    return file.isFile() && file.lastModified() <= debugElfTimestamp_;
}

void IdeWindow::highlightDebugLines(Source& source) {
    const int execution = executingSource_.path == source.path ? executingSource_.line : 0;
    const int selection = selectedSource_.path == source.path ? selectedSource_.line : 0;
    if ((execution || selection) && sourceMatchesElf(source)) source.editor->setDebugLines(execution, selection);
    else source.editor->setDebugLines(0, 0);
}

void IdeWindow::revealDebugLocation(const picosource::Location& location) {
    if (!isVisible() || !location.valid() || !belongsToProject(location.path) ||
        QFileInfo(location.path).lastModified() > debugElfTimestamp_) return;
    const auto* current = sourceForWidget(editors_->currentWidget());
    if (current && current->editor->document()->isModified() && current->path != location.path) return;
    if (const auto* source = findSource(location.path)) {
        if (!sourceMatchesElf(*source)) return;
    }
    const QScopedValueRollback<bool> guard(syncingSource_, true);
    if (openSource(location.path, false)) {
        if (auto* source = findSource(location.path)) {
            highlightDebugLines(*source);
            source->editor->revealDebugLine(location.line);
        }
    }
}

void IdeWindow::selectSource(bool navigate) {
    if (!debugger_ || syncingSource_) return;
    auto* source = sourceForWidget(editors_->currentWidget());
    picosource::Location location;
    if (source && !languageForFile(source->path).id.isEmpty() && sourceMatchesElf(*source)) {
        location.path = source->path;
        location.line = source->editor->textCursor().blockNumber() + 1;
    }
    if (navigate && source && debugElfTimestamp_.isValid() && !sourceMatchesElf(*source)) {
        statusBar()->showMessage("Source modifiée depuis la compilation. Recompiler le projet.", 5500);
        return;
    }
    const bool mapped = debugger_->selectSourceLine(location, navigate);
    if (navigate && mapped && inspectAssembly_ && !showAssembly_->isChecked()) showAssembly_->trigger();
    if (navigate && !mapped)
        statusBar()->showMessage("Aucun assembleur disponible pour cette ligne. Charger un ELF avec -g et arrêter la carte.", 6500);
}

}
