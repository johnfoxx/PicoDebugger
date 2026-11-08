#pragma once
#include "picodbg/debugger.hpp"
#include <QMainWindow>
#include <array>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;
class QTimer;

class MainWindow : public QMainWindow {
public:
    MainWindow();
    void loadFile(const QString& path);
    void loadDemo();
private:
    std::unique_ptr<picodbg::Debugger> debugger_;
    std::array<std::array<uint32_t, 21>, 2> previous_{};
    bool previousValid_ = false;
    bool running_ = false;
    unsigned selected_ = 0;
    QTimer* timer_;
    QComboBox* chip_;
    QComboBox* core_;
    QLabel* banner_;
    QLabel* stop_;
    std::array<QTableWidget*, 2> registers_;
    std::array<QLabel*, 2> flags_;
    QTableWidget* assembly_;
    QTableWidget* memory_;
    QTableWidget* stack_;
    QTableWidget* mapping_;
    QTableWidget* peripherals_;
    QTableWidget* watches_;
    QTableWidget* points_;
    QLineEdit* memoryAddress_;
    QLineEdit* assemblyAddress_;
    QSpinBox* memoryLength_;
    QCheckBox* followPc_;
    QPlainTextEdit* source_;
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
