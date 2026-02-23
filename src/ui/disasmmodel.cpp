/*
 * Qt table model for Capstone disassembly output
 * src/ui/disasmmodel.cpp
 */

#include "disasmmodel.h"

#include <QColor>
#include <QString>
#include <QVariant>

#include "color_roles.h"

namespace Oneg4FM {

namespace {
QString formatBytes(const std::vector<std::uint8_t>& bytes) {
    QString out;
    out.reserve(static_cast<int>(bytes.size()) * 3);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out += QStringLiteral("%1 ").arg(bytes[i], 2, 16, QLatin1Char('0'));
    }
    if (!out.isEmpty()) {
        out.chop(1);
    }
    return out;
}
}  // namespace

DisasmModel::DisasmModel(QObject* parent) : QAbstractTableModel(parent) {}

int DisasmModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(instructions_.size());
}

int DisasmModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant DisasmModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const DisasmInstr& ins = instructions_[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Address:
                return QStringLiteral("0x%1").arg(ins.address, 0, 16);
            case Bytes:
                return formatBytes(ins.bytes);
            case Mnemonic:
                return QString::fromLatin1(ins.mnemonic.c_str());
            case Operands:
                return QString::fromLatin1(ins.opStr.c_str());
            default:
                break;
        }
    }
    else if (role == RoleCategory) {
        switch (index.column()) {
            case Address:
                return static_cast<int>(CellCategory::InstructionAddress);
            case Bytes:
                return static_cast<int>(CellCategory::InstructionBytes);
            case Mnemonic: {
                switch (ins.kind) {
                    case DisasmInstr::Kind::Branch:
                        return static_cast<int>(CellCategory::Branch);
                    case DisasmInstr::Kind::Call:
                        return static_cast<int>(CellCategory::Call);
                    case DisasmInstr::Kind::ReturnIns:
                        return static_cast<int>(CellCategory::ReturnIns);
                    case DisasmInstr::Kind::Nop:
                        return static_cast<int>(CellCategory::Nop);
                    case DisasmInstr::Kind::Normal:
                        return static_cast<int>(CellCategory::InstructionMnemonic);
                }
                break;
            }
            case Operands:
                return static_cast<int>(CellCategory::InstructionOperands);
            default:
                break;
        }
        return static_cast<int>(CellCategory::Normal);
    }
    else if (role == RoleAddress) {
        return static_cast<qulonglong>(ins.address);
    }
    else if (role == RolePatched || role == RoleBookmark || role == RoleSearchHit) {
        return false;
    }
    return {};
}

QVariant DisasmModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case Address:
                return tr("Address");
            case Bytes:
                return tr("Bytes");
            case Mnemonic:
                return tr("Mnemonic");
            case Operands:
                return tr("Operands");
            default:
                break;
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

bool DisasmModel::disassemble(const BinaryDocument& doc, quint64 offset, quint64 length, QString& errorOut) {
    errorOut.clear();
    QByteArray buffer;
    if (!doc.readSpan(offset, length, buffer, errorOut)) {
        return false;
    }

    if (!engine_.configure(doc.arch(), doc.littleEndian())) {
        errorOut = tr("Failed to configure Capstone engine.");
        return false;
    }

    std::vector<DisasmInstr> out;
    std::string err;
    if (!engine_.disassemble(reinterpret_cast<const std::uint8_t*>(buffer.constData()),
                             static_cast<std::size_t>(buffer.size()), doc.baseAddress() + offset, out, err)) {
        errorOut = QString::fromStdString(err.empty() ? "Capstone error" : err);
        return false;
    }

    beginResetModel();
    baseAddress_ = doc.baseAddress();
    instructions_ = std::move(out);
    endResetModel();
    return true;
}

void DisasmModel::clear() {
    beginResetModel();
    instructions_.clear();
    endResetModel();
}

}  // namespace Oneg4FM
