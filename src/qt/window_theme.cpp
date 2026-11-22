#include "window_theme.hpp"
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QMenu>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>

namespace {
bool& lightTheme() {
    static bool light = QSettings().value("appearance/theme").toString() == "light";
    return light;
}

const QHash<QString, QString>& lightColors() {
    static const QHash<QString, QString> colors{
        {"#222426", "#f1f3f4"}, {"#eef0f1", "#22292d"}, {"#1b1d1f", "#ffffff"},
        {"#26282b", "#f5f7f8"}, {"#303337", "#e3e8eb"}, {"#345a55", "#b5ded5"},
        {"#3b4748", "#d1dfdf"}, {"#24567d", "#bcd8f4"}, {"#ffffff", "#172528"}, {"#fff", "#172528"},
        {"#54595f", "#819097"}, {"#404448", "#c4ced4"}, {"#383c40", "#c0c9ce"},
        {"#181a1c", "#68777e"}, {"#86d5c7", "#126557"}, {"#a8afb5", "#536169"},
        {"#a1a6aa", "#66757c"}, {"#282b2e", "#e9edef"}, {"#303639", "#e4efee"},
        {"#d1d5d8", "#3b484e"}, {"#34383b", "#e7ecee"}, {"#41474a", "#d4e3e2"},
        {"#76c8bd", "#167b6b"}, {"#8bd6bb", "#1c6950"}, {"#757b80", "#65777e"},
        {"#8b9db1", "#4b6275"}, {"#add7a2", "#2c6c3d"}, {"#83cbde", "#146581"}, {"#a8c7ff", "#174b9b"},
        {"#f5c47e", "#874a0e"}, {"#d5adf4", "#73389a"}, {"#8ce4d6", "#0e7065"},
        {"#ff9490", "#a62725"}, {"#f1ce80", "#805000"}, {"#ffb591", "#963e19"},
        {"#ffd780", "#8b4f00"}, {"#dce6f0", "#243746"}, {"#e5ac74", "#824612"},
        {"#234d57", "#c1ebe9"}, {"#514070", "#e5d8f6"}, {"#655021", "#fae29a"},
        {"#285168", "#c4e8fb"}, {"#554477", "#e9dafa"}, {"#365740", "#d3efd7"},
        {"#695028", "#fae7bf"}, {"#673e53", "#f6d8e8"}, {"#285a56", "#caeee9"}
    };
    return colors;
}

QString themedStyle(const QString& dark) {
    if (!lightTheme()) return dark;
    static const QRegularExpression color("#(?:[0-9a-f]{6}|[0-9a-f]{3})\\b");
    QString result;
    int end = 0;
    auto matches = color.globalMatch(dark);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += dark.mid(end, match.capturedStart() - end);
        result += lightColors().value(match.captured(), match.captured());
        end = match.capturedEnd();
    }
    return result + dark.mid(end);
}

QEvent::Type themeEvent() {
    static const auto type = static_cast<QEvent::Type>(QEvent::registerEventType());
    return type;
}

void setLightTheme(bool light) {
    if (lightTheme() == light) return;
    lightTheme() = light;
    QSettings().setValue("appearance/theme", light ? "light" : "dark");
    QList<QPointer<QWidget>> windows;
    for (auto* window : QApplication::topLevelWidgets()) {
        if (!window->property("picoThemed").toBool()) continue;
        applyPicoTheme(*window);
        windows.push_back(window);
    }
    for (const auto& window : windows) {
        if (!window) continue;
        QEvent event(themeEvent());
        QApplication::sendEvent(window, &event);
    }
}

class ThemeMenu : public QObject {
public:
    ThemeMenu(QMenu& views, QWidget& window, std::function<void()> repaint)
        : QObject(&window), repaint_(std::move(repaint)) {
        auto* menu = views.addMenu("Thème");
        auto* group = new QActionGroup(menu);
        light_ = menu->addAction("Clair");
        light_->setObjectName("picoThemeLight");
        dark_ = menu->addAction("Sombre");
        dark_->setObjectName("picoThemeDark");
        for (auto* action : {light_, dark_}) {
            action->setCheckable(true);
            group->addAction(action);
        }
        updateCheck();
        connect(light_, &QAction::triggered, this, [] { setLightTheme(true); });
        connect(dark_, &QAction::triggered, this, [] { setLightTheme(false); });
        window.installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() != themeEvent()) return false;
        updateCheck();
        if (repaint_) repaint_();
        return true;
    }
private:
    QAction* light_;
    QAction* dark_;
    std::function<void()> repaint_;

    void updateCheck() { (lightTheme() ? light_ : dark_)->setChecked(true); }
};
}

QColor picoThemeColor(const char* darkColor) {
    const auto dark = QString::fromLatin1(darkColor);
    return QColor(lightTheme() ? lightColors().value(dark, dark) : dark);
}

void addPicoThemeMenu(QMenu& views, QWidget& window, std::function<void()> repaint) {
    new ThemeMenu(views, window, std::move(repaint));
}

void applyPicoTheme(QWidget& window) {
    window.setProperty("picoThemed", true);
    QPalette theme = QApplication::palette();
    theme.setColor(QPalette::Window, picoThemeColor("#222426"));
    theme.setColor(QPalette::WindowText, picoThemeColor("#eef0f1"));
    theme.setColor(QPalette::Base, picoThemeColor("#1b1d1f"));
    theme.setColor(QPalette::AlternateBase, picoThemeColor("#26282b"));
    theme.setColor(QPalette::Text, picoThemeColor("#eef0f1"));
    theme.setColor(QPalette::Button, picoThemeColor("#303337"));
    theme.setColor(QPalette::ButtonText, picoThemeColor("#eef0f1"));
    theme.setColor(QPalette::Highlight, picoThemeColor("#24567d"));
    theme.setColor(QPalette::HighlightedText, picoThemeColor("#ffffff"));
    theme.setColor(QPalette::ToolTipBase, picoThemeColor("#303337"));
    theme.setColor(QPalette::ToolTipText, picoThemeColor("#eef0f1"));
    theme.setColor(QPalette::Light, picoThemeColor("#54595f"));
    theme.setColor(QPalette::Midlight, picoThemeColor("#404448"));
    theme.setColor(QPalette::Mid, picoThemeColor("#383c40"));
    theme.setColor(QPalette::Dark, picoThemeColor("#181a1c"));
    theme.setColor(QPalette::Shadow, picoThemeColor("#181a1c"));
    theme.setColor(QPalette::Link, picoThemeColor("#86d5c7"));
    theme.setColor(QPalette::PlaceholderText, picoThemeColor("#a8afb5"));
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        theme.setColor(QPalette::Disabled, role, picoThemeColor("#a1a6aa"));
    window.setPalette(theme);
    window.setStyleSheet(themedStyle(
        "QMainWindow,QDialog,QDockWidget {background:#222426;color:#eef0f1;}"
        "QLabel,QCheckBox,QRadioButton,QGroupBox {color:#eef0f1;}"
        "QLabel:disabled,QCheckBox:disabled,QRadioButton:disabled {color:#a1a6aa;}"
        "QMenuBar,QMenu,QToolBar,QStatusBar {background:#282b2e;color:#eef0f1;}"
        "QMenu::item:selected,QMenuBar::item:selected {background:#345a55;color:#fff;}"
        "QDockWidget::title {background:#303337;padding:7px;font-weight:600;}"
        "QAbstractItemView,QPlainTextEdit,QTextEdit {background:#1b1d1f;color:#eef0f1;"
        "alternate-background-color:#26282b;selection-background-color:#24567d;selection-color:#fff;border:1px solid #404448;}"
        "QTreeView::item:selected {background:#24567d;color:#fff;}"
        "QTreeView::item:selected:!active {background:#24567d;color:#fff;}"
        "QTreeView::item:hover:!selected {background:#303639;color:#fff;}"
        "QHeaderView::section {background:#303337;color:#d1d5d8;padding:5px;border:none;}"
        "QLineEdit,QComboBox,QSpinBox {background:#1b1d1f;color:#eef0f1;border:1px solid #54595f;border-radius:3px;"
        "padding:5px;selection-background-color:#24567d;selection-color:#fff;}"
        "QPushButton {background:#34383b;color:#eef0f1;border:1px solid #54595f;border-radius:3px;padding:5px 9px;}"
        "QPushButton:hover,QToolButton:hover {background:#41474a;}"
        "QToolButton {background:#303337;color:#eef0f1;border:none;border-radius:3px;padding:7px;}"
        "QTabWidget::pane {border:1px solid #404448;}"
        "QTabBar {qproperty-drawBase:0;}"
        "QTabBar::tab {background:#282b2e;color:#d1d5d8;padding:8px 12px;border-bottom:2px solid transparent;}"
        "QTabBar::tab:selected {background:#303337;color:#fff;border-bottom:2px solid #76c8bd;}"
        "QTabBar::tab:hover:!selected {background:#34383b;}"
        "QLabel#sectionTitle,QLabel#ideProjectTitle,QLabel#sessionTitle {font-size:16px;font-weight:600;color:#fff;}"
        "QLabel#sessionState {color:#8bd6bb;font-weight:600;}"
        "QLabel#guidance {color:#d1d5d8;}"
        "QPushButton:disabled,QToolButton:disabled {color:#a1a6aa;background:#282b2e;}"
        "QLineEdit:focus,QComboBox:focus {border:1px solid #76c8bd;}"
        "QScrollBar {background:#26282b;}"
        "QScrollBar:vertical {width:12px;} QScrollBar:horizontal {height:12px;}"
        "QScrollBar::handle {background:#54595f;border:2px solid #26282b;border-radius:4px;min-width:24px;min-height:24px;}"
        "QScrollBar::handle:hover {background:#757b80;}"
        "QScrollBar::add-line,QScrollBar::sub-line {width:0;height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page {background:transparent;}"
        "QToolTip {color:#eef0f1;background:#303337;border:1px solid #54595f;}"
        "QToolBar#ideToolbar {spacing:3px;padding:4px;}"
        "QLabel#ideAnalysisStatus,QLabel#ideTargetStatus {padding:0 7px;color:#d1d5d8;}"
        "QToolButton#ideCloseTab {background:transparent;padding:0 4px;color:#d1d5d8;}"
        "QToolButton#ideCloseTab:hover {background:#41474a;color:#fff;}"
    ));
}
