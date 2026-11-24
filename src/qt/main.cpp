#include "main_window.hpp"
#include "ide/ide_window.hpp"
#include "window_policy.hpp"
#include "qt_diagnostics.hpp"
#include "qt_input_policy.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char** argv) {
    const bool diagnostics = installQtDiagnostics(argc, argv);
    prepareQtInputMethod(argc, argv);
    QApplication app(argc, argv);
    if (diagnostics) reportQtEnvironment();
    QApplication::setApplicationName("PicoDebugger");
    QApplication::setOrganizationName("PicoDebugger");
    QApplication::setApplicationVersion(PICODBG_VERSION);
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QCommandLineParser parser;
    parser.setApplicationDescription("IDE C et débogueur ARM/Thumb pour Raspberry Pi Pico");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"demo", "Ouvrir l'exemple intégré à l'arrêt"});
    parser.addOption({"ide", "Afficher la fenêtre IDE au démarrage"});
    parser.addOption({"debugger", "Afficher directement le débogueur"});
    parser.addOption({"chip", "Profil matériel : rp2040 ou rp2350", "chip", "rp2040"});
    parser.addOption({"board", "Carte : pico, pico_w, pico2 ou pico2_w (exclusif avec --chip)", "board"});
    parser.addOption({"smoke-test", "Fermer automatiquement après initialisation (validation de la GUI)"});
    parser.addOption({"qt-diagnostics", "Tracer les avertissements Qt de focus et de QSocketNotifier (y compris au démarrage)"});
    parser.addOption({"qt-system-input", "Conserver QT_IM_MODULE sous Qt5/Wayland, y compris IBus (désactive le contournement de démarrage)"});
    parser.addPositionalArgument("fichier", "Dossier de projet CMake ou exécutable ELF32 ARM", "[projet|elf]");
    parser.process(app);
    if (parser.value("chip") != "rp2040" && parser.value("chip") != "rp2350") parser.showHelp(1);
    picodbg::Board board = parser.value("chip") == "rp2350" ? picodbg::Board::Pico2 : picodbg::Board::Pico;
    if (parser.isSet("board") && (parser.isSet("chip") || !picodbg::parseBoard(parser.value("board").toStdString(), board))) parser.showHelp(1);
    if (parser.isSet("ide") && parser.isSet("debugger")) parser.showHelp(1);
    const bool smoke = parser.isSet("smoke-test");
    if (smoke) QTimer::singleShot(150, &app, &QCoreApplication::quit);
    const auto input = parser.positionalArguments().value(0);
    const auto project = QFileInfo(input).isDir() ? input
        : QFileInfo(input).fileName() == "CMakeLists.txt" ? QFileInfo(input).absolutePath() : QString();
    if (!parser.isSet("debugger") && (parser.isSet("ide") ||
        (!parser.isSet("demo") && (input.isEmpty() || !project.isEmpty())))) {
        picoide::IdeWindow ide(nullptr, board);
        if (parser.isSet("demo")) ide.prepareDemo();
        else if (!project.isEmpty()) ide.openProject(project);
        else if (!input.isEmpty()) ide.prepareElf(input);
        else if (!smoke) ide.restoreSessionProject();
        showInspectionWindow(&ide);
        return app.exec();
    }
    MainWindow window;
    prepareInspectionWindow(&window);
    window.selectBoard(board);
    if (parser.isSet("demo")) window.loadDemo();
    else if (!input.isEmpty()) window.loadFile(input);
    if (smoke) window.show();
    else window.showMaximized();
    return app.exec();
}
