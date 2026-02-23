/*
 * Qt adapter helpers around POSIX fs_ops
 * src/ui/fsqt.h
 */

#ifndef PCMANFM_FSQT_H
#define PCMANFM_FSQT_H

#include <QByteArray>
#include <QFile>
#include <QString>

namespace Oneg4FM::FsQt {

// Synchronous helpers for UI/Qt code to use POSIX core ops.
bool readFile(const QString& path, QByteArray& out, QString& errorOut);
bool writeFileAtomic(const QString& path, const QByteArray& data, QString& errorOut);
bool makeDirParents(const QString& path, QString& errorOut);
bool setPermissions(const QString& path, QFile::Permissions perms, QString& errorOut);

}  // namespace Oneg4FM::FsQt

#endif  // PCMANFM_FSQT_H
