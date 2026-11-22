#pragma once
#include "picodbg/peripherals.hpp"
#include "window_theme.hpp"
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <algorithm>

enum class DebugAccess { ReadOnly, Writable, Mixed, Unknown };

struct MemoryHighlight {
    uint32_t start;
    uint64_t end;
    QColor color;
    QString label;
    bool operator==(const MemoryHighlight& other) const {
        return start == other.start && end == other.end && color == other.color && label == other.label;
    }
};

inline DebugAccess registerAccess(const picodbg::RegisterDescription& reg) {
    if (picodbg::registerWritable(reg)) return DebugAccess::Writable;
    if (reg.access == "RO" || reg.access == "RC") return DebugAccess::ReadOnly;
    return DebugAccess::Unknown;
}

inline DebugAccess peripheralAccess(const picodbg::PeripheralDescription& device) {
    if (device.registers.empty()) return DebugAccess::Unknown;
    const auto first = registerAccess(device.registers.front());
    const bool mixed = std::any_of(device.registers.begin() + 1, device.registers.end(),
        [first](const picodbg::RegisterDescription& reg) { return registerAccess(reg) != first; });
    return mixed ? DebugAccess::Mixed : first;
}

inline DebugAccess regionAccess(const picodbg::Region* region) {
    if (!region || region->permissions == "MMIO") return DebugAccess::Unknown;
    if (picodbg::regionWritable(*region)) return DebugAccess::Writable;
    if (!region->permissions.empty() && region->permissions.front() == 'R') return DebugAccess::ReadOnly;
    return DebugAccess::Unknown;
}

inline DebugAccess memoryAccess(const picodbg::ChipDescription& chip, uint32_t address, unsigned size) {
    const auto* region = picodbg::regionAt(chip, address);
    const auto access = regionAccess(region);
    if (region && uint64_t(address) + size <= region->end) return access;
    for (unsigned i = 1; i < size; ++i) {
        if (regionAccess(picodbg::regionAt(chip, address + i)) != access) return DebugAccess::Mixed;
    }
    return access;
}

inline QString debugAccessText(DebugAccess access) {
    switch (access) {
    case DebugAccess::ReadOnly: return "Lecture seule";
    case DebugAccess::Writable: return "Inscriptible";
    case DebugAccess::Mixed: return "Accès mixte";
    case DebugAccess::Unknown: return "Accès à déterminer";
    }
    return {};
}

inline QString debugAccessLegend() {
    return "Bleu · Lecture seule\nVert · Inscriptible\nViolet · Accès mixte\nGris · Accès à déterminer";
}

inline QColor debugAccessColor(DebugAccess access) {
    switch (access) {
    case DebugAccess::ReadOnly: return picoThemeColor("#a8c7ff");
    case DebugAccess::Writable: return picoThemeColor("#add7a2");
    case DebugAccess::Mixed: return picoThemeColor("#d5adf4");
    case DebugAccess::Unknown: return picoThemeColor("#a8afb5");
    }
    return {};
}

inline QIcon debugColorIcon(const QColor& color) {
    static QHash<QRgb, QIcon> icons;
    auto found = icons.constFind(color.rgb());
    if (found != icons.cend()) return found.value();
    QPixmap square(8, 8);
    square.fill(color);
    QIcon icon(square);
    icon.addPixmap(square, QIcon::Selected);
    return icons.insert(color.rgb(), icon).value();
}

inline QIcon debugAccessIcon(DebugAccess access) {
    return debugColorIcon(debugAccessColor(access));
}
