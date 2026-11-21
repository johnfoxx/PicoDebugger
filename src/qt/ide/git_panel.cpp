#include "git_panel.hpp"

#include <QCloseEvent>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace picoide {

GitPanel::GitPanel(const QString& project, const QString& git, QWidget* parent)
    : QDialog(parent), project_(project), executable_(git) {
    setWindowTitle("Gestion de configuration · Git");
    setObjectName("ideGitPanel");
    resize(880, 600);
    auto* layout = new QVBoxLayout(this);
    repository_ = new QLabel;
    repository_->setTextFormat(Qt::PlainText);
    status_ = new QLabel;
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(repository_);
    auto* buttons = new QHBoxLayout;
    const auto button = [this, buttons](const QString& text, const char* name, const std::function<void()>& action) {
        auto* widget = new QPushButton(text);
        widget->setObjectName(name);
        buttons->addWidget(widget);
        connect(widget, &QPushButton::clicked, this, action);
        return widget;
    };
    init_ = button("Initialiser", "gitInit", [this] {
        command({"init"}, [this](bool ok, const QByteArray&, const QString& error) {
            if (ok) discover();
            else status_->setText(error);
        }, true);
    });
    stage_ = button("Indexer", "gitStage", [this] { index(true); });
    unstage_ = button("Désindexer", "gitUnstage", [this] { index(false); });
    commit_ = button("Commit…", "gitCommit", [this] { commit(); });
    history_ = button("Historique", "gitHistory", [this] {
        command({"log", "-30", "--format=%h  %s"}, [this](bool ok, const QByteArray& out, const QString& error) {
            if (ok) preview_->setPlainText(QString::fromUtf8(out));
            else status_->setText(error);
        });
    });
    remote_ = button("Distant…", "gitRemote", [this] { remote(); });
    fetch_ = button("Récupérer", "gitFetch", [this] { change({"fetch", "origin"}); });
    push_ = button("Pousser", "gitPush", [this] { change({"push", "-u", "origin", "HEAD"}); });
    refresh_ = button("Actualiser", "gitRefresh", [this] {
        if (root_.isEmpty()) discover();
        else refresh();
    });
    abort_ = button("Annuler", "gitAbort", [this] { git_.terminate(); });
    buttons->addStretch();
    layout->addLayout(buttons);
    auto* split = new QSplitter(Qt::Vertical);
    files_ = new QTreeWidget;
    files_->setObjectName("gitChanges");
    files_->setHeaderLabels({"Index", "Copie de travail", "Fichier"});
    files_->setRootIsDecorated(false);
    files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files_->setColumnWidth(0, 76);
    files_->setColumnWidth(1, 122);
    preview_ = new QPlainTextEdit;
    preview_->setObjectName("gitDiff");
    preview_->setReadOnly(true);
    preview_->setLineWrapMode(QPlainTextEdit::NoWrap);
    split->addWidget(files_); split->addWidget(preview_);
    layout->addWidget(split, 1);
    layout->addWidget(status_);

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("GIT_TERMINAL_PROMPT", "0");
    environment.insert("GIT_PAGER", "cat");
    git_.setProcessEnvironment(environment);
    connect(&git_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus state) { complete(state == QProcess::NormalExit && code == 0); });
    connect(&git_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false, git_.errorString());
    });
    connect(files_, &QTreeWidget::itemSelectionChanged, this, [this] { actions(); diff(); });
    if (!executable_.isEmpty()) discover();
    else { status_->setText("Git est introuvable. Renseigner son chemin dans Outils du projet."); actions(); }
}

void GitPanel::command(const QStringList& args, Reply reply, bool projectDirectory) {
    if (reply_ || executable_.isEmpty()) return;
    reply_ = std::move(reply);
    actions();
    git_.setWorkingDirectory(projectDirectory ? project_ : root_);
    QStringList options{"--no-pager", "--literal-pathspecs", "-c", "color.ui=false"};
    options += args;
    git_.start(executable_, options, QIODevice::ReadOnly);
}

void GitPanel::complete(bool ok, const QString& startError) {
    auto callback = std::move(reply_);
    reply_ = {};
    const auto error = QString::fromUtf8(git_.readAllStandardError()).trimmed();
    const auto out = git_.readAllStandardOutput();
    if (callback) callback(ok, out, startError.isEmpty() ? error : startError);
    actions();
}

void GitPanel::discover() {
    command({"rev-parse", "--show-toplevel"}, [this](bool ok, const QByteArray& out, const QString&) {
        root_ = ok ? QString::fromUtf8(out).trimmed() : QString();
        if (root_.isEmpty()) {
            repository_->setText(project_);
            status_->setText("Ce projet ne fait pas partie d'un dépôt Git.");
        } else {
            repository_->setText(root_);
            refresh();
        }
    }, true);
}

void GitPanel::refresh() {
    command({"status", "--porcelain=v1", "--branch", "-z", "--untracked-files=all"},
        [this](bool ok, const QByteArray& out, const QString& error) {
            if (!ok) { status_->setText(error); return; }
            files_->clear();
            const auto records = out.split('\0');
            QString branch;
            for (int index = 0; index < records.size(); ++index) {
                const auto& record = records.at(index);
                if (record.startsWith("## ")) { branch = QString::fromUtf8(record.mid(3)); continue; }
                if (record.size() < 4) continue;
                const auto path = QString::fromUtf8(record.mid(3));
                auto* item = new QTreeWidgetItem(files_, {QString::fromLatin1(record.left(1)).trimmed(),
                    QString::fromLatin1(record.mid(1, 1)).trimmed(), path});
                QStringList paths{path};
                if ((record.left(2).contains('R') || record.left(2).contains('C')) && index + 1 < records.size()) {
                    const auto old = QString::fromUtf8(records.at(++index));
                    item->setText(2, old + " → " + path);
                    paths << old;
                }
                item->setData(2, Qt::UserRole, paths);
            }
            repository_->setText(branch.isEmpty() ? root_ : root_ + "  ·  " + branch);
            status_->setText(files_->topLevelItemCount() ? "Sélectionner les fichiers à indexer." : "Copie de travail propre.");
        });
}

void GitPanel::actions() {
    const bool idle = !reply_ && !executable_.isEmpty();
    const bool repo = !root_.isEmpty();
    const bool selected = !files_->selectedItems().isEmpty();
    init_->setEnabled(idle && !repo);
    refresh_->setEnabled(idle);
    abort_->setEnabled(bool(reply_));
    stage_->setEnabled(idle && repo && selected);
    unstage_->setEnabled(stage_->isEnabled());
    for (auto* button : {commit_, history_, remote_, fetch_, push_}) button->setEnabled(idle && repo);
}

void GitPanel::diff() {
    const auto selected = files_->selectedItems();
    if (selected.size() != 1 || root_.isEmpty()) { preview_->clear(); return; }
    if (selected.first()->text(0) == "?") { preview_->setPlainText("Fichier non suivi. L'indexer pour afficher le diff."); return; }
    const auto paths = selected.first()->data(2, Qt::UserRole).toStringList();
    command(QStringList{"diff", "--no-ext-diff", "--no-textconv", "--cached", "--"} + paths,
        [this, paths](bool ok, const QByteArray& indexed, const QString& error) {
            if (!ok) { status_->setText(error); return; }
            command(QStringList{"diff", "--no-ext-diff", "--no-textconv", "--"} + paths,
                [this, indexed](bool success, const QByteArray& worktree, const QString& reason) {
                    if (success) preview_->setPlainText(QString::fromUtf8(indexed + worktree));
                    else status_->setText(reason);
                });
        });
}

void GitPanel::index(bool stage) {
    QStringList paths;
    for (auto* file : files_->selectedItems()) paths += file->data(2, Qt::UserRole).toStringList();
    if (paths.isEmpty()) return;
    paths.removeDuplicates();
    change((stage ? QStringList{"add", "-A", "--"} : QStringList{"reset", "--"}) + paths);
}

void GitPanel::commit() {
    bool accepted;
    const auto message = QInputDialog::getMultiLineText(this, "Commit Git", "Message du commit", {}, &accepted).trimmed();
    if (accepted && !message.isEmpty()) change({"commit", "-m", message});
}

void GitPanel::remote() {
    command({"remote", "get-url", "origin"}, [this](bool exists, const QByteArray& out, const QString&) {
        bool accepted;
        const auto current = exists ? QString::fromUtf8(out).trimmed() : QString();
        const auto url = QInputDialog::getText(this, "Dépôt distant", "Adresse HTTPS ou SSH du dépôt origin",
                                               QLineEdit::Normal, current, &accepted).trimmed();
        if (accepted && !url.isEmpty() && url != current)
            change({"remote", exists ? "set-url" : "add", "origin", url});
    });
}

void GitPanel::change(const QStringList& args) {
    command(args, [this](bool ok, const QByteArray& out, const QString& error) {
        if (!ok) { status_->setText(error.isEmpty() ? "La commande Git a échoué." : error); return; }
        preview_->setPlainText(QString::fromUtf8(out) + error);
        refresh();
    });
}

void GitPanel::closeEvent(QCloseEvent* event) {
    if (reply_) {
        status_->setText("Une commande Git est en cours.");
        event->ignore();
    } else QDialog::closeEvent(event);
}

void GitPanel::reject() {
    if (reply_) status_->setText("Attendre ou annuler la commande Git avant de fermer.");
    else QDialog::reject();
}

}
