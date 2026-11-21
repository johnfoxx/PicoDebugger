#pragma once

#include <QJsonObject>

class QWidget;

namespace picoide {

QJsonObject installedTools();
QString dependencyLinks();
bool manageLocalTools(QWidget* parent);

}
