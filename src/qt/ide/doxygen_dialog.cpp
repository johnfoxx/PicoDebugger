#include "doxygen_dialog.hpp"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace picoide {

void generateDoxygen(const QString& root, QWidget* parent, const std::function<bool()>& saveSources) {
    const QDir project(root);
    const auto program = QStandardPaths::findExecutable("doxygen");
    QString config;
    for (const auto* path : {"Doxyfile", "docs/Doxyfile"}) {
        if (QFileInfo(project.filePath(path)).isFile()) {
            config = path;
            break;
        }
    }
    const auto output = project.filePath(".picodbg/doxygen");
    const auto index = QDir(output).filePath("html/index.html");

    QDialog dialog(parent);
    dialog.setObjectName("ideDoxygenDialog");
    dialog.setWindowTitle("Documentation Doxygen");
    dialog.resize(760, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = new QLabel;
    status->setObjectName("ideDoxygenStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setText(program.isEmpty() ? "Installer Doxygen et vérifier qu’il est accessible dans le PATH."
        : config.isEmpty() ? "Documentation HTML du projet vers .picodbg/doxygen. Les fichiers seront enregistrés au lancement."
        : QString("Configuration du projet · %1. Les fichiers seront enregistrés au lancement.").arg(config));
    layout->addWidget(status);
    auto* log = new QPlainTextEdit;
    log->setObjectName("ideDoxygenLog");
    log->setReadOnly(true);
    log->setMaximumBlockCount(3000);
    layout->addWidget(log, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* start = buttons->addButton("Générer", QDialogButtonBox::ActionRole);
    start->setObjectName("ideDoxygenStart");
    start->setEnabled(!program.isEmpty());
    auto* stop = buttons->addButton("Arrêter", QDialogButtonBox::ActionRole);
    stop->setEnabled(false);
    auto* open = buttons->addButton("Ouvrir le HTML", QDialogButtonBox::ActionRole);
    open->setVisible(config.isEmpty());
    open->setEnabled(config.isEmpty() && QFileInfo(index).isFile());
    layout->addWidget(buttons);

    QProcess process;
    process.setWorkingDirectory(root);
    process.setProcessChannelMode(QProcess::MergedChannels);
    bool cancelled = false;
    const auto idle = [&] {
        start->setEnabled(true);
        stop->setEnabled(false);
    };
    const auto append = [&] {
        log->moveCursor(QTextCursor::End);
        log->insertPlainText(QString::fromLocal8Bit(process.readAll()));
    };
    QObject::connect(&process, &QProcess::readyRead, &dialog, append);
    QObject::connect(&process, &QProcess::started, &dialog, [&] {
        if (config.isEmpty()) {
            auto name = project.dirName();
            name.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', ' ').replace('\r', ' ');
            const auto defaults = QString("PROJECT_NAME = \"%1\"\nINPUT = .\nRECURSIVE = YES\n"
                "EXCLUDE_PATTERNS = */.git/* */.picodbg/* */CMakeFiles/*\n"
                "OUTPUT_DIRECTORY = .picodbg/doxygen\nGENERATE_HTML = YES\nGENERATE_LATEX = NO\n"
                "HAVE_DOT = NO\nQUIET = YES\nWARN_IF_UNDOCUMENTED = NO\n").arg(name);
            process.write(defaults.toUtf8());
        }
        process.closeWriteChannel();
    });
    QObject::connect(&process, &QProcess::errorOccurred, &dialog, [&](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        idle();
        status->setText("Impossible de lancer Doxygen · " + process.errorString());
    });
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &dialog,
                     [&](int code, QProcess::ExitStatus result) {
        append();
        idle();
        if (cancelled) status->setText("Génération arrêtée.");
        else if (result != QProcess::NormalExit || code != 0)
            status->setText("La génération a échoué. Consulter le journal.");
        else {
            status->setText("Génération terminée. Consulter le journal pour les avertissements.");
            open->setEnabled(config.isEmpty() && QFileInfo(index).isFile());
        }
    });
    QObject::connect(start, &QPushButton::clicked, &dialog, [&] {
        if (!saveSources()) return;
        if (config.isEmpty() && !project.mkpath(".picodbg/doxygen")) {
            status->setText("Impossible de créer .picodbg/doxygen.");
            return;
        }
        cancelled = false;
        start->setEnabled(false);
        stop->setEnabled(true);
        open->setEnabled(false);
        log->clear();
        status->setText("Génération en cours…");
        process.start(program, {config.isEmpty() ? QString("-") : config});
    });
    QObject::connect(stop, &QPushButton::clicked, &dialog, [&] {
        cancelled = true;
        stop->setEnabled(false);
        process.kill();
    });
    QObject::connect(open, &QPushButton::clicked, &dialog, [&] {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(index)))
            status->setText("Ouvrir " + index + " dans un navigateur.");
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(500);
    }
}

}
