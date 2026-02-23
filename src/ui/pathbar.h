/*
 * Qt-native Path Bar widget
 * src/ui/pathbar.h
 */

#ifndef PCMANFM_UI_PATHBAR_H
#define PCMANFM_UI_PATHBAR_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QUrl>

namespace Panel {
class FilePath;
}

namespace Oneg4FM {

class PathBar : public QWidget {
    Q_OBJECT
   public:
    explicit PathBar(QWidget* parent = nullptr);
    ~PathBar() override;

    void setPath(const QUrl& path);
    QUrl path() const;

    void setText(const QString& text);
    QString text() const;

    void selectAll();
    void openEditor();

   Q_SIGNALS:
    void pathChanged(const QUrl& path);
    void chdir(const Panel::FilePath& dirPath);
    void middleClickChdir(const Panel::FilePath& dirPath);
    void editingFinished();
    void returnPressed();

   private Q_SLOTS:
    void onReturnPressed();

   private:
    QLineEdit* pathEdit_;
    QUrl currentPath_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_UI_PATHBAR_H
