#pragma once

#include "picodbg/board.hpp"
#include <QProcess>
#include <QStringList>
#include <functional>
#include <vector>

namespace picoide {

struct Tools {
    QString cmake, gcc, sdk, clangd, gdb, openocd;
    static Tools detect();
    QProcessEnvironment buildEnvironment() const;
};

enum class Template { Sdk, Emulator };
QString createProject(const QString& parent, const QString& name, picodbg::Board board, Template kind);
QString boardName(picodbg::Board board);
QString projectBuildDirectory(const QString& root, picodbg::Board board, const Tools& tools);

struct Artifact {
    QString target;
    QString path;
};

class Build : public QObject {
public:
    explicit Build(QObject* parent);
    ~Build() override;
    bool busy() const { return stage_ != Idle; }
    void start(const QString& root, const QString& directory, picodbg::Board board,
               const Tools& tools, const QString& target, bool configureOnly);
    void cancel();
    std::function<void(const QString&)> output;
    std::function<void(bool, const QString&, const std::vector<Artifact>&)> finished;

private:
    class Process : public QProcess {
    public:
        Process();
        void stopTree(bool force);
    protected:
#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        void setupChildProcess() override;
#endif
    };
    enum Stage { Idle, Configure, Compile };
    Stage stage_ = Idle;
    Process process_;
    QByteArray outputBuffer_;
    QString directory_, tool_, target_;
    bool configureOnly_ = false;
    bool cancelled_ = false;
    std::vector<Artifact> artifacts_;
    void run(const QStringList& args);
    void readOutput(bool flush = false);
    void stopped(int code, QProcess::ExitStatus status);
    void complete(bool success, const QString& message);
};

}
