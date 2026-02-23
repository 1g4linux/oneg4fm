/*
 * Delegate to apply color scheme based on semantic roles
 * src/ui/color_delegate.cpp
 */

#include "color_delegate.h"

#include <QPainter>
#include <QStyleOptionViewItem>

#include "color_roles.h"

namespace Oneg4FM {

ColorDelegate::ColorDelegate(ColorManager* colors, QObject* parent) : QStyledItemDelegate(parent), colors_(colors) {}

void ColorDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (!colors_) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const auto& scheme = colors_->scheme();
    QColor fg = opt.palette.color(QPalette::Text);
    QColor bg = opt.palette.color(QPalette::Base);

    const CellCategory cat = static_cast<CellCategory>(index.data(RoleCategory).toInt());
    const bool selected = opt.state.testFlag(QStyle::State_Selected);
    const bool patched = index.data(RolePatched).toBool();
    const bool bookmark = index.data(RoleBookmark).toBool();
    const bool searchHit = index.data(RoleSearchHit).toBool();

    switch (cat) {
        case CellCategory::InstructionAddress:
            fg = selected ? scheme.address : scheme.address.lighter(125);
            break;
        case CellCategory::InstructionBytes:
            fg = scheme.bytes;
            break;
        case CellCategory::InstructionMnemonic:
            fg = scheme.mnemonic;
            break;
        case CellCategory::InstructionOperands:
            fg = scheme.operands;
            break;
        case CellCategory::Branch:
            fg = scheme.branch;
            break;
        case CellCategory::Call:
            fg = scheme.call;
            break;
        case CellCategory::ReturnIns:
            fg = scheme.ret;
            break;
        case CellCategory::Nop:
            fg = scheme.nop;
            break;
        default:
            break;
    }

    if (!opt.state.testFlag(QStyle::State_Selected)) {
        if (bookmark) {
            bg = scheme.bookmarkBg;
        }
        if (searchHit) {
            bg = scheme.searchHitBg;
        }
        if (patched) {
            bg = scheme.patchedBg;
        }
        opt.palette.setColor(QPalette::Text, fg);
        opt.palette.setColor(QPalette::WindowText, fg);
        opt.palette.setColor(QPalette::Base, bg);
    }

    QStyledItemDelegate::paint(painter, opt, index);
}

}  // namespace Oneg4FM
