/*
 * Auto-run dialog header
 * oneg4fm/autorundialog.h
 */

#ifndef PCMANFM_AUTORUNDIALOG_H
#define PCMANFM_AUTORUNDIALOG_H

#include <gio/gio.h>

#include <QDialog>

#include "ui_autorun.h"

namespace Oneg4FM {

class AutoRunDialog : public QDialog {
    Q_OBJECT

   public:
    explicit AutoRunDialog(GVolume* volume,
                           GMount* mount,
                           QWidget* parent = nullptr,
                           Qt::WindowFlags f = Qt::WindowFlags());
    virtual ~AutoRunDialog();

    virtual void accept();

   private Q_SLOTS:

   private:
    void launchSelectedApp(GAppInfo* app, GFile* mountRoot);
    void openInFileManager(GFile* mountRoot);

    static void onContentTypeFinished(GMount* mount, GAsyncResult* res, AutoRunDialog* pThis);

   private:
    Ui::AutoRunDialog ui;
    GCancellable* cancellable;
    GList* applications;
    GMount* mount_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_AUTORUNDIALOG_H
