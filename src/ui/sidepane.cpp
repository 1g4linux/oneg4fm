/*
 * Qt-native Side Pane widget
 * src/ui/sidepane.cpp
 */

#include "sidepane.h"
#include <QIcon>
#include <QDir>
#include <QStandardPaths>

namespace Oneg4FM {

SidePane::SidePane(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setIndentation(12);
    setUniformRowHeights(true);
    setExpandsOnDoubleClick(false);

    connect(this, &QTreeWidget::itemClicked, this, &SidePane::onItemClicked);

    setupPlaces();
}

SidePane::~SidePane() = default;

void SidePane::setMode(Mode mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    // In a real impl, we'd switch between a Places model and a Directory Tree model
    // For now, we stick to Places as a stub
    Q_EMIT modeChanged(mode);
}

SidePane::Mode SidePane::mode() const {
    return mode_;
}

void SidePane::restoreHiddenPlaces(const QSet<QString>& hidden) {
    // Stub
    Q_UNUSED(hidden);
}

void SidePane::setIconSize(const QSize& size) {
    QTreeWidget::setIconSize(size);
}

void SidePane::setCurrentPath(const QUrl& path) {
    QString searchPath;
    if (path.isLocalFile()) {
        searchPath = path.toLocalFile();
    }
    else {
        searchPath = path.toString();
    }

    QTreeWidgetItemIterator it(this);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == searchPath) {
            setCurrentItem(*it);
            return;
        }
        ++it;
    }
    clearSelection();
}

void SidePane::setShowHidden(bool show) {
    Q_UNUSED(show);
}

void SidePane::setupPlaces() {
    clear();

    // Home
    addPlace(tr("Home"), QStringLiteral("user-home"), QDir::homePath());

    // Desktop
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        addPlace(tr("Desktop"), QStringLiteral("user-desktop"), desktop);
    }

    // Trash (Stub path)
    addPlace(tr("Trash"), QStringLiteral("user-trash"), QStringLiteral("trash:///"));

    // Root
    addPlace(tr("File System"), QStringLiteral("drive-harddisk"), QStringLiteral("/"));
}

void SidePane::addPlace(const QString& name, const QString& iconName, const QString& path) {
    auto* item = new QTreeWidgetItem(this);
    item->setText(0, name);
    item->setIcon(0, QIcon::fromTheme(iconName));
    item->setData(0, Qt::UserRole, path);
    addTopLevelItem(item);
}

void SidePane::onItemClicked(QTreeWidgetItem* item, int column) {
    QString path = item->data(column, Qt::UserRole).toString();
    if (!path.isEmpty()) {
        QUrl url(path);
        if (url.scheme().isEmpty()) {
            url = QUrl::fromLocalFile(path);
        }
        Q_EMIT chdirRequested(0, url);
    }
}

}  // namespace Oneg4FM