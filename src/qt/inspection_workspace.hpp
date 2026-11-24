#pragma once
#include <QPointer>
#include <QTabWidget>
#include <functional>
#include <map>

class QDialog;

// Owns inspection pages in either a tab or a floating window. Moving a page
// never recreates it: selection, filters, UART input and core context survive.
class InspectionWorkspace : public QTabWidget {
public:
    explicit InspectionWorkspace(QWidget* parent = nullptr);
    ~InspectionWorkspace() override;
    void addFixedPage(QWidget* page, const QString& title);
    void addInspection(const QString& key, const QString& title, QWidget* page,
                       std::function<void(bool)> presentationChanged);
    bool revealInspection(const QString& key);
    void toggleDetached(const QString& key, const QByteArray& geometry = {});
    QByteArray detachedGeometry(const QWidget* page) const;
    void gatherInspections();
    void clearInspections();
private:
    struct Entry {
        QString title;
        QPointer<QWidget> page;
        QPointer<QDialog> window;
        std::function<void(bool)> presentationChanged;
    };
    std::map<QString, Entry> entries_;
    void attach(const QString& key);
    void closeInspection(const QString& key);
};
