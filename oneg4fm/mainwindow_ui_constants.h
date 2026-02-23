/*
 * Shared UI keys/constants used across main window feature modules.
 */

#ifndef ONEG4FM_UI_CONSTANTS_H
#define ONEG4FM_UI_CONSTANTS_H

#include <QString>

namespace Oneg4FM::UiConstants {

inline const QString kTabMimeType = QStringLiteral("application/oneg4fm-tab");
inline constexpr const char* kTabDroppedProperty = "_oneg4fm_tab_dropped";
inline constexpr const char* kBookmarkActionProperty = "oneg4fm_bookmark";

}  // namespace Oneg4FM::UiConstants

#endif  // ONEG4FM_UI_CONSTANTS_H
