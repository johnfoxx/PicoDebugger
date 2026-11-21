#include "probe_dialog.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

bool configureProbe(QWidget* parent, picodbg::ProbeOptions& options) {
    QDialog dialog(parent); dialog.setWindowTitle("Connexion — Raspberry Pi Debug Probe / SWD"); dialog.setObjectName("probeDialog");
    QVBoxLayout layout(&dialog);
    auto* explanation = new QLabel("Relier SWDIO, SWCLK et GND de la Debug Probe à la carte alimentée.\n"
        "La connexion arrête la cible. Le téléversement est une action séparée.\n"
        "Les variantes W sont choisies manuellement ; la famille RP2040/RP2350 est vérifiée.");
    explanation->setWordWrap(true); layout.addWidget(explanation);
    QSettings settings;
    QLineEdit gdb(settings.value("probe/gdb", options.gdb).toString()); gdb.setObjectName("probeGdb");
    QCheckBox launch("Démarrer OpenOCD pour la sonde officielle (CMSIS-DAP)"); launch.setChecked(settings.value("probe/launch", true).toBool());
    launch.setObjectName("probeLaunchOpenOcd");
    QLineEdit openocd(settings.value("probe/openocd", options.openOcd).toString()); openocd.setObjectName("probeOpenOcd");
    QLineEdit scripts(settings.value("probe/scripts").toString()); scripts.setPlaceholderText("Dossier scripts OpenOCD, facultatif");
    QLineEdit host(settings.value("probe/host", options.host).toString()); host.setObjectName("probeHost");
    QSpinBox port; port.setRange(1, 65535); port.setValue(settings.value("probe/port", options.port).toInt()); port.setObjectName("probePort");
    QSpinBox speed; speed.setRange(1, 10000); speed.setValue(settings.value("probe/speed", options.speed).toInt()); speed.setSuffix(" kHz");
    QFormLayout form; form.addRow("Exécutable GDB compatible ARM", &gdb); form.addRow(&launch);
    form.addRow("Exécutable OpenOCD", &openocd); form.addRow("Répertoire des scripts", &scripts); form.addRow("Vitesse SWD", &speed);
    form.addRow("Hôte du serveur GDB", &host); form.addRow("Port GDB", &port); layout.addLayout(&form);
    const auto toggle = [&] { openocd.setEnabled(launch.isChecked()); scripts.setEnabled(launch.isChecked()); speed.setEnabled(launch.isChecked()); host.setEnabled(!launch.isChecked()); };
    QObject::connect(&launch, &QCheckBox::toggled, &dialog, toggle); toggle();
    QGroupBox mapping("Correspondance avancée des cœurs (serveur externe)"); QFormLayout mapLayout(&mapping);
    QLineEdit core0(settings.value("probe/thread0").toString()), core1(settings.value("probe/thread1").toString());
    core0.setPlaceholderText("Automatique : nom OpenOCD / numéro de cœur"); core1.setPlaceholderText(core0.placeholderText());
    mapLayout.addRow("Thread GDB du cœur 0", &core0); mapLayout.addRow("Thread GDB du cœur 1", &core1); layout.addWidget(&mapping);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout.addWidget(&buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    options.gdb = gdb.text().trimmed(); options.launchOpenOcd = launch.isChecked(); options.openOcd = openocd.text().trimmed();
    options.scripts = scripts.text().trimmed(); options.host = host.text().trimmed(); options.port = static_cast<unsigned>(port.value()); options.speed = static_cast<unsigned>(speed.value());
    options.coreThreads = {{core0.text().trimmed(), core1.text().trimmed()}};
    settings.setValue("probe/gdb", options.gdb); settings.setValue("probe/launch", options.launchOpenOcd); settings.setValue("probe/openocd", options.openOcd);
    settings.setValue("probe/scripts", options.scripts); settings.setValue("probe/host", options.host); settings.setValue("probe/port", options.port); settings.setValue("probe/speed", options.speed);
    settings.setValue("probe/thread0", options.coreThreads[0]); settings.setValue("probe/thread1", options.coreThreads[1]);
    return true;
}
