/*
 * Shared roles and categories for colorized views
 * src/ui/color_roles.h
 */

#ifndef PCMANFM_COLOR_ROLES_H
#define PCMANFM_COLOR_ROLES_H

#include <Qt>

namespace Oneg4FM {

enum CustomRoles {
    RoleCategory = Qt::UserRole + 1,
    RoleAddress = Qt::UserRole + 2,
    RoleOffset = Qt::UserRole + 3,
    RolePatched = Qt::UserRole + 4,
    RoleBookmark = Qt::UserRole + 5,
    RoleSearchHit = Qt::UserRole + 6
};

enum class CellCategory {
    Normal,
    HexByte,
    AsciiChar,
    InstructionAddress,
    InstructionBytes,
    InstructionMnemonic,
    InstructionOperands,
    Branch,
    Call,
    ReturnIns,
    Nop,
    Comment,
    Label,
    CurrentPc
};

}  // namespace Oneg4FM

#endif  // PCMANFM_COLOR_ROLES_H
