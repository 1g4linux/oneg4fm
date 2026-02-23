/*
 * Qt table model for Capstone disassembly output
 * src/ui/disasmmodel.h
 */

#ifndef PCMANFM_DISASMMODEL_H
#define PCMANFM_DISASMMODEL_H

#include <QAbstractTableModel>

#include <vector>

#include "color_roles.h"
#include "binarydocument.h"
#include "disasm_engine.h"

namespace Oneg4FM {

class DisasmModel : public QAbstractTableModel {
    Q_OBJECT

   public:
    explicit DisasmModel(QObject* parent = nullptr);

    enum Column { Address = 0, Bytes, Mnemonic, Operands, ColumnCount };

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    bool disassemble(const BinaryDocument& doc, quint64 offset, quint64 length, QString& errorOut);
    void clear();

   private:
    DisasmEngine engine_;
    std::vector<DisasmInstr> instructions_;
    quint64 baseAddress_ = 0;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_DISASMMODEL_H
