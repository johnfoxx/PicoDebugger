#include "toolchain_catalog.hpp"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QResource>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

static void initToolsResources() {
    Q_INIT_RESOURCE(ide_tools_resources);
}

namespace picoide {
namespace {
void resources() {
    static const bool ready = [] { initToolsResources(); return true; }();
    Q_UNUSED(ready);
}

QByteArray contents(const QString& path, qint64 limit = 131072) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) return {};
    return file.readAll();
}

bool write(const QString& path, const QByteArray& data) {
    if (data.isEmpty()) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QString toolDirectory() {
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return data.isEmpty() ? QString() : QDir(data).filePath("toolchains");
}

QString catalogPath() {
    resources();
    const auto location = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (location.isEmpty() || !QDir().mkpath(location)) return {};
    const auto path = QDir(location).filePath("dependencies.json");
    if (!QFileInfo::exists(path) && !write(path, contents(":/ide/tools/dependencies.json"))) return {};
    return path;
}

QJsonObject info(const QString& path) {
    return QJsonDocument::fromJson(contents(path)).object();
}

QString platformName() {
    const auto cpu = QSysInfo::currentCpuArchitecture();
    return QSysInfo::kernelType() + "-" + (cpu == "arm64" ? QString("aarch64") : cpu == "amd64" ? QString("x86_64") : cpu);
}

class DownloadDialog : public QDialog {
public:
    DownloadDialog(const QString& catalog, QWidget* parent)
        : QDialog(parent), catalog_(catalog), prefix_(toolDirectory()), platform_(platformName()) {
        setWindowTitle("Installer des outils dans PicoDebugger");
        setObjectName("ideLocalTools");
        resize(825, 575);
        const auto data = info(catalog_);
        auto* layout = new QVBoxLayout(this);
        auto* folder = new QLabel(prefix_);
        folder->setTextFormat(Qt::PlainText);
        folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(folder);
        list_ = new QTreeWidget;
        list_->setObjectName("ideLocalPackages");
        list_->setRootIsDecorated(false);
        list_->setHeaderLabels({"Outil", "Version", "État"});
        list_->setColumnWidth(0, 285);
        const QDir registrations(QDir(prefix_).filePath("registered"));
        if (data.value("schema").toInt() == 1) {
            for (const auto& item : data.value("packages").toArray()) {
                const auto package = item.toObject();
                const auto id = package.value("id").toString();
                const auto available = !package.value("repository").toString().isEmpty() ||
                    package.value("platforms").toObject().contains(platform_);
                if (id.isEmpty() || !available) continue;
                const auto installed = QFileInfo::exists(registrations.filePath(id + ".json"));
                auto* row = new QTreeWidgetItem(list_, {package.value("name").toString(),
                    package.value("version").toString(), installed ? "Installé localement" : "Disponible"});
                row->setData(0, Qt::UserRole, id);
                row->setCheckState(0, Qt::Unchecked);
            }
        }
        layout->addWidget(list_, 2);
        log_ = new QPlainTextEdit;
        log_->setObjectName("ideInstallLog");
        log_->setReadOnly(true); log_->setMaximumBlockCount(1200);
        layout->addWidget(log_, 1);
        status_ = new QLabel(list_->topLevelItemCount() ? "Sélectionner les outils à télécharger."
                                                       : "Catalogue invalide ou aucun paquet disponible pour cette plateforme.");
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* row = new QHBoxLayout;
        auto* open = new QPushButton("Ouvrir le catalogue…");
        connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(catalog_)); });
        start_ = new QPushButton("Installer la sélection");
        start_->setObjectName("ideInstallSelected");
        cancel_ = new QPushButton("Annuler");
        auto* close = new QPushButton("Fermer");
        row->addWidget(open); row->addStretch(); row->addWidget(start_); row->addWidget(cancel_); row->addWidget(close);
        layout->addLayout(row);
        connect(start_, &QPushButton::clicked, this, [this] { start(); });
        connect(cancel_, &QPushButton::clicked, this, [this] {
            status_->setText("Annulation en cours…");
            cancel_->setEnabled(false);
            process_.terminate();
        });
        connect(close, &QPushButton::clicked, this, [this] { reject(); });
        connect(list_, &QTreeWidget::itemChanged, this, [this] { actions(); });
        process_.setProcessChannelMode(QProcess::MergedChannels);
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
            log_->moveCursor(QTextCursor::End);
            log_->insertPlainText(QString::fromLocal8Bit(process_.readAllStandardOutput()));
            log_->ensureCursorVisible();
        });
        connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus state) {
                running_ = false;
                status_->setText(state == QProcess::NormalExit && code == 0 ? "Installation terminée."
                    : code == 2 ? "Installation annulée." : "Installation incomplète. Voir les détails ci-dessus.");
                const QDir registrations(QDir(prefix_).filePath("registered"));
                for (int i = 0; i < list_->topLevelItemCount(); ++i) {
                    auto* item = list_->topLevelItem(i);
                    if (QFileInfo::exists(registrations.filePath(item->data(0, Qt::UserRole).toString() + ".json")))
                        item->setText(2, "Installé localement");
                }
                actions();
            });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                running_ = false;
                status_->setText(process_.errorString());
                actions();
            }
        });
        actions();
    }

    bool attempted() const { return attempted_; }
    void reject() override {
        if (running_) {
            status_->setText("Attendre la fin ou annuler l'installation.");
            return;
        }
        QDialog::reject();
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (running_) { event->ignore(); reject(); }
        else QDialog::closeEvent(event);
    }

private:
    QTemporaryDir temp_;
    QProcess process_;
    QString catalog_, prefix_, platform_;
    QTreeWidget* list_;
    QPlainTextEdit* log_;
    QLabel* status_;
    QPushButton *start_, *cancel_;
    bool running_ = false;
    bool attempted_ = false;

    QStringList selected() const {
        QStringList items;
        for (int i = 0; i < list_->topLevelItemCount(); ++i) {
            const auto* item = list_->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked) items << item->data(0, Qt::UserRole).toString();
        }
        return items;
    }
    void actions() {
        start_->setEnabled(!running_ && !selected().isEmpty() && !prefix_.isEmpty());
        cancel_->setEnabled(running_);
        list_->setEnabled(!running_);
    }
    void start() {
        const auto python = QStandardPaths::findExecutable("python3");
        const auto script = QDir(temp_.path()).filePath("install-ide-tools.py");
        if (python.isEmpty()) { status_->setText("Python 3 est nécessaire pour installer les outils."); return; }
        if (!temp_.isValid() || !write(script, contents(":/ide/tools/install-ide-tools.py"))) {
            status_->setText("Impossible de préparer l'installateur."); return;
        }
        const auto git = QSettings().value("ide/git", "git").toString();
        QStringList arguments{"-u", script, "--catalog", catalog_, "--prefix", prefix_, "--platform", platform_,
                              "--git", git, "--"};
        arguments += selected();
        running_ = attempted_ = true;
        log_->clear();
        status_->setText("Téléchargement et installation…");
        actions();
        process_.start(python, arguments);
    }
};
}

QJsonObject installedTools() {
    const auto root = toolDirectory();
    const QDir registrations(QDir(root).filePath("registered"));
    QJsonObject tools;
    if (root.isEmpty()) return tools;
    for (const auto& name : registrations.entryList({"*.json"}, QDir::Files)) {
        const auto details = info(registrations.filePath(name));
        for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
            const auto path = it.value().toString();
            const auto canonical = QFileInfo(path).canonicalFilePath();
            if (!canonical.isEmpty() && canonical.startsWith(QDir(root).canonicalPath() + '/'))
                tools.insert(it.key(), path);
        }
    }
    return tools;
}

QString dependencyLinks() {
    const auto links = info(catalogPath()).value("links").toObject();
    QStringList html;
    for (auto it = links.constBegin(); it != links.constEnd(); ++it) {
        const auto url = QUrl(it.value().toString());
        if (url.scheme() == "https" && !url.host().isEmpty())
            html << "<a href='" + url.toString(QUrl::FullyEncoded).toHtmlEscaped() + "'>" + it.key().toHtmlEscaped() + "</a>";
    }
    return html.join(" · ");
}

bool manageLocalTools(QWidget* parent) {
    const auto path = catalogPath();
    if (path.isEmpty()) {
        QMessageBox::warning(parent, "Installation locale", "Impossible de préparer le catalogue des dépendances.");
        return false;
    }
    DownloadDialog dialog(path, parent);
    dialog.exec();
    return dialog.attempted();
}

}
