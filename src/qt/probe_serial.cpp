#include "probe_serial.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <algorithm>
#include <iterator>

#ifdef PICODBG_QT_SERIALPORT
#include <QSerialPort>
#include <QSerialPortInfo>
#elif defined(Q_OS_UNIX)
#include <QSocketNotifier>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
bool isDebugProbe(unsigned vendor, unsigned product) {
    return vendor == 0x2e8a && product == 0x000c;
}

#if defined(Q_OS_LINUX) && !defined(PICODBG_QT_SERIALPORT)
QByteArray sysfsValue(const QDir& directory, const char* name) {
    QFile file(directory.filePath(QString::fromLatin1(name)));
    return file.open(QIODevice::ReadOnly) ? file.read(256).trimmed() : QByteArray();
}

ProbeSerialPort describePort(const QString& path, const QString& tty) {
    const auto device = QFileInfo(QDir("/sys/class/tty").filePath(tty + "/device")).canonicalFilePath();
    QDir directory(device);
    while (!device.isEmpty() && directory.absolutePath().startsWith("/sys/devices/")) {
        const auto vendor = sysfsValue(directory, "idVendor");
        const auto product = sysfsValue(directory, "idProduct");
        if (!vendor.isEmpty() && !product.isEmpty()) {
            const bool probe = isDebugProbe(vendor.toUInt(nullptr, 16), product.toUInt(nullptr, 16));
            auto label = QString::fromUtf8(sysfsValue(directory, "product"));
            if (probe) label = "Raspberry Pi Debug Probe";
            if (probe) {
                const auto serial = QString::fromUtf8(sysfsValue(directory, "serial"));
                if (!serial.isEmpty()) label += " · " + serial;
            }
            return {path, label, probe};
        }
        if (!directory.cdUp()) break;
    }
    return {path, {}, false};
}
#endif

#if defined(Q_OS_UNIX) && !defined(PICODBG_QT_SERIALPORT)
struct BaudRate { int value; speed_t flag; };
const BaudRate nativeRates[] = {
    {9600, B9600}, {19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200},
#ifdef B230400
    {230400, B230400},
#endif
#ifdef B460800
    {460800, B460800},
#endif
#ifdef B921600
    {921600, B921600},
#endif
};

QString errnoText(int code) {
    if (code == EACCES || code == EPERM)
        return "Accès refusé au port série. Vérifier les droits du périphérique.";
    if (code == EBUSY) return "Le port série est occupé.";
    return QString::fromLocal8Bit(std::strerror(code));
}
#endif
}

struct ProbeSerial::Device {
#ifdef PICODBG_QT_SERIALPORT
    std::unique_ptr<QSerialPort> serial;
#elif defined(Q_OS_UNIX)
    QSocketNotifier* notifier = nullptr;
    int fd = -1;
    termios previous{};
#endif
};

ProbeSerial::ProbeSerial(QObject* parent) : QObject(parent), device_(new Device) {}

ProbeSerial::~ProbeSerial() {
    close();
}

bool ProbeSerial::supported() {
#if defined(PICODBG_QT_SERIALPORT) || defined(Q_OS_UNIX)
    return true;
#else
    return false;
#endif
}

std::vector<ProbeSerialPort> ProbeSerial::ports() {
    std::vector<ProbeSerialPort> result;
#ifdef PICODBG_QT_SERIALPORT
    for (const auto& port : QSerialPortInfo::availablePorts()) {
        const bool probe = isDebugProbe(port.vendorIdentifier(), port.productIdentifier());
        auto label = probe ? QString("Raspberry Pi Debug Probe") : port.description();
        if (probe && !port.serialNumber().isEmpty()) label += " · " + port.serialNumber();
        result.push_back({port.systemLocation(), label, probe});
    }
#elif defined(Q_OS_LINUX)
    const QDir byId("/dev/serial/by-id");
    QMap<QString, QString> stablePaths;
    for (const auto& link : byId.entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto target = link.canonicalFilePath();
        if (!target.isEmpty() && !stablePaths.contains(target)) stablePaths.insert(target, link.absoluteFilePath());
    }
    const QDir dev("/dev");
    const auto nodes = dev.entryInfoList({"ttyACM*", "ttyUSB*"}, QDir::Files | QDir::System, QDir::Name);
    for (const auto& node : nodes) {
        const auto path = stablePaths.value(node.canonicalFilePath(), node.absoluteFilePath());
        result.push_back(describePort(path, node.fileName()));
    }
#elif defined(Q_OS_MACOS)
    const QDir dev("/dev");
    for (const auto& node : dev.entryInfoList({"cu.usbmodem*", "cu.usbserial*"}, QDir::Files | QDir::System))
        result.push_back({node.absoluteFilePath(), {}, false});
#endif
    std::sort(result.begin(), result.end(), [](const ProbeSerialPort& a, const ProbeSerialPort& b) {
        if (a.debugProbe != b.debugProbe) return a.debugProbe;
        return a.path < b.path;
    });
    return result;
}

QList<int> ProbeSerial::baudRates() {
#if defined(Q_OS_UNIX) && !defined(PICODBG_QT_SERIALPORT)
    QList<int> result;
    for (const auto& rate : nativeRates) result.append(rate.value);
    return result;
#else
    return {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
#endif
}

bool ProbeSerial::open(const QString& path, int baudRate, QString& error) {
    close();
    if (path.isEmpty()) {
        error = "Sélectionner un port série.";
        return false;
    }
#ifdef PICODBG_QT_SERIALPORT
    auto serial = std::unique_ptr<QSerialPort>(new QSerialPort(this));
    serial->setPortName(path);
    serial->setReadBufferSize(64 * 1024);
    if (!serial->setBaudRate(baudRate) || !serial->setDataBits(QSerialPort::Data8) ||
        !serial->setParity(QSerialPort::NoParity) || !serial->setStopBits(QSerialPort::OneStop) ||
        !serial->setFlowControl(QSerialPort::NoFlowControl) || !serial->open(QIODevice::ReadWrite)) {
        error = serial->errorString();
        return false;
    }
    serial->setDataTerminalReady(true);
    connect(serial.get(), &QIODevice::readyRead, this, [this] {
        if (!isOpen()) return;
        const auto bytes = device_->serial->readAll();
        if (received && !bytes.isEmpty()) received(bytes);
    });
    connect(serial.get(), &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError reason) {
        if (device_->serial && (reason == QSerialPort::ResourceError || reason == QSerialPort::ReadError))
            connectionLost(device_->serial->errorString());
    });
    device_->serial = std::move(serial);
    return true;
#elif defined(Q_OS_UNIX)
    const auto rate = std::find_if(std::begin(nativeRates), std::end(nativeRates),
        [baudRate](const BaudRate& item) { return item.value == baudRate; });
    if (rate == std::end(nativeRates)) {
        error = "Débit série non pris en charge.";
        return false;
    }
    const auto name = QFile::encodeName(path);
    const int fd = ::open(name.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { error = errnoText(errno); return false; }
    termios original{};
    struct stat metadata{};
    if (::fstat(fd, &metadata) != 0 || !S_ISCHR(metadata.st_mode) || ::tcgetattr(fd, &original) != 0) {
        error = "Le chemin sélectionné ne désigne pas un port série.";
        ::close(fd);
        return false;
    }
#ifdef TIOCEXCL
    if (::ioctl(fd, TIOCEXCL) != 0) {
        error = errnoText(errno);
        ::close(fd);
        return false;
    }
#endif
    auto setup = original;
    ::cfmakeraw(&setup);
    setup.c_cflag &= static_cast<tcflag_t>(~(CSIZE | CSTOPB | PARENB));
#ifdef CRTSCTS
    setup.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
    setup.c_cflag |= CS8 | CLOCAL | CREAD;
    setup.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY));
    setup.c_cc[VMIN] = 1;
    setup.c_cc[VTIME] = 0;
    if (::cfsetispeed(&setup, rate->flag) != 0 || ::cfsetospeed(&setup, rate->flag) != 0 ||
        ::tcsetattr(fd, TCSANOW, &setup) != 0) {
        error = errnoText(errno);
#ifdef TIOCNXCL
        ::ioctl(fd, TIOCNXCL);
#endif
        ::close(fd);
        return false;
    }
#if defined(TIOCMBIS) && defined(TIOCM_DTR)
    int dtr = TIOCM_DTR;
    ::ioctl(fd, TIOCMBIS, &dtr);
#endif
    device_->fd = fd;
    device_->previous = original;
    auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    device_->notifier = notifier;
    connect(notifier, &QSocketNotifier::activated, this, [this, fd] {
        char buffer[4096];
        for (int batch = 0; batch < 16 && device_->fd == fd; ++batch) {
            const auto size = ::read(fd, buffer, sizeof(buffer));
            if (size > 0) {
                if (received) received(QByteArray(buffer, static_cast<int>(size)));
                continue;
            }
            const int code = errno;
            if (size < 0 && code == EINTR) continue;
            if (size < 0 && (code == EAGAIN || code == EWOULDBLOCK)) return;
            connectionLost(size == 0 || code == EIO ? "Port série débranché." : errnoText(code));
            return;
        }
    });
    return true;
#else
    Q_UNUSED(baudRate);
    error = "Qt Serial Port est nécessaire sur cette plateforme.";
    return false;
#endif
}

bool ProbeSerial::isOpen() const {
#ifdef PICODBG_QT_SERIALPORT
    return device_->serial && device_->serial->isOpen();
#elif defined(Q_OS_UNIX)
    return device_->fd >= 0;
#else
    return false;
#endif
}

qint64 ProbeSerial::write(const QByteArray& bytes, QString& error) {
    if (!isOpen()) {
        error = "Port série déconnecté.";
        return -1;
    }
#ifdef PICODBG_QT_SERIALPORT
    const auto written = device_->serial->write(bytes);
    if (written < 0) error = device_->serial->errorString();
    return written;
#elif defined(Q_OS_UNIX)
    ssize_t written;
    do {
        written = ::write(device_->fd, bytes.constData(), static_cast<size_t>(bytes.size()));
    } while (written < 0 && errno == EINTR);
    if (written < 0) error = errnoText(errno);
    return written;
#else
    Q_UNUSED(bytes);
    return -1;
#endif
}

void ProbeSerial::close() {
#ifdef PICODBG_QT_SERIALPORT
    if (device_->serial) {
        auto* serial = device_->serial.release();
        serial->disconnect(this);
        serial->close();
        serial->deleteLater();
    }
#elif defined(Q_OS_UNIX)
    if (device_->notifier) {
        device_->notifier->setEnabled(false);
        device_->notifier->deleteLater();
        device_->notifier = nullptr;
    }
    if (device_->fd >= 0) {
        const int fd = device_->fd;
        device_->fd = -1;
#if defined(TIOCMBIC) && defined(TIOCM_DTR)
        int dtr = TIOCM_DTR;
        ::ioctl(fd, TIOCMBIC, &dtr);
#endif
        ::tcsetattr(fd, TCSANOW, &device_->previous);
#ifdef TIOCNXCL
        ::ioctl(fd, TIOCNXCL);
#endif
        ::close(fd);
    }
#endif
}

void ProbeSerial::connectionLost(const QString& message) {
    close();
    if (disconnected) disconnected(message);
}
