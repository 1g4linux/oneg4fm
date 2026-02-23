/*
 * Tests for archive extraction helpers
 * tests/archive_extract_test.cpp
 */

#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <archive.h>
#include <archive_entry.h>

#include "../src/core/archive_extract.h"
#include "../src/core/archive_writer.h"
#include "../src/core/fs_ops.h"

#include <vector>
#include <string>
#include <cerrno>

using PCManFM::ArchiveExtract::Options;
using PCManFM::FsOps::Error;
using PCManFM::FsOps::ProgressCallback;
using PCManFM::FsOps::ProgressInfo;

namespace {

struct FormatCase {
    QString format;
    QString filter;
    QString extension;
};

enum class EntryKind {
    Regular,
    Symlink,
    Hardlink,
    Directory,
};

struct ArchiveEntrySpec {
    EntryKind kind;
    QString path;
    QByteArray data;
    QString linkTarget;
    int permissions = 0644;
};

bool write_archive_entries(const QString& path,
                           const std::vector<ArchiveEntrySpec>& entries,
                           const QString& format,
                           const QString& filter,
                           QString* errorOut) {
    struct archive* ar = archive_write_new();
    if (!ar) {
        *errorOut = QStringLiteral("archive_write_new failed");
        return false;
    }

    if (!filter.isEmpty()) {
        if (archive_write_add_filter_by_name(ar, filter.toUtf8().constData()) != ARCHIVE_OK) {
            *errorOut = QStringLiteral("add_filter failed: %1").arg(QString::fromUtf8(archive_error_string(ar)));
            archive_write_free(ar);
            return false;
        }
    }
    else {
        archive_write_add_filter_none(ar);
    }

    if (archive_write_set_format_by_name(ar, format.toUtf8().constData()) != ARCHIVE_OK) {
        *errorOut = QStringLiteral("set_format failed: %1").arg(QString::fromUtf8(archive_error_string(ar)));
        archive_write_free(ar);
        return false;
    }

    if (archive_write_open_filename(ar, path.toUtf8().constData()) != ARCHIVE_OK) {
        *errorOut = QStringLiteral("open_filename failed: %1").arg(QString::fromUtf8(archive_error_string(ar)));
        archive_write_free(ar);
        return false;
    }

    for (const auto& spec : entries) {
        archive_entry* entry = archive_entry_new();
        if (!entry) {
            *errorOut = QStringLiteral("archive_entry_new failed");
            archive_write_free(ar);
            return false;
        }

        archive_entry_set_pathname(entry, spec.path.toUtf8().constData());
        archive_entry_set_perm(entry, spec.permissions);

        switch (spec.kind) {
            case EntryKind::Regular:
                archive_entry_set_filetype(entry, AE_IFREG);
                archive_entry_set_size(entry, spec.data.size());
                break;
            case EntryKind::Symlink:
                archive_entry_set_filetype(entry, AE_IFLNK);
                archive_entry_set_size(entry, 0);
                archive_entry_set_symlink(entry, spec.linkTarget.toUtf8().constData());
                break;
            case EntryKind::Hardlink:
                archive_entry_set_filetype(entry, AE_IFREG);
                archive_entry_set_size(entry, 0);
                archive_entry_set_hardlink(entry, spec.linkTarget.toUtf8().constData());
                break;
            case EntryKind::Directory:
                archive_entry_set_filetype(entry, AE_IFDIR);
                archive_entry_set_size(entry, 0);
                break;
        }

        if (archive_write_header(ar, entry) != ARCHIVE_OK) {
            *errorOut = QStringLiteral("write_header failed: %1").arg(QString::fromUtf8(archive_error_string(ar)));
            archive_entry_free(entry);
            archive_write_free(ar);
            return false;
        }

        if (spec.kind == EntryKind::Regular && !spec.data.isEmpty()) {
            if (archive_write_data(ar, spec.data.constData(), static_cast<size_t>(spec.data.size())) < 0) {
                *errorOut = QStringLiteral("write_data failed: %1").arg(QString::fromUtf8(archive_error_string(ar)));
                archive_entry_free(entry);
                archive_write_free(ar);
                return false;
            }
        }

        archive_entry_free(entry);
    }

    archive_write_close(ar);
    archive_write_free(ar);
    return true;
}

bool write_archive_file(const QString& path,
                        const QString& entryPath,
                        const QByteArray& data,
                        const QString& format,
                        const QString& filter,
                        QString* errorOut) {
    const std::vector<ArchiveEntrySpec> entries = {
        {EntryKind::Regular, entryPath, data, QString(), 0644},
    };
    return write_archive_entries(path, entries, format, filter, errorOut);
}

bool write_archive_with_symlink(const QString& path,
                                const QString& fileEntry,
                                const QByteArray& fileData,
                                const QString& symlinkEntry,
                                const QString& symlinkTarget,
                                const QString& format,
                                const QString& filter,
                                QString* errorOut) {
    const std::vector<ArchiveEntrySpec> entries = {
        {EntryKind::Regular, fileEntry, fileData, QString(), 0644},
        {EntryKind::Symlink, symlinkEntry, QByteArray(), symlinkTarget, 0777},
    };
    return write_archive_entries(path, entries, format, filter, errorOut);
}

bool truncate_archive_tail(const QString& path, qint64 bytesToRemove, QString* errorOut) {
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) {
        *errorOut = QStringLiteral("open failed: %1").arg(f.errorString());
        return false;
    }
    const qint64 size = f.size();
    if (bytesToRemove <= 0 || size <= bytesToRemove) {
        *errorOut = QStringLiteral("invalid truncation request: size=%1 remove=%2").arg(size).arg(bytesToRemove);
        return false;
    }
    if (!f.resize(size - bytesToRemove)) {
        *errorOut = QStringLiteral("resize failed: %1").arg(f.errorString());
        return false;
    }
    return true;
}

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

bool extract_with_archive_extract(const QString& archivePath,
                                  const QString& destDir,
                                  ProgressInfo& progress,
                                  Error& err) {
    auto cb = [](const ProgressInfo&) { return true; };
    Options opts;
    return PCManFM::ArchiveExtract::extract_archive(archivePath.toLocal8Bit().toStdString(),
                                                    destDir.toLocal8Bit().toStdString(), progress, cb, err, opts);
}

bool extract_with_archive_writer(const QString& archivePath,
                                 const QString& destDir,
                                 ProgressInfo& progress,
                                 Error& err) {
    auto cb = [](const ProgressInfo&) { return true; };
    return PCManFM::ArchiveWriter::extract_tar_zst(archivePath.toLocal8Bit().toStdString(),
                                                   destDir.toLocal8Bit().toStdString(), progress, cb, err);
}

}  // namespace

class ArchiveExtractTest : public QObject {
    Q_OBJECT

   private slots:
    void extractKnownFormats_data();
    void extractKnownFormats();
    void extractPreservesSymlinkInTar();
    void cancelStopsAndCleansUp();
    void rejectsUnsafePaths();
    void rejectsAbsolutePaths();
    void rejectsSymlinkComponentEscape();
    void rejectsParentReplacementWithSymlink();
    void rejectsUnsafeHardlinkTarget();
    void tarExtractorRejectsSymlinkComponentEscape();
    void truncatedTarFailsWithReadError();
    void tarExtractorTruncatedTarFails();
};

void ArchiveExtractTest::extractKnownFormats_data() {
    QTest::addColumn<QString>("format");
    QTest::addColumn<QString>("filter");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<QString>("entryPath");

    struct CaseExt {
        QString format;
        QString filter;
        QString extension;
        QString entryPath;
    };

    const std::vector<CaseExt> cases = {
        {QStringLiteral("gnutar"), QStringLiteral(""), QStringLiteral(".tar"), QStringLiteral("folder/hello.txt")},
        {QStringLiteral("gnutar"), QStringLiteral("gzip"), QStringLiteral(".tar.gz"),
         QStringLiteral("folder/hello.txt")},
        {QStringLiteral("gnutar"), QStringLiteral("bzip2"), QStringLiteral(".tar.bz2"),
         QStringLiteral("folder/hello.txt")},
        {QStringLiteral("gnutar"), QStringLiteral("xz"), QStringLiteral(".tar.xz"), QStringLiteral("folder/hello.txt")},
        {QStringLiteral("gnutar"), QStringLiteral("zstd"), QStringLiteral(".tar.zst"),
         QStringLiteral("folder/hello.txt")},
        {QStringLiteral("zip"), QStringLiteral(""), QStringLiteral(".zip"), QStringLiteral("folder/hello.txt")},
        {QStringLiteral("cpio"), QStringLiteral(""), QStringLiteral(".cpio"), QStringLiteral("folder/hello.txt")},
        {QStringLiteral("arbsd"), QStringLiteral(""), QStringLiteral(".ar"), QStringLiteral("hello.txt")},
    };

    for (const auto& c : cases) {
        QTest::addRow("%s%s", c.format.toUtf8().constData(), c.filter.isEmpty() ? "" : c.filter.toUtf8().constData())
            << c.format << c.filter << c.extension << c.entryPath;
    }
}

void ArchiveExtractTest::extractKnownFormats() {
    QFETCH(QString, format);
    QFETCH(QString, filter);
    QFETCH(QString, extension);
    QFETCH(QString, entryPath);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/sample") + extension;
    const QByteArray payload("hello-archive");
    QString error;
    QVERIFY2(write_archive_file(archivePath, entryPath, payload, format, filter, &error), qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-") + format + extension;

    ProgressInfo progress;
    Error err;
    bool sawProgress = false;
    ProgressCallback cb = [&sawProgress](const ProgressInfo& info) {
        sawProgress = sawProgress || info.bytesDone > 0 || info.filesDone > 0;
        return true;
    };

    Options opts;
    opts.enableFilterThreads = true;
    opts.maxFilterThreads = 0;

    const bool ok = PCManFM::ArchiveExtract::extract_archive(
        archivePath.toLocal8Bit().toStdString(), destDir.toLocal8Bit().toStdString(), progress, cb, err, opts);
    QVERIFY2(ok, err.message.c_str());
    QVERIFY2(sawProgress, "Progress callback not invoked");

    const QString extracted = destDir + QLatin1Char('/') + entryPath;
    QFileInfo fi(extracted);
    QVERIFY(fi.exists());
    QCOMPARE(readFile(extracted), QString::fromUtf8(payload));
}

void ArchiveExtractTest::extractPreservesSymlinkInTar() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/symlink.tar.zst");
    const QString fileEntry = QStringLiteral("data/file.txt");
    const QString linkEntry = QStringLiteral("data/link");
    const QByteArray payload("payload");
    QString error;
    QVERIFY2(write_archive_with_symlink(archivePath, fileEntry, payload, linkEntry, QStringLiteral("file.txt"),
                                        QStringLiteral("gnutar"), QStringLiteral("zstd"), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-symlink");
    ProgressInfo progress;
    Error err;
    auto cb = [](const ProgressInfo&) { return true; };
    Options opts;
    QVERIFY(PCManFM::ArchiveExtract::extract_archive(archivePath.toLocal8Bit().toStdString(),
                                                     destDir.toLocal8Bit().toStdString(), progress, cb, err, opts));

    const QString linkPath = destDir + QLatin1Char('/') + linkEntry;
    QFileInfo linkInfo(linkPath);
    QVERIFY(linkInfo.isSymLink());
    QCOMPARE(QFileInfo(linkPath).symLinkTarget(), destDir + QLatin1String("/data/file.txt"));
}

void ArchiveExtractTest::cancelStopsAndCleansUp() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/cancel.tar.zst");
    const QString entryPath = QStringLiteral("folder/hello.txt");
    QByteArray payload;
    payload.fill('x', 256 * 1024);
    QString error;
    QVERIFY2(
        write_archive_file(archivePath, entryPath, payload, QStringLiteral("gnutar"), QStringLiteral("zstd"), &error),
        qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-cancel");
    ProgressInfo progress;
    Error err;
    int callbacks = 0;
    ProgressCallback cb = [&callbacks](const ProgressInfo&) {
        callbacks++;
        return false;  // cancel immediately
    };

    Options opts;
    const bool ok = PCManFM::ArchiveExtract::extract_archive(
        archivePath.toLocal8Bit().toStdString(), destDir.toLocal8Bit().toStdString(), progress, cb, err, opts);
    QVERIFY(!ok);
    QVERIFY(err.code == ECANCELED || !err.message.empty());
    QVERIFY(!QFileInfo::exists(destDir));
    QVERIFY(callbacks > 0);
}

void ArchiveExtractTest::rejectsUnsafePaths() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/unsafe.tar");
    QString error;
    QVERIFY2(write_archive_file(archivePath, QStringLiteral("../evil.txt"), QByteArray("bad"), QStringLiteral("gnutar"),
                                QStringLiteral(""), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-unsafe");
    ProgressInfo progress;
    Error err;
    auto cb = [](const ProgressInfo&) { return true; };
    Options opts;

    const bool ok = PCManFM::ArchiveExtract::extract_archive(
        archivePath.toLocal8Bit().toStdString(), destDir.toLocal8Bit().toStdString(), progress, cb, err, opts);
    QVERIFY(!ok);
    QVERIFY(err.code == EINVAL || !err.message.empty());
    QVERIFY(!QFileInfo::exists(destDir));
}

void ArchiveExtractTest::rejectsAbsolutePaths() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/absolute.tar");
    QString error;
    QVERIFY2(write_archive_file(archivePath, QStringLiteral("/etc/passwd"), QByteArray("bad"), QStringLiteral("gnutar"),
                                QStringLiteral(""), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-absolute");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_extract(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(err.code == EINVAL || !err.message.empty());
    QVERIFY(!QFileInfo::exists(destDir));
}

void ArchiveExtractTest::rejectsSymlinkComponentEscape() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString outsideDir = dir.path() + QLatin1String("/outside-root");
    QVERIFY(QDir().mkpath(outsideDir));

    const QString archivePath = dir.path() + QLatin1String("/symlink-escape.tar");
    const std::vector<ArchiveEntrySpec> entries = {
        {EntryKind::Symlink, QStringLiteral("dirlink"), QByteArray(), outsideDir, 0777},
        {EntryKind::Regular, QStringLiteral("dirlink/escape.txt"), QByteArray("escape"), QString(), 0644},
    };
    QString error;
    QVERIFY2(write_archive_entries(archivePath, entries, QStringLiteral("gnutar"), QStringLiteral(""), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-symlink-escape");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_extract(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(!QFileInfo::exists(outsideDir + QLatin1String("/escape.txt")));
}

void ArchiveExtractTest::rejectsParentReplacementWithSymlink() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString outsideDir = dir.path() + QLatin1String("/outside-parent");
    QVERIFY(QDir().mkpath(outsideDir));

    const QString archivePath = dir.path() + QLatin1String("/replace-parent.tar");
    const std::vector<ArchiveEntrySpec> entries = {
        {EntryKind::Regular, QStringLiteral("a/b.txt"), QByteArray("first"), QString(), 0644},
        {EntryKind::Symlink, QStringLiteral("a"), QByteArray(), outsideDir, 0777},
    };
    QString error;
    QVERIFY2(write_archive_entries(archivePath, entries, QStringLiteral("gnutar"), QStringLiteral(""), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-replace-parent");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_extract(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(!QFileInfo::exists(outsideDir + QLatin1String("/b.txt")));
}

void ArchiveExtractTest::rejectsUnsafeHardlinkTarget() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/hardlink-unsafe.tar");
    const std::vector<ArchiveEntrySpec> entries = {
        {EntryKind::Hardlink, QStringLiteral("links/unsafe"), QByteArray(), QStringLiteral("/etc/passwd"), 0644},
    };
    QString error;
    QVERIFY2(write_archive_entries(archivePath, entries, QStringLiteral("gnutar"), QStringLiteral(""), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-hardlink-unsafe");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_extract(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(err.code == EINVAL || !err.message.empty());
}

void ArchiveExtractTest::tarExtractorRejectsSymlinkComponentEscape() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString outsideDir = dir.path() + QLatin1String("/outside-tar");
    QVERIFY(QDir().mkpath(outsideDir));

    const QString archivePath = dir.path() + QLatin1String("/tar-symlink-escape.tar");
    const std::vector<ArchiveEntrySpec> entries = {
        {EntryKind::Symlink, QStringLiteral("dirlink"), QByteArray(), outsideDir, 0777},
        {EntryKind::Regular, QStringLiteral("dirlink/escape.txt"), QByteArray("escape"), QString(), 0644},
    };
    QString error;
    QVERIFY2(write_archive_entries(archivePath, entries, QStringLiteral("gnutar"), QStringLiteral(""), &error),
             qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-tar-symlink-escape");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_writer(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(!QFileInfo::exists(outsideDir + QLatin1String("/escape.txt")));
}

void ArchiveExtractTest::truncatedTarFailsWithReadError() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/truncated.tar");
    QByteArray payload;
    payload.fill('a', 3000);
    QString error;
    QVERIFY2(write_archive_file(archivePath, QStringLiteral("big.bin"), payload, QStringLiteral("gnutar"),
                                QStringLiteral(""), &error),
             qPrintable(error));
    QVERIFY2(truncate_archive_tail(archivePath, 1500, &error), qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-truncated");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_extract(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(QString::fromStdString(err.message).contains(QStringLiteral("archive_read_data_skip")));
    QVERIFY(!QFileInfo::exists(destDir));
}

void ArchiveExtractTest::tarExtractorTruncatedTarFails() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.path() + QLatin1String("/truncated-tar-extractor.tar");
    QByteArray payload;
    payload.fill('b', 3000);
    QString error;
    QVERIFY2(write_archive_file(archivePath, QStringLiteral("big.bin"), payload, QStringLiteral("gnutar"),
                                QStringLiteral(""), &error),
             qPrintable(error));
    QVERIFY2(truncate_archive_tail(archivePath, 1500, &error), qPrintable(error));

    const QString destDir = dir.path() + QLatin1String("/out-truncated-tar-extractor");
    ProgressInfo progress;
    Error err;
    const bool ok = extract_with_archive_writer(archivePath, destDir, progress, err);
    QVERIFY(!ok);
    QVERIFY(QString::fromStdString(err.message).contains(QStringLiteral("archive_read_data")));
    QVERIFY(!QFileInfo::exists(destDir));
}

QTEST_MAIN(ArchiveExtractTest)
#include "archive_extract_test.moc"
