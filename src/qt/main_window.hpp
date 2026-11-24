#pragma once
#include "picodbg/debugger.hpp"
#include "picodbg/peripherals.hpp"
#include "gdb_target.hpp"
#include "source_map.hpp"
#include "stack_view.hpp"
#include <QByteArray>
#include <QMainWindow>
#include <QPointer>
#include <QVariantMap>
#include <array>
#include <memory>
#include <set>

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QHideEvent;
class QShowEvent;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class PeripheralPanel;
class PeripheralInspection;
class InspectionWorkspace;
class PeripheralExplorer;
class VirtualTimePanel;
class BoardPanel;
class BoardLed;
class ProbeSerialMonitor;
class ValueWatches;
class MemoryTable;
struct ValueWatch;
namespace picoide { class IdeWindow; }

class MainWindow : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;
    bool loadFile(const QString& path);
    void loadDemo();
    void selectChip(picodbg::Chip chip);
    void selectBoard(picodbg::Board board);
    void selectHardware(bool hardware);
    void connectHardware(const picodbg::ProbeOptions& options);
    void showIde();
    void attachIde(picoide::IdeWindow* ide);
    void setLayoutProject(const QString& root);
    void saveLayout();
    void syncIdeState();
    void pauseFromIde();
    void stepFromIde();
    void refreshIdeSource();
    bool selectSourceLine(const picosource::Location& location, bool navigate);
    picodbg::Board selectedBoard() const { return debugger_->board().board; }
    void launchProjectElf(const QString& path, picodbg::Board board, bool hardware, bool run);
protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    std::unique_ptr<picodbg::DebugTarget> debugger_;
    std::unique_ptr<picoide::IdeWindow> ownedIde_;
    ProbeSerialMonitor* serialMonitor_;
    QPointer<picoide::IdeWindow> ide_;
    void showProbeSerial();
    enum class IdeLaunchStage { Idle, Emulation, Symbols, Download, Run };
    IdeLaunchStage ideLaunchStage_ = IdeLaunchStage::Idle;
    QString ideElf_;
    picodbg::Board ideBoard_ = picodbg::Board::Pico;
    bool ideRam_ = false;
    bool ideRun_ = false;
    void finishIdeLaunch(const QString& message, bool error = false);
    void startIdeEmulation();
    void updateIdeHardware(picodbg::GdbTarget& target);
    bool chooseHardwareDownload(const QString& path, picodbg::Board board, bool runAfter, bool& ram,
                                QWidget* parent = nullptr);
    picodbg::ProbeOptions probeOptions_;
    std::vector<std::pair<QString, unsigned>> pendingExpressions_;
    std::set<unsigned> pendingBacktraces_;
    QAction* emulationAction_ = nullptr;
    QAction* hardwareAction_ = nullptr;
    QAction* connectAction_ = nullptr;
    QAction* disconnectAction_ = nullptr;
    QAction* downloadAction_ = nullptr;
    QAction* refreshTargetAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* demoAction_ = nullptr;
    QLabel* modeStatus_ = nullptr;
    QLabel* ideLinkStatus_ = nullptr;
    QString imagePath_;
    bool pendingSymbols_ = false, switchAfterDisconnect_ = false;
    enum class CloseStage { Idle, Waiting, Disconnecting };
    CloseStage closeStage_ = CloseStage::Idle;
    uint64_t lastHardwareRevision_ = 0;
    uint64_t lastHardwareSymbols_ = 0;
    picosource::SourceMap sourceMap_;
    StackDebugInfo stackInfo_;
    std::map<uint32_t, QString> registerSymbols_;
    picosource::Location executingSource_, selectedSource_;
    void indexSource(const QString& elf);
    void refreshSource(bool force = false, bool showSelection = false);
    void hardwareTick();
    void configureHardware();
    void downloadHardware();
    std::array<std::array<uint32_t, 21>, 2> previous_{};
    std::array<std::array<bool, 21>, 2> previousKnown_{};
    std::array<std::array<qint64, 21>, 2> registerHighlightUntil_{};
    bool previousValid_ = false;
    bool running_ = false;
    bool timeOnly_ = false;
    uint64_t remainingTime_ = 0;
    unsigned selected_ = 0;
    bool allCores_ = false;
    QByteArray overviewState_;
    QString layoutProject_;
    bool defaultLayout_ = true;
    bool layoutUsed_ = false;
    bool compactMonitor_ = false;
    bool assemblySuppressed_ = false;
    bool revealAssemblyAfterIde_ = false;
    int toolsHeight_ = 190;
    QTimer* timer_;
    QComboBox* chip_;
    QComboBox* core_;
    QLabel* banner_;
    QLabel* session_;
    QLabel* guidance_;
    QString imageName_;
    QLabel* stop_;
    QTableWidget* registers_;
    std::array<QLabel*, 2> flags_;
    QTableWidget* assembly_;
    MemoryTable* memory_;
    QLabel* memoryContext_;
    QPushButton* memoryPrevious_;
    QPushButton* memoryNext_;
    std::array<StackMemoryView*, 2> stacks_;
    QTableWidget* mapping_;
    PeripheralPanel* peripherals_;
    InspectionWorkspace* workspace_;
    QTabWidget* inspectionTabs_;
    QTabWidget* monitorTabs_;
    QDockWidget* inspectionDock_;
    QDockWidget* cpuDock_;
    QDockWidget* stackDock_;
    QDockWidget* assemblyDock_;
    QDockWidget* memoryDock_;
    QDockWidget* peripheralDock_;
    QDockWidget* monitorDock_;
    QPointer<PeripheralExplorer> explorer_;
    VirtualTimePanel* virtualTimePanel_;
    BoardPanel* boardPanel_;
    BoardLed* boardLed_;
    QLabel* timeLabel_;
    void openPeripheralExplorer();
    void installPeripheralCatalog(picodbg::Chip chip, const QByteArray& contents);
    void downloadPeripheralCatalog();
    void advanceVirtualTime(uint64_t duration, bool timeOnly);
    std::vector<QPointer<PeripheralInspection>> inspections_;
    unsigned inspectionSequence_ = 1;
    bool peripheralLayoutPending_ = false;
    PeripheralInspection* openPeripheral(const QString& name, unsigned core, bool duplicate = false);
    PeripheralInspection* addPeripheralInspection(const QString& name, unsigned core, bool duplicate,
                                                  const QVariantMap& selection = {});
    QString peripheralLayoutKey() const;
    QVariantMap savePeripheralLayout() const;
    void restoreProjectPeripherals();
    void closeInspections();
    QTableWidget* watches_;
    ValueWatches* valueWatches_;
    QAction* writeMemory_;
    QAction* readMemoryMmio_;
    QTableWidget* points_;
    QLineEdit* memoryAddress_;
    QString memoryExpression_ = "0x20000000";
    std::vector<picodbg::PeripheralDescription> memoryDevices_;
    QLineEdit* assemblyAddress_;
    QSpinBox* memoryLength_;
    QCheckBox* followPc_;
    QPlainTextEdit* console_;
    QWidget* consolePane_;
    QWidget* virtualTimeTab_;
    QWidget* serialTab_;
    QLineEdit* consoleInput_;
    QAction* consoleToggle_;
    QToolButton* toolsToggle_;
    QAction* stepAction_ = nullptr;
    QAction* runAction_;
    QAction* pauseAction_;
    QAction* resetAction_;
    void buildUi();
    void refresh();
    void refreshRegisters();
    QString registerHint(unsigned core, unsigned reg, uint32_t value) const;
    void toggleTools(bool show);
    void editCpuRegister(int row, int column);
    void showExtraRegisters();
    void refreshAssembly();
    void refreshMemory();
    void refreshMemoryContext();
    void setMemoryAddress(const QString& expression);
    void moveMemoryPage(bool forward);
    void showMemoryView();
    void restoreLayout(const QSettings& settings);
    void writeLayout(QSettings& settings) const;
    void restoreOverview();
    void setLayoutCore(int index);
    void setLayoutAssemblyVisible(bool visible);
    void sizeOverview();
    void showManual();
    void openMemoryWatch(const ValueWatch& watch);
    void openMemoryAt(uint32_t address, unsigned core, unsigned length = 0);
    void refreshStack();
    void refreshStack(unsigned core);
    StackViewFrame stackFrame(unsigned core, const std::vector<picodbg::StackFrame>& frames, unsigned level, bool inspectFields);
    void openStackInspector(unsigned core, unsigned level);
    void selectStackRange(unsigned core, unsigned level, StackSpan selection, StackInspector* source = nullptr, bool revealSource = true);
    void editStackMemory(unsigned core, unsigned level, StackMemoryView::EditMode mode, uint32_t address, int field, QWidget* parent);
    void editStackField(unsigned core, StackViewFrame frame, FrameField field, const QString& rendered,
                        QWidget* parent, StackMemoryView::EditMode mode = StackMemoryView::Typed);
    std::array<QVector<StackViewFrame>, 2> stackFrames_;
    std::array<QVector<StackSpan>, 2> stackSelections_;
    std::array<StackValueChanges, 2> stackValueChanges_;
    QVector<QPointer<StackInspector>> stackInspectors_;
    void revealConsole();
    void arrangeMainViews(const QString& view);
    void refreshPoints();
    void refreshWatches();
    void refreshValueWatches();
    void fillMapping();
    void refreshMappingAccess();
    void refreshPeripherals();
    void configurePeripheralPanel(PeripheralPanel& panel);
    void showValueWatches();
    void openValueWatch(const ValueWatch& watch);
    void memoryValueDialog(bool watch, int row = -1, int column = -1);
    void editPeripheralRegister(QWidget& parent, const QString& device, const picodbg::RegisterDescription& reg,
                                unsigned core, const QString& current);
    void editPeripheralField(QWidget& parent, picodbg::RegisterDescription reg, picodbg::RegisterField field,
                             unsigned core, uint32_t current);
    void singleStep();
    void runBatch();
    void setRunning(bool running);
    void addBreakpoint();
    void addMemoryWatch();
    void addExpression();
    void launchCore();
    void consoleCommand(const QString& command);
    void consoleStep(bool over);
    bool printBacktrace(unsigned core);
    void evaluate(const QString& expression);
    void failure(const std::exception& error);
    void log(const QString& message);
};
