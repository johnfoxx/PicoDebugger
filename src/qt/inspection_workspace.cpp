#include "inspection_workspace.hpp"
#include "window_theme.hpp"
#include "window_policy.hpp"
#include <QDialog>
#include <QTabBar>
#include <QVBoxLayout>

InspectionWorkspace::InspectionWorkspace(QWidget* parent) : QTabWidget(parent) {
    setObjectName("inspectionWorkspace");
    setDocumentMode(true); setTabsClosable(true); setUsesScrollButtons(true);
    tabBar()->setAutoHide(true);
    setMovable(true);
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* page = widget(index);
        for (const auto& entry : entries_) {
            if (entry.second.page == page) { const QString key = entry.first; closeInspection(key); return; }
        }
    });
}
InspectionWorkspace::~InspectionWorkspace() { clearInspections(); }
void InspectionWorkspace::addFixedPage(QWidget* page, const QString& title) {
    const int index = addTab(page, title);
    tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
    tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
}
void InspectionWorkspace::addInspection(const QString& key, const QString& title, QWidget* page,
                                        std::function<void(bool)> presentationChanged) {
    entries_[key] = {title, page, nullptr, std::move(presentationChanged)};
    setCurrentIndex(addTab(page, title));
    setTabToolTip(indexOf(page), title + " — inspection ; détachable à la demande");
}
bool InspectionWorkspace::revealInspection(const QString& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return false;
    if (found->second.window) showInspectionWindow(found->second.window);
    else setCurrentWidget(found->second.page);
    return true;
}
QByteArray InspectionWorkspace::detachedGeometry(const QWidget* page) const {
    for (const auto& entry : entries_)
        if (entry.second.page == page && entry.second.window) return entry.second.window->saveGeometry();
    return {};
}
void InspectionWorkspace::toggleDetached(const QString& key, const QByteArray& geometry) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return;
    auto& entry = found->second;
    if (entry.window) { entry.window->close(); return; }
    auto* window = new QDialog(this, Qt::Window);
    applyPicoTheme(*window);
    window->setObjectName("deviceWindow_" + key);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(entry.title + " — inspecteur PicoDebugger");
    window->resize(880, key.startsWith("UART") ? 790 : 650);
    if (!geometry.isEmpty()) window->restoreGeometry(geometry);
    prepareInspectionWindow(window);
    removeTab(indexOf(entry.page));
    auto* layout = new QVBoxLayout(window); layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(entry.page);
    entry.window = window;
    entry.presentationChanged(true);
    // Closing a floating shell returns the same page to its tab. clear/close
    // erase the entry first, so deferred dialog destruction cannot resurrect it.
    connect(window, &QDialog::finished, this, [this, key](int) { attach(key); });
    showInspectionWindow(window);
}
void InspectionWorkspace::attach(const QString& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end() || !found->second.window) return;
    auto& entry = found->second;
    entry.window->layout()->removeWidget(entry.page);
    entry.page->setParent(this);
    entry.window = nullptr;
    setCurrentIndex(addTab(entry.page, entry.title));
    entry.presentationChanged(false);
}
void InspectionWorkspace::gatherInspections() {
    for (auto& entry : entries_) if (entry.second.window) entry.second.window->close();
}
void InspectionWorkspace::closeInspection(const QString& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return;
    const auto page = found->second.page;
    const auto window = found->second.window;
    entries_.erase(found);
    if (page) {
        removeTab(indexOf(page));
        page->setObjectName({});
        page->hide();
        page->deleteLater();
    }
    if (window) {
        window->setObjectName({});
        window->close();
    }
}
void InspectionWorkspace::clearInspections() {
    while (!entries_.empty()) closeInspection(entries_.begin()->first);
}
