#pragma once

#include "stack_details.hpp"
#include <QDialog>
#include <QTreeWidget>
#include <QTimer>
#include <functional>

class QLabel;
class QPushButton;

struct StackSpan {
    uint32_t start = 0;
    uint64_t end = 0;
    bool contains(uint64_t at) const { return at >= start && at < end; }
    bool valid() const { return end > start; }
    bool overlaps(StackSpan other) const { return valid() && other.valid() && end > other.start && other.end > start; }
    bool operator==(StackSpan other) const { return start == other.start && end == other.end; }
};

struct StackFieldView {
    FrameField field;
    StackSpan span;
    QString label;
};

struct StackViewFrame {
    picodbg::StackFrame frame{0, 0, false, {}};
    unsigned level = 0;
    StackSpan memory;
    bool bounded = false;
    bool hasBase = false;
    uint32_t base = 0;
    QString status;
    QVector<StackFieldView> fields;
};

class StackValueChanges {
public:
    void clear() { bytes_.clear(); }
    qint64 observe(uint32_t address, int value, qint64 now);
    qint64 changedUntil(StackSpan span, qint64 now) const;
private:
    struct Byte { int value; qint64 until; };
    std::map<uint32_t, Byte> bytes_;
};

QColor stackAccent(unsigned index);
QString stackFrameName(const StackViewFrame& frame);
bool sameStackFrame(const StackViewFrame& a, const StackViewFrame& b);
FrameField stackArrayElement(const FrameField& field, unsigned index);

class StackMemoryView : public QTreeWidget {
public:
    enum Column { Address, Marker, Hex, Ascii };
    enum EditMode { HexByte, AsciiByte, Word, Typed };
    explicit StackMemoryView(bool overview, QWidget* parent = nullptr);
    void showOverview(const picodbg::DebugTarget& target, unsigned core, bool active, const QVector<StackViewFrame>& frames,
                      const QString& status, bool writable, const QVector<StackSpan>& selections, StackValueChanges& changes);
    void showPage(const picodbg::DebugTarget& target, unsigned core, const StackViewFrame& frame,
                  StackSpan page, bool writable, StackSpan selection, const QVector<QColor>& colors, StackValueChanges& changes);
    void setRangeSelection(unsigned level, StackSpan selection, bool reveal = false);
    std::function<void(unsigned)> inspectFrame;
    std::function<void()> previewChanged;
    std::function<void(unsigned, StackSpan)> rangeSelected;
    std::function<void(unsigned, EditMode, uint32_t, int)> editRequested;

    static constexpr unsigned wordBytes = 4;
private:
    bool overview_;
    bool writable_ = false;
    bool previewResizePending_ = false;
    QVector<StackViewFrame> frames_;
    QVector<StackSpan> selections_;
    QVector<QColor> zoneColors_;
    QTimer changeRepaint_;
    qint64 latestChange_ = 0;
    const StackViewFrame* frameFor(unsigned level) const;
    void fillRows(QTreeWidgetItem* root, const picodbg::DebugTarget& target, unsigned core, const StackViewFrame& frame,
                  StackSpan page, const QVector<uint32_t>& addresses, StackValueChanges& changes);
    void activate(QTreeWidgetItem* item, int column, int x, bool edit, bool keyboard = false);
    void openMenu(const QPoint& point);
    void updateHighlights(QTreeWidgetItem* root, StackSpan selection);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};

class StackInspector : public QDialog {
public:
    StackInspector(unsigned core, const StackViewFrame& frame, QWidget* parent);
    unsigned core() const { return core_; }
    const StackViewFrame& frame() const { return frame_; }
    void updateFrame(const picodbg::DebugTarget& target, const StackViewFrame& frame, bool writable, StackSpan selection, StackValueChanges& changes);
    void showUnavailable(const QString& status);
    void setRangeSelection(StackSpan selection, bool reveal = false);

    std::function<void()> refreshRequested;
    std::function<void(StackSpan, bool)> rangeSelected;
    std::function<void(StackMemoryView::EditMode, uint32_t, int)> editMemory;
    std::function<void(const FrameField&, const QString&)> editTyped;
private:
    void showFields(const picodbg::DebugTarget& target, const StackValueChanges& changes);
    QString zoneKey(const StackFieldView& field) const;
    int colorFieldIndex() const;
    QColor zoneColor(int index) const;
    void chooseZoneColor();
    void syncZoneControls();
    void updateFieldSelection();
    void visitPage(int direction);

    unsigned core_;
    StackViewFrame frame_;
    StackSpan selection_;
    uint32_t pageStart_ = 0;
    bool writable_ = false;
    bool available_ = false;
    QLabel* context_;
    QLabel* pageLabel_;
    StackMemoryView* memory_;
    QTreeWidget* fields_;
    QPushButton* previous_;
    QPushButton* next_;
    QPushButton* zoneColorButton_;
    struct ZoneOption { QColor color; bool enabled = false; };
    struct DecodedSample { QString value; qint64 until; };
    QHash<QString, ZoneOption> zoneOptions_;
    QHash<QString, DecodedSample> decodedSamples_;
    QTimer changeRepaint_;
    qint64 latestChange_ = 0;
};
