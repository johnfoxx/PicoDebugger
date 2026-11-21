#include "window_theme.hpp"
#include <QApplication>
#include <QPalette>
#include <QWidget>

void applyPicoTheme(QWidget& window) {
    QPalette theme = QApplication::palette();
    theme.setColor(QPalette::Window, QColor("#222426"));
    theme.setColor(QPalette::WindowText, QColor("#eef0f1"));
    theme.setColor(QPalette::Base, QColor("#1b1d1f"));
    theme.setColor(QPalette::AlternateBase, QColor("#26282b"));
    theme.setColor(QPalette::Text, QColor("#eef0f1"));
    theme.setColor(QPalette::Button, QColor("#303337"));
    theme.setColor(QPalette::ButtonText, QColor("#eef0f1"));
    theme.setColor(QPalette::Highlight, QColor("#345a55"));
    theme.setColor(QPalette::Inactive, QPalette::Highlight, QColor("#3b4748"));
    theme.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    theme.setColor(QPalette::ToolTipBase, QColor("#303337"));
    theme.setColor(QPalette::ToolTipText, QColor("#eef0f1"));
    theme.setColor(QPalette::Light, QColor("#54595f"));
    theme.setColor(QPalette::Midlight, QColor("#404448"));
    theme.setColor(QPalette::Mid, QColor("#383c40"));
    theme.setColor(QPalette::Dark, QColor("#181a1c"));
    theme.setColor(QPalette::Shadow, QColor("#181a1c"));
    theme.setColor(QPalette::Link, QColor("#86d5c7"));
    theme.setColor(QPalette::PlaceholderText, QColor("#a8afb5"));
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        theme.setColor(QPalette::Disabled, role, QColor("#a1a6aa"));
    window.setPalette(theme);
    window.setStyleSheet(
        "QMainWindow,QDialog,QDockWidget {background:#222426;color:#eef0f1;}"
        "QLabel,QCheckBox,QRadioButton,QGroupBox {color:#eef0f1;}"
        "QLabel:disabled,QCheckBox:disabled,QRadioButton:disabled {color:#a1a6aa;}"
        "QMenuBar,QMenu,QToolBar,QStatusBar {background:#282b2e;color:#eef0f1;}"
        "QMenu::item:selected,QMenuBar::item:selected {background:#345a55;color:#fff;}"
        "QDockWidget::title {background:#303337;padding:7px;font-weight:600;}"
        "QAbstractItemView,QPlainTextEdit,QTextEdit {background:#1b1d1f;color:#eef0f1;"
        "alternate-background-color:#26282b;selection-background-color:#345a55;selection-color:#fff;border:1px solid #404448;}"
        "QTreeView::item:selected {background:#345a55;color:#fff;}"
        "QTreeView::item:selected:!active {background:#3b4748;color:#fff;}"
        "QTreeView::item:hover:!selected {background:#303639;color:#fff;}"
        "QHeaderView::section {background:#303337;color:#d1d5d8;padding:5px;border:none;}"
        "QLineEdit,QComboBox,QSpinBox {background:#1b1d1f;color:#eef0f1;border:1px solid #54595f;border-radius:3px;"
        "padding:5px;selection-background-color:#345a55;selection-color:#fff;}"
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
    );
}
