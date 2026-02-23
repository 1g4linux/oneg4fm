/*
 * Binary document wrapper around the windowed file reader with ELF autodetect
 * src/ui/binarydocument.h
 */

#ifndef PCMANFM_BINARYDOCUMENT_H
#define PCMANFM_BINARYDOCUMENT_H

#include <QtGlobal>
#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>
#include <string>

#include "disasm_engine.h"
#include "../core/windowed_file_reader.h"

namespace Oneg4FM {

class BinaryDocument {
   public:
    BinaryDocument() = default;
    ~BinaryDocument();

    BinaryDocument(const BinaryDocument&) = delete;
    BinaryDocument& operator=(const BinaryDocument&) = delete;

    bool open(const QString& path, QString& errorOut);
    bool isOpen() const { return reader_ != nullptr; }
    quint64 size() const { return static_cast<quint64>(fileSize_); }
    const QString& path() const { return path_; }

    CpuArch arch() const { return arch_; }
    bool littleEndian() const { return littleEndian_; }
    quint64 baseAddress() const { return baseAddress_; }

    // Reads up to length bytes from offset into out. Returns false on error.
    bool readSpan(quint64 offset, quint64 length, QByteArray& out, QString& errorOut) const;

   private:
    void detectElf(const QByteArray& header);

    QString path_;
    std::unique_ptr<WindowedFileReader> reader_;
    std::size_t fileSize_ = 0;
    CpuArch arch_ = CpuArch::X86_64;
    bool littleEndian_ = true;
    quint64 baseAddress_ = 0;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_BINARYDOCUMENT_H
