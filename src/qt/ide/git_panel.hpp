#pragma once
#include <QDialog>
#include <QProcess>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

namespace picoide {

// Optional project integration backed by the installed Git CLI.
class GitPanel : public QDialog {
public:
    GitPanel(const QString& project, const QString& git, QWidget* parent);
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    using Reply = std::function<void(bool, const QByteArray&, const QString&)>;
    QProcess git_;
    QString project_, root_, executable_;
    Reply reply_;
    QLabel* repository_;
    QLabel* status_;
    QTreeWidget* files_;
    QPlainTextEdit* preview_;
    QPushButton *init_, *stage_, *unstage_, *commit_, *remote_, *fetch_, *push_, *history_, *refresh_;
    QPushButton* abort_;

    void command(const QStringList& args, Reply reply, bool projectDirectory = false);
    void complete(bool ok, const QString& startError = {});
    void discover();
    void refresh();
    void actions();
    void diff();
    void index(bool stage);
    void commit();
    void remote();
    void change(const QStringList& args);
};

}
