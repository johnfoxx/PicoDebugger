#include "memory_table.hpp"
#include "debug_value.hpp"
#include "picodbg/target.hpp"
#include <QFontDatabase>
#include <QFontInfo>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>

namespace {
constexpr int asciiInset = 7;
constexpr int lastValueRole = Qt::UserRole;
constexpr int changedRole = Qt::UserRole + 1;
constexpr int tooltipRole = Qt::UserRole + 2;

QString q(const std::string& text) { return QString::fromStdString(text); }

QTableWidgetItem& cell(QTableWidget& table, int row, int column) {
    auto* item = table.item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        item->setFlags(column == MemoryTable::AsciiColumn ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (column > 0 && column < MemoryTable::AsciiColumn) item->setTextAlignment(Qt::AlignCenter);
        table.setItem(row, column, item);
    }
    return *item;
}

void paintWatchUnderline(QPainter& painter, const QRect& rect, const QBrush& watch) {
    if (watch.style() != Qt::NoBrush)
        painter.fillRect(rect.adjusted(1, rect.height() - 3, -1, -1), watch);
}

class ByteDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        if (option.state.testFlag(QStyle::State_Selected))
            paintWatchUnderline(*painter, option.rect, index.data(Qt::BackgroundRole).value<QBrush>());
    }
};

class AsciiDelegate final : public QStyledItemDelegate {
public:
    explicit AsciiDelegate(MemoryTable& table) : QStyledItemDelegate(&table), table_(table) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto background = option;
        initStyleOption(&background, index);
        background.text.clear();
        table_.style()->drawControl(QStyle::CE_ItemViewItem, &background, painter, &table_);
        const auto text = index.data().toString();
        const int active = table_.currentRow() == index.row() ? table_.currentColumn() - 1 : -1;
        const auto advance = option.fontMetrics.horizontalAdvance(QLatin1Char('M'));
        painter->save();
        painter->setClipRect(option.rect);
        painter->setFont(option.font);
        QRect glyph(option.rect.left() + asciiInset, option.rect.top(), advance, option.rect.height());
        for (int i = 0; i < text.size(); ++i) {
            const auto* byte = table_.item(index.row(), i + 1);
            if (i == active) painter->fillRect(glyph, option.palette.highlight());
            else if (byte && byte->background().style() != Qt::NoBrush) painter->fillRect(glyph, byte->background());
            painter->setPen(i == active ? option.palette.highlightedText().color() : option.palette.text().color());
            painter->drawText(glyph, Qt::AlignCenter, text.mid(i, 1));
            if (i == active && byte) paintWatchUnderline(*painter, glyph, byte->background());
            glyph.translate(advance, 0);
        }
        painter->restore();
    }
private:
    const MemoryTable& table_;
};
}

MemoryTable::MemoryTable(QWidget* parent) : QTableWidget(0, AsciiColumn + 1, parent) {
    setObjectName("memoryTable");
    QStringList headers{"Adresse"};
    for (unsigned i = 0; i < MaxBytesPerRow; ++i)
        headers << QString::number(i, 16).rightJustified(2, QLatin1Char('0')).toUpper();
    headers << "ASCII";
    setHorizontalHeaderLabels(headers);
    horizontalHeaderItem(0)->setToolTip(debugAccessLegend());
    for (int column = 1; column < AsciiColumn; ++column)
        horizontalHeaderItem(column)->setToolTip("Offset hexadécimal par rapport à l’adresse de la ligne");
    horizontalHeaderItem(AsciiColumn)->setToolTip("Cliquer sur un caractère pour sélectionner l’octet correspondant");
    auto fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(fixed).fixedPitch()) fixed.setFamily("monospace");
    fixed.setPixelSize(14);
    fixed.setStyleHint(QFont::Monospace);
    fixed.setFixedPitch(true);
    setFont(fixed);
    horizontalHeader()->setFont(fixed);
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    verticalHeader()->hide();
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setMinimumSectionSize(18);
    verticalHeader()->setDefaultSectionSize(19);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    horizontalHeader()->setMinimumSectionSize(16);
    horizontalHeader()->setStretchLastSection(true);
    setShowGrid(false);
    setColumnWidth(0, fontMetrics().horizontalAdvance("0x00000000") + 22);
    byteWidth_ = fontMetrics().horizontalAdvance("00") + 7;
    asciiAdvance_ = fontMetrics().horizontalAdvance(QLatin1Char('M'));
    for (int column = 1; column < AsciiColumn; ++column)
        setColumnWidth(column, byteWidth_ + (column % 4 == 0 && column != MaxBytesPerRow ? 4 : 0));
    setColumnWidth(AsciiColumn, asciiAdvance_ * bytesPerRow_ + 2 * asciiInset);
    setItemDelegate(new ByteDelegate(this));
    setItemDelegateForColumn(AsciiColumn, new AsciiDelegate(*this));
    connect(this, &QTableWidget::currentCellChanged, this, [this](int row, int, int previous, int) {
        for (int at : {previous, row}) {
            if (auto* item = this->item(at, AsciiColumn)) viewport()->update(visualItemRect(item));
        }
    });
}

void MemoryTable::clearRange() {
    target_ = nullptr;
    length_ = 0;
    setRowCount(0);
}

bool MemoryTable::addressAt(int row, int column, uint32_t& address) const {
    if (row < 0 || column < 0 || column > bytesPerRow_) return false;
    const auto offset = static_cast<unsigned>(row * bytesPerRow_ + std::max(0, column - 1));
    if (offset >= length_) return false;
    address = start_ + offset;
    return true;
}

void MemoryTable::selectAddress(uint32_t address) {
    if (address < start_ || uint64_t(address) >= uint64_t(start_) + length_) return;
    const auto offset = address - start_;
    setCurrentCell(static_cast<int>(offset / static_cast<unsigned>(bytesPerRow_)),
                   static_cast<int>(offset % static_cast<unsigned>(bytesPerRow_)) + 1);
}

void MemoryTable::setHighlights(const std::vector<MemoryHighlight>& highlights) {
    if (highlights_ == highlights) return;
    highlights_ = highlights;
    colorBytes();
}

void MemoryTable::colorBytes() {
    for (unsigned i = 0; i < length_; ++i) {
        const uint32_t address = start_ + i;
        const MemoryHighlight* match = nullptr;
        for (const auto& highlight : highlights_) {
            if (address < highlight.start || address >= highlight.end) continue;
            if (!match || highlight.end - highlight.start < match->end - match->start) match = &highlight;
        }
        const auto columns = static_cast<unsigned>(bytesPerRow_);
        if (auto* byte = item(static_cast<int>(i / columns), static_cast<int>(i % columns) + 1)) {
            byte->setBackground(match ? QBrush(match->color) : QBrush());
            const auto tip = byte->data(tooltipRole).toString();
            byte->setToolTip(match ? tip + "\nSuivi · " + match->label : tip);
        }
    }
    viewport()->update();
}

void MemoryTable::mousePressEvent(QMouseEvent* event) {
    const auto index = indexAt(event->pos());
    if (event->button() == Qt::LeftButton && index.column() == AsciiColumn) {
        const int x = event->pos().x() - visualRect(index).left() - asciiInset;
        const int offset = x / fontMetrics().horizontalAdvance(QLatin1Char('M'));
        uint32_t address = 0;
        if (x >= 0 && offset < bytesPerRow_ && addressAt(index.row(), offset + 1, address)) {
            setFocus(Qt::MouseFocusReason);
            selectAddress(address);
            return;
        }
    }
    QTableWidget::mousePressEvent(event);
}

int MemoryTable::bytesThatFit() const {
    const int available = width() - 2 * frameWidth() - verticalScrollBar()->sizeHint().width();
    const int fixed = columnWidth(0) + 2 * asciiInset;
    for (int columns : {16, 8}) {
        if (fixed + columns * (byteWidth_ + asciiAdvance_ + 1) <= available) return columns;
    }
    return 4;
}

void MemoryTable::resizeEvent(QResizeEvent* event) {
    QTableWidget::resizeEvent(event);
    if (resizePending_ || bytesThatFit() == bytesPerRow_) return;
    resizePending_ = true;
    QTimer::singleShot(0, this, [this] {
        resizePending_ = false;
        if (resized) resized();
    });
}

void MemoryTable::showRange(const picodbg::DebugTarget& target, unsigned core, uint32_t start, unsigned length) {
    const bool sameRange = target_ == &target && core_ == core && start_ == start;
    const int columns = bytesThatFit();
    const bool newColumns = columns != bytesPerRow_;
    const auto paletteKey = palette().cacheKey();
    const auto revision = target.revision();
    if (sameRange && length_ == length && revision_ == revision && paletteKey_ == paletteKey && !newColumns) return;
    uint32_t selection = 0;
    const bool keepSelection = sameRange && newColumns && addressAt(currentRow(), currentColumn(), selection);
    if (!sameRange || newColumns) setRowCount(0);
    if (newColumns) {
        bytesPerRow_ = columns;
        for (int column = 1; column < AsciiColumn; ++column) setColumnHidden(column, column > columns);
        setColumnWidth(AsciiColumn, asciiAdvance_ * columns + 2 * asciiInset);
    }
    const bool newSimulationSample = !target.hardware() && revision_ != revision;
    target_ = &target;
    core_ = core;
    start_ = start;
    length_ = length;
    revision_ = revision;
    paletteKey_ = paletteKey;
    const auto bytesPerRow = static_cast<unsigned>(bytesPerRow_);
    setRowCount(static_cast<int>((length + bytesPerRow - 1) / bytesPerRow));
    const auto& chip = target.description();
    const auto highlight = picoThemeColor("#ffd780");
    const auto unavailable = picoThemeColor("#a8afb5");
    const auto foreground = picoThemeColor("#dce6f0");
    for (unsigned i = 0; i < length; i += bytesPerRow) {
        const int row = static_cast<int>(i / bytesPerRow);
        const auto count = std::min(bytesPerRow, length - i);
        const auto base = start + i;
        const auto access = memoryAccess(chip, base, count);
        auto& address = cell(*this, row, 0);
        address.setText(q(picodbg::hex(base)));
        address.setIcon(debugAccessIcon(access));
        address.setForeground(debugAccessColor(access));
        const auto* first = picodbg::regionAt(chip, base);
        address.setToolTip(first && uint64_t(base) + count <= first->end
            ? q(first->name) + " · " + (first->permissions == "MMIO" ? QString("Accès selon le registre") : debugAccessText(access))
            : debugAccessText(access));
        QString ascii;
        for (unsigned j = 0; j < bytesPerRow; ++j) {
            auto& byte = cell(*this, row, static_cast<int>(j) + 1);
            if (j >= count) {
                byte.setText({});
                byte.setToolTip({});
                byte.setData(lastValueRole, {});
                byte.setData(changedRole, {});
                byte.setBackground({});
                byte.setFlags(Qt::NoItemFlags);
                continue;
            }
            byte.setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            const auto at = base + j;
            const auto* region = picodbg::regionAt(chip, at);
            QString tip = q(picodbg::hex(at)) + " · " + debugAccessText(regionAccess(region));
            try {
                const auto raw = peekMemoryByte(target, at, core);
                const auto previous = byte.data(lastValueRole);
                const bool changed = previous.isValid() && previous.toUInt() != raw;
                const bool marked = changed || (!newSimulationSample && byte.data(changedRole).toBool());
                byte.setData(lastValueRole, raw);
                byte.setData(changedRole, marked);
                byte.setText(QString::number(raw, 16).rightJustified(2, QLatin1Char('0')).toUpper());
                byte.setForeground(marked ? highlight : foreground);
                ascii += raw >= 32 && raw < 127 ? QChar(static_cast<char>(raw)) : QChar('.');
            } catch (const picodbg::Error& error) {
                byte.setText("??");
                byte.setForeground(unavailable);
                byte.setData(changedRole, false);
                tip += "\n" + q(error.what());
                ascii += '?';
            }
            byte.setData(tooltipRole, tip);
        }
        cell(*this, row, AsciiColumn).setText(ascii);
    }
    colorBytes();
    if (keepSelection) selectAddress(selection);
}
