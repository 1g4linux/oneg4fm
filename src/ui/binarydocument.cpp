/*
 * Binary document wrapper around the windowed file reader with ELF autodetect
 * src/ui/binarydocument.cpp
 */

#include "binarydocument.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QtEndian>

#include <cstring>

#include "../core/windowed_file_reader.h"

namespace Oneg4FM {

namespace {
constexpr qsizetype kProbeSize = 256;

template <typename T>
T readEndian(const unsigned char* data, bool little) {
    T val = 0;
    std::memcpy(&val, data, sizeof(T));
    return little ? qFromLittleEndian(val) : qFromBigEndian(val);
}
}  // namespace

BinaryDocument::~BinaryDocument() = default;

bool BinaryDocument::open(const QString& path, QString& errorOut) {
    errorOut.clear();
    path_ = path;
    fileSize_ = 0;
    baseAddress_ = 0;
    arch_ = CpuArch::X86_64;
    littleEndian_ = true;
    reader_.reset();

    const QByteArray encoded = QFile::encodeName(path);
    std::string err;
    auto reader = std::make_unique<WindowedFileReader>(encoded.constData(), 0, &err);
    if (!reader->valid()) {
        errorOut = QString::fromStdString(err);
        return false;
    }

    fileSize_ = reader->size();
    reader_ = std::move(reader);

    QByteArray header;
    QString readErr;
    if (readSpan(0, kProbeSize, header, readErr)) {
        detectElf(header);
    }
    return true;
}

bool BinaryDocument::readSpan(quint64 offset, quint64 length, QByteArray& out, QString& errorOut) const {
    errorOut.clear();
    out.clear();
    if (!reader_) {
        errorOut = QObject::tr("Document not open.");
        return false;
    }
    if (offset >= fileSize_ || length == 0) {
        return true;
    }

    const std::size_t cappedLength =
        static_cast<std::size_t>(std::min<quint64>(length, static_cast<quint64>(fileSize_) - offset));

    out.resize(static_cast<int>(cappedLength));
    std::size_t bytesRead = 0;
    std::string err;
    if (!reader_->read(offset, cappedLength, reinterpret_cast<std::uint8_t*>(out.data()), bytesRead, err)) {
        errorOut = QString::fromStdString(err);
        out.clear();
        return false;
    }
    if (static_cast<qsizetype>(bytesRead) < out.size()) {
        out.resize(static_cast<int>(bytesRead));
    }
    return true;
}

void BinaryDocument::detectElf(const QByteArray& header) {
    if (header.size() < 20) {
        return;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(header.constData());
    if (!(bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F')) {
        return;
    }

    const quint8 elfClass = bytes[4];
    const quint8 elfData = bytes[5];
    littleEndian_ = (elfData == 1);
    const bool is64 = elfClass == 2;

    // e_machine at offset 18 (16-bit)
    const quint16 machine = readEndian<quint16>(bytes + 18, littleEndian_);

    // e_entry at offset 24
    if (is64 && header.size() >= 32) {
        baseAddress_ = readEndian<quint64>(bytes + 24, littleEndian_);
    }
    else if (!is64 && header.size() >= 28) {
        baseAddress_ = readEndian<quint32>(bytes + 24, littleEndian_);
    }

    switch (machine) {
        case 0x3E:  // EM_X86_64
            arch_ = CpuArch::X86_64;
            break;
        case 0x03:  // EM_386
            arch_ = CpuArch::X86_32;
            break;
        case 0xB7:  // EM_AARCH64
            arch_ = CpuArch::ARM64;
            break;
        case 0x28:  // EM_ARM
            arch_ = CpuArch::ARM;
            break;
        case 0x08:  // EM_MIPS
            arch_ = is64 ? CpuArch::MIPS64 : CpuArch::MIPS32;
            break;
        case 0x15:  // EM_PPC64
            arch_ = CpuArch::PPC64;
            break;
        case 0x14:  // EM_PPC
            arch_ = CpuArch::PPC32;
            break;
        case 0xF3:  // EM_RISCV
            arch_ = is64 ? CpuArch::RISCV64 : CpuArch::RISCV32;
            break;
        default:
            arch_ = CpuArch::Unknown;
            break;
    }
}

}  // namespace Oneg4FM
