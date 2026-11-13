#include "main_window.hpp"
#include "peripheral_panel.hpp"
#include "window_policy.hpp"
#include "choice_box.hpp"
#include "inspection_workspace.hpp"
#include "peripheral_inspection.hpp"
#include "peripheral_explorer.hpp"
#include "virtual_time_panel.hpp"
#include "board_panel.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

using namespace picodbg;
namespace {
QString q(const std::string& text) { return QString::fromStdString(text); }
QString hx(uint32_t value) { return q(hex(value)); }
QTableWidget* table(const QStringList& headers) {
    auto* widget = new QTableWidget(0, headers.size());
    widget->setHorizontalHeaderLabels(headers);
    widget->verticalHeader()->hide();
    widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    widget->setSelectionBehavior(QAbstractItemView::SelectRows);
    widget->setSelectionMode(QAbstractItemView::SingleSelection);
    widget->setAlternatingRowColors(true);
    widget->setShowGrid(false);
    widget->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    widget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    widget->horizontalHeader()->setStretchLastSection(true);
    widget->verticalHeader()->setDefaultSectionSize(23);
    return widget;
}
void cell(QTableWidget* widget, int row, int column, const QString& text, const QString& tip = {}) {
    auto* item = widget->item(row, column);
    if (!item) { item = new QTableWidgetItem; widget->setItem(row, column, item); }
    item->setText(text); item->setToolTip(tip);
}
QWidget* vertical(QWidget* top, QWidget* body) {
    auto* pane = new QWidget; auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(6);
    layout->addWidget(top); layout->addWidget(body, 1); return pane;
}
QWidget* row(const QList<QWidget*>& widgets) {
    auto* pane = new QWidget; auto* layout = new QHBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    for (auto* widget : widgets) layout->addWidget(widget);
    return pane;
}
QDockWidget* dock(QMainWindow* parent, const QString& name, QWidget* content, Qt::DockWidgetArea area) {
    auto* panel = new QDockWidget(name, parent); panel->setObjectName(name); panel->setWidget(content);
    prepareInspectionWindow(panel);
    // Qt5 calls activateWindow() internally at the end of a floating dock drag.
    // Dedicated windows remain available through buttons, without that path.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (waylandDesktop()) panel->setFeatures(panel->features() & ~QDockWidget::DockWidgetFloatable);
#endif
    parent->addDockWidget(area, panel); parent->menuBar()->findChild<QMenu*>("views")->addAction(panel->toggleViewAction());
    return panel;
}
}
MainWindow::MainWindow() : debugger_(new Debugger), timer_(new QTimer(this)) {
    setWindowTitle("PicoDebugger — ARM / Thumb"); resize(1440, 900);
    // Supply a complete palette as well as widget accents: detached pages,
    // scrollbars and empty tab-strip areas must not fall back to a light theme.
    QPalette theme = palette();
    theme.setColor(QPalette::Window, QColor("#111923")); theme.setColor(QPalette::WindowText, QColor("#dce6f0"));
    theme.setColor(QPalette::Base, QColor("#131e2b")); theme.setColor(QPalette::AlternateBase, QColor("#192635"));
    theme.setColor(QPalette::Text, QColor("#dce6f0")); theme.setColor(QPalette::Button, QColor("#213044"));
    theme.setColor(QPalette::ButtonText, QColor("#dce6f0")); theme.setColor(QPalette::Highlight, QColor("#28536b"));
    theme.setColor(QPalette::HighlightedText, QColor("#ffffff")); theme.setColor(QPalette::Light, QColor("#40536a"));
    theme.setColor(QPalette::Midlight, QColor("#304052")); theme.setColor(QPalette::Mid, QColor("#26394b"));
    theme.setColor(QPalette::Dark, QColor("#0b111a")); theme.setColor(QPalette::Shadow, QColor("#0b111a"));
    theme.setColor(QPalette::Disabled, QPalette::Text, QColor("#637487"));
    theme.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#637487"));
    setPalette(theme);
    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    // Three stable regions: CPU comparison, central workspace, monitoring.
    // Secondary inspection views share a bottom tab strip instead of competing
    // for height. Docks remain resizable and can be restored in one action.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setStyleSheet(
        "QMainWindow,QDialog,QDockWidget {background:#111923;color:#dce6f0;}"
        "QWidget {background:#111923;color:#dce6f0;}"
        "QMenuBar,QMenu,QToolBar,QStatusBar {background:#182331;}"
        "QMenu::item:selected {background:#28536b;}"
        "QDockWidget::title {background:#213044;padding:7px;font-weight:600;}"
        "QTableWidget,QTreeWidget,QPlainTextEdit {background:#131e2b;alternate-background-color:#192635;selection-background-color:#28536b;border:1px solid #304052;}"
        "QHeaderView::section {background:#213044;color:#a5bdd3;padding:5px;border:none;}"
        "QLineEdit,QComboBox,QSpinBox {background:#111923;color:#dce6f0;border:1px solid #40536a;border-radius:3px;padding:5px;}"
        "QComboBox QAbstractItemView {background:#213044;color:#dce6f0;selection-background-color:#365f7f;}"
        "QPushButton {background:#29445c;border:1px solid #405b75;border-radius:3px;padding:5px 9px;}"
        "QPushButton:hover {background:#365f7f;}"
        "QToolButton {background:#213044;border:none;border-radius:3px;padding:7px;} QToolButton:hover {background:#29445c;}"
        "QToolButton:disabled {color:#637487;}"
        "QTabWidget::pane {border:1px solid #304052;}"
        "QTabBar {qproperty-drawBase:0;}"
        "QTabBar::tab {background:#182331;color:#a5bdd3;padding:8px 12px;border-bottom:2px solid transparent;}"
        "QTabBar::tab:selected {background:#213044;color:#eff7ff;border-bottom:2px solid #60c5c0;}"
        "QTabBar::tab:hover {background:#29445c;}"
        "QLabel#sectionTitle {font-size:16px;font-weight:600;color:#eff7ff;}"
        "QLabel#sessionTitle {font-size:16px;font-weight:600;}"
        "QLabel#sessionState {color:#8bd6bb;font-weight:600;}"
        "QLabel#guidance {color:#a5bdd3;}"
        "QPushButton:disabled {color:#637487;background:#182331;}"
        "QLineEdit:focus,QComboBox:focus {border:1px solid #60c5c0;}"
    );
    buildUi();
    connect(timer_, &QTimer::timeout, this, [this] { runBatch(); });
    timer_->setInterval(20);
    fillMapping(); refresh();
    log("Prêt. Ouvrir un ELF ARM ou l'exemple. Exécution arrêtée par défaut.");
    log("Simulation partielle : les accès non émulés arrêtent la session. Aucune garantie de compatibilité matérielle.");
}
void MainWindow::buildUi() {
    auto* file = menuBar()->addMenu("Fichier");
    auto* views = menuBar()->addMenu("Vues"); views->setObjectName("views");
    auto* overview = views->addAction("Rétablir la disposition initiale");
    overview->setObjectName("overviewAction");
    overview->setShortcut(QKeySequence("Ctrl+Shift+0"));
    connect(overview, &QAction::triggered, this, [this] {
        restoreState(overviewState_); workspace_->setCurrentIndex(0);
        inspectionTabs_->setCurrentIndex(0); monitorTabs_->setCurrentIndex(0);
        inspectionTabs_->setTabVisible(4, false);
    });
    views->addSeparator();
    auto* debug = menuBar()->addMenu("Débogage");
    auto* help = menuBar()->addMenu("Aide");
    auto* toolbar = addToolBar("Exécution"); toolbar->setObjectName("execution"); toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toolbar->setIconSize(QSize(16, 16));
    auto* open = file->addAction("Ouvrir un ELF…"); open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        setRunning(false);
        const auto path = QFileDialog::getOpenFileName(this, "Exécutable ELF32 ARM", QSettings().value("lastDirectory").toString(), "ELF (*.elf *.axf);;Tous les fichiers (*)");
        if (!path.isEmpty()) loadFile(path);
    });
    auto* demo = file->addAction("Exemple intégré"); connect(demo, &QAction::triggered, this, [this] { loadDemo(); });
    open->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton)); open->setIconText("Ouvrir ELF");
    demo->setIconText("Exemple");
    file->addSeparator(); connect(file->addAction("Quitter"), &QAction::triggered, this, &QWidget::close);
    toolbar->addAction(open); toolbar->addAction(demo); toolbar->addSeparator();
    chip_ = new ChoiceBox;
    for (const auto board : {Board::Pico, Board::Pico2, Board::PicoW, Board::Pico2W}) {
        const auto description = describeBoard(board);
        chip_->addItem(q(description.name) + (description.chip == Chip::RP2040 ? " / RP2040" : " / RP2350"), static_cast<int>(board));
        chip_->setItemData(chip_->count() - 1, q(description.name) + " · simulation partielle · " +
            (description.wireless ? "LED sur CYW43, radio non émulée. " : "LED sur GPIO25. ") +
            "Choisir un ELF pour cette carte. Voir Carte et câblage et Aide → Couverture et limites.", Qt::ToolTipRole);
    }
    chip_->setToolTip("Carte cible ; les quatre profils sont partiels. Le changement de carte vide la session.");
    chip_->setObjectName("chipSelector");
    toolbar->addWidget(chip_);
    connect(chip_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int index) {
        setRunning(false); closeInspections(); debugger_.reset(new Debugger(static_cast<Board>(chip_->itemData(index).toInt())));
        imageName_.clear(); previousValid_ = false; fillMapping(); refresh(); log("Profil changé : charger un ELF pour ce profil.");
    });
    toolbar->addSeparator();
    core_ = new ChoiceBox; core_->addItems({"Contexte : cœur 0", "Contexte : cœur 1"}); toolbar->addWidget(core_);
    core_->setObjectName("coreSelector");
    connect(core_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int index) { selected_ = static_cast<unsigned>(index); refresh(); });
    stepAction_ = debug->addAction("Pas instruction"); stepAction_->setShortcut(QKeySequence(Qt::Key_F10));
    stepAction_->setObjectName("stepAction");
    connect(stepAction_, &QAction::triggered, this, [this] { singleStep(); });
    runAction_ = debug->addAction("Exécuter les cœurs"); runAction_->setShortcut(QKeySequence(Qt::Key_F5));
    runAction_->setObjectName("runAction");
    connect(runAction_, &QAction::triggered, this, [this] { setRunning(false); setRunning(true); refresh(); });
    pauseAction_ = debug->addAction("Pause"); pauseAction_->setShortcut(QKeySequence("Shift+F5"));
    pauseAction_->setObjectName("pauseAction");
    connect(pauseAction_, &QAction::triggered, this, [this] { setRunning(false); debugger_->pause(); refresh(); });
    resetAction_ = debug->addAction("Reset / restart"); resetAction_->setShortcut(QKeySequence("Ctrl+R"));
    resetAction_->setObjectName("resetAction");
    connect(resetAction_, &QAction::triggered, this, [this] { setRunning(false); try { debugger_->reset(); previousValid_ = false; refresh(); log(q(debugger_->stop.message)); } catch (const std::exception& e) { failure(e); } });
    stepAction_->setIcon(style()->standardIcon(QStyle::SP_ArrowRight)); stepAction_->setIconText("Pas · F10");
    runAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay)); runAction_->setIconText("Exécuter · F5");
    pauseAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    resetAction_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload)); resetAction_->setIconText("Reset");
    stepAction_->setToolTip("Exécuter une instruction du cœur sélectionné (F10)");
    runAction_->setToolTip("Exécuter les deux cœurs démarrés jusqu'au prochain arrêt (F5)");
    resetAction_->setToolTip("Recharger l'image et remettre le modèle à son état initial (Ctrl+R)");
    toolbar->addAction(runAction_); toolbar->addAction(stepAction_); toolbar->addAction(pauseAction_);
    toolbar->addSeparator(); toolbar->addAction(resetAction_);
    auto* launch = debug->addAction("Démarrer un cœur explicitement…"); connect(launch, &QAction::triggered, this, [this] { launchCore(); });
    auto* bp = debug->addAction("Ajouter un point d'arrêt…"); bp->setShortcut(QKeySequence(Qt::Key_F9)); connect(bp, &QAction::triggered, this, [this] { addBreakpoint(); });
    auto* wp = debug->addAction("Ajouter un watchpoint mémoire…"); connect(wp, &QAction::triggered, this, [this] { addMemoryWatch(); });
    auto* expressionAction = debug->addAction("Ajouter une expression surveillée…");
    expressionAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(expressionAction, &QAction::triggered, this, [this] { addExpression(); });
    connect(help->addAction("Couverture et limites"), &QAction::triggered, this, [this] {
        QMessageBox::information(this, "Couverture de cette version",
            "Exécution locale sans carte, ARM/Thumb. Modèle fonctionnel partiel.\n\n"
            "RP2040 : instructions Thumb usuelles, BL, MRS/MSR, pile, HardFault/SVC, SysTick simplifié, timer/alarmes en temps virtuel, SRAM, Flash, FIFO et spinlocks.\n"
            "RP2350 / Cortex-M33 : moteur Thumb commun au RP2040, plus MOVW/MOVT et CBZ/CBNZ. Thumb-2 général et IT, division, DSP, FPU, accès exclusifs, MPU et TrustZone absents.\n"
            "Les registres NVIC/SysTick RP2350 sont consultables, mais toute entrée d'exception/IRQ M33 arrête le moteur : les handlers ne sont pas exécutables.\n"
            "Périphériques partiels des deux profils : timers, UART, SPI loopback, PWM, latches GPIO, resets, NVIC/SysTick et scratch watchdog.\n\n"
            "Pas de Boot ROM, SDK complet, DMA, PIO, USB, horloges ni timing cycle par cycle. Un accès hors couverture arrête l'exécution.\n"
            "Le cœur 1 se lance explicitement ; le protocole de lancement ROM n'est pas émulé.\n"
            "Le reset recharge l'ELF et remet la RAM à zéro : ce n'est pas le reset matériel du SoC.\n\n"
            "La pile est une vue mémoire brute, pas une reconstruction des appels C++. Les symboles ELF sont disponibles ; DWARF/source viendra ensuite.\n"
            "Voir README.md et docs/COVERAGE.md dans le projet.");
    });
    session_ = new QLabel; session_->setObjectName("sessionTitle");
    session_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    banner_ = new QLabel; banner_->setObjectName("sessionState");
    guidance_ = new QLabel; guidance_->setObjectName("guidance"); guidance_->setWordWrap(true);
    auto* sessionHeader = new QWidget; auto* sessionLayout = new QVBoxLayout(sessionHeader);
    sessionLayout->setContentsMargins(10, 8, 10, 4);
    sessionLayout->addWidget(row({session_, banner_})); sessionLayout->addWidget(guidance_);
    workspace_ = new InspectionWorkspace;
    assembly_ = table({"", "Adresse", "Opcode", "Instruction", "Symbole ELF"});
    assembly_->setObjectName("assemblyTable");
    assemblyAddress_ = new QLineEdit("pc"); assemblyAddress_->setPlaceholderText("Adresse ou symbole");
    followPc_ = new QCheckBox("Suivre PC"); followPc_->setChecked(true);
    auto* disassemble = new QPushButton("Afficher");
    connect(disassemble, &QPushButton::clicked, this, [this] { followPc_->setChecked(false); refreshAssembly(); });
    connect(assemblyAddress_, &QLineEdit::returnPressed, disassemble, &QPushButton::click);
    connect(followPc_, &QCheckBox::toggled, this, [this] { refreshAssembly(); });
    connect(assembly_, &QTableWidget::cellDoubleClicked, this, [this](int line, int) {
        const auto* item = assembly_->item(line, 1); if (!item) return;
        const uint32_t address = item->data(Qt::UserRole).toUInt();
        if (!address) return;
        if (debugger_->breakpoints.count(address)) debugger_->breakpoints.erase(address); else debugger_->breakpoints.insert(address);
        refreshAssembly(); refreshPoints();
        monitorTabs_->setCurrentIndex(1); showInspectionWindow(monitorDock_);
    });
    assemblyAddress_->setMinimumWidth(80);
    auto* assemblyPane = vertical(row({new QLabel("Adresse"), assemblyAddress_, disassemble, followPc_}), assembly_);
    source_ = new QPlainTextEdit; source_->setReadOnly(true);
    source_->setPlainText("Correspondance C++ — itération 2\n\nLes symboles ELF sont affichés à côté des instructions.\n\nL'import des sources et la lecture DWARF seront ajoutés dans une couche séparée.");
    codePages_ = new QStackedWidget;
    auto* welcome = new QWidget; auto* welcomeLayout = new QVBoxLayout(welcome);
    welcomeLayout->setContentsMargins(28, 24, 28, 24); welcomeLayout->addStretch();
    auto* welcomeTitle = new QLabel("Votre session de débogage"); welcomeTitle->setObjectName("sectionTitle");
    auto* welcomeText = new QLabel("1. Choisissez Pico ou Pico 2 dans la barre du haut.\n"
        "2. Ouvrez un ELF, ou explorez l'exemple intégré.\n"
        "3. Avancez avec F10 ; poursuivez avec F5.\n\n"
        "Les deux CPU restent visibles. Mémoire, pile et journal sont regroupés en bas.\n"
        "Dans Périphériques, ouvrez un inspecteur en onglet ; détachez-le si nécessaire.");
    welcomeText->setWordWrap(true);
    welcomeLayout->addWidget(welcomeTitle); welcomeLayout->addWidget(welcomeText);
    auto* welcomeOpen = new QPushButton("Ouvrir un ELF…"); auto* welcomeDemo = new QPushButton("Essayer l'exemple");
    welcomeOpen->setObjectName("welcomeOpenButton"); welcomeDemo->setObjectName("welcomeDemoButton");
    connect(welcomeOpen, &QPushButton::clicked, open, &QAction::trigger);
    connect(welcomeDemo, &QPushButton::clicked, demo, &QAction::trigger);
    welcomeLayout->addWidget(row({welcomeOpen, welcomeDemo})); welcomeLayout->addStretch();
    codePages_->addWidget(welcome); codePages_->addWidget(assemblyPane);
    workspace_->addFixedPage(codePages_, "Code");
    setCentralWidget(vertical(sessionHeader, workspace_));
    auto* cpuPane = new QWidget; auto* cpuLayout = new QVBoxLayout(cpuPane);
    cpuLayout->setContentsMargins(6, 6, 6, 6);
    for (unsigned c = 0; c < 2; ++c) {
        flags_[c] = new QLabel; flags_[c]->setWordWrap(true);
        cpuLayout->addWidget(flags_[c]);
    }
    registers_ = table({"Registre", "Cœur 0", "Cœur 1"}); registers_->setObjectName("registersTable");
    registers_->horizontalHeader()->setStretchLastSection(false);
    cpuLayout->addWidget(registers_, 1);
    auto* cpuDock = dock(this, "CPU · comparaison des cœurs", cpuPane, Qt::LeftDockWidgetArea);
    cpuDock_ = cpuDock;
    memory_ = table({"Adresse", "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F", "ASCII"});
    memory_->setObjectName("memoryTable");
    memoryAddress_ = new QLineEdit("0x20000000"); memoryLength_ = new QSpinBox; memoryLength_->setRange(16, 4096); memoryLength_->setSingleStep(16); memoryLength_->setValue(256);
    auto* showMemory = new QPushButton("Lire");
    connect(showMemory, &QPushButton::clicked, this, [this] { refreshMemory(); });
    connect(memoryAddress_, &QLineEdit::returnPressed, showMemory, &QPushButton::click);
    inspectionTabs_ = new QTabWidget; inspectionTabs_->setObjectName("inspectionTabs"); inspectionTabs_->setDocumentMode(true);
    inspectionTabs_->addTab(vertical(row({new QLabel("Adresse"), memoryAddress_, new QLabel("Octets"), memoryLength_, showMemory}), memory_), "Mémoire");
    inspectionDock_ = dock(this, "Inspection", inspectionTabs_, Qt::BottomDockWidgetArea);
    stack_ = table({"", "Adresse pile", "Mot 32 bits", "Symbole proche / région"});
    stack_->setObjectName("stackTable");
    inspectionTabs_->addTab(stack_, "Pile");
    mapping_ = table({"Début", "Fin incluse", "Région", "Accès", "Couverture"});
    mapping_->setObjectName("mappingTable");
    peripherals_ = new PeripheralPanel;
    peripherals_->setObjectName("peripheralCatalogPage");
    peripherals_->openDevice = [this](const QString& name) { openPeripheral(name); };
    peripherals_->openExplorer = [this] { openPeripheralExplorer(); };
    workspace_->addFixedPage(peripherals_, "Périphériques");
    auto* peripheralWindow = views->addAction("Catalogue des périphériques");
    peripheralWindow->setObjectName("peripheralWindowAction");
    peripheralWindow->setShortcut(QKeySequence("Ctrl+P"));
    connect(peripheralWindow, &QAction::triggered, this, [this] { workspace_->setCurrentWidget(peripherals_); });
    auto* explorer = views->addAction("Fenêtre des périphériques…"); explorer->setObjectName("peripheralExplorerAction");
    explorer->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(explorer, &QAction::triggered, this, [this] { openPeripheralExplorer(); });
    auto* gather = views->addAction("Rassembler les inspecteurs"); gather->setObjectName("gatherInspectorsAction");
    gather->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(gather, &QAction::triggered, workspace_, &InspectionWorkspace::gatherInspections);
    connect(mapping_, &QTableWidget::cellDoubleClicked, this, [this](int line, int) {
        if (!mapping_->item(line, 0)) return;
        memoryAddress_->setText(mapping_->item(line, 0)->text()); refreshMemory();
        inspectionTabs_->setCurrentIndex(0); showInspectionWindow(inspectionDock_);
    });
    watches_ = table({"Expression", "Valeur", "Contexte"});
    watches_->setObjectName("watchesTable");
    auto* addWatch = new QPushButton("+ Expression"); auto* removeWatch = new QPushButton("Supprimer");
    connect(addWatch, &QPushButton::clicked, this, [this] { addExpression(); });
    connect(removeWatch, &QPushButton::clicked, this, [this] { if (watches_->currentRow() >= 0) watches_->removeRow(watches_->currentRow()); refreshWatches(); });
    monitorTabs_ = new QTabWidget; monitorTabs_->setObjectName("monitorTabs"); monitorTabs_->setDocumentMode(true);
    auto* watchTools = vertical(new QLabel("Valeurs suivies, liées à leur cœur"), row({addWatch, removeWatch}));
    monitorTabs_->addTab(vertical(watchTools, watches_), "Expressions");
    monitorDock_ = dock(this, "Surveillance", monitorTabs_, Qt::RightDockWidgetArea);
    // Empty watches and breakpoint lists should not reserve a whole column.
    // Adding an item or using Views reveals it, without recreating its state.
    monitorDock_->hide();
    points_ = table({"Type", "Adresse", "Taille / accès"});
    points_->setObjectName("pointsTable");
    auto* addPoint = new QPushButton("+ Arrêt"); auto* addMemory = new QPushButton("+ Accès"); auto* removePoint = new QPushButton("Supprimer");
    addPoint->setToolTip("Breakpoint : arrêter avant une instruction (F9)");
    addMemory->setToolTip("Watchpoint : arrêter après un accès mémoire CPU");
    connect(addPoint, &QPushButton::clicked, this, [this] { addBreakpoint(); });
    connect(addMemory, &QPushButton::clicked, this, [this] { addMemoryWatch(); });
    connect(removePoint, &QPushButton::clicked, this, [this] {
        const int r = points_->currentRow(); if (r < 0) return;
        const auto* item = points_->item(r, 0); const auto value = item->data(Qt::UserRole).toUInt();
        if (item->text() == "Break") debugger_->breakpoints.erase(value);
        else if (value < debugger_->watchpoints.size()) debugger_->watchpoints.erase(debugger_->watchpoints.begin() + value);
        refreshPoints(); refreshAssembly();
    });
    auto* pointHelp = new QLabel("Instruction (breakpoint) ou accès mémoire (watchpoint)"); pointHelp->setWordWrap(true);
    monitorTabs_->addTab(vertical(vertical(pointHelp, row({addPoint, addMemory, removePoint})), points_), "Arrêts");
    console_ = new QPlainTextEdit; console_->setReadOnly(true); console_->setMaximumBlockCount(2000); console_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    console_->setObjectName("consoleView");
    auto* expression = new QLineEdit; expression->setPlaceholderText("print read32(counter), r0 + 4, *sp… Entrée pour évaluer");
    connect(expression, &QLineEdit::returnPressed, this, [this, expression] { evaluate(expression->text()); expression->clear(); });
    inspectionTabs_->addTab(vertical(expression, console_), "Console");
    inspectionTabs_->addTab(mapping_, "Carte mémoire");
    inspectionTabs_->addTab(source_, "Source C++");
    inspectionTabs_->setTabVisible(4, false);
    inspectionTabs_->setTabToolTip(4, "Emplacement réservé : import source/DWARF non disponible");
    virtualTimePanel_ = new VirtualTimePanel;
    virtualTimePanel_->advanceRequested = [this](uint64_t duration, bool timeOnly) { advanceVirtualTime(duration, timeOnly); };
    virtualTimePanel_->pauseRequested = [this] { pauseAction_->trigger(); };
    inspectionTabs_->addTab(virtualTimePanel_, "Temps virtuel");
    boardPanel_ = new BoardPanel; inspectionTabs_->addTab(boardPanel_, "Carte et câblage");
    // Every secondary view has a stable keyboard/menu route. Selecting a view
    // also reveals its dock, so closing a panel cannot leave a hidden result.
    const auto inspectAction = [this, views](const QString& name, const char* id, const char* shortcut, int index) {
        auto* action = views->addAction(name); action->setObjectName(id);
        if (*shortcut) action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, [this, index] {
            inspectionTabs_->setTabVisible(index, true);
            inspectionTabs_->setCurrentIndex(index); showInspectionWindow(inspectionDock_);
        });
    };
    inspectAction("Mémoire", "memoryViewAction", "Ctrl+3", 0);
    inspectAction("Pile du cœur sélectionné", "stackViewAction", "Ctrl+4", 1);
    inspectAction("Console / print / diagnostics", "consoleViewAction", "Ctrl+5", 2);
    inspectAction("Carte mémoire", "mappingViewAction", "", 3);
    inspectAction("Source C++ — prochaine itération", "sourceViewAction", "", 4);
    inspectAction("Temps virtuel", "virtualTimeViewAction", "Ctrl+6", 5);
    inspectAction("Carte et câblage", "boardViewAction", "Ctrl+7", 6);
    auto* codeView = views->addAction("Code / assembleur"); codeView->setObjectName("codeViewAction");
    codeView->setShortcut(QKeySequence("Ctrl+1"));
    connect(codeView, &QAction::triggered, this, [this] { workspace_->setCurrentWidget(codePages_); });
    auto* catalogView = views->addAction("Périphériques"); catalogView->setShortcut(QKeySequence("Ctrl+2"));
    connect(catalogView, &QAction::triggered, peripheralWindow, &QAction::trigger);
    auto* watchView = views->addAction("Expressions surveillées"); watchView->setObjectName("watchesViewAction");
    connect(watchView, &QAction::triggered, this, [this] { monitorTabs_->setCurrentIndex(0); showInspectionWindow(monitorDock_); });
    auto* pointView = views->addAction("Points d'arrêt / watchpoints"); pointView->setObjectName("pointsViewAction");
    connect(pointView, &QAction::triggered, this, [this] { monitorTabs_->setCurrentIndex(1); showInspectionWindow(monitorDock_); });
    stop_ = new QLabel; stop_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(stop_, 1);
    timeLabel_ = new QLabel; timeLabel_->setObjectName("virtualTimeStatus");
    timeLabel_->setToolTip("Ouvrir les commandes du temps virtuel (Ctrl+6)");
    statusBar()->addPermanentWidget(timeLabel_);
    connect(timeLabel_, &QLabel::linkActivated, this, [this] {
        inspectionTabs_->setCurrentIndex(5); showInspectionWindow(inspectionDock_);
    });
    resizeDocks({cpuDock, monitorDock_}, {330, 300}, Qt::Horizontal);
    resizeDocks({inspectionDock_}, {245}, Qt::Vertical);
    overviewState_ = saveState();
}
void MainWindow::loadFile(const QString& path) {
    setRunning(false);
    try {
        debugger_->load(path.toStdString()); previousValid_ = false;
        imageName_ = QFileInfo(path).fileName();
        QSettings().setValue("lastDirectory", QFileInfo(path).absolutePath());
        setWindowTitle("PicoDebugger — " + QFileInfo(path).fileName());
        log("ELF chargé : " + path); log(q(debugger_->stop.message)); refresh();
    } catch (const std::exception& e) { failure(e); }
}
void MainWindow::selectChip(Chip chip) { chip_->setCurrentIndex(chip == Chip::RP2350 ? 1 : 0); }
void MainWindow::selectBoard(Board board) { chip_->setCurrentIndex(chip_->findData(static_cast<int>(board))); }
void MainWindow::loadDemo() {
    setRunning(false);
    try { debugger_->loadImage(demoImage()); imageName_ = "Exemple intégré"; previousValid_ = false; log("Exemple chargé. Pour le cœur 1 : entrée core1_entry, pile 0x20042000."); refresh(); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::singleStep() {
    setRunning(false);
    try { debugger_->step(selected_); refresh(); if (debugger_->stop.kind != StopKind::Step) log(q(debugger_->stop.message)); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::setRunning(bool running) {
    running_ = running && debugger_->loaded() && !debugger_->terminalStop();
    if (!running_) { remainingTime_ = 0; timeOnly_ = false; }
    if (running_) timer_->start(); else timer_->stop();
    if (stepAction_) {
        stepAction_->setEnabled(!running_ && debugger_->loaded() && !debugger_->terminalStop());
        runAction_->setEnabled(!running_ && debugger_->loaded() && !debugger_->terminalStop());
        pauseAction_->setEnabled(running_); resetAction_->setEnabled(debugger_->loaded());
    }
}
void MainWindow::runBatch() {
    // No worker shares mutable CPU state with widgets. Short batches return to
    // Qt regularly; pause, resizing and inspection remain responsive.
    try {
        QElapsedTimer budget; budget.start();
        do {
            const uint64_t before = debugger_->virtualTime();
            Stop result;
            if (timeOnly_) {
                debugger_->advanceTime(std::min<uint64_t>(remainingTime_, 200));
                result = debugger_->stop;
                result.kind = StopKind::Budget;
            } else result = remainingTime_ ? debugger_->runFor(remainingTime_, 200) : debugger_->run(200);
            if (remainingTime_) {
                remainingTime_ -= debugger_->virtualTime() - before;
                if (!remainingTime_) {
                    setRunning(false); log("Durée virtuelle terminée à t = " + QString::number(debugger_->virtualTime()) + " µs"); break;
                }
            }
            if (result.kind != StopKind::Budget) { setRunning(false); log(q(result.message)); break; }
        } while (budget.elapsed() < 6);
        refresh();
    } catch (const std::exception& e) { failure(e); }
}
void MainWindow::refresh() {
    setRunning(running_);
    const bool loaded = debugger_->loaded();
    virtualTimePanel_->updateState(debugger_->virtualTime(), remainingTime_, loaded && !debugger_->terminalStop(), running_);
    timeLabel_->setText(QString("<a href='time' style='color:#8bd6bb'>t = %1 µs</a>").arg(static_cast<qulonglong>(debugger_->virtualTime())));
    codePages_->setCurrentIndex(loaded ? 1 : 0);
    session_->setText(loaded ? imageName_ : "Aucune image chargée"); session_->setToolTip(session_->text());
    banner_->setText(!loaded ? "HORS LIGNE" : running_ ? (timeOnly_ ? "AVANCE DU TEMPS" : "EN EXÉCUTION") : q(stopName(debugger_->stop.kind)));
    banner_->setStyleSheet(debugger_->terminalStop() ? "color:#ffb591;font-weight:600;" : "color:#8bd6bb;font-weight:600;");
    const QString instructionHint = QString("PC %1 · F10 : une instruction · F5 : continuer").arg(hx(debugger_->cpus[selected_].r[15]));
    guidance_->setText(!loaded ? "Simulation partielle · ARM / Thumb · sans carte"
        : QString("Simulation partielle · ELF direct, sans ROM · cœur sélectionné : %1\n%2")
            .arg(selected_).arg(running_ ? (timeOnly_ ? "CPU figés · périphériques en avance · Maj+F5 pour mettre en pause" : "États échantillonnés · Maj+F5 pour mettre en pause")
                : debugger_->stop.kind == StopKind::Ready || debugger_->stop.kind == StopKind::Step
                    ? instructionHint : q(debugger_->stop.message)));
    inspectionTabs_->setTabToolTip(1, QString("Pile brute du cœur %1 · pas de déroulage des appels C++").arg(selected_));
    refreshRegisters(); refreshAssembly(); refreshMemory(); refreshStack(); refreshWatches(); refreshPoints(); refreshPeripherals();
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    stop_->setText(QString("%1  |  cœur %2  |  %3  |  révision %4  |  %5")
        .arg(running_ ? "EN EXÉCUTION — instantané échantillonné" : q(stopName(debugger_->stop.kind)))
        .arg(debugger_->stop.core).arg(q(debugger_->stop.message)).arg(static_cast<qulonglong>(debugger_->revision())).arg(stamp));
    stop_->setToolTip(stop_->text());
}
void MainWindow::refreshRegisters() {
    registers_->ensurePolished();
    registers_->setRowCount(21);
    for (unsigned c = 0; c < 2; ++c) {
        const Cpu& cpu = debugger_->cpus[c]; auto* widget = registers_;
        const int column = static_cast<int>(c + 1);
        widget->horizontalHeaderItem(column)->setText(QString("Cœur %1%2").arg(c).arg(selected_ == c ? " •" : ""));
        widget->horizontalHeaderItem(column)->setToolTip(selected_ == c ? "Contexte sélectionné pour F10, assembleur et pile" : "Autre cœur ; visible en parallèle");
        for (unsigned r = 0; r < 21; ++r) {
            const uint32_t extras[] = {cpu.xpsr, cpu.msp, cpu.psp, cpu.control, cpu.primask};
            const char* names[] = {"xPSR", "MSP", "PSP", "CONTROL", "PRIMASK"};
            const uint32_t value = r < 16 ? cpu.r[r] : extras[r - 16];
            cell(widget, static_cast<int>(r), 0, r < 16 ? q(registerName(r)) : names[r - 16]);
            cell(widget, static_cast<int>(r), column, debugger_->loaded() ? hx(value) : QString("—"));
            widget->item(static_cast<int>(r), column)->setForeground(previousValid_ && value != previous_[c][r] ? QColor("#ffd780") : QColor("#dce6f0"));
            previous_[c][r] = value;
        }
        flags_[c]->setText(!debugger_->loaded() ? QString("Cœur %1 · pas d'image").arg(c) : QString("Cœur %1 · %2 · %4 instructions\n%3").arg(c)
            .arg(cpu.held ? "RETENU" : cpu.lockup ? "LOCKUP" : cpu.sleeping ? "SOMMEIL" : running_ && !timeOnly_ ? "ACTIF" : "ARRÊTÉ")
            .arg(q(cpu.flags())).arg(static_cast<qulonglong>(cpu.instructions)));
    }
    // Measure the actual styled columns, including header padding, rather than
    // estimating character widths before Qt has applied the desktop's DPI/font.
    registers_->resizeColumnsToContents();
    const int width = registers_->horizontalHeader()->length() + 2 * registers_->frameWidth()
        + style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    registers_->setMinimumWidth(width); cpuDock_->setMinimumWidth(width + 12);
    previousValid_ = true;
}
void MainWindow::refreshAssembly() {
    assembly_->setRowCount(0);
    if (!debugger_->loaded()) return;
    if (followPc_->isChecked() && debugger_->cpus[selected_].held) {
        assembly_->setRowCount(1);
        cell(assembly_, 0, 3, "Cœur retenu — utiliser Débogage → Démarrer un cœur explicitement…");
        return;
    }
    try {
        uint32_t at = followPc_->isChecked() ? debugger_->cpus[selected_].r[15] : debugger_->evaluate(assemblyAddress_->text().toStdString(), selected_) & ~1u;
        for (int r = 0; r < 32; ++r) {
            assembly_->setRowCount(r + 1);
            cell(assembly_, r, 0, QString(at == debugger_->cpus[selected_].r[15] ? "▶ " : "") + (debugger_->breakpoints.count(at) ? "●" : ""));
            cell(assembly_, r, 1, hx(at)); assembly_->item(r, 1)->setData(Qt::UserRole, at);
            try {
                const auto instruction = decode(debugger_->memory, at, selected_);
                cell(assembly_, r, 2, q(hex(instruction.raw, instruction.size * 2)));
                cell(assembly_, r, 3, q(instruction.text)); cell(assembly_, r, 4, q(debugger_->sourceHint(at)));
                if (at == debugger_->cpus[selected_].r[15]) for (int col = 0; col < 5; ++col) assembly_->item(r, col)->setBackground(QColor("#234d57"));
                if (uint64_t(at) + instruction.size > 0xffffffffull) break;
                at += instruction.size;
            } catch (const Error& e) { cell(assembly_, r, 3, q(e.what())); break; }
        }
    } catch (const Error& e) { assembly_->setRowCount(1); cell(assembly_, 0, 3, q(e.what())); }
}
void MainWindow::refreshMemory() {
    memory_->setRowCount(0);
    if (!debugger_->loaded()) return;
    try {
        const uint32_t start = debugger_->evaluate(memoryAddress_->text().toStdString(), selected_);
        const unsigned count = static_cast<unsigned>(memoryLength_->value());
        if (uint64_t(start) + count > 0x100000000ull) throw Error(ErrorKind::InvalidArgument, "Plage débordante");
        for (unsigned i = 0; i < count; i += 16) {
            const int r = static_cast<int>(i / 16); memory_->setRowCount(r + 1);
            QString bytes, ascii, error;
            for (unsigned j = 0; j < 16 && i + j < count; ++j) {
                try {
                    const auto value = debugger_->memory.peek(start + i + j, 1, selected_);
                    bytes += q(hex(value, 2).substr(2)) + " "; ascii += value >= 32 && value < 127 ? QChar(static_cast<char>(value)) : QChar('.');
                } catch (const Error& e) { bytes += "?? "; ascii += '?'; error = q(e.what()); }
            }
            cell(memory_, r, 0, hx(start + i)); cell(memory_, r, 1, bytes, error); cell(memory_, r, 2, ascii);
        }
    } catch (const Error& e) { memory_->setRowCount(1); cell(memory_, 0, 1, q(e.what())); }
}
void MainWindow::refreshStack() {
    stack_->setRowCount(0); if (!debugger_->loaded()) return;
    if (debugger_->cpus[selected_].held && !debugger_->cpus[selected_].r[13]) {
        stack_->setRowCount(1); cell(stack_, 0, 2, "Pile non initialisée · cœur retenu"); return;
    }
    for (int r = 0; r < 32; ++r) {
        const uint64_t wide = uint64_t(debugger_->cpus[selected_].r[13]) + static_cast<unsigned>(r) * 4u;
        if (wide > 0xffffffffull) break;
        const uint32_t at = static_cast<uint32_t>(wide); stack_->setRowCount(r + 1);
        cell(stack_, r, 0, r == 0 ? "SP ▶" : ""); cell(stack_, r, 1, hx(at));
        try {
            const uint32_t value = debugger_->memory.peek(at, 4, selected_); cell(stack_, r, 2, hx(value));
            const auto* region = regionAt(debugger_->memory.description(), value);
            cell(stack_, r, 3, region ? q(region->name) : "");
        } catch (const Error& e) { cell(stack_, r, 2, "??", q(e.what())); cell(stack_, r, 3, q(e.what())); break; }
    }
}
void MainWindow::fillMapping() {
    boardPanel_->setBoard(debugger_->board());
    mapping_->setRowCount(0);
    for (const auto& region : debugger_->memory.description().regions) {
        const int r = mapping_->rowCount(); mapping_->setRowCount(r + 1);
        cell(mapping_, r, 0, hx(region.begin)); cell(mapping_, r, 1, hx(static_cast<uint32_t>(region.end - 1)));
        cell(mapping_, r, 2, q(region.name)); cell(mapping_, r, 3, q(region.permissions)); cell(mapping_, r, 4, q(region.coverage));
    }
}
void MainWindow::refreshWatches() {
    monitorTabs_->setTabText(0, watches_->rowCount() ? QString("Expressions (%1)").arg(watches_->rowCount()) : "Expressions");
    for (int r = 0; r < watches_->rowCount(); ++r) {
        const auto* item = watches_->item(r, 0); if (!item) continue;
        const unsigned core = item->data(Qt::UserRole).toUInt();
        try { cell(watches_, r, 1, hx(debugger_->evaluate(item->text().toStdString(), core))); }
        catch (const Error& e) { cell(watches_, r, 1, "Indisponible", q(e.what())); }
        cell(watches_, r, 2, "Cœur " + QString::number(core));
    }
}
void MainWindow::refreshPeripherals() {
    peripherals_->updateTarget(*debugger_, selected_);
    if (explorer_) explorer_->updateTarget(*debugger_);
    for (auto it = inspections_.begin(); it != inspections_.end();) {
        if (!*it) { it = inspections_.erase(it); continue; }
        (*it)->updateTarget(*debugger_);
        ++it;
    }
}
void MainWindow::closeInspections() {
    if (explorer_) { explorer_->close(); explorer_ = nullptr; }
    workspace_->clearInspections();
    inspections_.clear();
}
void MainWindow::openPeripheralExplorer() {
    if (!explorer_) {
        explorer_ = new PeripheralExplorer(*debugger_, selected_, this);
        explorer_->setPalette(palette());
        explorer_->refreshRequested = [this] { refreshPeripherals(); };
        explorer_->injectUart = [this](unsigned uart, const std::string& bytes) {
            setRunning(false); debugger_->pause();
            const auto accepted = debugger_->injectUart(uart, bytes); refresh(); return accepted;
        };
        connect(explorer_, &QDialog::finished, this, [this] { explorer_ = nullptr; });
    }
    showInspectionWindow(explorer_);
}
void MainWindow::advanceVirtualTime(uint64_t duration, bool timeOnly) {
    setRunning(false);
    if (!debugger_->loaded() || debugger_->terminalStop()) return;
    remainingTime_ = duration; timeOnly_ = timeOnly;
    setRunning(true); refresh();
}
void MainWindow::openPeripheral(const QString& name, bool duplicate, int context) {
    if (name.isEmpty() || (!duplicate && workspace_->revealInspection(name))) return;
    const QString suffix = duplicate ? QString::number(++inspectionSequence_) : QString();
    const QString key = duplicate ? name + "#" + suffix : name;
    auto* inspection = new PeripheralInspection(name, context < 0 ? selected_ : static_cast<unsigned>(context));
    inspection->setObjectName("deviceInspection_" + key);
    inspection->refreshRequested = [this] { refreshPeripherals(); };
    inspection->detachRequested = [this, key] { workspace_->toggleDetached(key); };
    inspection->duplicateRequested = [this, name, inspection] { openPeripheral(name, true, static_cast<int>(inspection->core())); };
    inspection->injectUart = [this](unsigned uart, const std::string& bytes) {
        setRunning(false);
        const auto accepted = debugger_->injectUart(uart, bytes); refresh(); return accepted;
    };
    inspections_.push_back(inspection);
    workspace_->addInspection(key, duplicate ? name + " · " + suffix : name, inspection,
        [inspection](bool detached) { inspection->setDetached(detached); });
    refreshPeripherals();
}
void MainWindow::refreshPoints() {
    const auto count = debugger_->breakpoints.size() + debugger_->watchpoints.size();
    monitorTabs_->setTabText(1, count ? QString("Arrêts (%1)").arg(static_cast<qulonglong>(count)) : "Arrêts");
    points_->setRowCount(0);
    for (const auto at : debugger_->breakpoints) {
        const int r = points_->rowCount(); points_->setRowCount(r + 1);
        cell(points_, r, 0, "Break"); points_->item(r, 0)->setData(Qt::UserRole, at); cell(points_, r, 1, hx(at)); cell(points_, r, 2, "Avant instruction");
    }
    for (size_t i = 0; i < debugger_->watchpoints.size(); ++i) {
        const auto& w = debugger_->watchpoints[i]; const int r = points_->rowCount(); points_->setRowCount(r + 1);
        cell(points_, r, 0, "Watch"); points_->item(r, 0)->setData(Qt::UserRole, static_cast<unsigned>(i)); cell(points_, r, 1, hx(w.address));
        cell(points_, r, 2, QString::number(w.length) + " octets / " + (w.read ? "R" : "") + (w.write ? "W" : ""));
    }
}
void MainWindow::addBreakpoint() {
    setRunning(false); bool ok = false;
    const auto text = QInputDialog::getText(this, "Point d'arrêt", "Adresse, registre ou symbole", QLineEdit::Normal, "pc", &ok);
    if (!ok || text.isEmpty()) return;
    try { debugger_->breakpoints.insert(debugger_->evaluate(text.toStdString(), selected_) & ~1u); refresh(); monitorTabs_->setCurrentIndex(1); showInspectionWindow(monitorDock_); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::addMemoryWatch() {
    setRunning(false);
    QDialog dialog(this); dialog.setWindowTitle("Watchpoint mémoire"); QFormLayout layout(&dialog);
    QLineEdit address("0x20000000"); QSpinBox length; length.setRange(1, 65536); length.setValue(4);
    ChoiceBox mode; mode.addItems({"Écriture", "Lecture", "Lecture et écriture"}); QPushButton add("Ajouter");
    layout.addRow("Adresse / symbole", &address); layout.addRow("Octets", &length); layout.addRow("Accès", &mode); layout.addRow(&add);
    connect(&add, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;
    try { debugger_->addWatchpoint(debugger_->evaluate(address.text().toStdString(), selected_), static_cast<uint32_t>(length.value()), mode.currentIndex() != 0, mode.currentIndex() != 1); refreshPoints(); monitorTabs_->setCurrentIndex(1); showInspectionWindow(monitorDock_); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::addExpression() {
    bool ok = false; const auto text = QInputDialog::getText(this, "Expression surveillée", "Expression (sans effet de bord), liée au cœur sélectionné", QLineEdit::Normal, "read32(0x20000000)", &ok);
    if (!ok || text.isEmpty()) return;
    const int r = watches_->rowCount(); watches_->setRowCount(r + 1); cell(watches_, r, 0, text); watches_->item(r, 0)->setData(Qt::UserRole, selected_); refreshWatches();
    monitorTabs_->setCurrentIndex(0); showInspectionWindow(monitorDock_);
}
void MainWindow::launchCore() {
    setRunning(false);
    QDialog dialog(this); dialog.setWindowTitle("Démarrage explicite — sans Boot ROM"); QFormLayout layout(&dialog);
    ChoiceBox core; core.addItems({"Cœur 0", "Cœur 1"}); core.setCurrentIndex(1);
    QLineEdit entry("core1_entry"), sp("0x20042000"); QPushButton launch("Démarrer à l'arrêt");
    layout.addRow("Cœur", &core); layout.addRow("Entrée Thumb (bit 0 = 1)", &entry); layout.addRow("SP initial", &sp); layout.addRow(&launch);
    connect(&launch, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;
    try { debugger_->startCore(static_cast<unsigned>(core.currentIndex()), debugger_->evaluate(entry.text().toStdString()), debugger_->evaluate(sp.text().toStdString())); refresh(); log(q(debugger_->stop.message)); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::evaluate(const QString& expression) {
    auto text = expression.trimmed(); if (text.startsWith("print ")) text = text.mid(6); else if (text.startsWith("p ")) text = text.mid(2);
    try { const uint32_t value = debugger_->evaluate(text.toStdString(), selected_); log(QString("[cœur %1] %2 = %3 (%4)").arg(selected_).arg(text).arg(hx(value)).arg(value)); }
    catch (const std::exception& e) { log("Erreur d'expression : " + q(e.what())); }
}
void MainWindow::failure(const std::exception& error) {
    setRunning(false); log("Erreur : " + q(error.what())); refresh();
    QMessageBox::warning(this, "PicoDebugger", q(error.what()));
}
void MainWindow::log(const QString& message) { console_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message); }
