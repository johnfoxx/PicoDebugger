#pragma once

#include "probe_serial.hpp"
#include <QMainWindow>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class ProbeSerialMonitor : public QMainWindow {
public:
    ProbeSerialMonitor();
    ~ProbeSerialMonitor() override;
    void refreshPorts();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Decoder;
    ProbeSerial serial_;
    std::unique_ptr<Decoder> decoder_;
    QTimer flushTimer_;
    QByteArray pending_;
    bool previousWasCR_ = false;
    QComboBox* port_;
    QComboBox* baud_;
    QPushButton* refresh_;
    QPushButton* connection_;
    QCheckBox* follow_;
    QPlainTextEdit* log_;
    QPushButton* save_;
    QPushButton* clear_;
    QLabel* status_;

    QString selectedPort() const;
    void toggleConnection();
    void updateConnection(const QString& status);
    void flushLog();
    void saveLog();
    void showWiring();
};
