#include "probe_serial_monitor.hpp"
#include "choice_box.hpp"
#include "window_theme.hpp"

#include <algorithm>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
#include <QTextCodec>
#endif

namespace {
constexpr int charLimit = 256 * 1024;
constexpr int pendingLimit = 64 * 1024;

QString displayPort(const ProbeSerialPort& device) {
    return device.description.isEmpty() ? device.path : device.description + " · " + device.path;
}
}

struct ProbeSerialMonitor::Decoder {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringDecoder utf8{QStringDecoder::Utf8};
    QString convert(const QByteArray& bytes) { return utf8(bytes); }
#else
    std::unique_ptr<QTextDecoder> utf8{QTextCodec::codecForName("UTF-8")->makeDecoder()};
    QString convert(const QByteArray& bytes) { return utf8->toUnicode(bytes); }
#endif
};

ProbeSerialMonitor::ProbeSerialMonitor()
    : serial_(this), decoder_(new Decoder) {
    setObjectName("probeSerialMonitor");
    setWindowTitle("Moniteur série · Raspberry Pi Debug Probe");
    resize(790, 440);
    applyPicoTheme(*this);

    auto* pane = new QWidget;
    auto* layout = new QVBoxLayout(pane);
    auto* ports = new QHBoxLayout;
    auto* portLabel = new QLabel("&Port");
    ports->addWidget(portLabel);
    port_ = new ChoiceBox;
    port_->setObjectName("probeSerialPort");
    port_->setEditable(true);
    port_->setInsertPolicy(QComboBox::NoInsert);
    port_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    port_->setMinimumContentsLength(15);
    port_->lineEdit()->setPlaceholderText("Sélectionner un port ou saisir son chemin");
    portLabel->setBuddy(port_);
    ports->addWidget(port_, 1);
    refresh_ = new QPushButton("Actualiser");
    refresh_->setObjectName("probeSerialRefresh");
    ports->addWidget(refresh_);
    layout->addLayout(ports);

    auto* controls = new QHBoxLayout;
    auto* baudLabel = new QLabel("&Débit");
    controls->addWidget(baudLabel);
    baud_ = new ChoiceBox;
    baud_->setObjectName("probeSerialBaud");
    for (const auto rate : ProbeSerial::baudRates()) baud_->addItem(QString::number(rate), rate);
    baudLabel->setBuddy(baud_);
    baud_->setToolTip("Bauds · 8 bits, sans parité, 1 bit d'arrêt");
    controls->addWidget(baud_);
    connection_ = new QPushButton("Connecter");
    connection_->setObjectName("probeSerialConnect");
    controls->addWidget(connection_);
    controls->addStretch();
    auto* wiring = new QPushButton("Câblage");
    controls->addWidget(wiring);
    layout->addLayout(controls);

    log_ = new QPlainTextEdit;
    log_->setObjectName("probeSerialLog");
    log_->setReadOnly(true);
    log_->setUndoRedoEnabled(false);
    log_->setMaximumBlockCount(10000);
    log_->setPlaceholderText("Les traces émises sur l’UART apparaîtront ici.");
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    log_->setFont(font);
    layout->addWidget(log_, 1);

    auto* history = new QHBoxLayout;
    follow_ = new QCheckBox("Défilement automatique");
    follow_->setObjectName("probeSerialFollow");
    history->addWidget(follow_);
    history->addStretch();
    clear_ = new QPushButton("Effacer");
    clear_->setObjectName("probeSerialClear");
    history->addWidget(clear_);
    save_ = new QPushButton("Enregistrer…");
    save_->setObjectName("probeSerialSave");
    history->addWidget(save_);
    layout->addLayout(history);

    status_ = new QLabel;
    status_->setObjectName("probeSerialStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setCentralWidget(pane);

    QSettings settings;
    const auto savedBaud = settings.value("probeSerial/baud", 115200).toInt();
    const int baudIndex = baud_->findData(savedBaud);
    baud_->setCurrentIndex(baudIndex >= 0 ? baudIndex : baud_->findData(115200));
    follow_->setChecked(settings.value("probeSerial/follow", true).toBool());
    const auto geometry = settings.value("probeSerial/geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    port_->setEditText(settings.value("probeSerial/port").toString());

    connect(refresh_, &QPushButton::clicked, this, &ProbeSerialMonitor::refreshPorts);
    connect(connection_, &QPushButton::clicked, this, &ProbeSerialMonitor::toggleConnection);
    connect(wiring, &QPushButton::clicked, this, &ProbeSerialMonitor::showWiring);
    connect(port_, &QComboBox::editTextChanged, this, [this] {
        const bool selected = !selectedPort().isEmpty();
        connection_->setEnabled(ProbeSerial::supported() && (serial_.isOpen() || selected));
        if (selected && (status_->text().startsWith("Aucun port") || status_->text().startsWith("Sélectionner")))
            status_->setText("Prêt à connecter · 8N1");
    });
    connect(follow_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        flushTimer_.stop();
        pending_.clear();
        decoder_.reset(new Decoder);
        previousWasCR_ = false;
        log_->clear();
    });
    connect(save_, &QPushButton::clicked, this, &ProbeSerialMonitor::saveLog);
    connect(log_, &QPlainTextEdit::textChanged, this, [this] {
        const bool hasText = !log_->document()->isEmpty();
        save_->setEnabled(hasText);
        clear_->setEnabled(hasText);
    });
    save_->setEnabled(false); clear_->setEnabled(false);

    flushTimer_.setSingleShot(true);
    flushTimer_.setInterval(25);
    connect(&flushTimer_, &QTimer::timeout, this, &ProbeSerialMonitor::flushLog);
    serial_.received = [this](const QByteArray& bytes) {
        pending_ += bytes;
        if (pending_.size() >= pendingLimit) flushLog();
        else if (!flushTimer_.isActive()) flushTimer_.start();
    };
    serial_.disconnected = [this](const QString& reason) {
        flushLog();
        updateConnection("Connexion interrompue · " + reason);
    };
    updateConnection(ProbeSerial::supported() ?
        "Sélectionner le port UART de la sonde." :
        "Qt Serial Port est nécessaire sur cette plateforme.");
}

ProbeSerialMonitor::~ProbeSerialMonitor() {
    serial_.close();
}

QString ProbeSerialMonitor::selectedPort() const {
    const int index = port_->currentIndex();
    const auto text = port_->currentText().trimmed();
    if (index >= 0 && text == port_->itemText(index)) return port_->itemData(index).toString();
    return text;
}

void ProbeSerialMonitor::refreshPorts() {
    if (serial_.isOpen()) return;
    const auto previous = selectedPort();
    const auto devices = ProbeSerial::ports();
    {
        const QSignalBlocker blocked(port_);
        port_->clear();
        for (const auto& device : devices) port_->addItem(displayPort(device), device.path);
        const int found = port_->findData(previous);
        if (found >= 0) port_->setCurrentIndex(found);
        else if (!previous.isEmpty()) port_->setEditText(previous);
        else if (std::count_if(devices.begin(), devices.end(),
                     [](const ProbeSerialPort& device) { return device.debugProbe; }) == 1) port_->setCurrentIndex(0);
        else port_->setCurrentIndex(-1);
    }
    const bool selected = !selectedPort().isEmpty();
    connection_->setEnabled(ProbeSerial::supported() && selected);
    if (!ProbeSerial::supported()) return;
    if (devices.empty() && !selected)
        status_->setText("Aucun port détecté. Brancher la sonde puis actualiser.");
    else if (!selected)
        status_->setText("Sélectionner le port UART de la sonde.");
    else if (status_->text().startsWith("Aucun port") || status_->text().startsWith("Sélectionner"))
        status_->setText("Prêt à connecter · 8N1");
}

void ProbeSerialMonitor::toggleConnection() {
    if (serial_.isOpen()) {
        serial_.close();
        flushLog();
        updateConnection("Port série déconnecté.");
        return;
    }
    const auto path = selectedPort();
    const int rate = baud_->currentData().toInt();
    decoder_.reset(new Decoder);
    previousWasCR_ = false;
    QString error;
    if (!serial_.open(path, rate, error)) {
        updateConnection("Connexion impossible · " + error);
        return;
    }
    QSettings settings;
    settings.setValue("probeSerial/port", path);
    settings.setValue("probeSerial/baud", rate);
    updateConnection(QString("Connecté · %1 · %2 bauds · 8N1").arg(path).arg(rate));
}

void ProbeSerialMonitor::updateConnection(const QString& status) {
    const bool open = serial_.isOpen();
    status_->setText(status);
    connection_->setText(open ? "Déconnecter" : "Connecter");
    connection_->setEnabled(ProbeSerial::supported() && (open || !selectedPort().isEmpty()));
    port_->setEnabled(!open && ProbeSerial::supported());
    baud_->setEnabled(!open && ProbeSerial::supported());
    refresh_->setEnabled(!open && ProbeSerial::supported());
}

void ProbeSerialMonitor::flushLog() {
    flushTimer_.stop();
    if (pending_.isEmpty()) return;
    const auto decoded = decoder_->convert(pending_);
    pending_.clear();
    QString text;
    text.reserve(decoded.size());
    for (const QChar character : decoded) {
        if (character == '\n' && previousWasCR_) {
            previousWasCR_ = false;
            continue;
        }
        previousWasCR_ = character == '\r';
        if (previousWasCR_) text += '\n';
        else if (character == '\n' || character == '\t' || character.unicode() >= 0x20) text += character;
    }
    if (text.isEmpty()) return;
    auto* scroll = log_->verticalScrollBar();
    const int previousScroll = scroll->value();
    QTextCursor tail(log_->document());
    tail.movePosition(QTextCursor::End);
    tail.insertText(text);
    const int excess = log_->document()->characterCount() - 1 - charLimit;
    if (excess > 0) {
        QTextCursor head(log_->document());
        const int cut = excess + (log_->document()->characterAt(excess).isLowSurrogate() ? 1 : 0);
        head.setPosition(cut, QTextCursor::KeepAnchor);
        head.removeSelectedText();
    }
    scroll->setValue(follow_->isChecked() ? scroll->maximum() : previousScroll);
}

void ProbeSerialMonitor::saveLog() {
    flushLog();
    const auto path = QFileDialog::getSaveFileName(this, "Enregistrer les traces UART", "traces-uart.log",
                                                   "Journaux (*.log *.txt);;Tous les fichiers (*)");
    if (path.isEmpty()) return;
    flushLog();
    const auto bytes = log_->toPlainText().toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::warning(this, "Traces UART", "Impossible d'enregistrer le journal.\n" + file.errorString());
    }
}

void ProbeSerialMonitor::showWiring() {
    QMessageBox::information(this, "Câbler l'UART de la Debug Probe",
        "Utiliser le port U (UART) de la sonde. Relier d'abord les masses GND, puis le TX de la carte "
        "au RX de la sonde. Alimenter la carte séparément.\n\n"
        "Choisir le même débit que dans le firmware. Pour printf avec le Pico SDK, activer stdio UART et appeler stdio_init_all().");
}

void ProbeSerialMonitor::closeEvent(QCloseEvent* event) {
    serial_.close();
    flushLog();
    updateConnection("Port série déconnecté.");
    QSettings settings;
    settings.setValue("probeSerial/geometry", saveGeometry());
    settings.setValue("probeSerial/follow", follow_->isChecked());
    QMainWindow::closeEvent(event);
}
