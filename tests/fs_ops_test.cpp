/*
 * Tests for POSIX filesystem core helpers
 * tests/fs_ops_test.cpp
 */

#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>

#include "../src/core/fs_ops.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fstream>

using namespace PCManFM::FsOps;

namespace {

QString makePath(const QTemporaryDir& dir, const QString& name) {
    return dir.path() + QLatin1Char('/') + name;
}

QByteArray readQtFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return f.readAll();
}

QString writeTempFile(const QTemporaryDir& dir, const QString& name, const QByteArray& data) {
    const QString path = makePath(dir, name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    return path;
}

}  // namespace

class FsOpsTest : public QObject {
    Q_OBJECT

   private slots:
    void readWriteRoundTrip();
    void copyFile();
    void copyDirectoryRecursive();
    void moveFileRenamePath();
    void moveFileCopyFallback();
    void moveFallbackSourceDeleteFailurePreservesDestination();
    void deletePathRecursive();
    void makeDirParentsCreatesHierarchy();
    void setPermissionsChangesMode();
    void copyCancelledViaCallback();
    void copyIntoExistingDestinationCancelledPreservesPreexisting();
    void copyIntoExistingDestinationErrorPreservesPreexisting();
    void copyRollbackRemovesOnlyCreatedPaths();
    void readMissingFileFails();
    void makeDirParentsFailsOnExistingFile();
    void setPermissionsFailsOnMissing();
    void copySymlinkPreservesLink();
    void copyPreservesMtime();
};

void FsOpsTest::readWriteRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString filePath = makePath(dir, QStringLiteral("sample.txt"));
    const std::string nativePath = filePath.toLocal8Bit().toStdString();
    const QByteArray payload("hello world");

    Error err;
    QVERIFY(write_file_atomic(nativePath, reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));
    QVERIFY(!err.isSet());

    std::vector<std::uint8_t> out;
    QVERIFY(read_file_all(nativePath, out, err));
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(out.data()), static_cast<int>(out.size())), payload);
}

void FsOpsTest::copyFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = makePath(dir, QStringLiteral("src.bin"));
    const QString dstPath = makePath(dir, QStringLiteral("dst.bin"));

    const QByteArray payload("copy-me");
    Error err;
    QVERIFY(write_file_atomic(srcPath.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    ProgressInfo progress;
    progress.filesTotal = 1;
    auto progressCb = [](const ProgressInfo&) { return true; };

    QVERIFY(
        copy_path(srcPath.toLocal8Bit().toStdString(), dstPath.toLocal8Bit().toStdString(), progress, progressCb, err));
    QVERIFY(!err.isSet());
    QVERIFY(QFileInfo(dstPath).exists());
    QCOMPARE(readQtFile(dstPath), payload);
}

void FsOpsTest::copyDirectoryRecursive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = makePath(dir, QStringLiteral("srcdir"));
    const QString dstDir = makePath(dir, QStringLiteral("dstdir"));

    Error err;
    QVERIFY(make_dir_parents(srcDir.toLocal8Bit().toStdString(), err));

    const QString nested = srcDir + QStringLiteral("/nested.txt");
    const QByteArray payload("nested-data");
    QVERIFY(write_file_atomic(nested.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    ProgressInfo progress;
    progress.filesTotal = 1;
    auto progressCb = [](const ProgressInfo&) { return true; };

    QVERIFY(
        copy_path(srcDir.toLocal8Bit().toStdString(), dstDir.toLocal8Bit().toStdString(), progress, progressCb, err));
    QVERIFY(!err.isSet());
    QVERIFY(QFileInfo(dstDir + QStringLiteral("/nested.txt")).exists());
    QCOMPARE(readQtFile(dstDir + QStringLiteral("/nested.txt")), payload);
}

void FsOpsTest::moveFileRenamePath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = makePath(dir, QStringLiteral("src.txt"));
    const QString dstPath = makePath(dir, QStringLiteral("moved.txt"));

    Error err;
    const QByteArray payload("move-me");
    QVERIFY(write_file_atomic(srcPath.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    ProgressInfo progress;
    progress.filesTotal = 1;
    auto progressCb = [](const ProgressInfo&) { return true; };

    QVERIFY(
        move_path(srcPath.toLocal8Bit().toStdString(), dstPath.toLocal8Bit().toStdString(), progress, progressCb, err));
    QVERIFY(!err.isSet());
    QVERIFY(!QFileInfo(srcPath).exists());
    QCOMPARE(readQtFile(dstPath), payload);
}

void FsOpsTest::moveFileCopyFallback() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = makePath(dir, QStringLiteral("src2.txt"));
    const QString dstPath = makePath(dir, QStringLiteral("moved2.txt"));

    Error err;
    const QByteArray payload("fallback");
    QVERIFY(write_file_atomic(srcPath.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    ProgressInfo progress;
    progress.filesTotal = 1;
    auto progressCb = [](const ProgressInfo&) { return true; };

    QVERIFY(move_path(srcPath.toLocal8Bit().toStdString(), dstPath.toLocal8Bit().toStdString(), progress, progressCb,
                      err, /*forceCopyFallbackForTests=*/true));
    QVERIFY(!err.isSet());
    QVERIFY(!QFileInfo(srcPath).exists());
    QCOMPARE(readQtFile(dstPath), payload);
}

void FsOpsTest::moveFallbackSourceDeleteFailurePreservesDestination() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString lockedDir = makePath(dir, QStringLiteral("locked"));
    const QString srcDir = lockedDir + QLatin1String("/src");
    const QString dstDir = makePath(dir, QStringLiteral("dst"));

    Error err;
    QVERIFY(make_dir_parents(srcDir.toLocal8Bit().toStdString(), err));
    QVERIFY(make_dir_parents(dstDir.toLocal8Bit().toStdString(), err));

    const QString srcFile = srcDir + QLatin1String("/new.txt");
    QVERIFY(write_file_atomic(srcFile.toLocal8Bit().toStdString(), reinterpret_cast<const std::uint8_t*>("moved-data"),
                              std::size_t{10}, err));

    const QString keepPath = writeTempFile(dir, QStringLiteral("dst/keep.txt"), "keep");

    // Block deleting the source root from its parent after the copy succeeds.
    QVERIFY(::chmod(lockedDir.toLocal8Bit().constData(), 0555) == 0);

    ProgressInfo progress;
    auto progressCb = [](const ProgressInfo&) { return true; };
    QVERIFY(!move_path(srcDir.toLocal8Bit().toStdString(), dstDir.toLocal8Bit().toStdString(), progress, progressCb,
                       err, /*forceCopyFallbackForTests=*/true));

    // Restore permissions so QTemporaryDir can clean up.
    QVERIFY(::chmod(lockedDir.toLocal8Bit().constData(), 0755) == 0);

    QCOMPARE(readQtFile(keepPath), QByteArray("keep"));
    QCOMPARE(readQtFile(dstDir + QLatin1String("/new.txt")), QByteArray("moved-data"));
    QVERIFY(QFileInfo(dstDir).isDir());
}

void FsOpsTest::deletePathRecursive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString subdir = makePath(dir, QStringLiteral("todelete"));
    Error err;
    QVERIFY(make_dir_parents((subdir + QStringLiteral("/child")).toLocal8Bit().toStdString(), err));
    const QString nestedFile = subdir + QStringLiteral("/child/file.txt");
    const QByteArray payload("delete-me");
    QVERIFY(write_file_atomic(nestedFile.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    ProgressInfo progress;
    auto progressCb = [](const ProgressInfo&) { return true; };
    QVERIFY(delete_path(subdir.toLocal8Bit().toStdString(), progress, progressCb, err));
    QVERIFY(!err.isSet());
    QVERIFY(!QFileInfo(subdir).exists());
}

void FsOpsTest::makeDirParentsCreatesHierarchy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString newDir = makePath(dir, QStringLiteral("a/b/c"));
    Error err;
    QVERIFY(make_dir_parents(newDir.toLocal8Bit().toStdString(), err));
    QVERIFY(QFileInfo(newDir).isDir());
}

void FsOpsTest::setPermissionsChangesMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString filePath = makePath(dir, QStringLiteral("perms.txt"));
    Error err;
    const QByteArray payload("perms");
    QVERIFY(write_file_atomic(filePath.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    QVERIFY(set_permissions(filePath.toLocal8Bit().toStdString(), 0600, err));
    struct stat st{};
    QVERIFY(::stat(filePath.toLocal8Bit().constData(), &st) == 0);
    QVERIFY((st.st_mode & 0777) == 0600);
}

void FsOpsTest::copyCancelledViaCallback() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = makePath(dir, QStringLiteral("big.bin"));
    const QString dstPath = makePath(dir, QStringLiteral("big_copy.bin"));

    Error err;
    QByteArray payload(256 * 1024, 'x');  // 256 KB
    QVERIFY(write_file_atomic(srcPath.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    ProgressInfo progress;
    progress.filesTotal = 1;
    bool cancelled = false;
    auto cancelCb = [&cancelled](const ProgressInfo&) {
        cancelled = true;
        return false;  // cancel immediately
    };

    QVERIFY(
        !copy_path(srcPath.toLocal8Bit().toStdString(), dstPath.toLocal8Bit().toStdString(), progress, cancelCb, err));
    QVERIFY(err.code == ECANCELED);
    QVERIFY(cancelled);
}

void FsOpsTest::copyIntoExistingDestinationCancelledPreservesPreexisting() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = makePath(dir, QStringLiteral("src"));
    const QString dstDir = makePath(dir, QStringLiteral("dst"));

    Error err;
    QVERIFY(make_dir_parents(srcDir.toLocal8Bit().toStdString(), err));
    QVERIFY(make_dir_parents(dstDir.toLocal8Bit().toStdString(), err));

    const QString srcFile = srcDir + QLatin1String("/new.bin");
    QByteArray payload(512 * 1024, 'x');
    QVERIFY(write_file_atomic(srcFile.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    const QString keepPath = writeTempFile(dir, QStringLiteral("dst/keep.txt"), "keep");

    ProgressInfo progress;
    auto cancelCb = [](const ProgressInfo& info) { return info.bytesDone == 0; };

    QVERIFY(
        !copy_path(srcDir.toLocal8Bit().toStdString(), dstDir.toLocal8Bit().toStdString(), progress, cancelCb, err));
    QCOMPARE(err.code, ECANCELED);
    QCOMPARE(readQtFile(keepPath), QByteArray("keep"));
    QVERIFY(!QFileInfo(dstDir + QLatin1String("/new.bin")).exists());
}

void FsOpsTest::copyIntoExistingDestinationErrorPreservesPreexisting() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = makePath(dir, QStringLiteral("src.bin"));
    const QString dstDir = makePath(dir, QStringLiteral("dst"));

    Error err;
    QVERIFY(make_dir_parents(dstDir.toLocal8Bit().toStdString(), err));
    QVERIFY(write_file_atomic(srcPath.toLocal8Bit().toStdString(), reinterpret_cast<const std::uint8_t*>("source-data"),
                              std::size_t{11}, err));

    const QString keepPath = writeTempFile(dir, QStringLiteral("dst/keep.txt"), "keep");

    ProgressInfo progress;
    auto progressCb = [](const ProgressInfo&) { return true; };

    QVERIFY(
        !copy_path(srcPath.toLocal8Bit().toStdString(), dstDir.toLocal8Bit().toStdString(), progress, progressCb, err));
    QVERIFY(err.isSet());
    QVERIFY(err.code == EISDIR || err.code == EACCES);
    QCOMPARE(readQtFile(keepPath), QByteArray("keep"));
    QVERIFY(QFileInfo(dstDir).isDir());
}

void FsOpsTest::copyRollbackRemovesOnlyCreatedPaths() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = makePath(dir, QStringLiteral("src"));
    const QString srcNestedDir = srcDir + QLatin1String("/nested");
    const QString dstDir = makePath(dir, QStringLiteral("dst"));

    Error err;
    QVERIFY(make_dir_parents(srcNestedDir.toLocal8Bit().toStdString(), err));
    QVERIFY(make_dir_parents(dstDir.toLocal8Bit().toStdString(), err));

    QByteArray payload(512 * 1024, 'y');
    const QString srcNestedFile = srcNestedDir + QLatin1String("/child.bin");
    QVERIFY(write_file_atomic(srcNestedFile.toLocal8Bit().toStdString(),
                              reinterpret_cast<const std::uint8_t*>(payload.constData()),
                              static_cast<std::size_t>(payload.size()), err));

    const QString keepPath = writeTempFile(dir, QStringLiteral("dst/keep.txt"), "keep");

    ProgressInfo progress;
    auto cancelCb = [](const ProgressInfo& info) { return info.bytesDone == 0; };

    QVERIFY(
        !copy_path(srcDir.toLocal8Bit().toStdString(), dstDir.toLocal8Bit().toStdString(), progress, cancelCb, err));
    QCOMPARE(err.code, ECANCELED);
    QCOMPARE(readQtFile(keepPath), QByteArray("keep"));
    QVERIFY(!QFileInfo(dstDir + QLatin1String("/nested/child.bin")).exists());
    QVERIFY(!QFileInfo(dstDir + QLatin1String("/nested")).exists());
    QVERIFY(QFileInfo(dstDir).isDir());
}

void FsOpsTest::readMissingFileFails() {
    Error err;
    std::vector<std::uint8_t> out;
    QVERIFY(!read_file_all("/path/that/does/not/exist", out, err));
    QVERIFY(err.isSet());
}

void FsOpsTest::makeDirParentsFailsOnExistingFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString filePath = makePath(dir, QStringLiteral("already_file"));
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x", 1);
    f.close();

    Error err;
    QVERIFY(!make_dir_parents(filePath.toLocal8Bit().toStdString(), err));
    QVERIFY(err.isSet());
    QVERIFY(err.code == ENOTDIR || err.code == EEXIST);
}

void FsOpsTest::setPermissionsFailsOnMissing() {
    Error err;
    ProgressInfo progress;
    QString bogus = QStringLiteral("/path/that/does/not/exist/perms");
    QVERIFY(!set_permissions(bogus.toLocal8Bit().toStdString(), 0600, err));
    QVERIFY(err.isSet());
}

void FsOpsTest::copySymlinkPreservesLink() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = writeTempFile(dir, QStringLiteral("target.txt"), "data");
    const QString linkPath = dir.path() + QLatin1String("/link.txt");
    QVERIFY(::symlink(target.toLocal8Bit().constData(), linkPath.toLocal8Bit().constData()) == 0);

    const QString destLink = dir.path() + QLatin1String("/copied_link.txt");
    ProgressInfo progress;
    auto progressCb = [](const ProgressInfo&) { return true; };
    Error err;
    QVERIFY(copy_path(linkPath.toLocal8Bit().toStdString(), destLink.toLocal8Bit().toStdString(), progress, progressCb,
                      err));
    QVERIFY(!err.isSet());

    struct stat st{};
    QVERIFY(::lstat(destLink.toLocal8Bit().constData(), &st) == 0);
    QVERIFY(S_ISLNK(st.st_mode));

    char buf[PATH_MAX];
    const ssize_t len = ::readlink(destLink.toLocal8Bit().constData(), buf, sizeof buf - 1);
    QVERIFY(len > 0);
    buf[len] = '\0';
    QCOMPARE(QString::fromLocal8Bit(buf), target);
}

void FsOpsTest::copyPreservesMtime() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcPath = writeTempFile(dir, QStringLiteral("mtime.txt"), "mtime");
    struct stat stSrc{};
    QVERIFY(::stat(srcPath.toLocal8Bit().constData(), &stSrc) == 0);

    const QString dstPath = dir.path() + QLatin1String("/mtime_copy.txt");
    ProgressInfo progress;
    auto progressCb = [](const ProgressInfo&) { return true; };
    Error err;
    QVERIFY(
        copy_path(srcPath.toLocal8Bit().toStdString(), dstPath.toLocal8Bit().toStdString(), progress, progressCb, err));
    QVERIFY(!err.isSet());

    struct stat stDst{};
    QVERIFY(::stat(dstPath.toLocal8Bit().constData(), &stDst) == 0);
    QCOMPARE(stDst.st_mtim.tv_sec, stSrc.st_mtim.tv_sec);
}

QTEST_MAIN(FsOpsTest)
#include "fs_ops_test.moc"
