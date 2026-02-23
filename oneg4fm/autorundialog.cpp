/*
 * Auto-run dialog implementation for oneg4fm
 * oneg4fm/autorundialog.cpp
 */

#include "autorundialog.h"

// LibFM-Qt Headers
#include "panel/panel.h"

// Qt Headers
#include <QIcon>
#include <QListWidgetItem>

// Local Headers
#include "application.h"
#include "mainwindow.h"

namespace Oneg4FM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

AutoRunDialog::AutoRunDialog(GVolume* volume, GMount* mount, QWidget* parent, Qt::WindowFlags f)
    : QDialog(parent, f),
      cancellable(g_cancellable_new()),
      applications(nullptr),
      mount_(mount ? G_MOUNT(g_object_ref(mount)) : nullptr) {
    setAttribute(Qt::WA_DeleteOnClose);
    ui.setupUi(this);

    // show the volume icon for the medium if available
    if (volume) {
        GIcon* gicon = g_volume_get_icon(volume);
        if (gicon) {
            QIcon icon = Panel::IconInfo::fromGIcon(gicon)->qicon();
            ui.icon->setPixmap(icon.pixmap(QSize(48, 48)));
            g_object_unref(gicon);
        }
    }

    // add generic file-manager action as the fallback choice
    auto* item =
        new QListWidgetItem(QIcon::fromTheme(QStringLiteral("system-file-manager")), tr("Open in file manager"));
    ui.listWidget->addItem(item);

    // query content types asynchronously to populate application-specific actions
    if (mount_) {
        g_mount_guess_content_type(mount_, TRUE, cancellable,
                                   reinterpret_cast<GAsyncReadyCallback>(onContentTypeFinished), this);
    }

    connect(ui.listWidget, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
}

AutoRunDialog::~AutoRunDialog() {
    // free the application list and unref each GAppInfo
    if (applications) {
        g_list_free_full(applications, g_object_unref);
        applications = nullptr;
    }

    if (mount_) {
        g_object_unref(mount_);
        mount_ = nullptr;
    }

    if (cancellable) {
        // cancel any outstanding content-type query and drop our reference
        g_cancellable_cancel(cancellable);
        g_object_unref(cancellable);
        cancellable = nullptr;
    }
}

void AutoRunDialog::accept() {
    // ensure there is always a current item if the list is non-empty
    if (!ui.listWidget->currentItem() && ui.listWidget->count() > 0) {
        ui.listWidget->setCurrentRow(0);
    }

    QList<QListWidgetItem*> selected = ui.listWidget->selectedItems();
    if (selected.isEmpty()) {
        QDialog::accept();
        return;
    }

    auto* item = selected.first();
    if (!item || !mount_) {
        QDialog::accept();
        return;
    }

    GFile* gf = g_mount_get_root(mount_);
    if (!gf) {
        QDialog::accept();
        return;
    }

    void* p = item->data(Qt::UserRole).value<void*>();
    if (p) {
        launchSelectedApp(static_cast<GAppInfo*>(p), gf);
    }
    else {
        openInFileManager(gf);
    }

    g_object_unref(gf);
    QDialog::accept();
}

void AutoRunDialog::launchSelectedApp(GAppInfo* app, GFile* mountRoot) {
    if (!app || !mountRoot)
        return;

    GList* filelist = g_list_prepend(nullptr, mountRoot);
    GError* error = nullptr;

    if (!g_app_info_launch(app, filelist, nullptr, &error)) {
        if (error) {
            qWarning() << "Failed to launch app:" << error->message;
            g_error_free(error);
        }
    }
    g_list_free(filelist);
}

void AutoRunDialog::openInFileManager(GFile* mountRoot) {
    if (!mountRoot)
        return;

    Settings& settings = appSettings();

    // Construct FilePath from GFile, taking ownership (ref/unref logic handled by FilePath)
    Panel::FilePath path{mountRoot, true};

    // open the path in a new main window using the configured initial geometry
    auto* win = new MainWindow(path);
    win->resize(settings.windowWidth(), settings.windowHeight());
    if (settings.windowX() != -1 && settings.windowY() != -1) {
        win->move(settings.windowX(), settings.windowY());
    }

    win->show();
}

// static
void AutoRunDialog::onContentTypeFinished(GMount* mount, GAsyncResult* res, AutoRunDialog* pThis) {
    Q_UNUSED(mount);

    if (!pThis)
        return;

    // once the async query is complete we no longer need the cancellable
    if (pThis->cancellable) {
        g_object_unref(pThis->cancellable);
        pThis->cancellable = nullptr;
    }

    char** types = g_mount_guess_content_type_finish(mount, res, nullptr);
    char* desc = nullptr;

    if (types) {
        if (types[0]) {
            // collect all applications that can handle any of the detected content types
            for (char** type = types; *type; ++type) {
                GList* l = g_app_info_get_all_for_type(*type);
                if (l) {
                    pThis->applications = g_list_concat(pThis->applications, l);
                }
            }
            // use the first content type for the description label
            desc = g_content_type_get_description(types[0]);
        }
        g_strfreev(types);

        // populate the list widget with per-application actions
        if (pThis->applications) {
            int pos = 0;
            // Iterate safely through GList
            for (GList* l = pThis->applications; l; l = l->next) {
                GAppInfo* app = G_APP_INFO(l->data);

                // Avoid duplicates or invalid apps
                if (!app)
                    continue;

                GIcon* gicon = g_app_info_get_icon(app);
                QIcon icon;
                if (gicon) {
                    icon = Panel::IconInfo::fromGIcon(gicon)->qicon();
                }
                else {
                    icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
                }

                QString text = QString::fromUtf8(g_app_info_get_name(app));

                auto* item = new QListWidgetItem(icon, text);
                // store the raw GAppInfo pointer in user data
                // the lifetime is managed by the applications GList and freed in the destructor
                item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(app)));

                // Insert at the beginning (before the file manager default option, or strictly as found)
                // Using 'pos' to insert before the default item if desired, or simply append.
                // Here we insert before existing items to prioritize apps?
                // Original code used 'pos' which was incremented. It inserts at index 0, 1, 2...
                // pushing the "Open in file manager" (added in ctor) down.
                pThis->ui.listWidget->insertItem(pos, item);
                pos++;
            }
        }
    }

    if (desc) {
        pThis->ui.mediumType->setText(QString::fromUtf8(desc));
        g_free(desc);
    }
    else {
        pThis->ui.mediumType->setText(tr("Removable Disk"));
    }

    // select the first item so there is always a default choice
    if (pThis->ui.listWidget->count() > 0) {
        pThis->ui.listWidget->setCurrentRow(0);
    }
}

}  // namespace Oneg4FM
