/*
 * Tests for Linux filesystem safety wrappers
 * tests/linux_fs_safety_test.cpp
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "../src/core/linux_fs_safety.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using PCManFM::FsOps::Error;

namespace {

QString makePath(const QTemporaryDir& dir, const QString& name) {
    return dir.path() + QLatin1Char('/') + name;
}

PCManFM::LinuxFsSafety::Fd openRootDir(const QString& path) {
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    return PCManFM::LinuxFsSafety::Fd(fd);
}

}  // namespace

class LinuxFsSafetyTest : public QObject {
    Q_OBJECT

   private slots:
    void openUnderRejectsPathTraversal();
    void openDirPathAndStatx();
    void atomicReplaceRejectsSymlinkParentEscape();
    void renameRejectsSymlinkParentEscape();
    void unlinkAndRmdirAllowMissing();
};

void LinuxFsSafetyTest::openUnderRejectsPathTraversal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto rootFd = openRootDir(dir.path());
    QVERIFY(rootFd.valid());

    PCManFM::LinuxFsSafety::Fd outFd;
    Error err;
    QVERIFY(!PCManFM::LinuxFsSafety::open_under(rootFd.get(), QStringLiteral("../etc/passwd").toStdString(),
                                                O_RDONLY | O_CLOEXEC, 0, PCManFM::LinuxFsSafety::kResolveNoSymlinks,
                                                outFd, err));
    QCOMPARE(err.code, EINVAL);
}

void LinuxFsSafetyTest::openDirPathAndStatx() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto rootFd = openRootDir(dir.path());
    QVERIFY(rootFd.valid());

    Error err;
    PCManFM::LinuxFsSafety::Fd nestedDir;
    QVERIFY(PCManFM::LinuxFsSafety::open_dir_path_under(rootFd.get(), "a/b/c", true, 0777, nestedDir, err));
    QVERIFY(!err.isSet());

    const QByteArray payload("statx-data");
    QVERIFY(PCManFM::LinuxFsSafety::atomic_replace_under(rootFd.get(), "a/b/c/file.txt",
                                                         reinterpret_cast<const std::uint8_t*>(payload.constData()),
                                                         static_cast<std::size_t>(payload.size()), 0600, err));
    QVERIFY(!err.isSet());

    struct statx stx{};
    QVERIFY(PCManFM::LinuxFsSafety::statx_under(rootFd.get(), "a/b/c/file.txt", AT_SYMLINK_NOFOLLOW,
                                                STATX_TYPE | STATX_SIZE, stx, err));
    QVERIFY(!err.isSet());
    QVERIFY((stx.stx_mode & S_IFMT) == S_IFREG);
    QCOMPARE(static_cast<qint64>(stx.stx_size), static_cast<qint64>(payload.size()));
}

void LinuxFsSafetyTest::atomicReplaceRejectsSymlinkParentEscape() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString outsideDir = makePath(dir, QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(outsideDir));
    const QString linkDir = makePath(dir, QStringLiteral("linkdir"));
    QVERIFY(::symlink(outsideDir.toLocal8Bit().constData(), linkDir.toLocal8Bit().constData()) == 0);

    auto rootFd = openRootDir(dir.path());
    QVERIFY(rootFd.valid());

    Error err;
    const QByteArray payload("escape");
    QVERIFY(!PCManFM::LinuxFsSafety::atomic_replace_under(rootFd.get(), "linkdir/escape.txt",
                                                          reinterpret_cast<const std::uint8_t*>(payload.constData()),
                                                          static_cast<std::size_t>(payload.size()), 0600, err));
    QVERIFY(err.code == ELOOP || err.code == ENOTDIR || err.code == EINVAL);
    QVERIFY(!QFileInfo::exists(outsideDir + QLatin1String("/escape.txt")));
}

void LinuxFsSafetyTest::renameRejectsSymlinkParentEscape() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto rootFd = openRootDir(dir.path());
    QVERIFY(rootFd.valid());

    Error err;
    const QByteArray payload("move-data");
    QVERIFY(PCManFM::LinuxFsSafety::atomic_replace_under(rootFd.get(), "src.txt",
                                                         reinterpret_cast<const std::uint8_t*>(payload.constData()),
                                                         static_cast<std::size_t>(payload.size()), 0600, err));
    QVERIFY(!err.isSet());

    const QString outsideDir = makePath(dir, QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(outsideDir));
    const QString linkDir = makePath(dir, QStringLiteral("linkdir"));
    QVERIFY(::symlink(outsideDir.toLocal8Bit().constData(), linkDir.toLocal8Bit().constData()) == 0);

    QVERIFY(!PCManFM::LinuxFsSafety::rename_under(rootFd.get(), "src.txt", rootFd.get(), "linkdir/dst.txt", 0, err));
    QVERIFY(err.code == ELOOP || err.code == ENOTDIR || err.code == EINVAL);
    QVERIFY(QFileInfo::exists(makePath(dir, QStringLiteral("src.txt"))));
    QVERIFY(!QFileInfo::exists(outsideDir + QLatin1String("/dst.txt")));
}

void LinuxFsSafetyTest::unlinkAndRmdirAllowMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto rootFd = openRootDir(dir.path());
    QVERIFY(rootFd.valid());

    Error err;
    PCManFM::LinuxFsSafety::Fd nestedDir;
    QVERIFY(PCManFM::LinuxFsSafety::open_dir_path_under(rootFd.get(), "tmp", true, 0777, nestedDir, err));
    QVERIFY(!err.isSet());

    QVERIFY(PCManFM::LinuxFsSafety::unlink_under(rootFd.get(), "tmp/missing.txt", err, true));
    QVERIFY(!err.isSet());
    QVERIFY(PCManFM::LinuxFsSafety::rmdir_under(rootFd.get(), "tmp/missing-dir", err, true));
    QVERIFY(!err.isSet());
}

QTEST_MAIN(LinuxFsSafetyTest)
#include "linux_fs_safety_test.moc"
