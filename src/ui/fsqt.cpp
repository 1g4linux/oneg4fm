/*
 * Qt adapter helpers around POSIX fs_ops
 * src/ui/fsqt.cpp
 */

#include "fsqt.h"

#include "../core/fs_ops.h"

namespace Oneg4FM::FsQt {

namespace {

std::string toNative(const QString& path) {
    const QByteArray bytes = QFile::encodeName(path);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString errorToQString(const FsOps::Error& err) {
    return err.isSet() ? QString::fromLocal8Bit(err.message.c_str()) : QString();
}

}  // namespace

bool readFile(const QString& path, QByteArray& out, QString& errorOut) {
    FsOps::Error err;
    std::vector<std::uint8_t> data;
    const bool ok = FsOps::read_file_all(toNative(path), data, err);
    errorOut = errorToQString(err);
    if (ok) {
        out = QByteArray(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    }
    else {
        out.clear();
    }
    return ok;
}

bool writeFileAtomic(const QString& path, const QByteArray& data, QString& errorOut) {
    FsOps::Error err;
    const bool ok = FsOps::write_file_atomic(toNative(path), reinterpret_cast<const std::uint8_t*>(data.constData()),
                                             static_cast<std::size_t>(data.size()), err);
    errorOut = errorToQString(err);
    return ok;
}

bool makeDirParents(const QString& path, QString& errorOut) {
    FsOps::Error err;
    const bool ok = FsOps::make_dir_parents(toNative(path), err);
    errorOut = errorToQString(err);
    return ok;
}

bool setPermissions(const QString& path, QFile::Permissions perms, QString& errorOut) {
    FsOps::Error err;
    const bool ok = FsOps::set_permissions(toNative(path), static_cast<unsigned int>(perms), err);
    errorOut = errorToQString(err);
    return ok;
}

}  // namespace Oneg4FM::FsQt
