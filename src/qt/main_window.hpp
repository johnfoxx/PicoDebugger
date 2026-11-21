#pragma once
#include "picodbg/debugger.hpp"
#include "gdb_target.hpp"
#include "source_map.hpp"
#include <QByteArray>
#include <QMainWindow>
#include <QPointer>
#include <array>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QHideEvent;
class QShowEvent;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QStackedWidget;
class QTimer;
class PeripheralPanel;
class PeripheralInspection;
class InspectionWorkspace;
class PeripheralExplorer;
class VirtualTimePanel;
class BoardPanel;
class ProbeSerialMonitor;
namespace picoide { class IdeWindow; class CodeEditor; }

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
    std::unique_ptr<ProbeSerialMonitor> serialMonitor_;
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
    bool pendingSymbols_ = false, switchAfterDisconnect_ = false, closeAfterDisconnect_ = false;
    uint64_t lastHardwareRevision_ = 0;
    uint64_t lastHardwareSymbols_ = 0;
    picosource::SourceMap sourceMap_;
    picosource::Location executingSource_, selectedSource_;
    QString previewPath_;
    QDateTime previewTimestamp_;
    void indexSource(const QString& elf);
    void refreshSource(bool force = false, bool showSelection = false);
    void hardwareTick();
    void configureHardware();
    void downloadHardware();
    std::array<std::array<uint32_t, 21>, 2> previous_{};
    bool previousValid_ = false;
    bool running_ = false;
    bool timeOnly_ = false;
    uint64_t remainingTime_ = 0;
    unsigned selected_ = 0;
    QByteArray overviewState_;
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
    QTableWidget* memory_;
    QTableWidget* stack_;
    QTableWidget* mapping_;
    PeripheralPanel* peripherals_;
    InspectionWorkspace* workspace_;
    QStackedWidget* codePages_;
    QAction* showDebuggerAssembly_;
    QTabWidget* inspectionTabs_;
    QTabWidget* monitorTabs_;
    QDockWidget* inspectionDock_;
    QDockWidget* cpuDock_;
    QDockWidget* monitorDock_;
    QPointer<PeripheralExplorer> explorer_;
    VirtualTimePanel* virtualTimePanel_;
    BoardPanel* boardPanel_;
    QLabel* timeLabel_;
    void openPeripheralExplorer();
    void advanceVirtualTime(uint64_t duration, bool timeOnly);
    std::vector<QPointer<PeripheralInspection>> inspections_;
    unsigned inspectionSequence_ = 1;
    void openPeripheral(const QString& name, bool duplicate = false, int context = -1);
    void closeInspections();
    QTableWidget* watches_;
    QTableWidget* points_;
    QLineEdit* memoryAddress_;
    QLineEdit* assemblyAddress_;
    QSpinBox* memoryLength_;
    QCheckBox* followPc_;
    picoide::CodeEditor* source_;
    QLabel* sourceStatus_;
    QPlainTextEdit* console_;
    QAction* stepAction_ = nullptr;
    QAction* runAction_;
    QAction* pauseAction_;
    QAction* resetAction_;
    void buildUi();
    void refresh();
    void refreshRegisters();
    void refreshAssembly();
    void refreshMemory();
    void refreshStack();
    void refreshPoints();
    void refreshWatches();
    void fillMapping();
    void refreshPeripherals();
    void singleStep();
    void runBatch();
    void setRunning(bool running);
    void addBreakpoint();
    void addMemoryWatch();
    void addExpression();
    void launchCore();
    void evaluate(const QString& expression);
    void failure(const std::exception& error);
    void log(const QString& message);
};
