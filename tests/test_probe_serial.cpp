#include "main_window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Condition>
bool waitFor(Condition ready, int timeout = 2500) {
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (ready()) return true;
        QThread::msleep(5);
    } while (timer.elapsed() < timeout);
    return false;
}

struct PseudoTerminal {
    int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    ~PseudoTerminal() { unplug(); }
    void unplug() {
        if (master >= 0) ::close(master);
        master = -1;
    }
    void send(const QByteArray& bytes) const {
        require(::write(master, bytes.constData(), static_cast<size_t>(bytes.size())) == bytes.size(),
                "could not write the UART fixture");
    }
};

void testMonitor() {
    PseudoTerminal uart;
    require(uart.master >= 0 && ::grantpt(uart.master) == 0 && ::unlockpt(uart.master) == 0,
            "could not create a pseudo terminal");
    const char* slave = ::ptsname(uart.master);
    require(slave, "could not find the pseudo terminal slave");

    MainWindow debugger;
    debugger.show();
    auto* action = debugger.findChild<QAction*>("openProbeSerialAction");
    require(action && action->isEnabled(), "serial action unavailable before SWD");
    action->trigger();
    QWidget* monitor = nullptr;
    for (auto* window : QApplication::topLevelWidgets()) {
        if (window->objectName() == "probeSerialMonitor") monitor = window;
    }
    require(monitor && monitor->isVisible() && !monitor->parentWidget(), "missing independent serial monitor");
    auto* port = monitor->findChild<QComboBox*>("probeSerialPort");
    auto* baud = monitor->findChild<QComboBox*>("probeSerialBaud");
    auto* connection = monitor->findChild<QPushButton*>("probeSerialConnect");
    auto* status = monitor->findChild<QLabel*>("probeSerialStatus");
    auto* log = monitor->findChild<QPlainTextEdit*>("probeSerialLog");
    auto* save = monitor->findChild<QPushButton*>("probeSerialSave");
    auto* clear = monitor->findChild<QPushButton*>("probeSerialClear");
    require(port && baud && connection && status && log && save && clear, "missing monitor controls");
    require(log->isReadOnly() && !save->isEnabled() && !clear->isEnabled(), "incorrect empty log state");
    require(baud->currentData().toInt() == 115200, "unexpected default baud rate");
    port->setEditText("/dev/null");
    connection->click();
    require(status->text().startsWith("Connexion impossible") && port->isEnabled(), "invalid port accepted");

    port->setEditText(QString::fromLocal8Bit(slave));
    connection->click();
    require(status->text().startsWith("Connecté") && !port->isEnabled(), "serial connection failed");
    uart.send("d\xC3");
    require(waitFor([&] { return log->toPlainText() == "d"; }), "partial line did not appear");
    uart.send("\xA9marrage\r");
    require(waitFor([&] { return log->toPlainText() == QString::fromUtf8("démarrage\n"); }),
            "split UTF-8 data did not decode");
    uart.send("\ntrace");
    const auto expected = QString::fromUtf8("démarrage\ntrace");
    require(waitFor([&] { return log->toPlainText() == expected; }), "split CRLF or last partial line failed");
    require(save->isEnabled() && clear->isEnabled(), "log actions disabled after receiving data");

    monitor->close();
    action->trigger();
    require(monitor->isVisible() && port->isEnabled() && log->toPlainText() == expected,
            "reopening the monitor did not preserve the log");
    connection->click();
    require(status->text().startsWith("Connecté"), "serial port was not released on close");
    uart.unplug();
    require(waitFor([&] { return status->text().startsWith("Connexion interrompue"); }),
            "serial disconnect did not appear");
    require(port->isEnabled() && log->toPlainText() == expected, "serial disconnect discarded the log");
    clear->click();
    require(log->document()->isEmpty() && !save->isEnabled(), "clear did not reset the log");
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir settings;
        require(settings.isValid(), "could not isolate test settings");
        QCoreApplication::setOrganizationName("PicoDebuggerTests");
        QCoreApplication::setApplicationName("ProbeSerial");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        testMonitor();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
