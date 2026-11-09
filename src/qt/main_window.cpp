#include "main_window.hpp"
#include "peripheral_panel.hpp"
#include "window_policy.hpp"
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
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
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
    setWindowTitle("PicoDebugger — ARM / Thumb"); resize(1500, 980);
    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    // Keep the side columns full-height. Memory and console occupy the bottom
    // of the center column, rather than pushing registers off a short screen.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setStyleSheet(
        "QMainWindow,QDialog,QDockWidget {background:#111923;color:#dce6f0;}"
        "QWidget {color:#dce6f0;}"
        "QMenuBar,QMenu,QToolBar,QStatusBar {background:#182331;}"
        "QMenu::item:selected {background:#28536b;}"
        "QDockWidget::title {background:#213044;padding:7px;font-weight:600;}"
        "QTableWidget,QTreeWidget,QPlainTextEdit {background:#131e2b;alternate-background-color:#192635;selection-background-color:#28536b;border:1px solid #304052;}"
        "QHeaderView::section {background:#213044;color:#a5bdd3;padding:5px;border:none;}"
        "QLineEdit,QComboBox,QSpinBox {background:#111923;color:#dce6f0;border:1px solid #40536a;border-radius:3px;padding:5px;}"
        "QComboBox QAbstractItemView {background:#213044;color:#dce6f0;selection-background-color:#365f7f;}"
        "QPushButton {background:#29445c;border:1px solid #405b75;border-radius:3px;padding:5px 9px;}"
        "QPushButton:hover {background:#365f7f;}"
        "QToolButton {padding:7px;} QToolButton:hover {background:#29445c;}"
        "QToolButton:disabled {color:#637487;}"
        "QTabBar::tab {background:#213044;padding:7px;} QTabBar::tab:selected {background:#36516b;}"
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
    auto* overview = views->addAction("Rétablir la vue d'ensemble");
    overview->setObjectName("overviewAction");
    overview->setShortcut(QKeySequence("Ctrl+Shift+0"));
    connect(overview, &QAction::triggered, this, [this] { restoreState(overviewState_); });
    views->addSeparator();
    auto* debug = menuBar()->addMenu("Débogage");
    auto* help = menuBar()->addMenu("Aide");
    auto* toolbar = addToolBar("Exécution"); toolbar->setObjectName("execution"); toolbar->setMovable(false);
    auto* open = file->addAction("Ouvrir un ELF…"); open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        setRunning(false);
        const auto path = QFileDialog::getOpenFileName(this, "Exécutable ELF32 ARM", QSettings().value("lastDirectory").toString(), "ELF (*.elf *.axf);;Tous les fichiers (*)");
        if (!path.isEmpty()) loadFile(path);
    });
    auto* demo = file->addAction("Exemple intégré"); connect(demo, &QAction::triggered, this, [this] { loadDemo(); });
    file->addSeparator(); connect(file->addAction("Quitter"), &QAction::triggered, this, &QWidget::close);
    toolbar->addAction(open); toolbar->addAction(demo); toolbar->addSeparator();
    chip_ = new QComboBox; chip_->addItems({"Pico / RP2040", "Pico 2 / RP2350 (expérimental)"});
    chip_->setObjectName("chipSelector");
    toolbar->addWidget(chip_);
    connect(chip_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int index) {
        setRunning(false); closeInspections(); debugger_.reset(new Debugger(index == 0 ? Chip::RP2040 : Chip::RP2350));
        previousValid_ = false; fillMapping(); refresh(); log("Profil changé : charger un ELF pour ce profil.");
    });
    toolbar->addSeparator();
    core_ = new QComboBox; core_->addItems({"Contexte : cœur 0", "Contexte : cœur 1"}); toolbar->addWidget(core_);
    core_->setObjectName("coreSelector");
    connect(core_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int index) { selected_ = static_cast<unsigned>(index); refresh(); });
    stepAction_ = debug->addAction("Pas instruction"); stepAction_->setShortcut(QKeySequence(Qt::Key_F10));
    stepAction_->setObjectName("stepAction");
    connect(stepAction_, &QAction::triggered, this, [this] { singleStep(); });
    runAction_ = debug->addAction("Exécuter les cœurs"); runAction_->setShortcut(QKeySequence(Qt::Key_F5));
    runAction_->setObjectName("runAction");
    connect(runAction_, &QAction::triggered, this, [this] { setRunning(true); });
    pauseAction_ = debug->addAction("Pause"); pauseAction_->setShortcut(QKeySequence("Shift+F5"));
    pauseAction_->setObjectName("pauseAction");
    connect(pauseAction_, &QAction::triggered, this, [this] { setRunning(false); debugger_->pause(); refresh(); });
    resetAction_ = debug->addAction("Reset / restart"); resetAction_->setShortcut(QKeySequence("Ctrl+R"));
    resetAction_->setObjectName("resetAction");
    connect(resetAction_, &QAction::triggered, this, [this] { setRunning(false); try { debugger_->reset(); previousValid_ = false; refresh(); log(q(debugger_->stop.message)); } catch (const std::exception& e) { failure(e); } });
    toolbar->addAction(stepAction_); toolbar->addAction(runAction_); toolbar->addAction(pauseAction_); toolbar->addAction(resetAction_);
    auto* launch = debug->addAction("Démarrer un cœur explicitement…"); connect(launch, &QAction::triggered, this, [this] { launchCore(); });
    auto* bp = debug->addAction("Ajouter un point d'arrêt…"); bp->setShortcut(QKeySequence(Qt::Key_F9)); connect(bp, &QAction::triggered, this, [this] { addBreakpoint(); });
    auto* wp = debug->addAction("Ajouter un watchpoint mémoire…"); connect(wp, &QAction::triggered, this, [this] { addMemoryWatch(); });
    connect(help->addAction("Couverture et limites"), &QAction::triggered, this, [this] {
        QMessageBox::information(this, "Couverture de cette version",
            "Exécution locale sans carte, ARM/Thumb. Modèle fonctionnel partiel.\n\n"
            "RP2040 : instructions Thumb usuelles, BL, MRS/MSR, pile, HardFault/SVC, SysTick simplifié, timer/alarmes en temps virtuel, SRAM, Flash, FIFO et spinlocks.\n"
            "RP2350 : mémoire et sous-ensemble Thumb ; Cortex-M33 complet, TrustZone, FPU et exceptions non émulés.\n"
            "Périphériques partiels des deux profils : timers, UART, SPI loopback, PWM, latches GPIO, resets, NVIC/SysTick et scratch watchdog.\n\n"
            "Pas de Boot ROM, SDK complet, DMA, PIO, USB, horloges ni timing cycle par cycle. Un accès hors couverture arrête l'exécution.\n"
            "Le cœur 1 se lance explicitement ; le protocole de lancement ROM n'est pas émulé.\n"
            "Le reset recharge l'ELF et remet la RAM à zéro : ce n'est pas le reset matériel du SoC.\n\n"
            "La pile est une vue mémoire brute, pas une reconstruction des appels C++. Les symboles ELF sont disponibles ; DWARF/source viendra ensuite.\n"
            "Voir README.md et docs/COVERAGE.md dans le projet.");
    });
    banner_ = new QLabel("SIMULATION PARTIELLE  •  ARM / THUMB  •  HORS LIGNE");
    banner_->setStyleSheet("background:#49391d;color:#ffda8b;padding:8px;font-weight:600;");
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
    });
    auto* assemblyPane = vertical(row({new QLabel("Assembleur — instruction à PC"), assemblyAddress_, disassemble, followPc_}), assembly_);
    source_ = new QPlainTextEdit; source_->setReadOnly(true); source_->setMaximumWidth(380);
    source_->setPlainText("Correspondance C++ — itération 2\n\nLes symboles ELF sont affichés à côté des instructions.\n\nL'import des sources et la lecture DWARF seront ajoutés dans une couche séparée.");
    setCentralWidget(vertical(banner_, assemblyPane));
    auto* cpuPane = new QSplitter;
    for (unsigned c = 0; c < 2; ++c) {
        registers_[c] = table({"Registre", "Valeur"}); flags_[c] = new QLabel; flags_[c]->setWordWrap(true);
        registers_[c]->setObjectName(QString("registers%1Table").arg(c));
        // Never squeeze a 32-bit register into an elided value: preserve the
        // full hexadecimal width, with scrolling if the user narrows the dock.
        registers_[c]->horizontalHeader()->setStretchLastSection(false);
        cpuPane->addWidget(vertical(flags_[c], registers_[c]));
    }
    auto* cpuDock = dock(this, "CPU — deux cœurs", cpuPane, Qt::LeftDockWidgetArea); cpuDock->setMinimumWidth(490);
    memory_ = table({"Adresse", "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F", "ASCII"});
    memory_->setObjectName("memoryTable");
    memoryAddress_ = new QLineEdit("0x20000000"); memoryLength_ = new QSpinBox; memoryLength_->setRange(16, 4096); memoryLength_->setSingleStep(16); memoryLength_->setValue(256);
    auto* showMemory = new QPushButton("Lire");
    connect(showMemory, &QPushButton::clicked, this, [this] { refreshMemory(); });
    connect(memoryAddress_, &QLineEdit::returnPressed, showMemory, &QPushButton::click);
    auto* memoryDock = dock(this, "Mémoire partagée", vertical(row({new QLabel("Adresse"), memoryAddress_, new QLabel("Octets"), memoryLength_, showMemory}), memory_), Qt::BottomDockWidgetArea);
    stack_ = table({"", "Adresse pile", "Mot 32 bits", "Symbole proche / région"});
    stack_->setObjectName("stackTable");
    auto* stackDock = dock(this, "Pile du cœur sélectionné (brute)", stack_, Qt::LeftDockWidgetArea);
    splitDockWidget(cpuDock, stackDock, Qt::Vertical);
    mapping_ = table({"Début", "Fin incluse", "Région", "Accès", "Couverture"});
    mapping_->setObjectName("mappingTable");
    auto* mappingDock = dock(this, "Cartographie / périphériques", mapping_, Qt::RightDockWidgetArea);
    peripherals_ = new PeripheralPanel;
    peripherals_->openDevice = [this](const QString& name) { openPeripheral(name); };
    auto* peripheralDock = dock(this, "Périphériques — catalogue matériel", peripherals_, Qt::RightDockWidgetArea);
    splitDockWidget(mappingDock, peripheralDock, Qt::Vertical);
    auto* peripheralWindow = views->addAction("Ouvrir la fenêtre des périphériques");
    peripheralWindow->setObjectName("peripheralWindowAction");
    peripheralWindow->setShortcut(QKeySequence("Ctrl+P"));
    connect(peripheralWindow, &QAction::triggered, this, [peripheralDock] {
        peripheralDock->setFloating(true); peripheralDock->resize(1120, 900);
        showInspectionWindow(peripheralDock);
    });
    connect(mapping_, &QTableWidget::cellDoubleClicked, this, [this, memoryDock](int line, int) {
        if (!mapping_->item(line, 0)) return;
        memoryAddress_->setText(mapping_->item(line, 0)->text()); refreshMemory(); showInspectionWindow(memoryDock);
    });
    watches_ = table({"Expression", "Valeur", "Contexte"});
    watches_->setObjectName("watchesTable");
    auto* addWatch = new QPushButton("+ Expression"); auto* removeWatch = new QPushButton("Supprimer");
    connect(addWatch, &QPushButton::clicked, this, [this] { addExpression(); });
    connect(removeWatch, &QPushButton::clicked, this, [this] { if (watches_->currentRow() >= 0) watches_->removeRow(watches_->currentRow()); });
    auto* watchesDock = dock(this, "Expressions surveillées", vertical(row({addWatch, removeWatch}), watches_), Qt::RightDockWidgetArea);
    splitDockWidget(peripheralDock, watchesDock, Qt::Vertical);
    points_ = table({"Type", "Adresse", "Taille / accès"});
    points_->setObjectName("pointsTable");
    auto* addPoint = new QPushButton("+ Break"); auto* addMemory = new QPushButton("+ Watch"); auto* removePoint = new QPushButton("Supprimer");
    connect(addPoint, &QPushButton::clicked, this, [this] { addBreakpoint(); });
    connect(addMemory, &QPushButton::clicked, this, [this] { addMemoryWatch(); });
    connect(removePoint, &QPushButton::clicked, this, [this] {
        const int r = points_->currentRow(); if (r < 0) return;
        const auto* item = points_->item(r, 0); const auto value = item->data(Qt::UserRole).toUInt();
        if (item->text() == "Break") debugger_->breakpoints.erase(value);
        else if (value < debugger_->watchpoints.size()) debugger_->watchpoints.erase(debugger_->watchpoints.begin() + value);
        refreshPoints(); refreshAssembly();
    });
    auto* pointsDock = dock(this, "Points d'arrêt / watchpoints", vertical(row({addPoint, addMemory, removePoint}), points_), Qt::RightDockWidgetArea);
    splitDockWidget(watchesDock, pointsDock, Qt::Vertical);
    console_ = new QPlainTextEdit; console_->setReadOnly(true); console_->setMaximumBlockCount(2000); console_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    console_->setObjectName("consoleView");
    auto* expression = new QLineEdit; expression->setPlaceholderText("print read32(counter), r0 + 4, *sp… Entrée pour évaluer");
    connect(expression, &QLineEdit::returnPressed, this, [this, expression] { evaluate(expression->text()); expression->clear(); });
    auto* consoleDock = dock(this, "Console / print / diagnostics", vertical(expression, console_), Qt::BottomDockWidgetArea);
    splitDockWidget(memoryDock, consoleDock, Qt::Vertical);
    // Source import is a future feature. Keep it available through the Views
    // menu, without reserving screen area needed by live chip inspection.
    auto* sourceDock = dock(this, "Source C++ — prochaine itération", source_, Qt::RightDockWidgetArea);
    sourceDock->hide();
    stop_ = new QLabel; stop_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(stop_, 1);
    resizeDocks({cpuDock, mappingDock}, {510, 420}, Qt::Horizontal);
    resizeDocks({cpuDock, stackDock}, {580, 240}, Qt::Vertical);
    resizeDocks({mappingDock, peripheralDock, watchesDock, pointsDock}, {180, 370, 145, 145}, Qt::Vertical);
    resizeDocks({memoryDock, consoleDock}, {230, 130}, Qt::Vertical);
    overviewState_ = saveState();
}
void MainWindow::loadFile(const QString& path) {
    setRunning(false);
    try {
        debugger_->load(path.toStdString()); previousValid_ = false;
        QSettings().setValue("lastDirectory", QFileInfo(path).absolutePath());
        setWindowTitle("PicoDebugger — " + QFileInfo(path).fileName());
        log("ELF chargé : " + path); log(q(debugger_->stop.message)); refresh();
    } catch (const std::exception& e) { failure(e); }
}
void MainWindow::selectChip(Chip chip) { chip_->setCurrentIndex(chip == Chip::RP2350 ? 1 : 0); }
void MainWindow::loadDemo() {
    setRunning(false);
    try { debugger_->loadImage(demoImage()); previousValid_ = false; log("Exemple chargé. Pour le cœur 1 : entrée core1_entry, pile 0x20042000."); refresh(); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::singleStep() {
    setRunning(false);
    try { debugger_->step(selected_); refresh(); if (debugger_->stop.kind != StopKind::Step) log(q(debugger_->stop.message)); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::setRunning(bool running) {
    running_ = running && debugger_->loaded() && !debugger_->terminalStop();
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
            const Stop result = debugger_->run(200);
            if (result.kind != StopKind::Budget) { setRunning(false); log(q(result.message)); break; }
        } while (budget.elapsed() < 6);
        refresh();
    } catch (const std::exception& e) { failure(e); }
}
void MainWindow::refresh() {
    setRunning(running_);
    banner_->setText(debugger_->loaded() ? "SIMULATION PARTIELLE • ELF DIRECT • SANS BOOT ROM" : "SIMULATION PARTIELLE • ARM / THUMB • HORS LIGNE");
    refreshRegisters(); refreshAssembly(); refreshMemory(); refreshStack(); refreshWatches(); refreshPoints(); refreshPeripherals();
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    stop_->setText(QString("%1  |  cœur %2  |  %3  |  révision %4  |  %5")
        .arg(running_ ? "EN EXÉCUTION — instantané échantillonné" : q(stopName(debugger_->stop.kind)))
        .arg(debugger_->stop.core).arg(q(debugger_->stop.message)).arg(static_cast<qulonglong>(debugger_->revision())).arg(stamp));
    stop_->setToolTip(stop_->text());
}
void MainWindow::refreshRegisters() {
    for (unsigned c = 0; c < 2; ++c) {
        const Cpu& cpu = debugger_->cpus[c]; auto* widget = registers_[c]; widget->setRowCount(21);
        for (unsigned r = 0; r < 21; ++r) {
            const uint32_t extras[] = {cpu.xpsr, cpu.msp, cpu.psp, cpu.control, cpu.primask};
            const char* names[] = {"xPSR", "MSP", "PSP", "CONTROL", "PRIMASK"};
            const uint32_t value = r < 16 ? cpu.r[r] : extras[r - 16];
            cell(widget, static_cast<int>(r), 0, r < 16 ? q(registerName(r)) : names[r - 16]);
            cell(widget, static_cast<int>(r), 1, hx(value));
            widget->item(static_cast<int>(r), 1)->setForeground(previousValid_ && value != previous_[c][r] ? QColor("#ffd780") : QColor("#dce6f0"));
            previous_[c][r] = value;
        }
        flags_[c]->setText(QString("CŒUR %1  •  %2\n%3\n%4 instructions").arg(c)
            .arg(cpu.held ? "RETENU" : cpu.lockup ? "LOCKUP" : cpu.sleeping ? "SOMMEIL" : running_ ? "ACTIF" : "ARRÊTÉ")
            .arg(q(cpu.flags())).arg(static_cast<qulonglong>(cpu.instructions)));
    }
    previousValid_ = true;
}
void MainWindow::refreshAssembly() {
    assembly_->setRowCount(0);
    if (!debugger_->loaded()) return;
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
    mapping_->setRowCount(0);
    for (const auto& region : debugger_->memory.description().regions) {
        const int r = mapping_->rowCount(); mapping_->setRowCount(r + 1);
        cell(mapping_, r, 0, hx(region.begin)); cell(mapping_, r, 1, hx(static_cast<uint32_t>(region.end - 1)));
        cell(mapping_, r, 2, q(region.name)); cell(mapping_, r, 3, q(region.permissions)); cell(mapping_, r, 4, q(region.coverage));
    }
}
void MainWindow::refreshWatches() {
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
    for (auto it = inspections_.begin(); it != inspections_.end();) {
        if (!it->window) { it = inspections_.erase(it); continue; }
        it->panel->updateTarget(*debugger_, static_cast<unsigned>(it->core->currentIndex()));
        if (it->output) {
            const auto& bytes = debugger_->memory.uartOutput(static_cast<unsigned>(it->uart));
            const auto text = QString::fromLatin1(bytes.data(), static_cast<int>(bytes.size()));
            if (it->output->toPlainText() != text) it->output->setPlainText(text);
        }
        ++it;
    }
}
void MainWindow::closeInspections() {
    for (auto& inspection : inspections_) if (inspection.window) inspection.window->close();
    inspections_.clear();
}
void MainWindow::openPeripheral(const QString& name) {
    if (name.isEmpty()) return;
    auto* window = new QDialog(this, Qt::Window);
    window->setObjectName("deviceWindow_" + name);
    window->setAttribute(Qt::WA_DeleteOnClose); prepareInspectionWindow(window);
    window->setWindowTitle(name + " — " + chip_->currentText());
    window->resize(780, name.startsWith("UART") ? 650 : 470);
    auto* layout = new QVBoxLayout(window);
    auto* context = new QComboBox; context->setObjectName("inspectionCore");
    context->addItems({"Contexte : cœur 0", "Contexte : cœur 1"}); context->setCurrentIndex(static_cast<int>(selected_));
    layout->addWidget(context);
    auto* panel = new PeripheralPanel(window, name); layout->addWidget(panel, 1);
    QPlainTextEdit* output = nullptr;
    const int uart = name == "UART0" ? 0 : name == "UART1" ? 1 : -1;
    if (uart >= 0) {
        layout->addWidget(new QLabel("Sortie TX — derniers octets émis"));
        output = new QPlainTextEdit; output->setObjectName("uartOutput"); output->setReadOnly(true); output->setMaximumHeight(130); layout->addWidget(output);
        auto* input = new QLineEdit; input->setObjectName("uartInput"); input->setPlaceholderText("Octets RX en texte UTF-8 (sans ajout de retour ligne)");
        auto* send = new QPushButton("Injecter RX"); send->setObjectName("uartSend");
        layout->addWidget(row({input, send}));
        auto* result = new QLabel; result->setObjectName("uartInputResult"); layout->addWidget(result);
        connect(send, &QPushButton::clicked, this, [this, input, result, uart] {
            setRunning(false);
            try {
                const auto bytes = input->text().toUtf8();
                const auto accepted = debugger_->injectUart(static_cast<unsigned>(uart), std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
                result->setText(QString("%1 / %2 octets acceptés (UART activé et FIFO disponible)").arg(static_cast<qulonglong>(accepted)).arg(bytes.size()));
                refresh();
            } catch (const std::exception& error) { result->setText(error.what()); }
        });
        connect(input, &QLineEdit::returnPressed, send, &QPushButton::click);
    }
    inspections_.push_back({window, panel, context, output, uart});
    connect(context, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] { refreshPeripherals(); });
    refreshPeripherals(); showInspectionWindow(window);
}
void MainWindow::refreshPoints() {
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
    try { debugger_->breakpoints.insert(debugger_->evaluate(text.toStdString(), selected_) & ~1u); refresh(); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::addMemoryWatch() {
    setRunning(false);
    QDialog dialog(this); dialog.setWindowTitle("Watchpoint mémoire"); QFormLayout layout(&dialog);
    QLineEdit address("0x20000000"); QSpinBox length; length.setRange(1, 65536); length.setValue(4);
    QComboBox mode; mode.addItems({"Écriture", "Lecture", "Lecture et écriture"}); QPushButton add("Ajouter");
    layout.addRow("Adresse / symbole", &address); layout.addRow("Octets", &length); layout.addRow("Accès", &mode); layout.addRow(&add);
    connect(&add, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;
    try { debugger_->addWatchpoint(debugger_->evaluate(address.text().toStdString(), selected_), static_cast<uint32_t>(length.value()), mode.currentIndex() != 0, mode.currentIndex() != 1); refreshPoints(); }
    catch (const std::exception& e) { failure(e); }
}
void MainWindow::addExpression() {
    bool ok = false; const auto text = QInputDialog::getText(this, "Expression surveillée", "Expression (sans effet de bord), liée au cœur sélectionné", QLineEdit::Normal, "read32(0x20000000)", &ok);
    if (!ok || text.isEmpty()) return;
    const int r = watches_->rowCount(); watches_->setRowCount(r + 1); cell(watches_, r, 0, text); watches_->item(r, 0)->setData(Qt::UserRole, selected_); refreshWatches();
}
void MainWindow::launchCore() {
    setRunning(false);
    QDialog dialog(this); dialog.setWindowTitle("Démarrage explicite — sans Boot ROM"); QFormLayout layout(&dialog);
    QComboBox core; core.addItems({"Cœur 0", "Cœur 1"}); core.setCurrentIndex(1);
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
