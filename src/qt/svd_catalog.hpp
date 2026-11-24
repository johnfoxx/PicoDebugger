#pragma once
#include "picodbg/peripherals.hpp"
#include <QString>
#include <memory>

namespace picosvd {
std::shared_ptr<const std::vector<picodbg::PeripheralDescription>> catalogSnapshot(picodbg::Chip chip);
const std::vector<picodbg::PeripheralDescription>& catalog(picodbg::Chip chip);
std::vector<picodbg::PeripheralDescription> parse(picodbg::Chip chip, const QByteArray& contents);
QString source(picodbg::Chip chip);
QString officialUrl(picodbg::Chip chip);
// Validate, store and activate a catalogue for the selected chip.
void install(picodbg::Chip chip, const QByteArray& contents);
}
