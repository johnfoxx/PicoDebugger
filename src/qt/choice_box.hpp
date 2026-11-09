#pragma once
#include <QComboBox>
#include <QPointer>

// Keep Qt's native mouse/keyboard popup, but let the compositor restore the
// owner window's focus before restoring the widget's keyboard focus.
class ChoiceBox final : public QComboBox {
public:
    explicit ChoiceBox(QWidget* parent = nullptr);
    void hidePopup() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void deferFocus();
    void restoreFocus();
    QWidget* popup_;
    QPointer<QWidget> pendingFocus_;
};
