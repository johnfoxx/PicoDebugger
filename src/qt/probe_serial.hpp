#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

struct ProbeSerialPort {
    QString path;
    QString description;
    bool debugProbe;
};

class ProbeSerial : public QObject {
public:
    explicit ProbeSerial(QObject* parent = nullptr);
    ~ProbeSerial() override;

    static bool supported();
    static std::vector<ProbeSerialPort> ports();
    static QList<int> baudRates();
    bool open(const QString& path, int baudRate, QString& error);
    qint64 write(const QByteArray& bytes, QString& error);
    void close();
    bool isOpen() const;

    std::function<void(const QByteArray&)> received;
    std::function<void(const QString&)> disconnected;

private:
    struct Device;
    std::unique_ptr<Device> device_;
    void connectionLost(const QString& message);
};
