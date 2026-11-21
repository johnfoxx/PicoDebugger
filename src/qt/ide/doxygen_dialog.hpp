#pragma once

#include <functional>

class QString;
class QWidget;

namespace picoide {

void generateDoxygen(const QString& root, QWidget* parent, const std::function<bool()>& saveSources);

}
