/*
 * XDG directory utilities header
 * oneg4fm/xdgdir.h
 */

#include <QObject>
#include <QString>

class XdgDir : public QObject {
    Q_OBJECT

   public:
    static QString readDesktopDir();

    static void setDesktopDir(QString path);

   private:
    static QString readUserDirsFile();
};
