#pragma once
#include "debug_access.hpp"
#include <QTableWidget>
#include <cstdint>
#include <functional>

namespace picodbg { class DebugTarget; }

class MemoryTable final : public QTableWidget {
public:
    enum { MaxBytesPerRow = 16, AsciiColumn = MaxBytesPerRow + 1 };
    explicit MemoryTable(QWidget* parent = nullptr);
    void showRange(const picodbg::DebugTarget& target, unsigned core, uint32_t start, unsigned length);
    void setHighlights(const std::vector<MemoryHighlight>& highlights);
    void clearRange();
    uint32_t start() const { return start_; }
    unsigned length() const { return length_; }
    bool addressAt(int row, int column, uint32_t& address) const;
    void selectAddress(uint32_t address);
    std::function<void()> resized;
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    const picodbg::DebugTarget* target_ = nullptr;
    unsigned core_ = 0;
    uint32_t start_ = 0;
    unsigned length_ = 0;
    uint64_t revision_ = 0;
    qint64 paletteKey_ = 0;
    int bytesPerRow_ = MaxBytesPerRow;
    int byteWidth_ = 0;
    int asciiAdvance_ = 0;
    bool resizePending_ = false;
    std::vector<MemoryHighlight> highlights_;
    void colorBytes();
    int bytesThatFit() const;
};
