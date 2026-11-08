#include "main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PicoDebugger");
    QApplication::setOrganizationName("PicoDebugger");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QCommandLineParser parser;
    parser.setApplicationDescription("Débogueur ARM/Thumb hors ligne — émulation partielle");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"demo", "Ouvrir l'exemple intégré à l'arrêt"});
    parser.addOption({"smoke-test", "Fermer automatiquement après initialisation (validation de la GUI)"});
    parser.addPositionalArgument("elf", "Exécutable ELF32 ARM", "[elf]");
    parser.process(app);
    MainWindow window;
    if (parser.isSet("demo")) window.loadDemo();
    else if (!parser.positionalArguments().isEmpty()) window.loadFile(parser.positionalArguments().first());
    if (parser.isSet("smoke-test")) window.show();
    else window.showMaximized();
    if (parser.isSet("smoke-test")) QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
