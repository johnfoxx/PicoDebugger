#include "stack_view.hpp"

#include "debug_access.hpp"
#include <QColorDialog>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr int addressRole = Qt::UserRole;
constexpr int levelRole = Qt::UserRole + 1;
constexpr int colorsRole = Qt::UserRole + 2;
constexpr int selectedRole = Qt::UserRole + 3;
constexpr int bytesRole = Qt::UserRole + 4;
constexpr int markersRole = Qt::UserRole + 5;
constexpr int fieldRole = Qt::UserRole + 6;
constexpr int elementRole = Qt::UserRole + 7;
constexpr int changesRole = Qt::UserRole + 8;
constexpr int knownBytesRole = Qt::UserRole + 9;
constexpr qint64 changeMs = 2400;
constexpr unsigned pageBytes = 256;
constexpr int byteInset = 3;
constexpr int zoneColumn = 5;

QString addressText(uint32_t value) {
    return QString("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}
QString hexText(uint32_t value) { return "0x" + addressText(value); }
QString byteText(int value) {
    return value >= 0 ? QString("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper() : value == -1 ? "??" : "  ";
}
QChar asciiChar(int value) {
    if (value < 0) return value == -1 ? QChar('?') : QChar(' ');
    return value >= 32 && value < 127 ? QChar(value) : QChar('.');
}

QFont fixedFont(int pixels) {
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) font.setFamily("monospace");
    font.setPixelSize(pixels);
    return font;
}

StackSpan wordSpan(StackSpan range, uint32_t base) {
    StackSpan span;
    span.start = std::max(range.start, base);
    span.end = std::min(range.end, uint64_t(base) + StackMemoryView::wordBytes);
    return span;
}

QVector<uint32_t> wordAddresses(StackSpan span) {
    QVector<uint32_t> addresses;
    for (uint64_t base = span.start & ~uint32_t(3); base < span.end; base += StackMemoryView::wordBytes)
        addresses.push_back(static_cast<uint32_t>(base));
    return addresses;
}

QVector<uint32_t> overviewAddresses(const StackViewFrame& frame, int rows) {
    auto preview = frame.memory;
    const uint64_t first = preview.start & ~uint32_t(3);
    preview.end = std::min(preview.end, first + rows * StackMemoryView::wordBytes);
    QVector<uint32_t> saved;
    for (const auto& field : frame.fields) {
        if (field.span.valid() && (field.field.savedFramePointer || field.field.role == "Retour"))
            saved.push_back(field.span.start & ~uint32_t(3));
    }
    const bool clipped = std::any_of(saved.cbegin(), saved.cend(), [&](uint32_t base) { return base >= preview.end; });
    if (!clipped) return wordAddresses(preview);

    std::sort(saved.begin(), saved.end());
    saved.erase(std::unique(saved.begin(), saved.end()), saved.end());
    const int pinned = static_cast<int>(saved.size());
    preview.end = std::min(preview.end, first + std::max(1, rows - pinned - 1) * StackMemoryView::wordBytes);
    auto addresses = wordAddresses(preview);
    for (const auto base : saved)
        if (base >= preview.end) addresses.push_back(base);
    return addresses;
}

int fieldAt(const StackViewFrame& frame, uint32_t at) {
    int index = -1;
    for (int i = 0; i < frame.fields.size(); ++i) {
        const auto span = frame.fields[i].span;
        if (span.contains(at) && (index < 0 || span.end - span.start < frame.fields[index].span.end - frame.fields[index].span.start))
            index = i;
    }
    return index;
}

int markerWidth(const QFontMetrics& metrics, const QString& text) {
    return metrics.horizontalAdvance(text) + 5;
}

QColor stackInk(const QColor& background) {
    const auto linearize = [](double component) {
        return component <= 0.04045 ? component / 12.92 : std::pow((component + 0.055) / 1.055, 2.4);
    };
    const auto luminance = 0.2126 * linearize(background.redF()) +
        0.7152 * linearize(background.greenF()) + 0.0722 * linearize(background.blueF());
    // Choose the stronger of the black/white contrast ratios (WCAG relative luminance).
    return luminance >= 0.179 ? QColor(Qt::black) : QColor(Qt::white);
}

void markChange(QPainter* painter, const QRect& rect, const QColor& background, int thickness) {
    const auto color = stackInk(background) == QColor(Qt::black) ? QColor("#713800") : QColor("#ffd780");
    painter->fillRect(QRect(rect.left() + 1, rect.bottom() - thickness, std::max(0, rect.width() - 2), thickness), color);
}

int byteWidth(int digit, bool hex, bool compact) {
    return hex ? digit * 2 + (compact ? 3 : digit) : digit;
}

void drawZoneCheck(QPainter* painter, const QRect& indicator, const QPalette& palette, bool checked, bool enabled) {
    const auto outline = picoThemeColor(enabled ? "#8b9db1" : "#757b80");
    const auto fill = checked ? palette.highlight().color() : palette.base().color();
    const auto box = indicator.adjusted(1, 1, -1, -1);
    painter->save();
    painter->setPen(outline);
    painter->setBrush(fill);
    painter->drawRoundedRect(box, 2, 2);
    if (checked) {
        painter->setPen(QPen(stackInk(fill), 1.5));
        const auto middle = box.center();
        painter->drawLine(QPoint(box.left() + 3, middle.y()), QPoint(middle.x() - 1, box.bottom() - 3));
        painter->drawLine(QPoint(middle.x() - 1, box.bottom() - 3), QPoint(box.right() - 2, box.top() + 3));
    }
    painter->restore();
}

void drawStackLabel(QPainter* painter, QStyleOptionViewItem option, const QBrush& fill, bool changed = false) {
    const auto label = option.text;
    auto background = option;
    background.text.clear();
    background.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    background.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected | QStyle::State_HasFocus);
    const auto* style = option.widget->style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, option.widget);
    auto rect = option.rect.adjusted(3, 0, -3, 0);
    if (rect.width() <= 0) return;
    painter->save();
    if (fill.style() != Qt::NoBrush) {
        painter->fillRect(option.rect, fill);
        painter->setPen(stackInk(fill.color()));
    } else painter->setPen(option.palette.text().color());
    if (option.features.testFlag(QStyleOptionViewItem::HasCheckIndicator)) {
        const auto indicator = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, option.widget);
        drawZoneCheck(painter, indicator, option.palette, option.checkState == Qt::Checked, option.state & QStyle::State_Enabled);
        rect.setLeft(std::max(rect.left(), indicator.right() + 5));
    }
    auto font = option.font;
    if (changed) font.setBold(true);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const auto shown = metrics.elidedText(label, option.textElideMode, rect.width());
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, shown);
    if (changed) {
        rect.setWidth(std::min(rect.width(), metrics.horizontalAdvance(shown) + 2));
        markChange(painter, rect, fill.style() == Qt::NoBrush ? option.palette.base().color() : fill.color(), 2);
    }
    painter->restore();
}

class StackLabelDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto styled = option;
        initStyleOption(&styled, index);
        const bool changed = index.data(changesRole).toLongLong() > QDateTime::currentMSecsSinceEpoch();
        if (styled.backgroundBrush.style() == Qt::NoBrush && !changed && !styled.features.testFlag(QStyleOptionViewItem::HasCheckIndicator))
            QStyledItemDelegate::paint(painter, option, index);
        else drawStackLabel(painter, styled, styled.backgroundBrush, changed);
    }
};

class StackByteDelegate : public QStyledItemDelegate {
public:
    StackByteDelegate(bool compact, QObject* parent) : QStyledItemDelegate(parent), compact_(compact) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto bytes = index.data(bytesRole).toList();
        if (bytes.isEmpty()) {
            auto styled = option;
            initStyleOption(&styled, index);
            if (styled.backgroundBrush.style() == Qt::NoBrush) QStyledItemDelegate::paint(painter, option, index);
            else drawStackLabel(painter, styled, styled.backgroundBrush);
            return;
        }
        auto background = option;
        initStyleOption(&background, index);
        background.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver);
        const int column = index.column();
        const auto colors = index.data(colorsRole).toList();
        const auto active = index.data(selectedRole).toUInt();
        const auto known = index.data(knownBytesRole).toUInt();
        const auto dates = index.data(changesRole).toList();
        const auto now = QDateTime::currentMSecsSinceEpoch();
        auto* style = option.widget->style();
        if (column == StackMemoryView::Address) {
            drawStackLabel(painter, background, active ? option.palette.highlight() : QBrush());
            return;
        }
        background.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, option.widget);
        painter->save();
        painter->setClipRect(option.rect);
        auto changedFont = option.font;
        changedFont.setBold(true);
        const auto draw = [&](QRect part, const QString& text, QColor color, bool selected, bool dim, bool changed) {
            const auto fill = selected ? option.palette.highlight().color() : color;
            if (fill.isValid()) painter->fillRect(part, fill);
            if (selected && color.isValid()) painter->fillRect(part.adjusted(0, part.height() - 2, 0, 0), color);
            const QColor ink = fill.isValid() ? stackInk(fill) :
                dim && !changed ? picoThemeColor("#a8afb5") : option.palette.text().color();
            painter->setPen(ink);
            painter->setFont(changed && !compact_ ? changedFont : option.font);
            painter->drawText(part, Qt::AlignCenter, text);
            if (changed)
                markChange(painter, part, fill.isValid() ? fill : option.palette.base().color(), compact_ ? 1 : 2);
        };
        int left = option.rect.left() + byteInset;
        if (column == StackMemoryView::Marker) {
            const auto markers = index.data(markersRole).toList();
            for (const auto& marker : markers) {
                const auto parts = marker.toList();
                const auto label = parts[0].toString();
                const int width = markerWidth(option.fontMetrics, label);
                draw(QRect(left, option.rect.top(), width - 2, option.rect.height()), label, parts[2].value<QColor>(),
                     (active & parts[1].toUInt()) != 0, false, false);
                left += width;
            }
        } else {
            const bool hex = column == StackMemoryView::Hex;
            const int advance = option.fontMetrics.horizontalAdvance(QLatin1Char('0'));
            const int width = byteWidth(advance, hex, compact_);
            for (int i = 0; i < bytes.size(); ++i) {
                const auto value = bytes[i].toInt();
                const auto color = colors.value(i).value<QColor>();
                draw(QRect(left, option.rect.top(), width, option.rect.height()),
                     hex ? byteText(value) : QString(asciiChar(value)), color, active & (1u << i),
                     !(known & (1u << i)) || value < 0, value >= 0 && dates.value(i).toLongLong() > now);
                left += width;
            }
        }
        painter->restore();
    }
private:
    bool compact_;
};

void repaintChanges(QTimer& timer, QWidget* viewport, const qint64& latest) {
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, viewport, [&timer, viewport, &latest] {
        viewport->update();
        if (latest <= QDateTime::currentMSecsSinceEpoch()) timer.stop();
    });
}

QTreeWidgetItem* rowItem(QTreeWidget* tree, QTreeWidgetItem* root, int index) {
    if (root) return index < root->childCount() ? root->child(index) : new QTreeWidgetItem(root);
    return index < tree->topLevelItemCount() ? tree->topLevelItem(index) : new QTreeWidgetItem(tree);
}

void trimRows(QTreeWidget* tree, QTreeWidgetItem* root, int count) {
    if (root) while (root->childCount() > count) delete root->takeChild(root->childCount() - 1);
    else while (tree->topLevelItemCount() > count) delete tree->takeTopLevelItem(tree->topLevelItemCount() - 1);
}
}

qint64 StackValueChanges::observe(uint32_t address, int value, qint64 now) {
    if (value < 0) return 0;
    auto found = bytes_.find(address);
    if (found == bytes_.end()) {
        if (bytes_.size() >= 32768) bytes_.erase(bytes_.begin());
        bytes_.emplace(address, Byte{value, 0});
        return 0;
    }
    if (found->second.value != value) found->second = {value, now + changeMs};
    return found->second.until;
}

qint64 StackValueChanges::changedUntil(StackSpan span, qint64 now) const {
    qint64 latest = 0;
    if (!span.valid()) return latest;
    for (auto it = bytes_.lower_bound(span.start); it != bytes_.end() && it->first < span.end; ++it)
        if (it->second.until > now) latest = std::max(latest, it->second.until);
    return latest;
}

QColor stackAccent(unsigned index) {
    static const char* colors[] = {"#9AD0EC", "#E5B2D4", "#B4DAA4", "#ECC28B",
                                   "#B9C7F1", "#EDAFA7", "#A1D9CA", "#D0BAED"};
    return QColor(QLatin1String(colors[index % (sizeof(colors) / sizeof(*colors))]));
}

QString stackFrameName(const StackViewFrame& frame) {
    const auto name = QString::fromStdString(frame.frame.function);
    return QString("#%1 %2").arg(frame.level).arg(name.isEmpty() || name == "??" ? hexText(frame.frame.pc) : name);
}

bool sameStackFrame(const StackViewFrame& a, const StackViewFrame& b) {
    if (a.frame.activation && b.frame.activation) {
        return a.frame.activation == b.frame.activation &&
            (a.frame.function.empty() || b.frame.function.empty() || a.frame.function == b.frame.function);
    }
    return a.level == b.level && a.frame.pc == b.frame.pc &&
        (!a.frame.hasSp || !b.frame.hasSp || a.frame.sp == b.frame.sp);
}

FrameField stackArrayElement(const FrameField& field, unsigned index) {
    auto child = field;
    const auto suffix = QString("[%1]").arg(index);
    child.name += suffix;
    if (!child.expression.isEmpty()) child.expression += suffix;
    child.type.name = child.type.name.section('[', 0, 0).trimmed();
    child.type.count = 1;
    child.type.isArray = false;
    child.address += index * child.type.size;
    child.rendered.clear();
    return child;
}

StackMemoryView::StackMemoryView(bool overview, QWidget* parent) : QTreeWidget(parent), overview_(overview) {
    setHeaderLabels({"Adresse", "Rep.", "Hex", "ASCII"});
    setFont(fixedFont(overview ? 11 : 13));
    header()->setFont(font());
    setItemDelegate(new StackByteDelegate(overview_, this));
    repaintChanges(changeRepaint_, viewport(), latestChange_);
    setRootIsDecorated(overview);
    setIndentation(overview ? 7 : 0);
    setExpandsOnDoubleClick(false);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::NoSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("QHeaderView::section { padding:2px; }");
    header()->setStretchLastSection(false);
    header()->setMinimumSectionSize(16);
    header()->setSectionResizeMode(QHeaderView::Fixed);
    const int digit = fontMetrics().horizontalAdvance(QLatin1Char('0'));
    const int widths[] = {8 * digit + 2 * indentation() + 11, (overview ? 7 : 9) * digit,
                         4 * byteWidth(digit, true, overview_) + 2 * byteInset, std::max(5 * digit, 4 * digit + 2 * byteInset)};
    int width = verticalScrollBar()->sizeHint().width() + 2;
    for (int column = 0; column < columnCount(); ++column) {
        setColumnWidth(column, widths[column]);
        width += widths[column];
    }
    setMinimumWidth(width);
    header()->setSectionResizeMode(Ascii, QHeaderView::Stretch);
    connect(this, &QTreeWidget::itemExpanded, this, [this] { if (previewChanged) previewChanged(); });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        activate(item, column, 0, true, true);
    });
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &StackMemoryView::openMenu);
}

void StackMemoryView::showOverview(const picodbg::DebugTarget& target, unsigned core, bool active,
                                   const QVector<StackViewFrame>& frames, const QString& status, bool writable,
                                   const QVector<StackSpan>& selections, StackValueChanges& changes) {
    const QSignalBlocker blocker(this);
    writable_ = writable;
    frames_ = frames;
    selections_ = selections;
    zoneColors_.clear();
    headerItem()->setText(Address, QString("Cœur %1%2").arg(core).arg(active ? " •" : ""));
    const int scroll = verticalScrollBar()->value();
    if (frames_.isEmpty()) {
        trimRows(this, nullptr, 0);
        auto* item = new QTreeWidgetItem(this, {status});
        item->setFirstColumnSpanned(true);
        return;
    }
    trimRows(this, nullptr, frames_.size());
    int expandedFrames = 0;
    for (const auto& frame : frames_) {
        auto* item = rowItem(this, nullptr, static_cast<int>(frame.level));
        const bool fresh = !item->data(0, levelRole).isValid() ||
            item->childIndicatorPolicy() != QTreeWidgetItem::ShowIndicator;
        item->setData(0, levelRole, frame.level);
        item->setFirstColumnSpanned(true);
        const auto title = stackFrameName(frame);
        const auto sp = frame.frame.hasSp ? hexText(frame.frame.sp) : QString("—");
        item->setText(0, title + " · SP " + sp);
        auto tip = QString("%1 · Cœur %2\nPC %3 · SP %4").arg(title).arg(core).arg(hexText(frame.frame.pc), sp);
        if (frame.hasBase) tip += " · CFA " + hexText(frame.base);
        item->setToolTip(0, tip + "\nDouble-clic pour ouvrir la trame");
        item->setBackground(0, QBrush());
        auto font = this->font();
        font.setBold(frame.level == 0);
        item->setFont(0, font);
        item->setChildIndicatorPolicy(frame.memory.valid() ? QTreeWidgetItem::ShowIndicator : QTreeWidgetItem::DontShowIndicator);
        if (fresh) item->setExpanded(frame.level < 2 && frame.memory.valid());
        if (item->isExpanded() && frame.memory.valid()) ++expandedFrames;
    }
    const int availableRows = viewport()->height() / (fontMetrics().height() + 2);
    int rowsLeft = std::max(4 * expandedFrames, availableRows - std::min(3, int(frames_.size())));
    for (const auto& frame : frames_) {
        auto* item = topLevelItem(static_cast<int>(frame.level));
        if (!item->isExpanded() || !frame.memory.valid()) { trimRows(this, item, 0); continue; }
        const int rows = std::max(4, rowsLeft / expandedFrames);
        fillRows(item, target, core, frame, frame.memory, overviewAddresses(frame, rows), changes);
        updateHighlights(item, selections_.value(static_cast<int>(frame.level)));
        rowsLeft -= item->childCount();
        --expandedFrames;
    }
    verticalScrollBar()->setValue(scroll);
}

void StackMemoryView::showPage(const picodbg::DebugTarget& target, unsigned core, const StackViewFrame& frame,
                                StackSpan page, bool writable, StackSpan selection, const QVector<QColor>& colors, StackValueChanges& changes) {
    writable_ = writable;
    frames_ = {frame};
    zoneColors_ = colors;
    if (!page.valid()) { clear(); return; }
    fillRows(nullptr, target, core, frame, page, wordAddresses(page), changes);
    updateHighlights(nullptr, selection);
}

void StackMemoryView::fillRows(QTreeWidgetItem* root, const picodbg::DebugTarget& target, unsigned core,
                                const StackViewFrame& frame, StackSpan page, const QVector<uint32_t>& addresses,
                                StackValueChanges& changes) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    int row = 0;
    uint64_t next = page.start & ~uint32_t(3);
    for (const uint64_t base : addresses) {
        if (base > next) {
            auto* gap = rowItem(this, root, row++);
            gap->setFirstColumnSpanned(true);
            gap->setFlags(gap->flags() & ~Qt::ItemIsSelectable);
            gap->setData(0, addressRole, {});
            gap->setData(0, levelRole, {});
            const auto words = (base - next) / wordBytes;
            gap->setText(Address, words == 1 ? QString("… 1 mot omis") : QString("… %L1 mots omis").arg(words));
            gap->setToolTip(Address, QString("%1 – %2 · Double-clic sur l’en-tête pour voir ces octets.")
                .arg(hexText(static_cast<uint32_t>(next)), hexText(static_cast<uint32_t>(base - 1))));
            for (int column = 0; column < columnCount(); ++column) {
                gap->setData(column, bytesRole, {});
                if (column != Address) gap->setText(column, {});
            }
        }
        next = base + wordBytes;
        auto* item = rowItem(this, root, row++);
        item->setFirstColumnSpanned(false);
        item->setFlags(item->flags() | Qt::ItemIsSelectable);
        item->setData(0, addressRole, static_cast<uint32_t>(base));
        item->setData(0, levelRole, frame.level);
        item->setText(Address, addressText(static_cast<uint32_t>(base)));
        QVariantList bytes, colors, markers, dates;
        unsigned known = 0;
        QStringList hexadecimal, labels;
        QString ascii;
        QMap<int, unsigned> fields;
        for (unsigned i = 0; i < wordBytes; ++i) {
            int value = -2;
            QColor color;
            const auto at = static_cast<uint32_t>(base + i);
            if (page.contains(at)) {
                try { value = static_cast<int>(target.peek(at, 1, core)); }
                catch (const picodbg::Error&) { value = -1; }
                const int field = fieldAt(frame, at);
                if (field >= 0) {
                    fields[field] |= 1u << i;
                    known |= 1u << i;
                }
                // A checked surrounding zone can be highlighted even when its overlapping inner field is unchecked.
                for (int zone = 0; zone < zoneColors_.size(); ++zone) {
                    if (zoneColors_[zone].isValid() && frame.fields[zone].span.contains(at)) color = zoneColors_[zone];
                }
            }
            const auto until = changes.observe(at, value, now);
            if (until > now) latestChange_ = std::max(latestChange_, until);
            dates << until;
            bytes << value;
            colors << color;
            hexadecimal << byteText(value);
            ascii += asciiChar(value);
        }
        const bool stackWord = (frame.frame.sp & ~uint32_t(3)) == base;
        const bool savedWord = std::any_of(fields.keyBegin(), fields.keyEnd(), [&](int field) {
            return frame.fields[field].field.savedRegister >= 0 || frame.fields[field].field.role == "Retour";
        });
        if (stackWord && !savedWord) {
            markers << QVariant(QVariantList{"[SP]", 0xfu, QColor(), -1});
            labels << "[SP]";
        }
        for (auto entry = fields.cbegin(); entry != fields.cend(); ++entry) {
            const auto& field = frame.fields[entry.key()];
            labels << field.label;
            markers << QVariant(QVariantList{field.label, entry.value(), zoneColors_.value(entry.key()), entry.key()});
        }
        item->setText(Marker, labels.join(' '));
        item->setText(Hex, hexadecimal.join(' '));
        item->setText(Ascii, ascii);
        auto tip = QString("Cœur %1 · %2\n%3 · SP + 0x%4\n%5 · %6")
            .arg(core).arg(stackFrameName(frame), hexText(static_cast<uint32_t>(base)))
            .arg(std::max(base, uint64_t(frame.frame.sp)) - frame.frame.sp, 0, 16)
            .arg(item->text(Hex), ascii);
        for (auto entry = fields.cbegin(); entry != fields.cend(); ++entry)
            tip += "\n" + frame.fields[entry.key()].label + " · " + frame.fields[entry.key()].field.name;
        if (fields.isEmpty()) tip += "\nOctets sans symbole";
        if (stackWord)
            tip += "\nMot en mémoire à l’adresse de SP, indiquée dans l’en-tête.";
        tip += "\nDouble-clic sur un octet ou un caractère pour l’éditer";
        for (int column = 0; column < columnCount(); ++column) {
            item->setData(column, bytesRole, bytes);
            item->setData(column, markersRole, markers);
            item->setData(column, colorsRole, colors);
            item->setData(column, knownBytesRole, known);
            item->setData(column, changesRole, dates);
            item->setToolTip(column, tip);
        }
    }
    trimRows(this, root, row);
    if (latestChange_ > now && !changeRepaint_.isActive()) changeRepaint_.start();
}

const StackViewFrame* StackMemoryView::frameFor(unsigned level) const {
    const auto found = std::find_if(frames_.cbegin(), frames_.cend(), [level](const StackViewFrame& frame) { return frame.level == level; });
    return found == frames_.cend() ? nullptr : &*found;
}

void StackMemoryView::updateHighlights(QTreeWidgetItem* root, StackSpan selection) {
    const int count = root ? root->childCount() : topLevelItemCount();
    for (int index = 0; index < count; ++index) {
        auto* item = root ? root->child(index) : topLevelItem(index);
        const auto bytes = item->data(0, bytesRole).toList();
        const auto base = item->data(0, addressRole).toUInt();
        unsigned mask = 0;
        for (int b = 0; b < bytes.size(); ++b)
            if (bytes[b].toInt() != -2 && selection.contains(uint64_t(base) + static_cast<unsigned>(b))) mask |= 1u << b;
        for (int column = 0; column < columnCount(); ++column) item->setData(column, selectedRole, mask);
    }
}

void StackMemoryView::setRangeSelection(unsigned level, StackSpan selection, bool reveal) {
    auto* root = overview_ ? topLevelItem(static_cast<int>(level)) : nullptr;
    if (overview_ && !root) return;
    if (overview_) {
        if (selections_.size() <= static_cast<int>(level)) selections_.resize(static_cast<int>(level + 1));
        selections_[static_cast<int>(level)] = selection;
    }
    updateHighlights(root, selection);
    if (!reveal || !selection.valid()) return;
    const int count = root ? root->childCount() : topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        auto* item = root ? root->child(i) : topLevelItem(i);
        if (item->data(0, selectedRole).toUInt()) { scrollToItem(item); break; }
    }
}

void StackMemoryView::activate(QTreeWidgetItem* item, int column, int x, bool edit, bool keyboard) {
    if (!item || !item->data(0, levelRole).isValid()) return;
    const auto level = item->data(0, levelRole).toUInt();
    if (overview_ && !item->parent()) {
        if (edit && inspectFrame) inspectFrame(level);
        return;
    }
    const auto* frame = frameFor(level);
    if (!frame) return;
    const auto base = item->data(0, addressRole).toUInt();
    auto choice = wordSpan(frame->memory, base);
    auto mode = Word;
    int field = -1;
    uint32_t address = choice.start;
    if (column == Hex || column == Ascii) {
        const int size = byteWidth(fontMetrics().horizontalAdvance(QLatin1Char('0')), column == Hex, overview_);
        const auto bytes = item->data(0, bytesRole).toList();
        int offset = keyboard ? static_cast<int>(choice.start - base) : (x - byteInset) / size;
        if ((!keyboard && x < byteInset) || offset < 0 || offset >= bytes.size() || bytes[offset].toInt() == -2) return;
        address = base + static_cast<uint32_t>(offset);
        mode = column == Ascii ? AsciiByte : HexByte;
        field = fieldAt(*frame, address);
    } else if (column == Marker) {
        int left = byteInset;
        for (const auto& marker : item->data(0, markersRole).toList()) {
            const auto parts = marker.toList();
            const int right = left + markerWidth(fontMetrics(), parts[0].toString());
            if (keyboard || (x >= left && x < right)) {
                field = parts[3].toInt();
                if (field >= 0) {
                    mode = Typed;
                    address = std::max(choice.start, frame->fields[field].span.start);
                }
                break;
            }
            left = right;
        }
    }
    if (field >= 0) choice = frame->fields[field].span;
    if (rangeSelected) rangeSelected(level, choice);
    if (edit && writable_ && editRequested) editRequested(level, mode, address, field);
}

void StackMemoryView::mousePressEvent(QMouseEvent* event) {
    const auto index = indexAt(event->pos());
    QTreeWidget::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && index.isValid())
        activate(itemAt(event->pos()), index.column(), event->pos().x() - visualRect(index).left(), false);
}

void StackMemoryView::mouseDoubleClickEvent(QMouseEvent* event) {
    const auto index = indexAt(event->pos());
    if (event->button() == Qt::LeftButton && index.isValid()) {
        activate(itemAt(event->pos()), index.column(), event->pos().x() - visualRect(index).left(), true);
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void StackMemoryView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F2 || (overview_ && currentItem() && !currentItem()->parent() && event->key() == Qt::Key_Return)) {
        activate(currentItem(), currentColumn(), 0, true, true);
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

void StackMemoryView::resizeEvent(QResizeEvent* event) {
    QTreeWidget::resizeEvent(event);
    if (!overview_ || !previewChanged || previewResizePending_ || event->oldSize().height() == event->size().height()) return;
    previewResizePending_ = true;
    QTimer::singleShot(0, this, [this] {
        previewResizePending_ = false;
        if (previewChanged) previewChanged();
    });
}

void StackMemoryView::openMenu(const QPoint& point) {
    const auto index = indexAt(point);
    const auto* item = itemAt(point);
    if (!item || !item->data(0, levelRole).isValid() || !index.isValid()) return;
    const bool root = overview_ && !item->parent();
    const auto level = item->data(0, levelRole).toUInt();
    const auto address = item->data(0, addressRole);
    const auto* frame = frameFor(level);
    if (!frame) return;
    const auto before = *frame;
    QMenu menu(this);
    auto* edit = menu.addAction(root ? "Ouvrir la trame…" : "Modifier…");
    edit->setEnabled(root || writable_);
    if (menu.exec(viewport()->mapToGlobal(point)) == edit) {
        const auto current = indexAt(point);
        const auto* currentFrame = frameFor(level);
        auto* currentItem = itemAt(point);
        if (isEnabled() && current.isValid() && currentFrame && sameStackFrame(before, *currentFrame) &&
            currentItem && currentItem->data(0, levelRole).toUInt() == level && currentItem->data(0, addressRole) == address)
            activate(currentItem, current.column(), point.x() - visualRect(current).left(), true);
    }
}

StackInspector::StackInspector(unsigned core, const StackViewFrame& frame, QWidget* parent)
    : QDialog(parent, Qt::Window), core_(core), frame_(frame), pageStart_(frame.memory.start) {
    setObjectName("stackInspector");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString("Pile · cœur %1 · %2").arg(core).arg(stackFrameName(frame)));
    resize(985, 565);
    auto* layout = new QVBoxLayout(this);
    context_ = new QLabel;
    context_->setObjectName("stackInspectorContext");
    context_->setTextFormat(Qt::PlainText);
    context_->setWordWrap(true);
    layout->addWidget(context_);
    auto* panes = new QSplitter(Qt::Horizontal);
    auto* raw = new QWidget;
    auto* rawLayout = new QVBoxLayout(raw);
    rawLayout->setContentsMargins(0, 0, 0, 0);
    auto* navigation = new QHBoxLayout;
    pageLabel_ = new QLabel;
    previous_ = new QPushButton("←");
    previous_->setToolTip("Page précédente de la trame");
    next_ = new QPushButton("→");
    next_->setToolTip("Page suivante de la trame");
    navigation->addWidget(previous_);
    navigation->addWidget(pageLabel_, 1);
    navigation->addWidget(next_);
    rawLayout->addLayout(navigation);
    memory_ = new StackMemoryView(false);
    memory_->setObjectName("stackInspectorMemory");
    rawLayout->addWidget(memory_, 1);
    panes->addWidget(raw);
    fields_ = new QTreeWidget;
    fields_->setObjectName("stackInspectorFields");
    fields_->setHeaderLabels({"Rep.", "Offset / reg.", "Nom", "Type · taille", "Décodé", "Zone"});
    fields_->headerItem()->setToolTip(zoneColumn, "Cocher une zone pour conserver sa couleur dans l’inspecteur.");
    auto fieldFont = fields_->font();
    fieldFont.setPixelSize(14);
    fields_->setFont(fieldFont);
    fields_->header()->setFont(fieldFont);
    fields_->setStyleSheet("QHeaderView::section { padding:3px; }");
    fields_->setIndentation(13);
    fields_->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    fields_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    fields_->setItemDelegate(new StackLabelDelegate(fields_));
    fields_->setColumnWidth(0, 55);
    fields_->setColumnWidth(1, 88);
    fields_->setColumnWidth(3, 140);
    fields_->setColumnWidth(4, 130);
    fields_->setColumnWidth(zoneColumn, 54);
    fields_->header()->setSectionResizeMode(zoneColumn, QHeaderView::Fixed);
    repaintChanges(changeRepaint_, fields_->viewport(), latestChange_);
    fields_->setSelectionMode(QAbstractItemView::NoSelection);
    fields_->setUniformRowHeights(true);
    fields_->setExpandsOnDoubleClick(false);
    panes->addWidget(fields_);
    panes->setStretchFactor(1, 1);
    panes->setSizes({295, 605});
    layout->addWidget(panes, 1);
    auto* footer = new QHBoxLayout;
    auto* help = new QLabel("Cochez les zones à colorer. Un trait souligne les valeurs récemment modifiées.");
    help->setWordWrap(true);
    footer->addWidget(help, 1);
    zoneColorButton_ = new QPushButton("Couleur…");
    zoneColorButton_->setObjectName("stackFieldColorButton");
    zoneColorButton_->setToolTip("Choisir la couleur de la zone sélectionnée.");
    footer->addWidget(zoneColorButton_);
    layout->addLayout(footer);
    syncZoneControls();
    connect(zoneColorButton_, &QPushButton::clicked, this, &StackInspector::chooseZoneColor);
    connect(fields_, &QTreeWidget::currentItemChanged, this, [this] { syncZoneControls(); });
    connect(fields_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (!available_ || column != zoneColumn || item->parent()) return;
        const int index = item->data(0, fieldRole).toInt();
        if (index < 0 || index >= frame_.fields.size() || !frame_.fields[index].span.valid()) return;
        auto& preference = zoneOptions_[zoneKey(frame_.fields[index])];
        preference.enabled = item->checkState(zoneColumn) == Qt::Checked;
        if (!preference.color.isValid()) preference.color = stackAccent(static_cast<unsigned>(index));
        if (refreshRequested) refreshRequested();
    });
    connect(previous_, &QPushButton::clicked, this, [this] { visitPage(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { visitPage(1); });
    memory_->rangeSelected = [this](unsigned, StackSpan span) { if (rangeSelected) rangeSelected(span, false); };
    memory_->editRequested = [this](unsigned, StackMemoryView::EditMode mode, uint32_t at, int field) {
        if (editMemory) editMemory(mode, at, field);
    };
    connect(fields_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column == zoneColumn) return;
        const int index = item->data(0, fieldRole).toInt();
        if (available_ && index >= 0 && index < frame_.fields.size() && rangeSelected)
            rangeSelected(frame_.fields[index].span, true);
    });
    connect(fields_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (!available_ || column == zoneColumn) return;
        const auto index = item->data(0, fieldRole).toInt();
        if (index < 0 || index >= frame_.fields.size()) return;
        const auto& field = frame_.fields[index].field;
        if (field.type.isArray && !item->parent()) { item->setExpanded(!item->isExpanded()); return; }
        const auto element = item->data(0, elementRole);
        const auto value = element.isValid() ? stackArrayElement(field, element.toUInt()) : field;
        if (column == 4 && writable_ && value.editable && editTyped) editTyped(value, item->text(4));
    });
}

QString StackInspector::zoneKey(const StackFieldView& entry) const {
    const auto& field = entry.field;
    const auto name = field.savedRegister >= 0 ? QString("r%1 sauvé").arg(field.savedRegister) : field.role + "/" + field.name;
    return name + "/" + hexText(entry.span.start) + "/" + QString::number(entry.span.end);
}

int StackInspector::colorFieldIndex() const {
    const auto* item = fields_->currentItem();
    if (!available_ || !item) return -1;
    const auto index = item->data(0, fieldRole).toInt();
    return index >= 0 && index < frame_.fields.size() && frame_.fields[index].span.valid() ? index : -1;
}

QColor StackInspector::zoneColor(int index) const {
    const auto choice = zoneOptions_.value(zoneKey(frame_.fields[index]));
    return choice.enabled ? choice.color : QColor();
}

void StackInspector::syncZoneControls() {
    zoneColorButton_->setEnabled(colorFieldIndex() >= 0);
}

void StackInspector::chooseZoneColor() {
    const int index = colorFieldIndex();
    if (index < 0) return;
    const auto key = zoneKey(frame_.fields[index]);
    auto color = zoneOptions_.value(key).color;
    if (!color.isValid()) color = stackAccent(static_cast<unsigned>(index));
    QColorDialog dialog(color, this);
    dialog.setObjectName("stackZoneColorDialog");
    dialog.setOption(QColorDialog::DontUseNativeDialog);
    dialog.setWindowTitle("Couleur de la zone");
    applyPicoTheme(dialog);
    if (dialog.exec() != QDialog::Accepted || !available_) return;
    color = dialog.selectedColor();
    if (!color.isValid()) return;
    auto& choice = zoneOptions_[key];
    choice.color = color;
    choice.enabled = true;
    if (refreshRequested) refreshRequested();
}

void StackInspector::updateFrame(const picodbg::DebugTarget& target, const StackViewFrame& frame, bool writable, StackSpan selection,
                                 StackValueChanges& changes) {
    if (frame_.memory.start != frame.memory.start || !frame.memory.contains(pageStart_)) pageStart_ = frame.memory.start;
    frame_ = frame;
    selection_ = selection;
    available_ = true;
    writable_ = writable;
    memory_->setEnabled(true);
    fields_->setEnabled(true);
    const auto title = QString("Cœur %1 · %2").arg(core_).arg(stackFrameName(frame_));
    setWindowTitle("Pile · " + title);
    auto status = title + " · PC " + hexText(frame_.frame.pc) + " · SP " +
        (frame_.frame.hasSp ? hexText(frame_.frame.sp) : QString("—"));
    if (frame_.hasBase) status += " · CFA " + hexText(frame_.base);
    status += frame_.bounded ? QString() : " · Aperçu";
    if (!frame_.status.isEmpty()) status += "\n" + frame_.status;
    context_->setText(status);
    StackSpan page;
    page.start = pageStart_;
    page.end = std::min(frame_.memory.end, uint64_t(pageStart_) + pageBytes);
    pageLabel_->setText(page.valid() ? hexText(page.start) + " – " + hexText(static_cast<uint32_t>(page.end - 1)) : "Aucun octet dans la trame");
    previous_->setEnabled(pageStart_ > frame_.memory.start);
    next_->setEnabled(page.end < frame_.memory.end);
    QVector<QColor> colors;
    colors.reserve(frame_.fields.size());
    for (int i = 0; i < frame_.fields.size(); ++i) colors.push_back(zoneColor(i));
    memory_->showPage(target, core_, frame_, page, writable_, selection_, colors, changes);
    showFields(target, changes);
    syncZoneControls();
}

void StackInspector::showUnavailable(const QString& status) {
    available_ = writable_ = false;
    memory_->setEnabled(false);
    fields_->setEnabled(false);
    previous_->setEnabled(false);
    next_->setEnabled(false);
    syncZoneControls();
    context_->setText(QString("Cœur %1 · %2\n%3").arg(core_).arg(stackFrameName(frame_), status));
}

void StackInspector::showFields(const picodbg::DebugTarget& target, const StackValueChanges& changes) {
    const QSignalBlocker blocker(fields_);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> expanded;
    for (int i = 0; i < fields_->topLevelItemCount(); ++i) {
        const auto* item = fields_->topLevelItem(i);
        if (item->isExpanded()) expanded.insert(item->text(2));
    }
    const int scroll = fields_->verticalScrollBar()->value();
    const auto add = [&](QTreeWidgetItem* item, const StackFieldView& entry, const FrameField& field, int index, QVariant element) {
        const auto where = field.inMemory && frame_.frame.hasSp ? QString("%1%2").arg(field.address >= frame_.frame.sp ? "+0x" : "−0x")
            .arg(field.address >= frame_.frame.sp ? field.address - frame_.frame.sp : frame_.frame.sp - field.address, 0, 16)
            : field.reg >= 0 ? QString::fromStdString(picodbg::registerName(static_cast<unsigned>(field.reg))) : QString("—");
        QString value;
        if (field.type.isArray && field.type.size) {
            QStringList preview;
            for (unsigned i = 0; field.inMemory && i < std::min(field.type.count, 4u); ++i)
                preview << frameValue(target, core_, stackArrayElement(field, i));
            if (!preview.isEmpty() && field.type.count > 4) preview << "…";
            value = preview.isEmpty() ? QString("%1 éléments").arg(field.type.count) : "[" + preview.join(", ") + "]";
        } else value = frameValue(target, core_, field);
        auto type = field.type.name;
        type.replace("unsigned char", "uint8_t");
        type.replace("signed char", "int8_t");
        type.replace("short int", "short");
        type.replace("long int", "long");
        if (field.type.size) type.prepend(QString("%1 o · ").arg(field.type.size * field.type.count));
        const QStringList values{entry.label, where, field.name, type, value.isEmpty() ? QString("—") : value};
        item->setData(0, fieldRole, index);
        item->setData(0, elementRole, element);
        auto span = entry.span;
        if (span.valid() && element.isValid()) {
            span.start = field.address;
            span.end = uint64_t(field.address) + field.type.size;
        }
        auto changed = changes.changedUntil(span, now);
        const bool sampledElement = element.isValid() && span.valid() && span.start >= pageStart_ &&
            span.end <= uint64_t(pageStart_) + pageBytes;
        const bool decoded = !sampledElement && !field.type.isArray && !value.isEmpty() && value != "…" &&
            !value.startsWith('<') && (field.inMemory || field.reg >= 0 || value == field.rendered) &&
            value != "Emplacement indisponible" && value != "Copie en pile non observée";
        if (decoded) {
            const auto key = field.role + "/" + field.name + "/" + where;
            auto found = decodedSamples_.find(key);
            if (found == decodedSamples_.end()) decodedSamples_.insert(key, {value, 0});
            else {
                if (found->value != value) *found = {value, changed ? changed : now + changeMs};
                if (!changed && found->until > now) changed = found->until;
            }
        }
        item->setData(4, changesRole, changed);
        if (changed > now) latestChange_ = std::max(latestChange_, changed);
        for (int col = 0; col < values.size(); ++col) {
            item->setText(col, values[col]);
            item->setToolTip(col, QString("%1 · %2 · %3\n%4\n%5 · %6")
                .arg(entry.label, field.role, field.name, field.type.name, where, values[4]));
        }
    };
    for (int i = 0; i < frame_.fields.size(); ++i) {
        const auto& field = frame_.fields[i];
        auto* item = rowItem(fields_, nullptr, i);
        add(item, field, field.field, i, {});
        item->setData(zoneColumn, Qt::CheckStateRole, field.span.valid()
            ? QVariant(zoneColor(i).isValid() ? Qt::Checked : Qt::Unchecked) : QVariant());
        item->setToolTip(zoneColumn, field.span.valid() ? "Cocher pour surligner la zone en mémoire." : QString());
        const auto& type = field.field.type;
        unsigned first = 0, end = 0;
        if (type.isArray && field.field.inMemory && type.size) {
            StackSpan page;
            page.start = pageStart_;
            page.end = std::min(frame_.memory.end, uint64_t(pageStart_) + pageBytes);
            if (page.overlaps(field.span)) {
                first = (std::max(page.start, field.span.start) - field.span.start) / type.size;
                const auto lastByte = std::min(page.end, field.span.end) - 1;
                end = static_cast<unsigned>((lastByte - field.span.start) / type.size + 1);
            } else end = std::min(type.count, 4u);
        }
        for (unsigned n = first; n < end; ++n)
            add(rowItem(fields_, item, static_cast<int>(n - first)), field, stackArrayElement(field.field, n), i, n);
        trimRows(fields_, item, static_cast<int>(end - first));
        item->setExpanded(expanded.contains(item->text(2)));
    }
    trimRows(fields_, nullptr, frame_.fields.size());
    updateFieldSelection();
    if (latestChange_ > now && !changeRepaint_.isActive()) changeRepaint_.start();
    fields_->verticalScrollBar()->setValue(scroll);
}

void StackInspector::updateFieldSelection() {
    for (int i = 0; i < fields_->topLevelItemCount(); ++i) {
        auto* item = fields_->topLevelItem(i);
        const auto& entry = frame_.fields[i];
        const bool selected = selection_.overlaps(entry.span);
        const auto color = zoneColor(i);
        const auto brush = selected ? palette().highlight() : color.isValid() ? QBrush(color) : QBrush();
        const auto ink = selected ? palette().highlightedText() : QBrush();
        for (int col = 0; col < fields_->columnCount(); ++col) {
            item->setBackground(col, brush);
            item->setForeground(col, ink);
        }
        for (int j = 0; j < item->childCount(); ++j)
            for (int col = 0; col < fields_->columnCount(); ++col) {
                item->child(j)->setBackground(col, brush);
                item->child(j)->setForeground(col, ink);
            }
    }
}

void StackInspector::setRangeSelection(StackSpan selection, bool reveal) {
    if (!available_) return;
    selection_ = selection;
    if (reveal && frame_.memory.contains(selection.start) &&
        (selection.end <= pageStart_ || uint64_t(selection.start) >= uint64_t(pageStart_) + pageBytes)) {
        pageStart_ = frame_.memory.start + ((selection.start - frame_.memory.start) / pageBytes) * pageBytes;
        if (refreshRequested) refreshRequested();
    }
    memory_->setRangeSelection(frame_.level, selection, reveal);
    updateFieldSelection();
    if (selection.valid()) for (int i = 0; i < frame_.fields.size(); ++i) {
        if (selection.overlaps(frame_.fields[i].span)) {
            fields_->scrollToItem(fields_->topLevelItem(i));
            break;
        }
    }
}

void StackInspector::visitPage(int direction) {
    if (!available_) return;
    const auto offset = pageStart_ - frame_.memory.start;
    pageStart_ = frame_.memory.start + (direction < 0 ? offset > pageBytes ? offset - pageBytes : 0 : offset + pageBytes);
    if (refreshRequested) refreshRequested();
    memory_->scrollToTop();
}
