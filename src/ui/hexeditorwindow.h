/*
 * Hex editor window
 * src/ui/hexeditorwindow.h
 */

#ifndef PCMANFM_HEXEDITORWINDOW_H
#define PCMANFM_HEXEDITORWINDOW_H

#include <QMainWindow>
#include <QPointer>

#include <memory>

#include "hexdocument.h"
#include "hexeditorview.h"
#include "color_manager.h"

class QTimer;
class QLabel;
class QDockWidget;

namespace Oneg4FM {

class HexEditorWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit HexEditorWindow(QWidget* parent = nullptr);
    ~HexEditorWindow() override;

    bool openFile(const QString& path, QString& errorOut);

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    void setupUi();
    void updateWindowTitle();
    void updateStatus(std::uint64_t offset, int byteValue, bool hasSelection);
    void updateActionStates(bool hasSelection);
    bool promptToSave();
    void doSave(bool saveAs);
    void performFind(bool forward);
    void performReplace(bool replaceAll);
    void jumpToModified(bool forward);
    void checkExternalChanges();
    QByteArray parseSearchInput(const QString& input, bool& okOut) const;
    QByteArray promptForPattern(const QString& title, bool& okOut);
    QByteArray promptForReplace(bool& okOut);
    void goToOffset();
    void updateInspector(std::uint64_t offset);
    void computeChecksums(bool selectionOnly);
    void computeByteStats(bool selectionOnly);
    bool iterateRange(std::uint64_t start,
                      std::uint64_t length,
                      const std::function<bool(const QByteArray&)>& consumer,
                      QString& errorOut);
    void addBookmark();
    void listBookmarks();
    void jumpBookmark(bool forward);
    void diffWithFile();
    void nextDiff(bool forward);
    void sideBySideDiff();

    std::unique_ptr<HexDocument> doc_;
    std::unique_ptr<ColorManager> colors_;
    QPointer<HexEditorView> view_;
    QTimer* changeTimer_ = nullptr;
    bool suppressExternalPrompt_ = false;
    quint64 lastExternalFingerprint_ = 0;

    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* copyHexAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* findAction_ = nullptr;
    QAction* findNextAction_ = nullptr;
    QAction* findPrevAction_ = nullptr;
    QAction* findAllAction_ = nullptr;
    QAction* replaceAction_ = nullptr;
    QAction* replaceAllAction_ = nullptr;
    QAction* gotoAction_ = nullptr;
    QAction* insertToggleAction_ = nullptr;
    QAction* nextModifiedAction_ = nullptr;
    QAction* prevModifiedAction_ = nullptr;
    QAction* checksumAction_ = nullptr;
    QAction* statsAction_ = nullptr;
    QAction* bookmarkAddAction_ = nullptr;
    QAction* bookmarkPrevAction_ = nullptr;
    QAction* bookmarkNextAction_ = nullptr;
    QAction* bookmarkListAction_ = nullptr;
    QAction* diffAction_ = nullptr;
    QAction* nextDiffAction_ = nullptr;
    QAction* prevDiffAction_ = nullptr;
    QAction* diffSideBySideAction_ = nullptr;

    QByteArray lastSearch_;
    QByteArray lastReplace_;

    QLabel* modeLabel_ = nullptr;
    QLabel* modifiedLabel_ = nullptr;
    QDockWidget* inspectorDock_ = nullptr;
    QLabel* inspectorOffset_ = nullptr;
    QLabel* inspectorU8_ = nullptr;
    QLabel* inspectorI8_ = nullptr;
    QLabel* inspectorU16LE_ = nullptr;
    QLabel* inspectorU16BE_ = nullptr;
    QLabel* inspectorU32LE_ = nullptr;
    QLabel* inspectorU32BE_ = nullptr;
    QLabel* inspectorU64LE_ = nullptr;
    QLabel* inspectorU64BE_ = nullptr;
    QLabel* inspectorFloat_ = nullptr;
    QLabel* inspectorDouble_ = nullptr;
    QLabel* inspectorUtf8_ = nullptr;

    struct Bookmark {
        std::uint64_t offset = 0;
        QString label;
    };
    std::vector<Bookmark> bookmarks_;
    std::vector<std::uint64_t> diffOffsets_;
    int currentDiffIndex_ = -1;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_HEXEDITORWINDOW_H
