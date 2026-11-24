#pragma once
#include "picodbg/peripherals.hpp"
#include "picodbg/target.hpp"
#include <QString>

struct DebugValue {
    QString text;
    QString detail;
    bool available;
    uint32_t raw;
};

inline uint32_t peekMemoryByte(const picodbg::DebugTarget& target, uint32_t address, unsigned core) {
    const auto* region = picodbg::regionAt(target.description(), address);
    if (region && region->permissions == "MMIO")
        return (target.peek(address & ~3u, 4, core) >> (8 * (address & 3u))) & 0xffu;
    return target.peek(address, 1, core);
}

inline DebugValue inspectValue(const picodbg::DebugTarget& target, uint32_t address, unsigned size, unsigned core) {
    if (!target.loaded()) return {target.hardware() ? "Déconnectée" : "Pas d'image", {}, false, 0};
    if (target.executing() || target.busy()) return {"En attente", "Valeur disponible à l’arrêt de la cible", false, 0};
    try {
        const auto raw = target.peek(address, size, core);
        return {QString("0x%1").arg(raw, static_cast<int>(2 * size), 16, QLatin1Char('0')), {}, true, raw};
    } catch (const picodbg::Error& error) {
        const auto detail = QString::fromUtf8(error.what());
        if (detail.startsWith("Lecture explicite")) return {"Lecture explicite", detail, false, 0};
        if (detail.startsWith("Lecture matérielle en cours")) return {"Lecture…", detail, false, 0};
        return {"Indisponible", detail, false, 0};
    }
}

inline DebugValue inspectRegister(const picodbg::DebugTarget& target, const picodbg::RegisterDescription& reg,
                                  unsigned core, bool emulated) {
    if (!target.hardware() && reg.documentedOnly) return {"Non émulé", {}, false, 0};
    if (!target.loaded()) return inspectValue(target, reg.address, 4, core);
    if (reg.access == "WO") return {"Écriture seule", {}, false, 0};
    if (!target.hardware() && !emulated) return {"Non émulé", {}, false, 0};
    return inspectValue(target, reg.address, 4, core);
}

inline QString registerAccessText(const picodbg::RegisterDescription& reg) {
    if (reg.access == "RW") return "Lecture et écriture";
    if (reg.access == "WO") return "Écriture seule";
    if (reg.access == "W1C") return "Écrire 1 efface les bits correspondants";
    if (reg.access == "W1S") return "Écrire 1 active les bits correspondants";
    if (reg.access == "W1T") return "Écrire 1 inverse les bits correspondants";
    if (reg.access == "SPECIAL") return "Accès spécial selon la documentation du registre";
    if (reg.access == "RC") return "Lecture avec effacement";
    return "Lecture seule";
}
