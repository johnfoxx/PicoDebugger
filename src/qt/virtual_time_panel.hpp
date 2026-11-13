#pragma once
#include <QWidget>
#include <cstdint>
#include <functional>
class QLabel;
class QPushButton;
class QComboBox;
class QSpinBox;

class VirtualTimePanel : public QWidget {
public:
    explicit VirtualTimePanel(QWidget* parent = nullptr);
    void updateState(uint64_t time, uint64_t remaining, bool available, bool running);
    std::function<void(uint64_t, bool)> advanceRequested;
    std::function<void()> pauseRequested;
private:
    QLabel* clock_;
    QSpinBox* duration_;
    QComboBox* unit_;
    QComboBox* mode_;
    QPushButton* advance_;
    QPushButton* pause_;
};
