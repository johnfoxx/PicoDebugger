#include "main_window.hpp"
#include "peripheral_panel.hpp"
#include "debug_access.hpp"
#include "debug_value.hpp"
#include "window_policy.hpp"
#include "window_theme.hpp"
#include "choice_box.hpp"
#include "inspection_workspace.hpp"
#include "peripheral_inspection.hpp"
#include "peripheral_explorer.hpp"
#include "virtual_time_panel.hpp"
#include "value_watches.hpp"
#include "memory_table.hpp"
#include "board_panel.hpp"
#include "probe_dialog.hpp"
#include "probe_serial_monitor.hpp"
#include "ide/ide_window.hpp"
#include <QActionGroup>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShowEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

using namespace picodbg;
namespace {
constexpr int layoutVersion = 3;
constexpr int defaultMonitorWidth = 144;
constexpr int monitorLayoutVersion = 2;
const char* layoutSetting = "debugger/panelLayout";
QString q(const std::string& text) { return QString::fromStdString(text); }
QString hx(uint32_t value) { return q(hex(value)); }
uint32_t parseWriteValue(const QString& text, unsigned size) {
    static const QRegularExpression literal("^(?:[0-9]+|0[xX][0-9a-fA-F]+)$");
    const auto input = text.trimmed();
    bool ok = false;
    const bool hex = input.startsWith("0x", Qt::CaseInsensitive);
    const auto value = (hex ? input.mid(2) : input).toULongLong(&ok, hex ? 16 : 10);
    if (!ok || !literal.match(input).hasMatch() || value >= (uint64_t(1) << (8 * size)))
        throw Error(ErrorKind::InvalidArgument, "Entrez un entier non signé qui tient sur " + std::to_string(8 * size) + " bits");
    return static_cast<uint32_t>(value);
}
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
QWidget* scrollTool(QWidget* page) {
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    return scroll;
}
void revealDock(QDockWidget* panel) {
    showInspectionWindow(panel);
    if (!panel->isFloating()) panel->raise();
}
QDockWidget* dock(QMainWindow* parent, const QString& name, const char* id, QWidget* content) {
    auto* panel = new QDockWidget(name, parent); panel->setObjectName(id); panel->setWidget(content);
    panel->setStyleSheet("QDockWidget::title { padding:4px 7px; }");
    panel->setToolTip("Glisser le titre pour déplacer le panneau. Vues → Fenêtres flottantes pour le détacher.");
    prepareInspectionWindow(panel);
    // Qt5 calls activateWindow() internally at the end of a floating dock drag.
    // Dedicated windows remain available through buttons, without that path.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (waylandDesktop()) panel->setFeatures(panel->features() & ~QDockWidget::DockWidgetFloatable);
#endif
    parent->menuBar()->findChild<QMenu*>("debuggerPanelsMenu")->addAction(panel->toggleViewAction());
    auto* floating = parent->menuBar()->findChild<QMenu*>("debuggerFloatingMenu")->addAction(name);
    floating->setObjectName(QString("float_") + id);
    floating->setCheckable(true);
    QObject::connect(floating, &QAction::triggered, panel, [panel](bool checked) {
        panel->setFloating(checked);
        revealDock(panel);
    });
    QObject::connect(panel, &QDockWidget::topLevelChanged, floating, &QAction::setChecked);
    return panel;
}
}
static void loadDebuggerResources() {
    Q_INIT_RESOURCE(debugger_resources);
}

MainWindow::MainWindow() : debugger_(new Debugger), sourceMap_(this), stackInfo_(this), timer_(new QTimer(this)) {
    setWindowTitle("PicoDebugger — ARM / Thumb"); resize(1440, 900);
    applyPicoTheme(*this);
    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    buildUi();
    const QSettings settings;
    compactMonitor_ = settings.value("debugger/monitorLayoutVersion", 0).toInt() == monitorLayoutVersion;
    const auto saved = settings.value(layoutSetting).toByteArray();
    defaultLayout_ = saved.isEmpty() || !restoreState(saved, layoutVersion);
    if (!defaultLayout_)
        assemblyDock_->setVisible(QSettings().value("debugger/standaloneAssembly", true).toBool());
    else if (!saved.isEmpty()) restoreState(overviewState_, layoutVersion);
    sourceMap_.updated = [this] { refreshSource(true); refreshAssembly(); };
    stackInfo_.updated = [this] { refreshStack(); };
    connect(timer_, &QTimer::timeout, this, [this] { runBatch(); });
    timer_->setInterval(20);
    auto* hardwareTimer = new QTimer(this); hardwareTimer->setInterval(100);
    connect(hardwareTimer, &QTimer::timeout, this, [this] { hardwareTick(); }); hardwareTimer->start();
    fillMapping(); refresh();
    log("Prêt. Ouvrir un ELF ARM ou l'exemple. Exécution arrêtée par défaut.");
    log("Simulation partielle : les accès non émulés arrêtent la session. Aucune garantie de compatibilité matérielle.");
}
MainWindow::~MainWindow() {
    saveLayout();
    ide_.clear();
    ownedIde_.reset();
}

void MainWindow::buildUi() {
    auto* file = menuBar()->addMenu("Fichier");
    auto* ideAction = file->addAction("Ouvrir l'IDE C");
    ideAction->setObjectName("openIdeAction");
    ideAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(ideAction, &QAction::triggered, this, [this] { showIde(); });
    auto* views = menuBar()->addMenu("Vues"); views->setObjectName("views");
    addPicoThemeMenu(*views, *this, [this] {
        refreshMappingAccess();
        refresh();
        refreshSource(true);
    });
    auto* overview = views->addAction("Rétablir la disposition initiale");
    overview->setObjectName("overviewAction");
    overview->setShortcut(QKeySequence("Ctrl+Shift+0"));
    connect(overview, &QAction::triggered, this, [this] {
        workspace_->gatherInspections();
        restoreState(overviewState_, layoutVersion);
        sizeOverview();
        workspace_->setCurrentWidget(peripherals_);
        inspectionTabs_->setCurrentIndex(0);
        monitorTabs_->setCurrentWidget(valueWatches_);
        if (assemblySuppressed_) { revealAssemblyAfterIde_ = true; assemblyDock_->hide(); }
    });
    views->addMenu("Panneaux")->setObjectName("debuggerPanelsMenu");
    views->addMenu("Fenêtres flottantes")->setObjectName("debuggerFloatingMenu");
    views->addSeparator();
    auto* peripheralViews = views->addMenu("Périphériques");
    auto* monitorViews = views->addMenu("Surveillance");
    auto* toolViews = views->addMenu("Outils");
    auto* focusViews = views->addMenu("Mettre en avant");
    const std::pair<const char*, const char*> layouts[] = {
        {"memory", "Mémoire"}, {"peripherals", "Périphériques"}, {"assembly", "Assembleur"},
        {"split", "Mémoire et périphériques"}, {"all", "Toutes les vues"}
    };
    for (const auto& layout : layouts) {
        const QString id = QString::fromLatin1(layout.first);
        auto* action = focusViews->addAction(QString::fromUtf8(layout.second));
        action->setObjectName(id == "assembly" ? QString("focusAssemblyAction") : "focusView_" + id);
        connect(action, &QAction::triggered, this, [this, id] { arrangeMainViews(id); });
    }
    auto* debug = menuBar()->addMenu("Débogage");
    auto* target = menuBar()->addMenu("Cible");
    auto* modes = new QActionGroup(this);
    emulationAction_ = target->addAction("Émulation"); emulationAction_->setObjectName("emulationModeAction");
    hardwareAction_ = target->addAction("Carte réelle · SWD"); hardwareAction_->setObjectName("hardwareModeAction");
    for (auto* action : {emulationAction_, hardwareAction_}) { action->setCheckable(true); modes->addAction(action); }
    emulationAction_->setChecked(true);
    connect(emulationAction_, &QAction::triggered, this, [this] { selectHardware(false); });
    connect(hardwareAction_, &QAction::triggered, this, [this] { selectHardware(true); });
    target->addSeparator();
    connectAction_ = target->addAction("Connecter la sonde…"); connectAction_->setObjectName("connectProbeAction");
    connect(connectAction_, &QAction::triggered, this, [this] { configureHardware(); });
    disconnectAction_ = target->addAction("Déconnecter la sonde"); disconnectAction_->setObjectName("disconnectProbeAction");
    connect(disconnectAction_, &QAction::triggered, this, [this] {
        if (auto* hw = dynamic_cast<GdbTarget*>(debugger_.get())) try { hw->disconnectProbe(); refresh(); } catch (const std::exception& e) { failure(e); }
    });
    auto* serialAction = target->addAction("Moniteur série de la sonde…");
    serialAction->setObjectName("openProbeSerialAction");
    serialAction->setShortcut(QKeySequence("Ctrl+Shift+U"));
    connect(serialAction, &QAction::triggered, this, &MainWindow::showProbeSerial);
    downloadAction_ = target->addAction("Téléverser l'ELF sur la carte…"); downloadAction_->setObjectName("downloadElfAction");
    connect(downloadAction_, &QAction::triggered, this, [this] { downloadHardware(); });
    refreshTargetAction_ = target->addAction("Relire l'état matériel"); refreshTargetAction_->setObjectName("refreshHardwareAction");
    connect(refreshTargetAction_, &QAction::triggered, this, [this] {
        if (auto* hw = dynamic_cast<GdbTarget*>(debugger_.get())) try { hw->refreshHardware(); refresh(); } catch (const std::exception& e) { failure(e); }
    });
    auto* help = menuBar()->addMenu("Aide");
    auto* manual = help->addAction("Manuel utilisateur");
    manual->setObjectName("userManualAction");
    manual->setShortcut(QKeySequence(Qt::Key_F1));
    connect(manual, &QAction::triggered, this, &MainWindow::showManual);
    auto* toolbar = addToolBar("Exécution"); toolbar->setObjectName("execution"); toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toolbar->setIconSize(QSize(16, 16));
    auto* open = file->addAction("Ouvrir un ELF…"); open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        setRunning(false);
        const auto path = QFileDialog::getOpenFileName(this, "Exécutable ELF32 ARM", QSettings().value("lastDirectory").toString(), "ELF (*.elf *.axf);;Tous les fichiers (*)");
        if (!path.isEmpty()) loadFile(path);
    });
    openAction_ = open;
    open->setObjectName("openElfAction");
    auto* demo = file->addAction("Exemple intégré"); connect(demo, &QAction::triggered, this, [this] { loadDemo(); });
    demoAction_ = demo;
    demo->setObjectName("demoAction");
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
        const Board board = static_cast<Board>(chip_->itemData(index).toInt());
        if (auto* hw = dynamic_cast<GdbTarget*>(debugger_.get())) {
            if (hw->connected() || hw->connecting()) { const QSignalBlocker blocked(chip_); chip_->setCurrentIndex(chip_->findData(static_cast<int>(debugger_->board().board))); return; }
            closeInspections(); debugger_.reset(new GdbTarget(board));
        } else { setRunning(false); closeInspections(); debugger_.reset(new Debugger(board)); }
        imagePath_.clear(); imageName_.clear(); previousValid_ = false; lastHardwareSymbols_ = 0;
        indexSource({}); fillMapping(); refresh(); log("Carte sélectionnée : charger un ELF pour cette carte.");
    });
    toolbar->addSeparator();
    core_ = new ChoiceBox;
    core_->addItems({"Cœur 0", "Cœur 1", "Tous"});
    core_->setCurrentIndex(2);
    core_->setToolTip("Tous compare les deux cœurs. Le point indique le dernier cœur actif pour F10, la mémoire et les expressions.");
    toolbar->addWidget(core_);
    core_->setObjectName("coreSelector");
    connect(core_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        allCores_ = index == 2;
        if (!allCores_) selected_ = static_cast<unsigned>(index);
        refresh();
        if (!cpuDock_->isFloating() && !memoryDock_->isFloating() && !monitorDock_->isFloating())
            resizeDocks({cpuDock_, memoryDock_},
                        {allCores_ ? 490 : 397, std::max(440, width() - monitorDock_->width() - (allCores_ ? 500 : 407))},
                        Qt::Horizontal);
    });
    stepAction_ = debug->addAction("Pas instruction"); stepAction_->setShortcut(QKeySequence(Qt::Key_F10));
    stepAction_->setObjectName("stepAction");
    connect(stepAction_, &QAction::triggered, this, [this] { singleStep(); });
    runAction_ = debug->addAction("Continuer l'exécution"); runAction_->setShortcut(QKeySequence(Qt::Key_F5));
    runAction_->setObjectName("runAction");
    connect(runAction_, &QAction::triggered, this, [this] {
        try { if (debugger_->hardware()) debugger_->run(0); else { setRunning(false); setRunning(true); } refresh(); }
        catch (const std::exception& e) { failure(e); }
    });
    pauseAction_ = debug->addAction("Pause"); pauseAction_->setShortcut(QKeySequence("Shift+F5"));
    pauseAction_->setObjectName("pauseAction");
    connect(pauseAction_, &QAction::triggered, this, [this] {
        try { setRunning(false); debugger_->pause(); refresh(); } catch (const std::exception& e) { failure(e); }
    });
    resetAction_ = debug->addAction("Reset / restart"); resetAction_->setShortcut(QKeySequence("Ctrl+R"));
    resetAction_->setObjectName("resetAction");
    connect(resetAction_, &QAction::triggered, this, [this] {
        setRunning(false);
        try {
            debugger_->reset();
            for (auto& samples : stackValueChanges_) samples.clear();
            previousValid_ = false;
            refresh();
            log(q(debugger_->stop.message));
        } catch (const std::exception& e) { failure(e); }
    });
    stepAction_->setIcon(style()->standardIcon(QStyle::SP_ArrowRight)); stepAction_->setIconText("Pas · F10");
    runAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay)); runAction_->setIconText("Continuer · F5");
    pauseAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    resetAction_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload)); resetAction_->setIconText("Reset");
    stepAction_->setToolTip("Exécuter une instruction du cœur sélectionné (F10)");
    runAction_->setToolTip("Exécuter les deux cœurs démarrés jusqu'au prochain arrêt (F5)");
    resetAction_->setToolTip("Recharger l'image et remettre le modèle à son état initial (Ctrl+R)");
    toolbar->addAction(runAction_); toolbar->addAction(stepAction_); toolbar->addAction(pauseAction_);
    toolbar->addSeparator(); toolbar->addAction(resetAction_);
    consoleToggle_ = toolbar->addAction("Console");
    consoleToggle_->setObjectName("consoleToggleAction");
    consoleToggle_->setCheckable(true);
    consoleToggle_->setToolTip("Déplier ou replier les outils sous Mémoire et Périphériques");
    connect(consoleToggle_, &QAction::triggered, this, [this](bool visible) {
        if (visible) revealConsole();
        else toggleTools(false);
    });
    auto* launch = debug->addAction("Démarrer un cœur explicitement…"); connect(launch, &QAction::triggered, this, [this] { launchCore(); });
    auto* bp = debug->addAction("Ajouter un point d'arrêt…"); bp->setShortcut(QKeySequence(Qt::Key_F9)); connect(bp, &QAction::triggered, this, [this] { addBreakpoint(); });
    auto* wp = debug->addAction("Ajouter un watchpoint mémoire…"); connect(wp, &QAction::triggered, this, [this] { addMemoryWatch(); });
    auto* expressionAction = debug->addAction("Ajouter une expression surveillée…");
    expressionAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(expressionAction, &QAction::triggered, this, [this] { addExpression(); });
    connect(help->addAction("Couverture et limites"), &QAction::triggered, this, [this] {
        if (debugger_->hardware()) {
            QMessageBox::information(this, "Débogage matériel",
                "Connexion SWD via Raspberry Pi Debug Probe, OpenOCD et GDB ARM.\n"
                "Le firmware s'exécute sur la carte. F5 : exécuter ; F10 : pas instruction ; Maj+F5 : pause.\n"
                "Ouvrir un ELF charge ses symboles ; Cible → Téléverser écrit la mémoire puis laisse la carte arrêtée.\n\n"
                "Registres CPU, mémoire, pile et périphériques sont lus sur la cible à l'arrêt. Les lectures sensibles sont explicites.\n"
                "Les ressources de points d'arrêt et les permissions de debug dépendent du matériel. Le catalogue de registres reste partiel.\n"
                "Le port U de la Debug Probe permet de lire les traces via Cible → Moniteur série de la sonde…\n"
                "Le temps virtuel est réservé à la simulation. Les composants externes tels que CYW43 ne sont pas directement inspectables.\n"
                "ARM uniquement. Les lignes C sont affichées si l'ELF contient des informations DWARF. Voir docs/HARDWARE_DEBUG.md.");
            return;
        }
        QMessageBox::information(this, "Couverture de cette version",
            "Exécution locale sans carte, ARM/Thumb. Modèle fonctionnel partiel.\n\n"
            "RP2040 : instructions Thumb usuelles, BL, MRS/MSR, pile, HardFault/SVC, SysTick simplifié, timer/alarmes en temps virtuel, SRAM, Flash, FIFO et spinlocks.\n"
            "RP2350 / Cortex-M33 : moteur Thumb commun au RP2040, plus MOVW/MOVT et CBZ/CBNZ. Thumb-2 général et IT, division, DSP, FPU, accès exclusifs, MPU et TrustZone absents.\n"
            "Les registres NVIC/SysTick RP2350 sont consultables, mais toute entrée d'exception/IRQ M33 arrête le moteur : les handlers ne sont pas exécutables.\n"
            "Périphériques partiels des deux profils : timers, UART, SPI loopback, PWM, latches GPIO, resets, NVIC/SysTick et scratch watchdog.\n\n"
            "Pas de Boot ROM, SDK complet, DMA, PIO, USB, horloges ni timing cycle par cycle. Un accès hors couverture arrête l'exécution.\n"
            "Le cœur 1 se lance explicitement ; le protocole de lancement ROM n'est pas émulé.\n"
            "Le reset recharge l'ELF et remet la RAM à zéro : ce n'est pas le reset matériel du SoC.\n\n"
            "La pile montre les mots mémoire à partir de SP. Les symboles ELF et les lignes DWARF servent à naviguer dans l’assembleur et l’IDE.\n"
            "Voir README.md et docs/COVERAGE.md dans le projet.");
    });
    session_ = new QLabel; session_->setObjectName("sessionTitle");
    session_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    banner_ = new QLabel; banner_->setObjectName("sessionState");
    guidance_ = new QLabel; guidance_->setObjectName("guidance");
    guidance_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    addToolBarBreak();
    auto* sessionBar = addToolBar("Session");
    sessionBar->setObjectName("sessionBar");
    sessionBar->setMovable(false);
    auto* sessionHeader = new QWidget;
    auto* sessionLayout = new QHBoxLayout(sessionHeader);
    sessionLayout->setContentsMargins(6, 0, 6, 0);
    sessionLayout->addWidget(banner_);
    sessionLayout->addWidget(session_);
    sessionLayout->addWidget(guidance_, 1);
    session_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    session_->setMaximumWidth(440);
    sessionHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sessionBar->addWidget(sessionHeader);
    workspace_ = new InspectionWorkspace;
    workspace_->setStyleSheet("QTabBar::tab { padding:4px 10px; }");
    assembly_ = table({"", "Adresse", "Opcode", "Instruction", "Symbole ELF", "Source C"});
    assembly_->setObjectName("assemblyTable");
    auto assemblyFont = assembly_->font();
    assemblyFont.setPixelSize(14);
    assembly_->setFont(assemblyFont);
    assembly_->verticalHeader()->setMinimumSectionSize(17);
    assembly_->verticalHeader()->setDefaultSectionSize(18);
    assembly_->horizontalHeaderItem(5)->setToolTip("Cliquer sur une ligne source pour la révéler dans l’IDE relié");
    for (int column : {2, 4, 5}) assembly_->setColumnHidden(column, true);
    assembly_->setToolTip("Double-clic sur une instruction pour basculer son point d’arrêt. Clic droit pour choisir les colonnes.");
    assemblyAddress_ = new QLineEdit("pc"); assemblyAddress_->setPlaceholderText("Adresse ou symbole");
    assemblyAddress_->setToolTip("Adresse, symbole ELF ou expression. Entrée pour afficher le code à cette adresse.");
    followPc_ = new QCheckBox("PC"); followPc_->setObjectName("followAssemblyPc"); followPc_->setChecked(true);
    followPc_->setToolTip("Centrer le désassemblage sur le compteur ordinal du cœur choisi");
    auto* disassemble = new QPushButton("Aller");
    disassemble->setToolTip("Afficher le désassemblage à l’adresse saisie");
    connect(disassemble, &QPushButton::clicked, this, [this] { followPc_->setChecked(false); refreshAssembly(); });
    connect(assemblyAddress_, &QLineEdit::returnPressed, disassemble, &QPushButton::click);
    connect(followPc_, &QCheckBox::toggled, this, [this] {
        refreshSource(true); refreshAssembly();
    });
    connect(assembly_, &QTableWidget::cellClicked, this, [this](int line, int column) {
        if (column != 5 || !assembly_->item(line, 1)) return;
        const auto location = sourceMap_.location(assembly_->item(line, 1)->data(Qt::UserRole).toUInt());
        if (!location.valid()) return;
        selectedSource_ = location;
        if (ide_) showInspectionWindow(ide_);
        refreshSource(true, true);
        refreshAssembly();
    });
    connect(assembly_, &QTableWidget::cellDoubleClicked, this, [this](int line, int column) {
        if (column == 5) return;
        const auto* item = assembly_->item(line, 1); if (!item) return;
        const uint32_t address = item->data(Qt::UserRole).toUInt();
        if (!address) return;
        try { if (debugger_->breakpoints.count(address)) debugger_->removeBreakpoint(address); else debugger_->addBreakpoint(address); }
        catch (const std::exception& e) { failure(e); return; }
        refreshAssembly(); refreshPoints();
        monitorTabs_->setCurrentIndex(1); revealDock(monitorDock_);
    });
    assembly_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(assembly_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(this);
        auto* columns = menu.addMenu("Colonnes");
        for (int column : {2, 4, 5}) {
            auto* action = columns->addAction(assembly_->horizontalHeaderItem(column)->text());
            action->setCheckable(true);
            action->setChecked(!assembly_->isColumnHidden(column));
            connect(action, &QAction::toggled, assembly_, [this, column](bool shown) { assembly_->setColumnHidden(column, !shown); });
        }
        menu.exec(assembly_->viewport()->mapToGlobal(position));
    });
    auto* assemblyPane = vertical(row({assemblyAddress_, disassemble, followPc_}), assembly_);
    assemblyPane->setStyleSheet("QLineEdit,QPushButton { padding-top:2px; padding-bottom:2px; }"
                                 "QHeaderView::section { padding-top:2px; padding-bottom:2px; }");
    assembly_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    assembly_->setMinimumHeight(68);
    auto* cpuPane = new QWidget; auto* cpuLayout = new QVBoxLayout(cpuPane);
    cpuLayout->setContentsMargins(6, 6, 6, 6);
    for (auto*& flags : flags_) {
        flags = new QLabel;
        flags->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    cpuLayout->addWidget(row({flags_[0], flags_[1]}));
    registers_ = table({"Registre", "Cœur 0", "Cœur 1", "Signification"}); registers_->setObjectName("registersTable");
    registers_->horizontalHeader()->setStretchLastSection(false);
    registers_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    registers_->setSelectionBehavior(QAbstractItemView::SelectItems);
    auto compact = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    compact.setPixelSize(13);
    registers_->setFont(compact);
    registers_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    registers_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    registers_->setToolTip("Double-cliquer sur une valeur pour modifier le registre du cœur correspondant");
    connect(registers_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editCpuRegister);
    registers_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(registers_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const auto index = registers_->indexAt(point);
        QMenu menu(registers_);
        auto* edit = menu.addAction("Écrire le registre…");
        edit->setEnabled(index.isValid() && index.column() > 0 && index.column() < 3 && !running_ &&
            debugger_->registerAvailable(static_cast<unsigned>(index.column() - 1), static_cast<unsigned>(index.row())));
        auto* extras = menu.addAction("Registres supplémentaires…");
        extras->setEnabled(debugger_->hardware() && debugger_->loaded() && !debugger_->busy() && !running_);
        auto* chosen = menu.exec(registers_->viewport()->mapToGlobal(point));
        if (chosen == edit) editCpuRegister(index.row(), index.column());
        if (chosen == extras) showExtraRegisters();
    });
    cpuLayout->addWidget(registers_);
    registers_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    registers_->verticalHeader()->setMinimumSectionSize(16);
    registers_->verticalHeader()->setDefaultSectionSize(16);
    registers_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    cpuDock_ = dock(this, "Cœurs CPU", "cpuDock", cpuPane);
    assemblyDock_ = dock(this, "Assembleur", "assemblyDock", assemblyPane);
    memory_ = new MemoryTable;
    memory_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    memory_->setMinimumHeight(49);
    memory_->resized = [this] { refreshMemory(); };
    memoryAddress_ = new QLineEdit(memoryExpression_);
    memoryAddress_->setPlaceholderText("Adresse ou symbole");
    memoryAddress_->setMinimumWidth(memoryAddress_->fontMetrics().horizontalAdvance("0x00000000") + 18);
    memoryLength_ = new QSpinBox; memoryLength_->setRange(16, 4096); memoryLength_->setSingleStep(16); memoryLength_->setValue(256);
    memoryAddress_->setObjectName("memoryAddress");
    memoryLength_->setObjectName("memoryLength");
    memoryPrevious_ = new QPushButton("←"); memoryPrevious_->setObjectName("previousMemoryPage");
    memoryNext_ = new QPushButton("→"); memoryNext_->setObjectName("nextMemoryPage");
    memoryPrevious_->setAccessibleName("Page mémoire précédente");
    memoryNext_->setAccessibleName("Page mémoire suivante");
    memoryPrevious_->setToolTip("Page mémoire précédente");
    memoryNext_->setToolTip("Page mémoire suivante");
    connect(memoryPrevious_, &QPushButton::clicked, this, [this] { moveMemoryPage(false); });
    connect(memoryNext_, &QPushButton::clicked, this, [this] { moveMemoryPage(true); });
    connect(memoryLength_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { refreshMemory(); });
    memoryAddress_->setToolTip("Adresse, symbole ou expression. Entrée pour lire la plage.");
    memoryLength_->setToolTip("Nombre d’octets à afficher et taille d’une page");
    auto* showMemory = new QPushButton("Lire");
    showMemory->setToolTip("Afficher la mémoire à partir de l’adresse saisie");
    connect(showMemory, &QPushButton::clicked, this, [this] { setMemoryAddress(memoryAddress_->text()); });
    auto* memoryMenu = new QMenu(this);
    writeMemory_ = memoryMenu->addAction("Écrire…"); writeMemory_->setObjectName("writeMemoryAction");
    writeMemory_->setToolTip("Écrire à une adresse mémoire sur 8, 16 ou 32 bits");
    connect(writeMemory_, &QAction::triggered, this, [this] {
        memoryValueDialog(false, memory_->currentRow(), memory_->currentColumn());
    });
    auto* watchMemory = memoryMenu->addAction("Suivre…"); watchMemory->setObjectName("watchMemoryAction");
    watchMemory->setToolTip("Suivre une valeur mémoire sur 8, 16 ou 32 bits");
    connect(watchMemory, &QAction::triggered, this, [this] {
        memoryValueDialog(true, memory_->currentRow(), memory_->currentColumn());
    });
    connect(memory_, &QTableWidget::cellDoubleClicked, this, [this](int line, int column) {
        uint32_t at = 0;
        if (writeMemory_->isEnabled() && memory_->addressAt(line, column, at)) memoryValueDialog(false, line, column);
    });
    readMemoryMmio_ = memoryMenu->addAction("Lire le registre MMIO une fois");
    readMemoryMmio_->setObjectName("readHardwareAddressOnce");
    readMemoryMmio_->setToolTip("Lire le mot MMIO sélectionné. La lecture peut consommer une FIFO ou acquitter un indicateur.");
    memoryMenu->setToolTipsVisible(true);
    connect(readMemoryMmio_, &QAction::triggered, this, [this] {
        try {
            uint32_t address = 0;
            const bool selectedByte = memoryAddress_->text() == memoryExpression_ &&
                memory_->addressAt(memory_->currentRow(), memory_->currentColumn(), address);
            if (selectedByte) address &= ~3u;
            else address = debugger_->evaluate(memoryAddress_->text().toStdString(), selected_);
            debugger_->readRegisterOnce(address, selected_);
            if (selectedByte) refreshMemory();
            else setMemoryAddress(memoryAddress_->text());
        } catch (const std::exception& e) { failure(e); }
    });
    connect(memoryAddress_, &QLineEdit::returnPressed, showMemory, &QPushButton::click);
    auto* memoryActions = new QToolButton;
    memoryActions->setText("Actions");
    memoryActions->setToolTip("Écrire ou suivre l’octet sélectionné. Les mêmes commandes sont disponibles par clic droit.");
    memoryActions->setPopupMode(QToolButton::InstantPopup);
    memoryActions->setMenu(memoryMenu);
    memory_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(memory_, &QTableWidget::customContextMenuRequested, this, [this, memoryMenu](const QPoint& at) {
        const auto index = memory_->indexAt(at);
        if (index.isValid() && index.column() < memory_->columnCount() - 1)
            memory_->setCurrentCell(index.row(), index.column());
        memoryMenu->popup(memory_->viewport()->mapToGlobal(at));
    });
    inspectionTabs_ = new QTabWidget; inspectionTabs_->setObjectName("inspectionTabs"); inspectionTabs_->setDocumentMode(true);
    inspectionTabs_->setStyleSheet("QTabBar::tab { padding:4px 10px; }"
                                    "QLineEdit { padding-top:2px; padding-bottom:2px; }");
    memoryContext_ = new QLabel;
    memoryContext_->setObjectName("memoryContext");
    memoryContext_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    memoryContext_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* memoryToolbar = row({memoryAddress_, showMemory, memoryPrevious_, memoryNext_,
                               new QLabel("Octets"), memoryLength_, memoryActions});
    auto* memoryPane = new QWidget;
    memoryPane->setObjectName("memoryPane");
    memoryPane->setStyleSheet("QLineEdit,QSpinBox,QPushButton,QToolButton { padding-top:2px; padding-bottom:2px; }"
                              "QHeaderView::section { padding:2px 3px; }");
    auto contextFont = memoryContext_->font();
    contextFont.setPixelSize(14);
    memoryContext_->setFont(contextFont);
    auto* memoryLayout = new QVBoxLayout(memoryPane);
    memoryLayout->setContentsMargins(6, 4, 6, 4);
    memoryLayout->setSpacing(4);
    memoryLayout->addWidget(memoryToolbar);
    memoryLayout->addWidget(memoryContext_);
    memoryLayout->addWidget(memory_, 1);
    connect(memory_, &QTableWidget::currentCellChanged, this, [this] { refreshMemoryContext(); });
    memoryDock_ = dock(this, "Mémoire", "memoryDock", memoryPane);
    auto* stackPane = new QSplitter(Qt::Horizontal);
    stackPane->setObjectName("stackTable");
    stackPane->setChildrenCollapsible(false);
    for (unsigned c = 0; c < stacks_.size(); ++c) {
        auto* stack = stacks_[c] = new StackMemoryView(true);
        stack->setObjectName(QString("stackCore%1").arg(c));
        stackPane->addWidget(stack);
        stack->inspectFrame = [this, c](unsigned level) { openStackInspector(c, level); };
        stack->previewChanged = [this, c] { refreshStack(c); };
        stack->rangeSelected = [this, c](unsigned level, StackSpan selection) { selectStackRange(c, level, selection); };
        stack->editRequested = [this, c](unsigned level, StackMemoryView::EditMode mode, uint32_t address, int field) {
            editStackMemory(c, level, mode, address, field, this);
        };
    }
    stackDock_ = dock(this, "Piles par cœur", "stackDock", stackPane);
    mapping_ = table({"Début", "Fin incluse", "Région", "Accès", "Couverture"});
    mapping_->setObjectName("mappingTable");
    mapping_->horizontalHeaderItem(3)->setToolTip(debugAccessLegend());
    peripherals_ = new PeripheralPanel;
    peripherals_->setObjectName("peripheralCatalogPage");
    peripherals_->setStyleSheet("QLineEdit,QPushButton,QToolButton { padding-top:2px; padding-bottom:2px; }"
                                 "QHeaderView::section { padding-top:2px; padding-bottom:2px; }");
    peripherals_->openDevice = [this](const QString& name) { openPeripheral(name, selected_); };
    configurePeripheralPanel(*peripherals_);
    workspace_->addFixedPage(peripherals_, "Périphériques");
    peripheralDock_ = dock(this, "Périphériques", "peripheralDock", workspace_);
    auto* peripheralWindow = peripheralViews->addAction("Catalogue");
    peripheralWindow->setObjectName("peripheralWindowAction");
    peripheralWindow->setShortcuts({QKeySequence("Ctrl+2"), QKeySequence("Ctrl+P")});
    connect(peripheralWindow, &QAction::triggered, this, [this] {
        workspace_->setCurrentWidget(peripherals_);
        revealDock(peripheralDock_);
    });
    auto* explorer = peripheralViews->addAction("Explorateur indépendant…"); explorer->setObjectName("peripheralExplorerAction");
    explorer->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(explorer, &QAction::triggered, this, [this] { openPeripheralExplorer(); });
    auto* gather = peripheralViews->addAction("Rassembler les inspecteurs"); gather->setObjectName("gatherInspectorsAction");
    gather->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(gather, &QAction::triggered, workspace_, &InspectionWorkspace::gatherInspections);
    connect(mapping_, &QTableWidget::cellDoubleClicked, this, [this](int line, int) {
        if (!mapping_->item(line, 0)) return;
        setMemoryAddress(mapping_->item(line, 0)->text());
        showMemoryView();
    });
    watches_ = table({"Expression", "Valeur", "Contexte"});
    watches_->setObjectName("watchesTable");
    auto* addWatch = new QPushButton("+"); auto* removeWatch = new QPushButton("−");
    removeWatch->setFixedWidth(26);
    connect(addWatch, &QPushButton::clicked, this, [this] { addExpression(); });
    connect(removeWatch, &QPushButton::clicked, this, [this] { if (watches_->currentRow() >= 0) watches_->removeRow(watches_->currentRow()); refreshWatches(); });
    monitorTabs_ = new QTabWidget; monitorTabs_->setObjectName("monitorTabs"); monitorTabs_->setDocumentMode(true);
    monitorTabs_->setStyleSheet("QTabBar::tab { font-size:13px; padding-left:4px; padding-right:4px; }"
                               "QPushButton { font-size:13px; padding-left:4px; padding-right:4px; }");
    monitorTabs_->tabBar()->setUsesScrollButtons(false);
    monitorTabs_->tabBar()->setElideMode(Qt::ElideRight);
    addWatch->setToolTip("Ajouter une expression évaluée dans le contexte du cœur sélectionné (Ctrl+E)");
    removeWatch->setToolTip("Retirer l’expression sélectionnée de la surveillance");
    auto* watchTools = row({addWatch, removeWatch});
    monitorTabs_->addTab(vertical(watchTools, watches_), "Expr.");
    monitorTabs_->setTabToolTip(0, "Expressions surveillées");
    monitorDock_ = dock(this, "Surveillance", "monitorDock", monitorTabs_);
    monitorDock_->setStyleSheet("QDockWidget::title { font-size:13px; padding:4px; }");
    points_ = table({"Type", "Adresse", "Taille / accès"});
    points_->setObjectName("pointsTable");
    auto* addPoint = new QPushButton("Arrêt"); auto* addMemory = new QPushButton("Accès"); auto* removePoint = new QPushButton("−");
    removePoint->setFixedWidth(26);
    addPoint->setToolTip("Breakpoint : arrêter avant une instruction (F9)");
    addMemory->setToolTip("Watchpoint : arrêter après un accès mémoire CPU");
    removePoint->setToolTip("Supprimer le point d’arrêt ou le watchpoint sélectionné");
    connect(addPoint, &QPushButton::clicked, this, [this] { addBreakpoint(); });
    connect(addMemory, &QPushButton::clicked, this, [this] { addMemoryWatch(); });
    connect(removePoint, &QPushButton::clicked, this, [this] {
        const int r = points_->currentRow(); if (r < 0) return;
        const auto* item = points_->item(r, 0); const auto value = item->data(Qt::UserRole).toUInt();
        try {
            if (item->text() == "Break") debugger_->removeBreakpoint(value);
            else if (value < debugger_->watchpoints.size()) debugger_->removeWatchpoint(value);
        } catch (const std::exception& e) { failure(e); return; }
        refreshPoints(); refreshAssembly();
    });
    monitorTabs_->addTab(vertical(row({addPoint, addMemory, removePoint}), points_), "Arr.");
    monitorTabs_->setTabToolTip(1, "Points d’arrêt et watchpoints");
    valueWatches_ = new ValueWatches;
    valueWatches_->setObjectName("valueWatchesPage");
    valueWatches_->changed = [this] { refreshValueWatches(); refreshMemory(); refreshPeripherals(); };
    valueWatches_->open = [this](ValueWatch watch) { openValueWatch(watch); };
    valueWatches_->openMemory = [this](ValueWatch watch) { openMemoryWatch(watch); };
    monitorTabs_->addTab(valueWatches_, "Suivis");
    monitorTabs_->setCurrentWidget(valueWatches_);
    console_ = new QPlainTextEdit; console_->setReadOnly(true); console_->setMaximumBlockCount(2000); console_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    console_->setObjectName("consoleView");
    console_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    console_->setMinimumHeight(34);
    consoleInput_ = new QLineEdit;
    consoleInput_->setPlaceholderText("bt [0|1|all], break, watch, delete, c, n, s, print…");
    consoleInput_->setToolTip("Entrée exécute la commande. Tapez help pour les syntaxes.");
    consoleInput_->setObjectName("consoleExpression");
    connect(consoleInput_, &QLineEdit::returnPressed, this, [this] {
        const auto command = consoleInput_->text();
        consoleInput_->clear();
        consoleCommand(command);
    });
    consolePane_ = new QWidget;
    auto* consoleLayout = new QVBoxLayout(consolePane_);
    consoleLayout->setContentsMargins(6, 4, 6, 4);
    consoleLayout->setSpacing(4);
    consoleLayout->addWidget(console_, 1);
    consoleLayout->addWidget(consoleInput_);
    inspectionTabs_->addTab(consolePane_, "Console");
    inspectionTabs_->addTab(mapping_, "Carte mémoire");
    virtualTimePanel_ = new VirtualTimePanel;
    virtualTimePanel_->advanceRequested = [this](uint64_t duration, bool timeOnly) { advanceVirtualTime(duration, timeOnly); };
    virtualTimePanel_->pauseRequested = [this] { pauseAction_->trigger(); };
    virtualTimeTab_ = scrollTool(virtualTimePanel_);
    inspectionTabs_->addTab(virtualTimeTab_, "Temps virtuel");
    boardPanel_ = new BoardPanel;
    auto* boardTab = scrollTool(boardPanel_);
    inspectionTabs_->addTab(boardTab, "Carte et câblage");
    serialMonitor_ = new ProbeSerialMonitor;
    serialTab_ = scrollTool(serialMonitor_);
    inspectionTabs_->addTab(serialTab_, "Série TX/RX");
    connect(inspectionTabs_, &QTabWidget::currentChanged, this, [this] {
        if (inspectionTabs_->currentWidget() == serialTab_) serialMonitor_->refreshPorts();
    });
    inspectionDock_ = dock(this, "Outils", "toolsDock", inspectionTabs_);
    toolsToggle_ = new QToolButton;
    toolsToggle_->setObjectName("toolsToggleButton");
    toolsToggle_->setAutoRaise(true);
    toolsToggle_->setFixedSize(24, 21);
    toolsToggle_->setStyleSheet("QToolButton { padding:0; }");
    statusBar()->addPermanentWidget(toolsToggle_);
    connect(toolsToggle_, &QToolButton::clicked, this, [this] { toggleTools(inspectionDock_->isHidden()); });
    const auto toggleState = [this] {
        const QSignalBlocker blocker(consoleToggle_);
        consoleToggle_->setChecked(inspectionDock_->isVisible() && inspectionTabs_->currentWidget() == consolePane_);
        toolsToggle_->setArrowType(inspectionDock_->isVisible() ? Qt::DownArrow : Qt::UpArrow);
        toolsToggle_->setToolTip(inspectionDock_->isVisible() ? "Replier Outils" : "Déplier Outils");
        toolsToggle_->setAccessibleName(toolsToggle_->toolTip());
    };
    connect(inspectionDock_, &QDockWidget::visibilityChanged, this, toggleState);
    connect(inspectionTabs_, &QTabWidget::currentChanged, this, toggleState);
    // Every secondary view has a stable keyboard/menu route. Selecting a view
    // also reveals its dock, so closing a panel cannot leave a hidden result.
    const auto inspectAction = [this, toolViews](const QString& name, const char* id, const char* shortcut, QWidget* page) {
        auto* action = toolViews->addAction(name); action->setObjectName(id);
        if (*shortcut) action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, [this, page] {
            inspectionTabs_->setCurrentWidget(page);
            revealDock(inspectionDock_);
        });
    };
    auto* memoryAction = views->addAction("Mémoire");
    memoryAction->setObjectName("memoryViewAction");
    memoryAction->setShortcut(QKeySequence("Ctrl+3"));
    connect(memoryAction, &QAction::triggered, this, [this] { showMemoryView(); });
    auto* stackView = views->addAction("Piles des deux cœurs");
    stackView->setObjectName("stackViewAction");
    stackView->setShortcut(QKeySequence("Ctrl+4"));
    connect(stackView, &QAction::triggered, this, [this] { revealDock(stackDock_); });
    auto* consoleView = toolViews->addAction("Console");
    consoleView->setObjectName("consoleViewAction");
    consoleView->setShortcut(QKeySequence("Ctrl+5"));
    connect(consoleView, &QAction::triggered, this, &MainWindow::revealConsole);
    inspectAction("Carte mémoire", "mappingViewAction", "", mapping_);
    inspectAction("Temps virtuel", "virtualTimeViewAction", "Ctrl+6", virtualTimeTab_);
    inspectAction("Carte et câblage", "boardViewAction", "Ctrl+7", boardTab);
    inspectAction("Série TX/RX", "serialViewAction", "", serialTab_);
    auto* codeView = views->addAction("Assembleur"); codeView->setObjectName("codeViewAction");
    codeView->setShortcut(QKeySequence("Ctrl+1"));
    assemblyDock_->toggleViewAction()->setObjectName("showDebuggerAssemblyAction");
    connect(assemblyDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) refreshAssembly(); });
    connect(codeView, &QAction::triggered, this, [this] {
        revealDock(assemblyDock_);
        refreshAssembly();
    });
    auto* watchView = monitorViews->addAction("Expressions"); watchView->setObjectName("watchesViewAction");
    connect(watchView, &QAction::triggered, this, [this] { monitorTabs_->setCurrentIndex(0); revealDock(monitorDock_); });
    auto* pointView = monitorViews->addAction("Arrêts et watchpoints"); pointView->setObjectName("pointsViewAction");
    connect(pointView, &QAction::triggered, this, [this] { monitorTabs_->setCurrentIndex(1); revealDock(monitorDock_); });
    auto* valueWatchView = monitorViews->addAction("Suivis");
    valueWatchView->setObjectName("valueWatchesViewAction");
    connect(valueWatchView, &QAction::triggered, this, [this] { showValueWatches(); });
    stop_ = new QLabel; stop_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(stop_, 1);
    modeStatus_ = new QLabel; modeStatus_->setObjectName("targetModeStatus");
    modeStatus_->setMinimumWidth(350); modeStatus_->setMaximumWidth(680);
    statusBar()->addPermanentWidget(modeStatus_);
    ideLinkStatus_ = new QLabel;
    ideLinkStatus_->setObjectName("ideLinkStatus");
    statusBar()->addPermanentWidget(ideLinkStatus_);
    timeLabel_ = new QLabel; timeLabel_->setObjectName("virtualTimeStatus");
    timeLabel_->setToolTip("Ouvrir les commandes du temps virtuel (Ctrl+6)");
    statusBar()->addPermanentWidget(timeLabel_);
    connect(timeLabel_, &QLabel::linkActivated, this, [this] {
        inspectionTabs_->setCurrentWidget(virtualTimeTab_); revealDock(inspectionDock_);
    });
    addDockWidget(Qt::LeftDockWidgetArea, cpuDock_);
    splitDockWidget(cpuDock_, memoryDock_, Qt::Horizontal);
    splitDockWidget(cpuDock_, stackDock_, Qt::Vertical);
    splitDockWidget(memoryDock_, peripheralDock_, Qt::Vertical);
    splitDockWidget(peripheralDock_, assemblyDock_, Qt::Vertical);
    splitDockWidget(assemblyDock_, inspectionDock_, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, monitorDock_);
    sizeOverview();
    overviewState_ = saveState(layoutVersion);
}
bool MainWindow::loadFile(const QString& path) {
    setRunning(false);
    try {
        debugger_->load(path.toStdString()); previousValid_ = false;
        if (debugger_->hardware()) { log("Chargement des symboles demandé : " + path); refresh(); return true; }
        imagePath_ = QFileInfo(path).absoluteFilePath(); imageName_ = QFileInfo(path).fileName();
        indexSource(imagePath_);
        QSettings().setValue("lastDirectory", QFileInfo(path).absolutePath());
        setWindowTitle("PicoDebugger — " + QFileInfo(path).fileName());
        log("ELF chargé : " + path); log(q(debugger_->stop.message)); refresh();
        return true;
    } catch (const std::exception& e) { failure(e); return false; }
}
void MainWindow::selectChip(Chip chip) { chip_->setCurrentIndex(chip == Chip::RP2350 ? 1 : 0); }
void MainWindow::selectBoard(Board board) { chip_->setCurrentIndex(chip_->findData(static_cast<int>(board))); }
void MainWindow::loadDemo() {
    setRunning(false);
    try { debugger_->loadImage(demoImage()); imagePath_.clear(); imageName_ = "Exemple intégré"; indexSource({}); previousValid_ = false; log("Exemple chargé. Pour le cœur 1 : entrée core1_entry, pile 0x20042000."); refresh(); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::singleStep() {
    setRunning(false);
    try { debugger_->step(selected_); refresh(); if (debugger_->stop.kind != StopKind::Step) log(q(debugger_->stop.message)); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::setRunning(bool running) {
    if (debugger_->hardware()) {
        timer_->stop(); running_ = debugger_->executing(); remainingTime_ = 0; timeOnly_ = false;
        const bool ready = debugger_->loaded() && !debugger_->busy() && !running_;
        stepAction_->setEnabled(ready && !debugger_->terminalStop() && debugger_->registerAvailable(selected_, 15));
        runAction_->setEnabled(ready && !debugger_->terminalStop() && (debugger_->registerAvailable(0, 15) || debugger_->registerAvailable(1, 15)));
        pauseAction_->setEnabled(running_ && !debugger_->busy()); resetAction_->setEnabled(ready);
        return;
    }
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
    const bool real = debugger_->hardware();
    auto* hardware = dynamic_cast<GdbTarget*>(debugger_.get());
    const bool ready = loaded && !running_ && !debugger_->busy();
    writeMemory_->setEnabled(ready);
    const bool connected = hardware && (hardware->connected() || hardware->connecting());
    chip_->setEnabled(!connected); demoAction_->setEnabled(!real);
    readMemoryMmio_->setVisible(real); readMemoryMmio_->setEnabled(real && ready);
    openAction_->setEnabled(!real || ready);
    connectAction_->setEnabled(!connected); disconnectAction_->setEnabled(connected && (!debugger_->busy() || hardware->connecting()));
    downloadAction_->setEnabled(real && ready && !hardware->elfPath().isEmpty());
    refreshTargetAction_->setEnabled(real && ready);
    emulationAction_->setChecked(!real); hardwareAction_->setChecked(real);
    const QString mode = real ? "CARTE RÉELLE · " : "ÉMULATION · ";
    const QString state = real ? hardware->connectionText() : "Sans connexion matérielle";
    const QString status = mode + q(debugger_->board().name) + " · " + state;
    const QString shortState = !real ? (!loaded ? QString("Aucun ELF") :
        running_ ? (timeOnly_ ? QString("Temps virtuel en cours") : QString("Simulation en cours")) : QString("Simulation arrêtée")) :
        hardware->connecting() ? QString("Connexion…") :
        !hardware->connected() ? (hardware->errorText().isEmpty() ? QString("Déconnectée") : QString("Perdue / erreur")) :
        hardware->busy() ? QString("Connectée · opération…") : running_ ? QString("Connectée · en exécution") :
        hardware->errorText().isEmpty() ? QString("Connectée · arrêtée") : QString("Connectée · arrêtée / erreur");
    QString shortBoard = q(debugger_->board().name); shortBoard.remove("Raspberry Pi ");
    modeStatus_->setText(mode + shortBoard + " · " + shortState); modeStatus_->setToolTip(status);
    syncIdeState();
    timeLabel_->setVisible(!real);
    inspectionTabs_->setTabVisible(inspectionTabs_->indexOf(virtualTimeTab_), !real);
    findChild<QAction*>("virtualTimeViewAction")->setEnabled(!real);
    virtualTimePanel_->updateState(debugger_->virtualTime(), remainingTime_, loaded && !real && !debugger_->terminalStop(), running_);
    timeLabel_->setText(QString("<a href='time' style='color:%1'>t = %2 µs</a>")
        .arg(picoThemeColor("#8bd6bb").name()).arg(static_cast<qulonglong>(debugger_->virtualTime())));
    QString sessionName = imageName_;
    if (real) {
        const auto elfPath = hardware->elfPath();
        sessionName = elfPath.isEmpty() ? "Carte connectée · aucun ELF de symboles" : QFileInfo(elfPath).fileName();
    }
    session_->setText(loaded ? sessionName : "Aucune image chargée"); session_->setToolTip(session_->text());
    banner_->setText(!loaded ? "HORS LIGNE" : running_ ? (timeOnly_ ? "AVANCE DU TEMPS" : "EN EXÉCUTION") : q(stopName(debugger_->stop.kind)));
    banner_->setStyleSheet(QString("color:%1;font-weight:600;")
        .arg(picoThemeColor(debugger_->terminalStop() ? "#ffb591" : "#8bd6bb").name()));
    const auto instructionHint = QString("Cœur %1 · PC %2").arg(selected_).arg(hx(debugger_->cpus[selected_].r[15]));
    guidance_->setText(!loaded ? "Ouvrir un ELF ou lancer l’exemple intégré."
        : running_ ? "Maj+F5 pour mettre en pause" : instructionHint);
    if (real) {
        banner_->setText(hardware->connecting() ? "CONNEXION" : !loaded ? "DÉCONNECTÉE" : debugger_->busy() ? "OPÉRATION EN COURS" : running_ ? "EN EXÉCUTION" : "ARRÊTÉE");
        guidance_->setText(ready && debugger_->registerAvailable(selected_, 15) ? instructionHint : state);
    }
    guidance_->setToolTip(guidance_->text());
    refreshRegisters(); refreshSource(); refreshAssembly(); refreshStack(); refreshWatches();
    refreshPoints(); refreshValueWatches(); refreshMemory(); refreshPeripherals();
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    stop_->setText(QString("%1  |  cœur %2  |  %3  |  révision %4  |  %5")
        .arg(running_ ? "EN EXÉCUTION — instantané échantillonné" : q(stopName(debugger_->stop.kind)))
        .arg(debugger_->stop.core).arg(q(debugger_->stop.message)).arg(static_cast<qulonglong>(debugger_->revision())).arg(stamp));
    stop_->setToolTip(stop_->text());
}
void MainWindow::refreshRegisters() {
    registers_->ensurePolished();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const char* names[] = {"xPSR", "MSP", "PSP", "CONTROL", "PRIMASK"};
    if (!previousValid_) {
        previousKnown_ = {};
        registerHighlightUntil_ = {};
    }
    registers_->setRowCount(21);
    registers_->setColumnHidden(1, !allCores_ && selected_ != 0);
    registers_->setColumnHidden(2, !allCores_ && selected_ != 1);
    registers_->setColumnHidden(3, allCores_);
    for (unsigned r = 0; r < 21; ++r)
        cell(registers_, static_cast<int>(r), 0, r < 16 ? q(registerName(r)) : names[r - 16]);
    for (unsigned c = 0; c < 2; ++c) {
        const Cpu& cpu = debugger_->cpus[c];
        const int column = static_cast<int>(c + 1);
        auto* header = registers_->horizontalHeaderItem(column);
        header->setText(QString("Cœur %1%2").arg(c).arg(selected_ == c ? " •" : ""));
        header->setToolTip("Double-cliquez sur une valeur pour modifier ce cœur à l’arrêt.");
        for (unsigned r = 0; r < 21; ++r) {
            const bool available = debugger_->registerAvailable(c, r);
            const auto value = available ? debugger_->registerValue(c, r) : 0;
            if (available) {
                if (previousKnown_[c][r] && value != previous_[c][r])
                    registerHighlightUntil_[c][r] = now + 1400;
                previousKnown_[c][r] = true;
                previous_[c][r] = value;
            }
            const bool changed = available && registerHighlightUntil_[c][r] > now;
            const auto meaning = available ? registerHint(c, r, value) : QString();
            const auto help = !available ? QString("Valeur indisponible") : changed
                ? QString("Valeur modifiée récemment · double-clic pour éditer") : QString("Double-clic pour éditer à l’arrêt");
            cell(registers_, static_cast<int>(r), column, available ? hx(value) : QString("—"),
                 meaning.isEmpty() ? help : meaning + "\n" + help);
            if (c == selected_) cell(registers_, static_cast<int>(r), 3, meaning, meaning);
            auto* item = registers_->item(static_cast<int>(r), column);
            item->setForeground(picoThemeColor(changed ? "#ffd780" : "#dce6f0"));
            item->setBackground(changed ? QBrush(picoThemeColor("#655021")) : QBrush());
        }
        const auto state = !debugger_->loaded() ? "Hors ligne" : debugger_->hardware() && !debugger_->registerAvailable(c, 15)
            ? (running_ ? "En exécution" : "Indisponible")
            : cpu.held ? "Retenu" : cpu.lockup ? "Lockup" : cpu.sleeping ? "Sommeil" : running_ && !timeOnly_ ? "Actif" : "Arrêté";
        flags_[c]->setVisible(allCores_ || c == selected_);
        flags_[c]->setText(QString("C%1 · %2").arg(c).arg(state));
        flags_[c]->setToolTip(debugger_->registerAvailable(c, 16)
            ? q(cpu.flags()) : QString("Indicateurs indisponibles"));
    }
    registers_->resizeColumnsToContents();
    const auto* header = registers_->verticalHeader();
    registers_->setFixedHeight(registers_->horizontalHeader()->sizeHint().height()
        + header->length() + 2 * registers_->frameWidth() + 2);
    const int visible = allCores_ ? registers_->columnWidth(1) + registers_->columnWidth(2)
                                  : registers_->columnWidth(static_cast<int>(selected_ + 1)) + 125;
    registers_->setMinimumWidth(registers_->columnWidth(0) + visible + 2 * registers_->frameWidth() + 2);
    // Give the remaining height in this dock column to the stack, including after restoring a layout.
    auto* cpuPane = cpuDock_->widget();
    cpuPane->setFixedHeight(cpuPane->sizeHint().height());
    previousValid_ = true;
}

QString MainWindow::registerHint(unsigned core, unsigned reg, uint32_t value) const {
    if (reg == 16) return q(debugger_->cpus[core].flags());
    if (reg == 19) return QString("SPSEL → %1 · %2").arg(value & 2u ? "PSP" : "MSP", value & 1u ? "non privilégié" : "privilégié");
    if (reg == 20) return value & 1u ? "IRQ masquables bloquées" : "IRQ masquables autorisées";
    if ((reg <= 12 && value < 0x10000000u) || !value) return {};
    const auto name = debugger_->image.functionAt(value & ~1u);
    if (!name.empty()) return q(debugger_->sourceHint(value & ~1u));
    const auto symbol = registerSymbols_.find(value);
    if (symbol != registerSymbols_.end()) return symbol->second;
    if (const auto* region = regionAt(debugger_->description(), value)) return q(region->name);
    return {};
}

void MainWindow::editCpuRegister(int row, int column) {
    if (row < 0 || row >= 21 || column < 1 || column > 2 || running_ || debugger_->busy()) return;
    const auto core = static_cast<unsigned>(column - 1);
    const auto index = static_cast<unsigned>(row);
    if (!debugger_->registerAvailable(core, index)) return;
    QDialog dialog(this);
    dialog.setObjectName("cpuRegisterDialog");
    dialog.setWindowTitle(QString("Modifier %1 · cœur %2").arg(registers_->item(row, 0)->text()).arg(core));
    QFormLayout form(&dialog);
    QLineEdit value(hx(debugger_->registerValue(core, index)));
    value.setObjectName("cpuRegisterWriteValue");
    value.selectAll();
    form.addRow("Valeur hexadécimale ou décimale", &value);
    QLabel error;
    error.setWordWrap(true);
    error.hide();
    form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (running_) throw Error(ErrorKind::InvalidArgument, "Mettre la cible en pause pour écrire");
            debugger_->writeCpuRegister(core, index, parseWriteValue(value.text(), 4));
            if (!debugger_->hardware()) log(QString("Registre %1 modifié · cœur %2").arg(registers_->item(row, 0)->text()).arg(core));
            refresh();
            dialog.accept();
        } catch (const std::exception& e) {
            error.setText(q(e.what()));
            error.show();
        }
    });
    dialog.exec();
}

void MainWindow::showExtraRegisters() {
    const auto* hw = dynamic_cast<GdbTarget*>(debugger_.get());
    if (!hw || hw->busy() || hw->executing()) return;
    std::set<std::string> names;
    for (const auto& core : hw->extraRegisters())
        for (const auto& reg : core) names.insert(reg.first);
    QDialog dialog(this);
    dialog.setWindowTitle("Registres supplémentaires GDB");
    auto* values = table({"Registre", "Cœur 0", "Cœur 1"});
    QVBoxLayout layout(&dialog);
    layout.addWidget(values);
    values->setRowCount(static_cast<int>(names.size()));
    int row = 0;
    for (const auto& name : names) {
        cell(values, row, 0, q(name));
        for (unsigned c = 0; c < 2; ++c) {
            const auto& extra = hw->extraRegisters()[c];
            const auto found = extra.find(name);
            cell(values, row, static_cast<int>(c + 1), found == extra.end() ? QString("—") : q(found->second));
        }
        ++row;
    }
    dialog.resize(510, 340);
    dialog.exec();
}
void MainWindow::refreshAssembly() {
    if (assemblyDock_->isHidden()) return;
    assembly_->setRowCount(0);
    if (!debugger_->loaded()) return;
    if (debugger_->hardware() && !debugger_->registerAvailable(selected_, 15)) {
        assembly_->setRowCount(1); cell(assembly_, 0, 3, "Registres du cœur indisponibles — attendre l'arrêt ou choisir un cœur exposé"); return;
    }
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
            const auto location = sourceMap_.location(at);
            const auto sourceLabel = location.valid() ? QFileInfo(location.path).fileName() + ":" + QString::number(location.line) : QString();
            cell(assembly_, r, 5, sourceLabel, location.valid() ? location.path + ":" + QString::number(location.line) : QString());
            try {
                const auto instruction = debugger_->disassemble(at, selected_);
                cell(assembly_, r, 2, q(hex(instruction.raw, instruction.size * 2)));
                cell(assembly_, r, 3, q(instruction.text)); cell(assembly_, r, 4, q(debugger_->sourceHint(at)));
                const bool pc = at == (debugger_->cpus[selected_].r[15] & ~1u);
                const auto color = pc || (location.valid() && location == executingSource_) ? picosource::executionColor()
                    : location.valid() && location == selectedSource_ ? picosource::selectionColor() : QString();
                if (!color.isEmpty())
                    for (int col = 0; col < assembly_->columnCount(); ++col) assembly_->item(r, col)->setBackground(QColor(color));
                if (uint64_t(at) + instruction.size > 0xffffffffull) break;
                at += instruction.size;
            } catch (const Error& e) { cell(assembly_, r, 3, q(e.what())); break; }
        }
    } catch (const Error& e) { assembly_->setRowCount(1); cell(assembly_, 0, 3, q(e.what())); }
}
void MainWindow::refreshMemory() {
    if (!debugger_->loaded()) {
        memory_->clearRange();
        refreshMemoryContext();
        return;
    }
    try {
        const uint32_t start = debugger_->evaluate(memoryExpression_.toStdString(), selected_);
        const auto length = std::min<uint64_t>(static_cast<unsigned>(memoryLength_->value()), 0x100000000ull - start);
        memory_->setHighlights(valueWatches_->memoryHighlights(debugger_->description(), selected_));
        memory_->showRange(*debugger_, selected_, start, static_cast<unsigned>(length));
        refreshMemoryContext();
    } catch (const Error& e) {
        memory_->clearRange();
        refreshMemoryContext();
        memoryContext_->setText(q(e.what()));
        memoryContext_->setToolTip(q(e.what()));
    }
}

void MainWindow::refreshMemoryContext() {
    const auto start = memory_->start();
    const auto length = memory_->length();
    memoryPrevious_->setEnabled(length && start);
    memoryNext_->setEnabled(length && uint64_t(start) + length < 0x100000000ull);
    if (!length) {
        memoryContext_->setText("Chargez un ELF ou connectez une cible pour lire la mémoire.");
        memoryContext_->setToolTip({});
        return;
    }
    uint32_t at = start;
    const bool selected = memory_->addressAt(memory_->currentRow(), memory_->currentColumn(), at);
    QStringList parts{selected ? hx(at) : hx(start) + " – " + hx(start + length - 1)};
    if (selected) {
        const auto* value = memory_->item(memory_->currentRow(), memory_->currentColumn());
        if (value && memory_->currentColumn() > 0 && value->text() != "??") parts << "0x" + value->text();
    }
    if (const auto* region = regionAt(debugger_->description(), at)) {
        const auto device = std::find_if(memoryDevices_.begin(), memoryDevices_.end(),
            [region](const PeripheralDescription& entry) { return entry.base == region->begin; });
        const auto offset = "+" + q(hex(at - region->begin, 1));
        if (device != memoryDevices_.end()) {
            parts << "Périphérique " + q(device->name) << q(hex(device->base)) + " " + offset;
            const auto reg = std::find_if(device->registers.begin(), device->registers.end(),
                [at](const RegisterDescription& entry) { return at >= entry.address && uint64_t(at) < uint64_t(entry.address) + 4; });
            if (reg != device->registers.end()) parts << q(reg->name + " (" + reg->access + ")");
        } else {
            parts << q(region->name) + " " + offset;
            parts << debugAccessText(regionAccess(region));
        }
    } else {
        parts << "Adresse hors carte mémoire";
    }
    const auto text = parts.join(" · ");
    memoryContext_->setText(text);
    memoryContext_->setToolTip(text);
}

void MainWindow::setMemoryAddress(const QString& expression) {
    memoryExpression_ = expression.trimmed();
    memoryAddress_->setText(memoryExpression_);
    refreshMemory();
}

void MainWindow::moveMemoryPage(bool forward) {
    if (!memory_->length()) return;
    const auto start = memory_->start();
    const auto count = static_cast<unsigned>(memoryLength_->value());
    const auto next = forward ? std::min<uint64_t>(uint64_t(start) + count, 0x100000000ull - count)
                              : start > count ? start - count : 0;
    setMemoryAddress(hx(static_cast<uint32_t>(next)));
}

void MainWindow::showMemoryView() {
    revealDock(memoryDock_);
}

void MainWindow::openMemoryAt(uint32_t address, unsigned core, unsigned length) {
    if (allCores_) {
        if (selected_ != core) { selected_ = core; refresh(); }
    } else core_->setCurrentIndex(static_cast<int>(core));
    if (!length) {
        const auto device = std::find_if(memoryDevices_.begin(), memoryDevices_.end(),
            [address](const PeripheralDescription& entry) { return entry.base == address; });
        if (device != memoryDevices_.end() && !device->registers.empty()) {
            const auto last = std::max_element(device->registers.begin(), device->registers.end(),
                [](const RegisterDescription& a, const RegisterDescription& b) { return a.address < b.address; });
            length = static_cast<unsigned>(std::min<uint64_t>(4096, uint64_t(last->address) + 4 - address));
        }
    }
    if (length > static_cast<unsigned>(memoryLength_->value())) memoryLength_->setValue(static_cast<int>(length));
    setMemoryAddress(hx(address));
    memory_->selectAddress(address);
    showMemoryView();
}

void MainWindow::openMemoryWatch(const ValueWatch& watch) {
    uint32_t address = watch.address;
    if (!watch.size) {
        const auto device = std::find_if(memoryDevices_.begin(), memoryDevices_.end(),
            [&watch](const PeripheralDescription& entry) { return q(entry.name) == watch.device; });
        if (device == memoryDevices_.end()) return;
        address = device->base;
    }
    openMemoryAt(address, watch.core, watch.size);
}

void MainWindow::memoryValueDialog(bool watch, int line, int column) {
    const unsigned core = selected_;
    QDialog dialog(this);
    dialog.setObjectName(watch ? "watchMemoryDialog" : "writeMemoryDialog");
    dialog.setWindowTitle(watch ? "Suivre une valeur mémoire" : "Écrire en mémoire");
    QFormLayout layout(&dialog);
    uint32_t selectedAddress = 0;
    QLineEdit address(memory_->addressAt(line, column, selectedAddress) ? hx(selectedAddress) : memoryAddress_->text());
    address.setObjectName("memoryValueAddress");
    ChoiceBox width;
    width.setObjectName("memoryValueWidth");
    width.addItem("8 bits", 1); width.addItem("16 bits", 2); width.addItem("32 bits", 4);
    if (watch) width.addItem("Plage d’octets", 0);
    QSpinBox rangeLength;
    rangeLength.setObjectName("memoryWatchLength");
    rangeLength.setRange(1, 4096);
    rangeLength.setValue(16);
    QLabel rangeLabel("Octets à suivre");
    rangeLength.hide();
    rangeLabel.hide();
    QLineEdit value;
    value.setObjectName("memoryWriteValue");
    value.setPlaceholderText("Décimal ou 0x…");
    QLabel error;
    error.setWordWrap(true); error.hide();
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText(watch ? "Suivre" : "Écrire");
    layout.addRow("Adresse ou symbole", &address);
    layout.addRow(watch ? "Type de suivi" : "Largeur", &width);
    if (watch) {
        layout.addRow(&rangeLabel, &rangeLength);
        connect(&width, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&] {
            const bool range = width.currentData().toUInt() == 0;
            rangeLength.setVisible(range);
            rangeLabel.setVisible(range);
        });
    }
    if (!watch) layout.addRow("Valeur", &value);
    layout.addRow(new QLabel(QString("Cœur %1").arg(core)));
    layout.addRow(&error);
    layout.addRow(&buttons);
    if (watch) address.selectAll();
    else value.setFocus();
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            const auto at = debugger_->evaluate(address.text().trimmed().toStdString(), core);
            const bool range = watch && width.currentData().toUInt() == 0;
            const auto size = range ? static_cast<unsigned>(rangeLength.value()) : width.currentData().toUInt();
            if (uint64_t(at) + size > 0x100000000ull)
                throw Error(ErrorKind::InvalidArgument, "Plage mémoire débordante");
            if (watch) {
                valueWatches_->add({{}, at, size, core, range});
                showValueWatches();
            } else {
                if (running_) throw Error(ErrorKind::InvalidArgument, "Mettre la cible en pause pour écrire");
                const auto raw = parseWriteValue(value.text(), size);
                debugger_->writeMemory(at, raw, size, core);
                const auto start = memory_->start();
                if (!memory_->length() || at < start || uint64_t(at) + size > uint64_t(start) + memory_->length())
                    setMemoryAddress(hx(at));
                if (!debugger_->hardware()) log(QString("Écriture mémoire à %1 · %2 bits · cœur %3").arg(hx(at)).arg(8 * size).arg(core));
                refresh();
                memory_->selectAddress(at);
            }
            dialog.accept();
        } catch (const Error& e) {
            error.setText(q(e.what()));
            error.show();
        }
    });
    dialog.exec();
}

void MainWindow::editPeripheralRegister(QWidget& parent, const QString& device, const RegisterDescription& reg,
                                        unsigned core, const QString& current) {
    const auto address = reg.address;
    QDialog dialog(&parent);
    dialog.setObjectName("writePeripheralRegisterDialog");
    dialog.setWindowTitle("Écrire un registre");
    QFormLayout layout(&dialog);
    layout.addRow(new QLabel(QString("%1 · %2 · %3 · cœur %4").arg(device, q(reg.name), hx(address)).arg(core)));
    auto* access = new QLabel(registerAccessText(reg));
    access->setWordWrap(true);
    layout.addRow(access);
    if (current.startsWith("0x")) layout.addRow("Valeur lue", new QLabel(current));
    QLineEdit value;
    value.setObjectName("registerWriteValue");
    value.setPlaceholderText("32 bits · décimal ou 0x…");
    layout.addRow("Valeur à écrire", &value);
    QLabel error;
    error.setWordWrap(true); error.hide();
    layout.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText("Écrire");
    layout.addRow(&buttons);
    value.setFocus();
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (running_) throw Error(ErrorKind::InvalidArgument, "Mettre la cible en pause pour écrire");
            debugger_->writePeripheralRegister(address, parseWriteValue(value.text(), 4), core);
            if (!debugger_->hardware()) log(QString("Écriture registre à %1 · cœur %2").arg(hx(address)).arg(core));
            refresh();
            dialog.accept();
        } catch (const Error& e) {
            error.setText(q(e.what()));
            error.show();
        }
    });
    dialog.exec();
}

void MainWindow::refreshStack() {
    stackInspectors_.erase(std::remove_if(stackInspectors_.begin(), stackInspectors_.end(),
        [](const QPointer<StackInspector>& inspection) { return inspection.isNull(); }), stackInspectors_.end());
    for (unsigned core = 0; core < stacks_.size(); ++core) {
        const bool shown = allCores_ || core == selected_;
        stacks_[core]->setVisible(shown);
        const bool detached = std::any_of(stackInspectors_.cbegin(), stackInspectors_.cend(),
            [core](const QPointer<StackInspector>& entry) { return entry && entry->core() == core; });
        if (shown || detached) refreshStack(core);
    }
}

StackViewFrame MainWindow::stackFrame(unsigned core, const std::vector<StackFrame>& frames, unsigned level, bool inspectFields) {
    StackViewFrame result;
    result.level = level;
    result.frame = frames[level];
    const auto& frame = result.frame;
    FrameDetails decoded;
    if (!running_ && !debugger_->busy() && inspectFields) {
        decoded = stackInfo_.frame(*debugger_, core, frame.pc & ~1u, level == 0 ? nullptr : &frame);
        if (auto* hw = dynamic_cast<GdbTarget*>(debugger_.get())) {
            const auto nativeFields = decoded.fields;
            decoded.fields.erase(std::remove_if(decoded.fields.begin(), decoded.fields.end(), [](const FrameField& field) {
                return field.savedRegister < 0;
            }), decoded.fields.end());
            try {
                auto fields = hw->stackDetails(core, level).fields;
                for (auto& field : fields) {
                    if (field.inMemory || field.rendered.startsWith('<')) continue;
                    const auto match = std::find_if(nativeFields.cbegin(), nativeFields.cend(), [&](const FrameField& known) {
                        return known.reg >= 0 && known.name == field.name && known.role == field.role;
                    });
                    if (match != nativeFields.cend()) field.reg = match->reg;
                }
                decoded.fields += fields;
            } catch (const Error& error) { result.status = q(error.what()); }
        } else if (level == 0 && decoded.fields.isEmpty()) result.status = stackInfo_.status();

        if (level == 0 && debugger_->registerAvailable(core, 0) && !debugger_->cpus[core].handlerMode()) {
            try {
                const auto op = q(debugger_->disassemble(frame.pc & ~1u, core).text).simplified().toLower();
                const bool viaLr = op == "bx lr" || op == "mov pc, lr";
                const bool viaStack = op.startsWith("pop") && op.contains("pc");
                if (viaLr && debugger_->registerAvailable(core, 14)) {
                    FrameField address;
                    address.name = "Adresse de retour"; address.role = "Retour";
                    address.reg = 14; address.type = frameType("void *"); address.editable = true;
                    decoded.fields.push_front(address);
                }
                if ((viaLr || viaStack) && !(viaStack && (op.contains("r0") || (decoded.result.size > 4 && op.contains("r1"))))) {
                    const auto type = decoded.result;
                    if (type.kind != FrameType::Unknown && type.kind != FrameType::Float && !type.isArray && type.size &&
                        (type.size <= 4 || (!debugger_->hardware() && type.size == 8))) {
                        FrameField value;
                        value.name = "Valeur de retour"; value.role = "Résultat";
                        value.reg = 0; value.type = type; value.editable = true;
                        decoded.fields.push_front(value);
                    } else if (type.name.isEmpty() && type.size == 0 && !decoded.hasBase) {
                        FrameField value;
                        value.name = "R0 à la sortie"; value.role = "Résultat";
                        value.reg = 0; value.type = frameType("unsigned int"); value.editable = true;
                        decoded.fields.push_front(value);
                    }
                }
            } catch (const Error&) { /* Hardware disassembly can still be pending. */ }
        }
    }
    result.hasBase = decoded.hasBase;
    result.base = decoded.base;
    if (frame.hasSp && stackMemory(*debugger_, frame.sp, 1)) {
        const auto* region = regionAt(debugger_->description(), frame.sp);
        result.memory.start = frame.sp;
        result.memory.end = std::min(region->end, uint64_t(frame.sp) + 64);
        const auto boundary = [&](uint32_t pointer) {
            if (pointer < frame.sp || pointer > region->end) return;
            if (!result.bounded) result.memory.end = pointer;
            else result.memory.end = std::min(result.memory.end, uint64_t(pointer));
            result.bounded = true;
        };
        if (decoded.hasBase) boundary(decoded.base);
        if (level + 1 < frames.size() && frames[level + 1].hasSp) boundary(frames[level + 1].sp);
        if (!result.bounded) {
            for (const auto& field : decoded.fields) {
                const uint64_t end = uint64_t(field.address) + uint64_t(field.type.size) * field.type.count;
                if (field.inMemory && field.address >= frame.sp && end <= region->end &&
                    field.address - frame.sp <= 4096)
                    result.memory.end = std::max(result.memory.end, end);
            }
        }
    }
    unsigned args = 0, locals = 0, other = 0, returns = 0;
    for (const auto& field : decoded.fields) {
        StackFieldView entry;
        entry.field = field;
        if (field.role == "Argument") entry.label = "a" + QString::number(++args);
        else if (field.role == "Locale") entry.label = "l" + QString::number(++locals);
        else if (field.role == "Retour") { entry.label = returns ? "RET" + QString::number(returns + 1) : QString("RET"); ++returns; }
        else if (field.savedFramePointer) entry.label = "FP";
        else if (field.savedRegister >= 0) entry.label = "S" + QString::number(field.savedRegister);
        else if (field.role == "Résultat") entry.label = "RV";
        else entry.label = "v" + QString::number(++other);
        const auto size = uint64_t(field.type.size) * field.type.count;
        const uint64_t end = uint64_t(field.address) + size;
        if (field.inMemory && size && field.address >= result.memory.start && end <= result.memory.end) {
            entry.span.start = field.address;
            entry.span.end = end;
        }
        result.fields.push_back(entry);
    }
    return result;
}

void MainWindow::refreshStack(unsigned core) {
    std::vector<StackFrame> frames;
    QString status;
    try { frames = debugger_->backtrace(core); }
    catch (const Error& error) {
        status = q(error.what());
        if (!debugger_->executing() && !debugger_->cpus[core].held && debugger_->registerAvailable(core, 15)) {
            const auto pc = debugger_->registerValue(core, 15);
            const bool hasSp = debugger_->registerAvailable(core, 13);
            frames.emplace_back(pc, hasSp ? debugger_->registerValue(core, 13) : 0, hasSp, debugger_->image.functionAt(pc));
        }
    }
    const bool pending = debugger_->hardware() && !debugger_->executing() &&
        (debugger_->busy() || status.startsWith("Backtrace GDB en cours"));
    if (pending && !stackFrames_[core].isEmpty()) {
        if (allCores_ || core == selected_)
            stacks_[core]->showOverview(*debugger_, core, core == selected_, {}, "Lecture de la pile…", false, {}, stackValueChanges_[core]);
        for (const auto& entry : stackInspectors_)
            if (entry && entry->core() == core) entry->showUnavailable("Lecture de la pile…");
        return;
    }
    const auto previous = std::move(stackFrames_[core]);
    const auto oldSelections = std::move(stackSelections_[core]);
    auto& rows = stackFrames_[core];
    auto& selections = stackSelections_[core];
    rows.clear();
    selections.clear();
    for (unsigned level = 0; level < frames.size(); ++level) {
        const auto* item = stacks_[core]->topLevelItem(static_cast<int>(level));
        const bool detailed = std::any_of(stackInspectors_.cbegin(), stackInspectors_.cend(),
            [&](const QPointer<StackInspector>& entry) {
                if (!entry || entry->core() != core) return false;
                StackViewFrame candidate;
                candidate.level = level;
                candidate.frame = frames[level];
                return sameStackFrame(candidate, entry->frame());
            });
        const auto view = stackFrame(core, frames, level, level < 2 || detailed || (item && item->isExpanded()));
        rows.push_back(view);
        StackSpan selection;
        for (int old = 0; old < previous.size(); ++old)
            if (sameStackFrame(previous[old], view)) { selection = oldSelections.value(old); break; }
        if (selection.valid() && (!view.memory.contains(selection.start) || selection.end > view.memory.end)) selection = {};
        selections.push_back(selection);
    }
    const bool writable = debugger_->loaded() && !running_ && !debugger_->busy() && !debugger_->executing();
    if (allCores_ || core == selected_) {
        const QString label = !debugger_->loaded() ? "Hors ligne" : !debugger_->hardware() && debugger_->cpus[core].held
            ? "Cœur retenu" : debugger_->executing() ? "En exécution" : "Pile indisponible";
        stacks_[core]->showOverview(*debugger_, core, core == selected_, rows, label, writable, selections, stackValueChanges_[core]);
    }
    for (const auto& entry : stackInspectors_) {
        if (!entry || entry->core() != core || !entry->isVisible()) continue;
        const auto found = std::find_if(rows.cbegin(), rows.cend(), [&](const StackViewFrame& current) {
            return sameStackFrame(entry->frame(), current);
        });
        if (found != rows.cend() && !debugger_->executing() && !debugger_->busy())
            entry->updateFrame(*debugger_, *found, writable, selections.value(static_cast<int>(found->level)), stackValueChanges_[core]);
        else if (debugger_->executing() || running_) entry->showUnavailable("En exécution · dernier arrêt affiché");
        else if (!debugger_->loaded()) entry->showUnavailable("Cible indisponible · dernier arrêt affiché");
        else if (debugger_->busy() || status.startsWith("Backtrace GDB en cours")) entry->showUnavailable("En attente de la trame");
        else entry->showUnavailable("Trame terminée ou déplacée · dernier arrêt affiché");
    }
}

void MainWindow::openStackInspector(unsigned core, unsigned level) {
    if (level >= static_cast<unsigned>(stackFrames_[core].size())) return;
    const auto frame = stackFrames_[core][static_cast<int>(level)];
    for (const auto& existing : stackInspectors_) {
        if (existing && existing->isVisible() && existing->core() == core && sameStackFrame(frame, existing->frame())) {
            showInspectionWindow(existing);
            return;
        }
    }
    auto* view = new StackInspector(core, frame, this);
    applyPicoTheme(*view);
    stackInspectors_.push_back(view);
    view->refreshRequested = [this, core] { refreshStack(core); };
    view->rangeSelected = [this, core, view](StackSpan selection, bool reveal) {
        selectStackRange(core, view->frame().level, selection, view, reveal);
    };
    view->editMemory = [this, core, view](StackMemoryView::EditMode mode, uint32_t address, int field) {
        const auto level = view->frame().level;
        if (level < static_cast<unsigned>(stackFrames_[core].size()) &&
            sameStackFrame(view->frame(), stackFrames_[core][static_cast<int>(level)]))
            editStackMemory(core, level, mode, address, field, view);
    };
    view->editTyped = [this, core, view](const FrameField& field, const QString& value) {
        editStackField(core, view->frame(), field, value, view);
    };
    showInspectionWindow(view);
    refreshStack(core);
}

void MainWindow::selectStackRange(unsigned core, unsigned level, StackSpan selection, StackInspector* source, bool revealSource) {
    if (level >= static_cast<unsigned>(stackFrames_[core].size())) return;
    const auto frame = stackFrames_[core][static_cast<int>(level)];
    stackSelections_[core][static_cast<int>(level)] = selection;
    stacks_[core]->setRangeSelection(level, selection);
    for (const auto& entry : stackInspectors_)
        if (entry && entry->core() == core && sameStackFrame(frame, entry->frame()))
            entry->setRangeSelection(selection, entry != source || revealSource);
}

void MainWindow::editStackMemory(unsigned core, unsigned level, StackMemoryView::EditMode mode, uint32_t address,
                                 int fieldIndex, QWidget* parent) {
    if (level >= static_cast<unsigned>(stackFrames_[core].size())) return;
    const auto frame = stackFrames_[core][static_cast<int>(level)];
    if (!frame.memory.contains(address)) return;
    auto end = frame.memory.end;
    if (mode == StackMemoryView::Typed && fieldIndex >= 0 && fieldIndex < frame.fields.size()) {
        const auto& entry = frame.fields[fieldIndex];
        if (entry.span.contains(address)) {
            end = entry.span.end;
            if (entry.field.editable) {
                const auto field = entry.field.type.isArray
                    ? stackArrayElement(entry.field, (address - entry.span.start) / entry.field.type.size)
                    : entry.field;
                editStackField(core, frame, field, frameValue(*debugger_, core, field), parent);
                return;
            }
        }
    }
    const auto width = mode == StackMemoryView::Word || mode == StackMemoryView::Typed
        ? static_cast<unsigned>(std::min(uint64_t(4 - (address & 3u)), end - address)) : 1u;
    FrameField field;
    field.type = frameType("unsigned int");
    field.type.kind = FrameType::Pointer;
    field.type.size = width;
    field.type.name = width > 1 ? QString("%1 o · Hex little-endian").arg(width) : QString("Octet · Hex");
    field.name = hx(address);
    field.address = address;
    field.inMemory = field.editable = true;
    const bool ascii = mode == StackMemoryView::AsciiByte;
    auto rendered = frameValue(*debugger_, core, field);
    if (ascii) {
        try {
            const auto byte = debugger_->peek(address, 1, core);
            rendered = byte >= 32 && byte < 127 && byte != '\\' ? QString(QChar(static_cast<int>(byte)))
                : QString("\\x%1").arg(byte, 2, 16, QLatin1Char('0'));
        } catch (const Error&) { rendered.clear(); }
        field.type.name = "ASCII · caractère ou \\xNN";
    }
    editStackField(core, frame, field, rendered, parent, mode == StackMemoryView::Typed ? StackMemoryView::Word : mode);
}

void MainWindow::editStackField(unsigned core, StackViewFrame frame, FrameField field,
                                const QString& rendered, QWidget* parent, StackMemoryView::EditMode mode) {
    if (!field.editable || running_ || debugger_->busy() || debugger_->executing()) return;
    const auto target = debugger_.get();
    QDialog dialog(parent);
    dialog.setObjectName("stackValueDialog");
    dialog.setWindowTitle(QString("Modifier %1 · cœur %2").arg(field.name).arg(core));
    QFormLayout layout(&dialog);
    QLineEdit value(rendered == "…" ? QString() : rendered);
    value.setObjectName("stackWriteValue");
    value.selectAll();
    layout.addRow(field.type.name, &value);
    QLabel error;
    error.setWordWrap(true);
    error.hide();
    layout.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (debugger_.get() != target || running_ || debugger_->busy() || debugger_->executing())
                throw Error(ErrorKind::InvalidArgument, "Contexte modifié : rouvrir l’éditeur à l’arrêt");
            const auto frames = debugger_->backtrace(core);
            if (frame.level >= frames.size() || frames[frame.level].pc != frame.frame.pc || frames[frame.level].sp != frame.frame.sp ||
                frames[frame.level].activation != frame.frame.activation)
                throw Error(ErrorKind::InvalidArgument, "La trame a changé : resélectionnez la valeur");
            auto* hardware = dynamic_cast<GdbTarget*>(debugger_.get());
            if (hardware && !field.expression.isEmpty())
                hardware->writeStackVariable(core, frame.level, field, value.text());
            else {
                uint64_t raw;
                if (mode == StackMemoryView::AsciiByte) {
                    const auto input = value.text();
                    const auto match = QRegularExpression(R"(^\\x([0-9a-fA-F]{2})$)").match(input);
                    if (match.hasMatch()) raw = match.captured(1).toUInt(nullptr, 16);
                    else if (input.size() == 1 && input[0].unicode() >= 32 && input[0].unicode() < 127) raw = input[0].unicode();
                    else throw Error(ErrorKind::InvalidArgument, "Entrez un caractère ASCII ou une séquence \\xNN.");
                } else {
                    auto input = value.text().trimmed();
                    if (mode != StackMemoryView::Typed && !input.startsWith("0x", Qt::CaseInsensitive))
                        input.prepend("0x");
                    raw = parseFrameValue(input, field.type);
                }
                if (field.reg >= 0) {
                    const auto reg = static_cast<unsigned>(field.reg);
                    uint32_t bits = static_cast<uint32_t>(raw);
                    if (field.type.kind == FrameType::Signed && field.type.size < 4) {
                        const auto width = field.type.size * 8;
                        if (bits & (1u << (width - 1))) bits |= ~((1u << width) - 1);
                    }
                    debugger_->writeCpuRegister(core, reg, bits);
                    if (field.type.size > 4) debugger_->writeCpuRegister(core, 1, static_cast<uint32_t>(raw >> 32));
                } else {
                    if (!field.inMemory || !stackMemory(*debugger_, field.address, field.type.size))
                        throw Error(ErrorKind::InvalidArgument, "Adresse de pile indisponible en écriture");
                    debugger_->writeMemoryBytes(field.address, raw, field.type.size, core);
                }
                log(QString("%1 modifié · cœur %2").arg(field.name).arg(core));
            }
            refresh();
            dialog.accept();
        } catch (const std::exception& e) {
            error.setText(q(e.what()));
            error.show();
        }
    });
    dialog.exec();
}

void MainWindow::fillMapping() {
    memoryDevices_ = peripheralCatalog(debugger_->description().chip);
    boardPanel_->setBoard(debugger_->board(), debugger_->hardware());
    mapping_->setRowCount(0);
    for (const auto& region : debugger_->description().regions) {
        const int r = mapping_->rowCount(); mapping_->setRowCount(r + 1);
        cell(mapping_, r, 0, hx(region.begin)); cell(mapping_, r, 1, hx(static_cast<uint32_t>(region.end - 1)));
        cell(mapping_, r, 2, q(region.name)); cell(mapping_, r, 3, q(region.permissions)); cell(mapping_, r, 4, debugger_->hardware() ? "Accès réel via sonde, selon permissions du matériel" : q(region.coverage));
    }
    refreshMappingAccess();
}
void MainWindow::refreshMappingAccess() {
    const auto& regions = debugger_->description().regions;
    for (int r = 0; r < mapping_->rowCount(); ++r) {
        const auto& region = regions[static_cast<size_t>(r)];
        const auto access = regionAccess(&region);
        auto* item = mapping_->item(r, 3);
        item->setIcon(debugAccessIcon(access));
        item->setForeground(debugAccessColor(access));
        item->setToolTip(region.permissions == "MMIO" ? "Accès selon le registre" : debugAccessText(access));
    }
}
void MainWindow::refreshWatches() {
    monitorTabs_->setTabToolTip(0, QString("Expressions surveillées · %1").arg(watches_->rowCount()));
    for (int r = 0; r < watches_->rowCount(); ++r) {
        const auto* item = watches_->item(r, 0); if (!item) continue;
        const unsigned core = item->data(Qt::UserRole).toUInt();
        try { cell(watches_, r, 1, hx(debugger_->evaluate(item->text().toStdString(), core))); }
        catch (const Error& e) { cell(watches_, r, 1, "Indisponible", q(e.what())); }
        cell(watches_, r, 2, "Cœur " + QString::number(core));
    }
}
void MainWindow::refreshValueWatches() {
    valueWatches_->updateTarget(*debugger_);
    const int count = valueWatches_->count();
    monitorTabs_->setTabToolTip(monitorTabs_->indexOf(valueWatches_), QString("Suivis · %1").arg(count));
}
void MainWindow::showValueWatches() {
    monitorTabs_->setCurrentWidget(valueWatches_);
    revealDock(monitorDock_);
}
void MainWindow::openValueWatch(const ValueWatch& watch) {
    if (watch.device.isEmpty()) {
        openMemoryWatch(watch);
    } else if (auto* inspection = openPeripheral(watch.device, watch.core)) {
        if (watch.size) inspection->selectRegister(watch.address);
    }
}
void MainWindow::configurePeripheralPanel(PeripheralPanel& panel) {
    panel.openMemory = [this](uint32_t address, unsigned core) { openMemoryAt(address, core); };
    panel.isWatched = [this](const QString& device, uint32_t address, unsigned size, unsigned core) {
        return valueWatches_->contains({device, address, size, core});
    };
    panel.toggleWatch = [this](const QString& device, uint32_t address, unsigned size, unsigned core) {
        const ValueWatch watch{device, address, size, core};
        if (valueWatches_->contains(watch)) valueWatches_->remove(watch);
        else { valueWatches_->add(watch); showValueWatches(); }
    };
    panel.editRegister = [this, &panel](const QString& device, const RegisterDescription& reg, unsigned core, const QString& value) {
        editPeripheralRegister(panel, device, reg, core, value);
    };
}
void MainWindow::refreshPeripherals() {
    peripherals_->updateTarget(*debugger_, selected_, running_);
    if (explorer_) explorer_->updateTarget(*debugger_, running_);
    for (auto it = inspections_.begin(); it != inspections_.end();) {
        if (!*it) { it = inspections_.erase(it); continue; }
        (*it)->updateTarget(*debugger_, running_);
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
        applyPicoTheme(*explorer_);
        configurePeripheralPanel(explorer_->panel());
        explorer_->refreshRequested = [this] { refreshPeripherals(); };
        explorer_->injectUart = [this](unsigned uart, const std::string& bytes) {
            setRunning(false); debugger_->pause();
            const auto accepted = debugger_->injectUart(uart, bytes); refresh(); return accepted;
        };
        connect(explorer_, &QDialog::finished, this, [this] { explorer_ = nullptr; });
        refreshPeripherals();
    }
    showInspectionWindow(explorer_);
}
void MainWindow::advanceVirtualTime(uint64_t duration, bool timeOnly) {
    setRunning(false);
    if (debugger_->hardware() || !debugger_->loaded() || debugger_->terminalStop()) return;
    remainingTime_ = duration; timeOnly_ = timeOnly;
    setRunning(true); refresh();
}
PeripheralInspection* MainWindow::openPeripheral(const QString& name, unsigned core, bool duplicate) {
    if (name.isEmpty()) return nullptr;
    revealDock(peripheralDock_);
    if (!duplicate) {
        for (const auto& inspection : inspections_) {
            if (!inspection || inspection->panel().selectedDevice() != name) continue;
            if (inspection->core() != core) continue;
            const auto key = inspection->objectName().mid(QString("deviceInspection_").size());
            if (workspace_->revealInspection(key)) return inspection;
        }
        duplicate = workspace_->findChild<QWidget*>("deviceInspection_" + name) != nullptr;
    }
    const QString suffix = duplicate ? QString::number(++inspectionSequence_) : QString();
    const QString key = duplicate ? name + "#" + suffix : name;
    auto* inspection = new PeripheralInspection(name, core);
    inspection->setObjectName("deviceInspection_" + key);
    configurePeripheralPanel(inspection->panel());
    inspection->refreshRequested = [this] { refreshPeripherals(); };
    inspection->detachRequested = [this, key] { workspace_->toggleDetached(key); };
    inspection->duplicateRequested = [this, name, inspection] { openPeripheral(name, inspection->core(), true); };
    inspection->injectUart = [this](unsigned uart, const std::string& bytes) {
        setRunning(false);
        const auto accepted = debugger_->injectUart(uart, bytes); refresh(); return accepted;
    };
    inspections_.push_back(inspection);
    workspace_->addInspection(key, duplicate ? name + " · " + suffix : name, inspection,
        [inspection](bool detached) { inspection->setDetached(detached); });
    refreshPeripherals();
    return inspection;
}
void MainWindow::refreshPoints() {
    const auto count = debugger_->breakpoints.size() + debugger_->watchpoints.size();
    monitorTabs_->setTabToolTip(1, QString("Points d’arrêt et watchpoints · %1").arg(static_cast<qulonglong>(count)));
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
    try { debugger_->addBreakpoint(debugger_->evaluate(text.toStdString(), selected_) & ~1u); refresh(); monitorTabs_->setCurrentIndex(1); revealDock(monitorDock_); }
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
    try { debugger_->addWatchpoint(debugger_->evaluate(address.text().toStdString(), selected_), static_cast<uint32_t>(length.value()), mode.currentIndex() != 0, mode.currentIndex() != 1); refreshPoints(); monitorTabs_->setCurrentIndex(1); revealDock(monitorDock_); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::addExpression() {
    bool ok = false; const auto text = QInputDialog::getText(this, "Expression surveillée", "Expression (sans effet de bord), liée au cœur sélectionné", QLineEdit::Normal, "read32(0x20000000)", &ok);
    if (!ok || text.isEmpty()) return;
    const int r = watches_->rowCount(); watches_->setRowCount(r + 1); cell(watches_, r, 0, text); watches_->item(r, 0)->setData(Qt::UserRole, selected_); refreshWatches();
    monitorTabs_->setCurrentIndex(0); revealDock(monitorDock_);
}
void MainWindow::launchCore() {
    setRunning(false);
    QDialog dialog(this); dialog.setWindowTitle(debugger_->hardware() ? "Définir PC/SP du cœur arrêté" : "Démarrage explicite — sans Boot ROM"); QFormLayout layout(&dialog);
    ChoiceBox core; core.addItems({"Cœur 0", "Cœur 1"}); core.setCurrentIndex(1);
    QLineEdit entry("core1_entry"), sp("0x20042000"); QPushButton launch("Démarrer à l'arrêt");
    layout.addRow("Cœur", &core); layout.addRow("Entrée Thumb (bit 0 = 1)", &entry); layout.addRow("SP initial", &sp); layout.addRow(&launch);
    connect(&launch, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;
    try { debugger_->startCore(static_cast<unsigned>(core.currentIndex()), debugger_->evaluate(entry.text().toStdString()), debugger_->evaluate(sp.text().toStdString())); refresh(); log(q(debugger_->stop.message)); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::toggleTools(bool show) {
    if (show) {
        const bool hidden = inspectionDock_->isHidden();
        revealDock(inspectionDock_);
        if (hidden && !inspectionDock_->isFloating())
            resizeDocks({inspectionDock_}, {toolsHeight_}, Qt::Vertical);
    } else {
        toolsHeight_ = inspectionDock_->height();
        inspectionDock_->hide();
    }
}

void MainWindow::revealConsole() {
    inspectionTabs_->setCurrentWidget(consolePane_);
    toggleTools(true);
    consoleInput_->setFocus();
}

void MainWindow::arrangeMainViews(const QString& view) {
    const bool memory = view == "memory" || view == "split" || view == "all";
    const bool peripherals = view == "peripherals" || view == "split" || view == "all";
    if (memory) revealDock(memoryDock_); else memoryDock_->hide();
    if (peripherals) revealDock(peripheralDock_); else peripheralDock_->hide();
    if (!assemblySuppressed_) {
        if (view == "assembly" || view == "all") revealDock(assemblyDock_);
        else assemblyDock_->hide();
    }
}

void MainWindow::consoleCommand(const QString& command) {
    const auto line = command.trimmed();
    if (line.isEmpty()) return;
    const int sep = line.indexOf(QRegularExpression("\\s"));
    const auto name = (sep < 0 ? line : line.left(sep)).toLower();
    const auto args = sep < 0 ? QString() : line.mid(sep).trimmed();
    log("> " + line);
    try {
        if (name == "help" || name == "h") {
            log("break <adresse|symbole> · watch / rwatch / awatch <adresse|symbole>[, octets]");
            log("delete [breakpoints|watchpoints|adresse|symbole] · c / continue · n / next · s / step");
            log("print <expression> · bt / backtrace [0|1|all]. Sans cœur indiqué, bt prend le cœur sélectionné.");
        } else if (name == "bt" || name == "backtrace") {
            if (!args.isEmpty() && args != "0" && args != "1" && args != "all")
                throw Error(ErrorKind::InvalidArgument, "Syntaxe : bt [0|1|all]");
            const unsigned first = args == "all" || args == "0" ? 0 : args == "1" ? 1 : selected_;
            const unsigned last = args == "all" ? 1 : first;
            for (unsigned c = first; c <= last; ++c)
                if (!printBacktrace(c)) pendingBacktraces_.insert(c);
        } else if (name == "c" || name == "continue") {
            if (!args.isEmpty() || !runAction_->isEnabled())
                throw Error(ErrorKind::InvalidArgument, "c reprend la cible arrêtée, sans argument");
            runAction_->trigger();
        } else if (name == "n" || name == "next" || name == "s" || name == "step") {
            if (!args.isEmpty()) throw Error(ErrorKind::InvalidArgument, "n et s s’utilisent sans argument");
            consoleStep(name == "n" || name == "next");
        } else if (name == "b" || name == "break" || name == "watch" || name == "rwatch" || name == "awatch"
                   || name == "del" || name == "delete") {
            if (!debugger_->loaded() || running_ || debugger_->busy())
                throw Error(ErrorKind::InvalidArgument, "Il faut une cible à l’arrêt pour modifier les points");
            const auto addressOf = [this](QString text) {
                if (text.startsWith('*') || text.startsWith('&')) text.remove(0, 1);
                return debugger_->evaluate(text.trimmed().toStdString(), selected_);
            };
            if (name == "b" || name == "break") {
                const auto address = addressOf(args) & ~1u;
                debugger_->addBreakpoint(address);
                if (!debugger_->hardware()) log("Point d’arrêt à " + hx(address));
            } else if (name == "del" || name == "delete") {
                if (args.isEmpty() || args == "breakpoints") debugger_->clearBreakpoints();
                else if (args == "watchpoints") debugger_->clearWatchpoints();
                else debugger_->removeBreakpoint(addressOf(args));
                if (!debugger_->hardware()) log("Suppression des points demandés");
            } else {
                bool valid = true;
                const auto parts = args.split(',');
                const uint32_t length = parts.size() == 2 ? parts[1].trimmed().toUInt(&valid) : 4u;
                if (parts.size() > 2 || !valid || length == 0 || length > 65536)
                    throw Error(ErrorKind::InvalidArgument, "Syntaxe : watch <adresse|symbole>[, octets de 1 à 65536]");
                const auto address = addressOf(parts.front());
                debugger_->addWatchpoint(address, length, name != "watch", name != "rwatch");
                if (!debugger_->hardware()) log(QString("Watchpoint à %1 · %2 octets").arg(hx(address)).arg(length));
            }
            refresh();
        } else {
            evaluate(line);
        }
    } catch (const std::exception& e) {
        log("Erreur de commande · " + q(e.what()));
    }
}

void MainWindow::consoleStep(bool over) {
    if (!stepAction_->isEnabled() || !debugger_->registerAvailable(selected_, 15))
        throw Error(ErrorKind::InvalidArgument, "Sélectionnez un cœur arrêté dont le PC est disponible");
    const auto line = sourceMap_.location(debugger_->registerValue(selected_, 15) & ~1u);
    if (auto* hardware = dynamic_cast<GdbTarget*>(debugger_.get())) {
        hardware->stepSource(selected_, over, line.valid());
        refresh();
        return;
    }
    auto& simulator = static_cast<Debugger&>(*debugger_);
    const auto depth = simulator.callDepth(selected_);
    auto result = simulator.step(selected_);
    unsigned count = 1;
    while (result.kind == StopKind::Step) {
        const auto currentDepth = simulator.callDepth(selected_);
        const auto pc = simulator.cpus[selected_].r[15];
        if (simulator.breakpoints.count(pc)) {
            result = simulator.stopAtBreakpoint(selected_);
            break;
        }
        const bool finishCall = over && currentDepth > depth;
        if (!finishCall && (currentDepth != depth || !line.valid() || !(sourceMap_.location(pc) == line))) break;
        if (count == 4096) {
            log("Pas source suspendu après 4096 instructions");
            break;
        }
        result = simulator.step(selected_);
        ++count;
    }
    log(q(result.message));
    refresh();
}

bool MainWindow::printBacktrace(unsigned core) {
    try {
        const auto frames = debugger_->backtrace(core);
        log(QString("Backtrace · cœur %1").arg(core));
        for (size_t i = 0; i < frames.size(); ++i) {
            const auto& frame = frames[i];
            log(QString("  #%1 %2  %3%4").arg(static_cast<qulonglong>(i)).arg(hx(frame.pc),
                frame.function.empty() ? QString("??") : q(frame.function),
                frame.hasSp ? "  SP " + hx(frame.sp) : QString()));
        }
    } catch (const std::exception& e) {
        if (q(e.what()).startsWith("Backtrace GDB en cours")) return false;
        log(QString("Backtrace · cœur %1 · %2").arg(core).arg(q(e.what())));
    }
    return true;
}

void MainWindow::evaluate(const QString& expression) {
    auto text = expression.trimmed(); if (text.startsWith("print ")) text = text.mid(6); else if (text.startsWith("p ")) text = text.mid(2);
    try { const uint32_t value = debugger_->evaluate(text.toStdString(), selected_); log(QString("[cœur %1] %2 = %3 (%4)").arg(selected_).arg(text).arg(hx(value)).arg(value)); }
    catch (const std::exception& e) {
        if (debugger_->hardware() && q(e.what()).startsWith("Lecture matérielle en cours") && pendingExpressions_.size() < 32)
            pendingExpressions_.push_back({text, selected_});
        else log("Erreur d'expression : " + q(e.what()));
    }
}
void MainWindow::failure(const std::exception& error) {
    setRunning(false); log("Erreur : " + q(error.what())); refresh();
    QMessageBox::warning(this, "PicoDebugger", q(error.what()));
}
void MainWindow::log(const QString& message) { console_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message); }

void MainWindow::selectHardware(bool hardware) {
    if (hardware == debugger_->hardware() || closeStage_ != CloseStage::Idle) return;
    if (auto* hw = dynamic_cast<GdbTarget*>(debugger_.get())) {
        if (hw->busy()) { log("Attendre la fin de l'opération avant de changer de mode."); refresh(); return; }
        if (hw->connected()) {
            switchAfterDisconnect_ = true;
            try { hw->disconnectProbe(); } catch (const std::exception& e) { switchAfterDisconnect_ = false; failure(e); }
            refresh(); return;
        }
    }
    setRunning(false); timer_->stop(); closeInspections();
    pendingExpressions_.clear();
    pendingBacktraces_.clear();
    const Board board = debugger_->board().board;
    if (hardware) debugger_.reset(new GdbTarget(board)); else debugger_.reset(new Debugger(board));
    lastHardwareSymbols_ = 0;
    indexSource({});
    running_ = false; previousValid_ = false; lastHardwareRevision_ = 0; fillMapping(); refresh();
    log(hardware ? "Mode carte réelle. Cible → Connecter la sonde ; aucun téléversement automatique." : "Mode émulation, sans connexion matérielle.");
    if (!hardware && ideLaunchStage_ == IdeLaunchStage::Emulation) startIdeEmulation();
    else if (!hardware && !imagePath_.isEmpty()) loadFile(imagePath_);
}
void MainWindow::connectHardware(const ProbeOptions& options) {
    selectHardware(true);
    auto* hw = dynamic_cast<GdbTarget*>(debugger_.get()); if (!hw) return;
    hw->logMessage = [this](const QString& message) { log(message); };
    probeOptions_ = options; pendingSymbols_ = !imagePath_.isEmpty();
    try { hw->connectProbe(options); refresh(); } catch (const std::exception& e) { pendingSymbols_ = false; failure(e); }
}
void MainWindow::configureHardware() {
    const auto tools = picoide::Tools::detect();
    if (!tools.gdb.isEmpty()) probeOptions_.gdb = tools.gdb;
    if (!tools.openocd.isEmpty()) probeOptions_.openOcd = tools.openocd;
    if (!tools.openocdScripts.isEmpty()) probeOptions_.scripts = tools.openocdScripts;
    if (!configureProbe(this, probeOptions_)) return;
    connectHardware(probeOptions_);
}
void MainWindow::hardwareTick() {
    auto* hw = dynamic_cast<GdbTarget*>(debugger_.get()); if (!hw) return;
    hw->poll();
    if (closeStage_ == CloseStage::Waiting) {
        if (!hw->busy() && close()) refresh();
        return;
    }
    if (closeStage_ == CloseStage::Disconnecting) {
        if (hw->disconnecting()) return;
        if (!hw->connected()) {
            if (close()) refresh();
            return;
        }
        closeStage_ = CloseStage::Idle;
    }
    if (lastHardwareSymbols_ != hw->symbolRevision()) {
        lastHardwareSymbols_ = hw->symbolRevision();
        indexSource(hw->elfPath());
    }
    if (!hw->connected() && !hw->connecting() && !hw->busy()) {
        if (switchAfterDisconnect_) { switchAfterDisconnect_ = false; selectHardware(false); return; }
    }
    if (ideLaunchStage_ != IdeLaunchStage::Idle) updateIdeHardware(*hw);
    if (pendingSymbols_ && hw->loaded() && !hw->busy()) {
        pendingSymbols_ = false;
        try { hw->load(imagePath_.toStdString()); } catch (const std::exception& e) { log("Symboles non chargés : " + q(e.what())); }
    }
    if (hw->loaded() && !hw->busy() && !hw->elfPath().isEmpty()) {
        imagePath_ = hw->elfPath(); imageName_ = QFileInfo(imagePath_).fileName();
        setWindowTitle("PicoDebugger — " + imageName_);
    }
    if (lastHardwareRevision_ != hw->revision()) {
        lastHardwareRevision_ = hw->revision(); refresh();
        for (auto it = pendingBacktraces_.begin(); it != pendingBacktraces_.end();) {
            if (printBacktrace(*it)) it = pendingBacktraces_.erase(it);
            else ++it;
        }
        for (auto it = pendingExpressions_.begin(); it != pendingExpressions_.end();) {
            try {
                const auto value = hw->evaluate(it->first.toStdString(), it->second);
                log(QString("[cœur %1] %2 = %3 (%4)").arg(it->second).arg(it->first).arg(hx(value)).arg(value));
            } catch (const std::exception& e) {
                if (q(e.what()).startsWith("Lecture matérielle en cours")) { ++it; continue; }
                log("Erreur d'expression : " + q(e.what()));
            }
            it = pendingExpressions_.erase(it);
        }
    }
}
void MainWindow::downloadHardware() {
    QPointer<GdbTarget> hw(dynamic_cast<GdbTarget*>(debugger_.get()));
    if (!hw) return;
    bool ram = false;
    if (!chooseHardwareDownload(hw->elfPath(), hw->board().board, false, ram) || !hw) return;
    try { hw->download(ram); refresh(); } catch (const std::exception& e) { failure(e); }
}
bool MainWindow::chooseHardwareDownload(const QString& path, Board board, bool runAfter, bool& ram, QWidget* parent) {
    QDialog dialog(parent ? parent : this);
    dialog.setWindowTitle("Téléverser sur " + q(describeBoard(board).name)); dialog.setObjectName("downloadElfDialog");
    QVBoxLayout layout(&dialog);
    QLabel summary("Cette opération écrit la mémoire de la carte et peut remplacer son firmware.\nELF : " + path);
    summary.setTextFormat(Qt::PlainText); summary.setWordWrap(true); layout.addWidget(&summary);
    ChoiceBox startup; startup.setObjectName("downloadStartup");
    startup.addItems({"Flash · reset matériel après chargement", "RAM · démarrage direct depuis les vecteurs ELF"}); layout.addWidget(&startup);
    try {
        const auto image = ElfImage::read(path.toStdString());
        const uint64_t ramEnd = 0x20000000ull + describe(describeBoard(board).chip).ramSize;
        if (image.hasVectors && std::all_of(image.segments.begin(), image.segments.end(), [ramEnd](const Segment& segment) {
                return segment.bytes.empty() || (segment.address >= 0x20000000u &&
                    uint64_t(segment.address) + segment.bytes.size() <= ramEnd);
            })) startup.setCurrentIndex(1);
    } catch (const std::exception&) {
        // The target's existing ELF validation will report the error.
    }
    QLabel detail("Flash : utiliser un ELF amorçable pour la carte (par exemple Pico SDK).\n"
                  "RAM : ELF entièrement en SRAM, avec table de vecteurs SP/PC.\n"
                  + QString(runAfter ? "L'exécution démarre après un téléversement réussi." :
                                      "Après le téléversement, utiliser F5 pour lancer l'exécution."));
    detail.setWordWrap(true); layout.addWidget(&detail);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    ram = startup.currentIndex() == 1;
    return true;
}
void MainWindow::closeEvent(QCloseEvent* event) {
    switchAfterDisconnect_ = false;
    if (auto* hw = dynamic_cast<GdbTarget*>(debugger_.get())) {
        if (hw->connecting()) hw->disconnectProbe();
        if (hw->disconnecting()) {
            closeStage_ = CloseStage::Disconnecting;
            event->ignore();
            return;
        }
        if (hw->busy()) {
            if (closeStage_ == CloseStage::Idle) log("Fin du débogage après l’opération en cours.");
            closeStage_ = CloseStage::Waiting;
            event->ignore();
            return;
        }
        if (hw->connected()) {
            event->ignore();
            closeStage_ = CloseStage::Disconnecting;
            try { hw->disconnectProbe(); } catch (const std::exception& e) { closeStage_ = CloseStage::Idle; failure(e); }
            return;
        }
    } else if (running_) {
        setRunning(false);
        debugger_->pause();
        refresh();
    }
    closeStage_ = CloseStage::Idle;
    pendingSymbols_ = false;
    ideLaunchStage_ = IdeLaunchStage::Idle;
    ideElf_.clear();
    selectedSource_ = {};
    serialMonitor_->disconnectPort();
    for (const auto& view : stackInspectors_) if (view) view->close();
    saveLayout();
    layoutUsed_ = false;
    QMainWindow::closeEvent(event);
}

void MainWindow::sizeOverview() {
    const int cpuWidth = allCores_ ? 490 : 397;
    resizeDocks({cpuDock_, memoryDock_, monitorDock_}, {cpuWidth, std::max(440, width() - cpuWidth - defaultMonitorWidth - 12), defaultMonitorWidth}, Qt::Horizontal);
    const int height = contentsRect().height() - menuBar()->height() - statusBar()->height() - 80;
    const int center = std::max(640, height);
    const int assembly = assemblySuppressed_ ? 0 : 145;
    const int tools = 147;
    const int memory = (center - assembly - tools) * 46 / 100;
    resizeDocks({memoryDock_, peripheralDock_, assemblyDock_, inspectionDock_},
        {memory, center - assembly - tools - memory, assembly, tools}, Qt::Vertical);
}
void MainWindow::saveLayout() {
    if (!layoutUsed_) return;
    QSettings settings;
    settings.setValue(layoutSetting, saveState(layoutVersion));
    settings.setValue("debugger/monitorLayoutVersion", monitorLayoutVersion);
    settings.setValue("debugger/standaloneAssembly",
                      assemblySuppressed_ ? revealAssemblyAfterIde_ : !assemblyDock_->isHidden());
}

void MainWindow::showManual() {
    if (auto* existing = findChild<QDialog*>("userManualDialog")) {
        showInspectionWindow(existing);
        return;
    }
    loadDebuggerResources();
    QFile file(":/debugger/UserManual.md");
    if (!file.open(QIODevice::ReadOnly)) return;
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("userManualDialog");
    dialog->setWindowTitle("PicoDebugger · Manuel utilisateur");
    dialog->resize(820, 670);
    auto* layout = new QVBoxLayout(dialog);
    auto* browser = new QTextBrowser;
    browser->setMarkdown(QString::fromUtf8(file.readAll()));
    layout->addWidget(browser);
    showInspectionWindow(dialog);
}

void MainWindow::showProbeSerial() {
    serialMonitor_->refreshPorts();
    inspectionTabs_->setCurrentWidget(serialTab_);
    revealDock(inspectionDock_);
}

void MainWindow::showIde() {
    if (!ide_) {
        ownedIde_.reset(new picoide::IdeWindow(this, selectedBoard()));
        attachIde(ownedIde_.get());
    }
    showInspectionWindow(ide_);
    refreshSource(true);
}

void MainWindow::attachIde(picoide::IdeWindow* ide) {
    ide_ = ide;
    syncIdeState();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    layoutUsed_ = true;
    syncIdeState();
    refreshSource(true);
    if (defaultLayout_) {
        defaultLayout_ = false;
        compactMonitor_ = true;
        QTimer::singleShot(0, this, &MainWindow::sizeOverview);
    } else if (!compactMonitor_) {
        compactMonitor_ = true;
        QTimer::singleShot(0, this, [this] {
            if (!monitorDock_->isFloating() && monitorDock_->width() > defaultMonitorWidth)
                resizeDocks({monitorDock_, memoryDock_},
                            {defaultMonitorWidth, memoryDock_->width() + monitorDock_->width() - defaultMonitorWidth},
                            Qt::Horizontal);
        });
    }
}

void MainWindow::hideEvent(QHideEvent* event) {
    QMainWindow::hideEvent(event);
    syncIdeState();
    refreshSource(true);
}

void MainWindow::syncIdeState() {
    const bool ideVisible = ide_ && ide_->isVisible();
    ideLinkStatus_->setText(ideVisible ? "IDE relié" : "IDE fermé");
    if (ideVisible != assemblySuppressed_) {
        assemblySuppressed_ = ideVisible;
        if (ideVisible) {
            revealAssemblyAfterIde_ = !assemblyDock_->isHidden();
            assemblyDock_->hide();
        } else if (revealAssemblyAfterIde_) {
            assemblyDock_->show();
        }
        for (const auto* name : {"showDebuggerAssemblyAction", "codeViewAction", "float_assemblyDock", "focusAssemblyAction"})
            findChild<QAction*>(name)->setEnabled(!ideVisible);
    }
    if (!ide_) return;
    const bool debugging = isVisible();
    const bool hardwareReady = debugging && debugger_->hardware() && debugger_->loaded() &&
        !debugger_->busy() && !debugger_->executing() && ideLaunchStage_ == IdeLaunchStage::Idle;
    const bool inspectAssembly = debugging && debugger_->loaded() && !running_ && !debugger_->busy() &&
                                 sourceMap_.elfTimestamp().isValid();
    ide_->debuggerState(hardwareReady, debugging && pauseAction_->isEnabled(), debugging && stepAction_->isEnabled(),
                        debugging, inspectAssembly,
                        debugger_->board().board, modeStatus_->text());
}

void MainWindow::pauseFromIde() {
    if (pauseAction_->isEnabled()) pauseAction_->trigger();
}

void MainWindow::stepFromIde() {
    if (stepAction_->isEnabled()) stepAction_->trigger();
}

void MainWindow::refreshIdeSource() {
    refreshSource(true);
}

void MainWindow::finishIdeLaunch(const QString& message, bool error) {
    ideLaunchStage_ = IdeLaunchStage::Idle;
    ideElf_.clear();
    log(message);
    if (ide_) ide_->debuggerFeedback(message, error);
    refresh();
}

void MainWindow::startIdeEmulation() {
    selectBoard(ideBoard_);
    const auto image = ideElf_;
    if (!loadFile(image)) {
        finishIdeLaunch("Impossible de charger l'ELF dans l'émulateur.", true);
        return;
    }
    if (ideRun_) {
        if (!runAction_->isEnabled()) {
            finishIdeLaunch("L'émulateur a chargé l'ELF mais ne peut pas démarrer cette session.", true);
            return;
        }
        runAction_->trigger();
    }
    finishIdeLaunch(ideRun_ ? "ELF lancé dans l'émulateur." : "ELF chargé à l'arrêt dans le débogueur.");
}

void MainWindow::launchProjectElf(const QString& path, Board board, bool hardware, bool run) {
    if (ideLaunchStage_ != IdeLaunchStage::Idle) {
        if (ide_) ide_->debuggerFeedback("Un lancement est déjà en cours.", true);
        return;
    }
    QPointer<GdbTarget> target(dynamic_cast<GdbTarget*>(debugger_.get()));
    if (hardware) {
        if (!target || !target->loaded() || target->busy() || target->executing() || target->board().board != board) {
            finishIdeLaunch("Connecter et arrêter la carte correspondant au projet avant de lancer.", true);
            return;
        }
        if (!chooseHardwareDownload(path, board, run, ideRam_, ide_.data())) {
            finishIdeLaunch("Téléversement annulé.");
            return;
        }
        if (!target || !target->loaded() || target->busy() || target->executing()) {
            finishIdeLaunch("La carte n'est plus disponible pour le téléversement.", true);
            return;
        }
        ideElf_ = QFileInfo(path).absoluteFilePath();
        ideLaunchStage_ = IdeLaunchStage::Symbols;
        ideRun_ = run;
        pendingSymbols_ = false;
        try {
            target->load(ideElf_.toStdString());
            if (ide_) ide_->debuggerFeedback("Chargement des symboles avant téléversement…");
            showInspectionWindow(this);
            refresh();
        } catch (const std::exception& error) {
            finishIdeLaunch(q(error.what()), true);
        }
        return;
    }
    if (target && target->busy()) {
        finishIdeLaunch("Attendre la fin de l'opération sur la carte avant de démarrer l'émulation.", true);
        return;
    }
    ideElf_ = path;
    ideBoard_ = board;
    ideRun_ = run;
    ideLaunchStage_ = IdeLaunchStage::Emulation;
    showInspectionWindow(this);
    if (target) selectHardware(false);
    else startIdeEmulation();
}

void MainWindow::updateIdeHardware(GdbTarget& target) {
    if (target.busy()) return;
    if (ideLaunchStage_ == IdeLaunchStage::Emulation) {
        if (target.connected() && !target.errorText().isEmpty()) {
            switchAfterDisconnect_ = false;
            finishIdeLaunch("Impossible de déconnecter la carte : " + target.errorText(), true);
        }
        return;
    }
    if (!target.loaded() || !target.errorText().isEmpty()) {
        const auto reason = target.errorText().isEmpty() ? QString("Connexion perdue.") : target.errorText();
        finishIdeLaunch(reason, true);
        return;
    }
    try {
        switch (ideLaunchStage_) {
        case IdeLaunchStage::Symbols:
            if (target.elfPath() != ideElf_) throw Error(ErrorKind::InvalidImage, "Le fichier ELF n'a pas été chargé par GDB.");
            target.download(ideRam_);
            ideLaunchStage_ = IdeLaunchStage::Download;
            if (ide_) ide_->debuggerFeedback("Téléversement sur la carte…");
            break;
        case IdeLaunchStage::Download:
            if (target.terminalStop()) throw Error(ErrorKind::InvalidImage, "Le téléversement n'a pas abouti.");
            if (ideRun_) {
                target.run(0);
                ideLaunchStage_ = IdeLaunchStage::Run;
            } else finishIdeLaunch("ELF téléversé. Carte arrêtée.");
            break;
        case IdeLaunchStage::Run:
            finishIdeLaunch("ELF lancé sur la carte.");
            break;
        default:
            break;
        }
    } catch (const std::exception& error) {
        finishIdeLaunch(q(error.what()), true);
    }
}

void MainWindow::indexSource(const QString& elf) {
    for (const auto& view : stackInspectors_) if (view) view->close();
    stackFrames_ = {};
    stackSelections_ = {};
    for (auto& samples : stackValueChanges_) samples.clear();
    executingSource_ = {};
    selectedSource_ = {};
    if (!elf.isEmpty()) {
        const QSignalBlocker blocked(followPc_);
        followPc_->setChecked(true);
    }
    const auto compiler = elf.isEmpty() ? QString() : picoide::Tools::detect().gcc;
    sourceMap_.index(elf, compiler);
    stackInfo_.index(elf, compiler);
    registerSymbols_.clear();
    for (const auto& symbol : debugger_->image.symbols) registerSymbols_.emplace(symbol.second, q(symbol.first));
    refreshSource(true);
}

void MainWindow::refreshSource(bool force, bool showSelection) {
    const bool hasPc = debugger_->registerAvailable(selected_, 15) && !debugger_->cpus[selected_].held;
    const uint32_t pc = hasPc ? debugger_->cpus[selected_].r[15] & ~1u : 0;
    const auto current = hasPc ? sourceMap_.location(pc) : picosource::Location();
    if (ide_) {
        if (isVisible()) ide_->debuggerSourceState(current, selectedSource_, sourceMap_, hasPc, pc, force, showSelection);
        else ide_->debuggerSourceState({}, {}, sourceMap_, false, 0, false, false);
    }
    executingSource_ = current;
}

bool MainWindow::selectSourceLine(const picosource::Location& location, bool navigate) {
    if (!isVisible()) return false;
    uint32_t address = 0;
    const bool mapped = sourceMap_.addressFor(location, address);
    if (navigate && (!mapped || (debugger_->hardware() && (debugger_->busy() || debugger_->executing())))) return false;
    if (!navigate && location == selectedSource_) return mapped;
    selectedSource_ = location;
    if (navigate) {
        const QSignalBlocker blocked(followPc_);
        followPc_->setChecked(false);
        assemblyAddress_->setText(hx(address));
    }
    refreshSource(true, navigate);
    refreshAssembly();
    return mapped;
}
