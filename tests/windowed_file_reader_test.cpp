/*
 * Tests for windowed file reader
 * tests/windowed_file_reader_test.cpp
 */

#include <QTest>
#include <QTemporaryDir>
#include <QFile>

#include "../src/core/windowed_file_reader.h"

using namespace Oneg4FM;

namespace {

QString writeTestFile(QTemporaryDir& dir, int size) {
    const QString path = dir.path() + QLatin1String("/window.bin");
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i) {
        data[i] = static_cast<char>(i & 0xFF);
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    return path;
}

QByteArray slice(const QByteArray& data, int offset, int length) {
    return data.mid(offset, length);
}

}  // namespace

class WindowedFileReaderTest : public QObject {
    Q_OBJECT

   private slots:
    void readsAcrossBoundaries();
    void shortReadAtEnd();
};

void WindowedFileReaderTest::readsAcrossBoundaries() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const int fileSize = 8192 + 64;
    const QString path = writeTestFile(dir, fileSize);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray full = f.readAll();

    std::string err;
    WindowedFileReader reader(path.toLocal8Bit().constData(), 0, &err);
    QVERIFY2(reader.valid(), err.c_str());

    std::vector<std::uint8_t> buffer(64);
    std::size_t bytesRead = 0;
    std::string readErr;

    QVERIFY(reader.read(0, buffer.size(), buffer.data(), bytesRead, readErr));
    QCOMPARE(static_cast<int>(bytesRead), 64);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(buffer.data()), 64), slice(full, 0, 64));

    QVERIFY(reader.read(4000, buffer.size(), buffer.data(), bytesRead, readErr));
    QCOMPARE(static_cast<int>(bytesRead), 64);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(buffer.data()), 64), slice(full, 4000, 64));
}

void WindowedFileReaderTest::shortReadAtEnd() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const int fileSize = 300;
    const QString path = writeTestFile(dir, fileSize);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray full = f.readAll();

    std::string err;
    WindowedFileReader reader(path.toLocal8Bit().constData(), 0, &err);
    QVERIFY2(reader.valid(), err.c_str());

    std::vector<std::uint8_t> buffer(64);
    std::size_t bytesRead = 0;
    std::string readErr;

    const std::uint64_t offset = static_cast<std::uint64_t>(fileSize - 20);
    QVERIFY(reader.read(offset, buffer.size(), buffer.data(), bytesRead, readErr));
    QCOMPARE(static_cast<int>(bytesRead), 20);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(buffer.data()), 20), slice(full, fileSize - 20, 20));
}

QTEST_MAIN(WindowedFileReaderTest)
#include "windowed_file_reader_test.moc"
