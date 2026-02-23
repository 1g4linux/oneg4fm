/*
 * XDG directory utilities implementation
 * oneg4fm/xdgdir.cpp
 */

#include "xdgdir.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include "../src/ui/fsqt.h"

static const QRegularExpression desktopRegex(QStringLiteral("XDG_DESKTOP_DIR=\"([^\n]*)\""));

QString XdgDir::readUserDirsFile() {
    QByteArray data;
    QString error;
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs");
    if (Oneg4FM::FsQt::readFile(path, data, error)) {
        return QString::fromLocal8Bit(data);
    }
    return QString();
}

QString XdgDir::readDesktopDir() {
    QString str = readUserDirsFile();
    if (!str.isEmpty()) {
        QRegularExpressionMatch match;
        if (str.lastIndexOf(desktopRegex, -1, &match) != -1) {
            str = match.captured(1);
            if (str.startsWith(QStringLiteral("$HOME"))) {
                str = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + str.mid(5);
            }
            return str;
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Desktop");
}

void XdgDir::setDesktopDir(QString path) {
    QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (path.startsWith(home)) {
        path = QStringLiteral("$HOME") + path.mid(home.length());
    }
    QString str = readUserDirsFile();
    QString line = QStringLiteral("XDG_DESKTOP_DIR=\"") + path + QLatin1Char('\"');
    if (str.contains(desktopRegex)) {
        str.replace(desktopRegex, line);
    }
    else {
        if (!str.endsWith(QLatin1Char('\n'))) {
            str += QLatin1Char('\n');
        }
        str += line + QLatin1Char('\n');
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString error;
    if (Oneg4FM::FsQt::makeDirParents(dir, error)) {
        Oneg4FM::FsQt::writeFileAtomic(dir + QStringLiteral("/user-dirs.dirs"), str.toLocal8Bit(), error);
    }
}
