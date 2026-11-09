#include "choice_box.hpp"
#include "qt_input_policy.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QEventLoop>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QWindow>
#include <QTimer>
#include <dlfcn.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned activationRequests = 0;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void key(QWidget* widget, int code) {
    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
    QApplication::processEvents();
}
struct EnvironmentValue {
    const char* name;
    QByteArray value;
    explicit EnvironmentValue(const char* key) : name(key), value(qgetenv(key)) {}
    ~EnvironmentValue() { if (value.isNull()) qunsetenv(name); else qputenv(name, value); }
};
bool prepare(std::vector<QByteArray> arguments) {
    std::vector<char*> argv;
    for (auto& arg : arguments) argv.push_back(arg.data());
    return prepareQtInputMethod(static_cast<int>(argv.size()), argv.data());
}
void inputPolicyTests() {
    const EnvironmentValue savedPlatform("QT_QPA_PLATFORM"), savedInput("QT_IM_MODULE");
    qputenv("QT_QPA_PLATFORM", "wayland"); qputenv("QT_IM_MODULE", "ibus");
    require(prepare({"test"}) && qgetenv("QT_IM_MODULE").isNull(), "Qt5 Wayland must avoid premature IBus construction");
    qputenv("QT_IM_MODULE", "ibus");
    require(!prepare({"test", "--qt-system-input"}) && qgetenv("QT_IM_MODULE") == "ibus", "Explicit system input preference lost");
    require(!prepare({"test", "-platform", "xcb"}) && qgetenv("QT_IM_MODULE") == "ibus", "X11 input preference must be preserved");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    require(prepare({"test", "-platform=wayland"}), "Qt command-line platform takes precedence over environment");
    qputenv("QT_QPA_PLATFORM", "wayland"); qputenv("QT_IM_MODULE", "compose");
    require(!prepare({"test"}) && qgetenv("QT_IM_MODULE") == "compose", "Other input methods must be preserved");
}
}

// Linux/ELF test-only interposition at the Qt Widgets -> Qt Gui boundary.
// Count requests even with the offscreen plugin, which cannot emit the Wayland
// warning. Forward to Qt so neither focus changes nor real activations are
// suppressed by the test. Production binaries never link this implementation.
void QWindow::requestActivate() {
    ++activationRequests;
    using Function = void (*)(QWindow*);
    static const auto original = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "_ZN7QWindow15requestActivateEv"));
    if (!original) std::abort();
    original(this);
}

int main(int argc, char** argv) {
    try {
        inputPolicyTests();
        QApplication app(argc, argv);
        ChoiceBox choice;
        choice.addItems({"Pico", "Pico 2", "Indisponible"});
        auto* model = qobject_cast<QStandardItemModel*>(choice.model());
        require(model != nullptr, "Missing choice model");
        model->item(2)->setEnabled(false);
        choice.show();
        QApplication::processEvents();
        // Positive control: a request from Qt Widgets must reach the probe.
        choice.activateWindow();
        require(activationRequests > 0, "Activation probe not active");
        QApplication::processEvents();
        activationRequests = 0;

        int changes = 0;
        int selected = -1;
        QObject::connect(&choice, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                         [&changes] { ++changes; });
        QObject::connect(&choice, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
                         [&selected](int index) { selected = index; });
        choice.showPopup();
        QApplication::processEvents();
        auto* view = choice.view();
        require(view->isVisible(), "Popup not opened");
        key(view, Qt::Key_Down);
        key(view, Qt::Key_Down); // The disabled third row must be skipped.
        key(view, Qt::Key_Return);
        require(choice.currentIndex() == 1 && selected == 1 && changes == 1, "Keyboard selection lost");
        require(!view->isVisible(), "Selection did not close popup");
        require(activationRequests == 0, "Selection requested window activation");
        // Offscreen does not simulate the compositor restoring the owner.
        // Supply that notification through Qt, then check deferred keyboard
        // focus without allowing an additional request from ChoiceBox.
        choice.windowHandle()->requestActivate();
        QApplication::processEvents();
        require(choice.hasFocus() && activationRequests == 1, "Deferred keyboard focus not restored");
        activationRequests = 0;

        choice.showPopup();
        key(view, Qt::Key_Up);
        key(view, Qt::Key_Escape);
        require(choice.currentIndex() == 1 && changes == 1, "Escape changed selection");
        require(!view->isVisible() && activationRequests == 0, "Cancel requested activation");

        choice.showPopup();
        choice.hidePopup();
        require(!view->isVisible() && activationRequests == 0, "Programmatic close requested activation");
        choice.showPopup();
        QEventLoop settle;
        QTimer::singleShot(180, &settle, &QEventLoop::quit);
        settle.exec(); // Allow Qt's protection against the opening click to expire.
        auto* viewport = view->viewport();
        const auto point = view->visualRect(model->index(0, 0)).center();
        const QPointF local(point), global(viewport->mapToGlobal(point));
        QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move); QApplication::sendEvent(viewport, &press); QApplication::sendEvent(viewport, &release);
        QApplication::processEvents();
        require(choice.currentIndex() == 0 && changes == 2 && !view->isVisible(), "Mouse selection failed");
        require(activationRequests == 0, "Mouse selection requested activation");
        std::cout << "Popup checks passed: mouse/keyboard selection, cancellation, focus, input policy; 0 unexpected activation requests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Popup FAIL: " << error.what() << '\n';
        return 1;
    }
}
