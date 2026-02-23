/*
 * Capstone-based disassembly viewer
 * src/ui/disassemblywindow.h
 */

#ifndef PCMANFM_DISASSEMBLYWINDOW_H
#define PCMANFM_DISASSEMBLYWINDOW_H

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>

class QLabel;
class QTableView;

namespace Oneg4FM {

class BinaryDocument;
class ColorManager;
class ColorDelegate;
class DisasmModel;

class DisassemblyWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit DisassemblyWindow(QWidget* parent = nullptr);
    ~DisassemblyWindow() override = default;

    bool openFile(const QString& path, QString& errorOut);

   private:
    void setupUi();
    bool refresh(quint64 offset = 0);
    void updateLabels(const QString& path, bool truncated, qsizetype bytesRead);

    std::unique_ptr<BinaryDocument> doc_;
    std::unique_ptr<DisasmModel> model_;
    std::unique_ptr<ColorManager> colors_;
    ColorDelegate* delegate_ = nullptr;
    QTableView* view_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QString currentPath_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_DISASSEMBLYWINDOW_H
