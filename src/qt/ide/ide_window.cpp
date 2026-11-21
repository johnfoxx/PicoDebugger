#include "ide_window.hpp"
#include "../main_window.hpp"
#include "../choice_box.hpp"
#include "../window_policy.hpp"
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
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextBlock>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

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
bool readText(const QString& path, QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) return false;
    bytes = file.readAll();
    return file.error() == QFileDevice::NoError && !bytes.contains('\0') &&
           QString::fromUtf8(bytes).toUtf8() == (bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes);
}
}

IdeWindow::IdeWindow(MainWindow* debugger)
    : debugger_(debugger), tools_(Tools::detect()), language_(this), build_(this) {
    setObjectName("picoIdeWindow");
    setWindowTitle("PicoDebugger — IDE C");
    resize(1180, 800);
    setPalette(debugger->palette());
    setStyleSheet(debugger->styleSheet() +
        "QTreeView {background:#131e2b;border:1px solid #304052;alternate-background-color:#192635;}"
        "QTreeView::item {min-height:23px;} QTreeView::item:selected {background:#28536b;}"
        "QToolBar#ideToolbar {spacing:3px;padding:4px;}"
        "QLabel#ideAnalysisStatus,QLabel#ideTargetStatus {padding:0 7px;color:#a5bdd3;}"
        "QLabel#ideProjectTitle {color:#eff7ff;font-size:15px;font-weight:600;}");
    setupUi();
    setProjectBoard(debugger->selectedBoard());
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

void IdeWindow::setupUi() {
    auto* file = menuBar()->addMenu("Projet");
    auto* edit = menuBar()->addMenu("Édition");
    auto* buildMenu = menuBar()->addMenu("Compilation");
    auto* help = menuBar()->addMenu("Aide");
    auto* toolbar = addToolBar("Projet et lancement");
    toolbar->setObjectName("ideToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
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
    action(edit, "Rechercher…", "ideFind", QKeySequence::Find, [this] {
        auto* source = sourceForWidget(editors_->currentWidget());
        if (!source) return;
        bool ok;
        const auto text = QInputDialog::getText(this, "Rechercher dans le fichier", "Texte", QLineEdit::Normal,
                                               source->editor->textCursor().selectedText(), &ok);
        if (!ok || text.isEmpty()) return;
        auto cursor = source->editor->textCursor();
        if (!source->editor->find(text)) {
            source->editor->moveCursor(QTextCursor::Start);
            if (!source->editor->find(text)) {
                source->editor->setTextCursor(cursor);
                statusBar()->showMessage("Texte introuvable", 4000);
            }
        }
    });
    action(edit, "Autocomplétion", "ideCompletion", {}, [this] {
        if (auto* source = sourceForWidget(editors_->currentWidget())) requestCompletion(*source);
    })->setToolTip("Ctrl+Espace dans l'éditeur");
    configure_ = action(buildMenu, "Configurer CMake", "ideConfigure", {}, [this] { build(Task::Configure); });
    compile_ = action(buildMenu, "Compiler", "ideBuild", QKeySequence("Ctrl+B"), [this] { build(Task::Build); });
    debug_ = action(buildMenu, "Ouvrir dans le débogueur à l'arrêt", "ideDebug", QKeySequence("Ctrl+F5"), [this] { build(Task::Debug); });
    run_ = action(buildMenu, "Lancer en émulation", "ideRun", QKeySequence(Qt::Key_F5), [this] { build(Task::Run); });
    boardRun_ = action(buildMenu, "Lancer sur la carte…", "ideRunBoard", QKeySequence("Shift+F5"), [this] { build(Task::Board); });
    cancel_ = action(buildMenu, "Annuler la compilation", "ideCancel", {}, [this] { build_.cancel(); });
    action(buildMenu, "Outils du projet…", "ideToolchains", {}, [this] { configureTools(); });
    action(help, "Installation des outils…", "ideInstallGuide", {}, [this] { configureTools(); });
    auto* debugger = action(help, "Afficher le débogueur", "ideShowDebugger", QKeySequence("Ctrl+Shift+D"), [this] {
        if (debugger_) showInspectionWindow(debugger_);
    });
    openProject_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openProject_->setIconText("Ouvrir");
    compile_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    run_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    run_->setIconText("Émuler");
    boardRun_->setIconText("Carte");
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
    toolbar->addSeparator(); toolbar->addAction(compile_); toolbar->addAction(run_);
    toolbar->addAction(boardRun_); toolbar->addAction(cancel_);
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
    editors_->setTabsClosable(true); editors_->setMovable(true);
    connect(editors_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeSource(index); });
    connect(editors_, &QTabWidget::currentChanged, this, [this] { refreshActions(); });
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
    editors_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    editors_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    workspace->addWidget(editors_);
    workspace->setStretchFactor(1, 1);
    workspace->setSizes({245, 900});
    rootPane->addWidget(workspace);

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
    statusBar()->addPermanentWidget(analysisStatus_);
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
    populateBoards(card);
    card.setCurrentIndex(card.findData(static_cast<int>(projectBoard())));
    ChoiceBox type;
    type.addItem("Exemple C pour émulation", static_cast<int>(Template::Emulator));
    type.addItem("Firmware Pico SDK pour carte réelle", static_cast<int>(Template::Sdk));
    QLabel explanation;
    explanation.setWordWrap(true);
    const auto explain = [&] {
        explanation.setText(type.currentIndex() == 0
            ? "Un exemple accessible sans Pico SDK, à compiler avec ARM GCC. Il clignote sur la sortie GPIO25 simulée."
            : "Un exemple utilisant le Pico SDK et la LED de la carte. Le SDK et le compilateur ARM GCC sont nécessaires.");
    };
    connect(&type, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, explain);
    explain();
    layout.addRow("Nom", &name); layout.addRow("Dossier parent", folderRow);
    layout.addRow("Carte", &card); layout.addRow("Modèle", &type); layout.addRow(&explanation);
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
    return true;
}

void IdeWindow::resetProject() {
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
    statusBar()->showMessage("Fichier placé dans la corbeille · vérifier ses références dans le projet", 6000);
}

bool IdeWindow::openSource(const QString& path) {
    if (!belongsToProject(path)) return false;
    const auto canonical = QFileInfo(path).canonicalFilePath();
    if (auto* existing = findSource(canonical)) {
        editors_->setCurrentWidget(existing->editor);
        existing->editor->setFocus();
        return true;
    }
    QByteArray contents;
    if (!readText(canonical, contents)) { warn("Impossible d'ouvrir ce fichier. L'éditeur accepte les fichiers texte UTF-8 jusqu'à 2 Mio."); return false; }
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
    editor->setPlainText(QString::fromUtf8(contents));
    editor->document()->setModified(false);
    connect(source->sync, &QTimer::timeout, editor, [this, handle] {
        language_.change(handle->path, handle->editor->toPlainText());
    });
    connect(editor, &QPlainTextEdit::textChanged, editor, [this, handle] {
        handle->sync->start();
        if (!handle->diagnostics.empty()) {
            handle->diagnostics.clear();
            handle->editor->showProblems({});
            refreshProblems();
        }
    });
    connect(editor->document(), &QTextDocument::modificationChanged, editor, [this, handle](bool modified) {
        const int index = editors_->indexOf(handle->editor);
        if (index >= 0) editors_->setTabText(index, QFileInfo(handle->path).fileName() + (modified ? " •" : ""));
    });
    if (!language.id.isEmpty()) editor->completionRequested = [this, handle] { requestCompletion(*handle); };
    language_.open(canonical, language, editor->toPlainText());
    sources_.push_back(std::move(source));
    const auto index = editors_->addTab(editor, QFileInfo(canonical).fileName());
    editors_->setTabToolTip(index, canonical);
    editors_->setCurrentIndex(index);
    const auto treeIndex = files_->index(canonical);
    if (treeIndex.isValid()) {
        tree_->setCurrentIndex(treeIndex);
        tree_->scrollTo(treeIndex);
    }
    editor->setFocus();
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
    if (!belongsToProject(source.path) || !readText(source.path, disk) || disk != source.diskText) {
        warn("Le fichier a changé sur le disque. Les modifications de l'éditeur sont conservées dans l'onglet : " + source.path);
        return false;
    }
    auto text = source.editor->toPlainText();
    if (source.diskText.contains("\r\n")) text.replace("\n", "\r\n");
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
    auto* links = new QLabel(
        "<a href='https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html'>Guide et Pico SDK</a> · "
        "<a href='https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads'>Arm GNU Toolchain</a> · "
        "<a href='https://cmake.org/download/'>CMake</a> · "
        "<a href='https://clangd.llvm.org/installation'>clangd</a> · "
        "<a href='https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html'>Debug Probe et OpenOCD</a>");
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
        row->setForeground(0, issue.severity == 1 ? QColor("#ff9490") : QColor("#f1ce80"));
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
    if (!debugger_) return;
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
    debugger_->launchProjectElf(paths.at(index), projectBoard(), task_ == Task::Board, task_ != Task::Debug);
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
    compile_->setEnabled(ready); configure_->setEnabled(ready); run_->setEnabled(ready); debug_->setEnabled(ready);
    boardRun_->setEnabled(ready && !emulationTemplate_ && hardwareReady_ && hardwareBoard_ == projectBoard());
    boardRun_->setToolTip(emulationTemplate_ ? "Le modèle d'émulation produit un ELF pour le simulateur."
        : hardwareReady_ && hardwareBoard_ != projectBoard()
        ? "La carte connectée diffère du profil de compilation."
        : hardwareReady_ ? "Compiler, téléverser et lancer sur la carte connectée"
                         : "Connecter et arrêter la carte dans le débogueur");
    cancel_->setEnabled(busy);
    board_->setEnabled(ready); target_->setEnabled(ready);
    save_->setEnabled(hasProject && sourceForWidget(editors_->currentWidget()));
    saveAll_->setEnabled(hasProject && !sources_.empty());
}

void IdeWindow::debuggerState(bool ready, picodbg::Board card, const QString& description) {
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

}
