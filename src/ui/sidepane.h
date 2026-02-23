/*
 * Qt-native Side Pane widget
 * src/ui/sidepane.h
 */

#ifndef PCMANFM_UI_SIDEPANE_H
#define PCMANFM_UI_SIDEPANE_H

#include <QTreeWidget>
#include <QString>
#include <QUrl>
#include <QSet>

namespace Oneg4FM {

class SidePane : public QTreeWidget {
    Q_OBJECT
   public:
    enum Mode { PlacesMode, TreeMode };

    explicit SidePane(QWidget* parent = nullptr);
    ~SidePane() override;

    void setMode(Mode mode);
    Mode mode() const;

    // Stub for compatibility with existing settings
    void setMode(int mode) { setMode(static_cast<Mode>(mode)); }
    void restoreHiddenPlaces(const QSet<QString>& hidden);

    void setIconSize(const QSize& size);

    // Compatibility methods
    void setCurrentPath(const QUrl& path);
    void setShowHidden(bool show);

   Q_SIGNALS:
    void chdirRequested(int type, const QUrl& url);
    void openFolderInNewWindowRequested(const QUrl& url);
    void openFolderInNewTabRequested(const QUrl& url);
    void openFolderInTerminalRequested(const QUrl& url);
    void createNewFolderRequested(const QUrl& url);
    void modeChanged(Mode mode);
    void hiddenPlaceSet(const QString& name, bool hidden);

   private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem* item, int column);

   private:
    void setupPlaces();
    void addPlace(const QString& name, const QString& iconName, const QString& path);

    Mode mode_ = PlacesMode;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_UI_SIDEPANE_H
