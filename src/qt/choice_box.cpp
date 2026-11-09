#include "choice_box.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QTimer>
#include <QWindow>

ChoiceBox::ChoiceBox(QWidget* parent) : QComboBox(parent), popup_(view()->window()) {
    popup_->installEventFilter(this);
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { restoreFocus(); });
}

bool ChoiceBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popup_) {
        if (event->type() == QEvent::Show) pendingFocus_.clear();
        if (event->type() == QEvent::Close) deferFocus();
        if (event->type() == QEvent::Hide) {
            QTimer::singleShot(0, this, [this] { restoreFocus(); });
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void ChoiceBox::deferFocus() {
    // QApplicationPrivate::closePopup restores the owner's saved focus before
    // sending Hide. In Qt5 that calls requestActivate() while Wayland still
    // considers the popup focused. Clear it BEFORE closing, then wait for the
    // compositor's focusWindowChanged notification (no Qt private API).
    if (!pendingFocus_) pendingFocus_ = window()->focusWidget();
    if (pendingFocus_) pendingFocus_->clearFocus();
}

void ChoiceBox::hidePopup() {
    if (popup_->isVisible()) deferFocus();
    QComboBox::hidePopup();
}

void ChoiceBox::restoreFocus() {
    if (!pendingFocus_ || popup_->isVisible()) return;
    auto* owner = pendingFocus_->window();
    if (QGuiApplication::focusWindow() != owner->windowHandle() || !owner->isActiveWindow()) return;
    const auto target = pendingFocus_;
    pendingFocus_.clear();
    if (target->isVisible() && target->isEnabled()) target->setFocus(Qt::PopupFocusReason);
}
