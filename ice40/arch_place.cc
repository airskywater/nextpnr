/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "arch_place.h"
#include "cells.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static const NetInfo *get_net_or_empty(const CellInfo *cell,
                                       const IdString port)
{
    auto found = cell->ports.find(port);
    if (found != cell->ports.end())
        return found->second.net;
    else
        return nullptr;
};

static bool logicCellsCompatible(const std::vector<const CellInfo *> &cells)
{
    bool dffs_exist = false, dffs_neg = false;
    const NetInfo *cen = nullptr, *clk = nullptr, *sr = nullptr;
    static std::unordered_set<IdString> locals;
    locals.clear();

    for (auto cell : cells) {
        if (bool_or_default(cell->params, "DFF_ENABLE")) {
            if (!dffs_exist) {
                dffs_exist = true;
                cen = get_net_or_empty(cell, "CEN");
                clk = get_net_or_empty(cell, "CLK");
                sr = get_net_or_empty(cell, "SR");

                if (!is_global_net(cen) && cen != nullptr)
                    locals.insert(cen->name);
                if (!is_global_net(clk) && clk != nullptr)
                    locals.insert(clk->name);
                if (!is_global_net(sr) && sr != nullptr)
                    locals.insert(sr->name);

                if (bool_or_default(cell->params, "NEG_CLK")) {
                    dffs_neg = true;
                }
            } else {
                if (cen != get_net_or_empty(cell, "CEN"))
                    return false;
                if (clk != get_net_or_empty(cell, "CLK"))
                    return false;
                if (sr != get_net_or_empty(cell, "SR"))
                    return false;
                if (dffs_neg != bool_or_default(cell->params, "NEG_CLK"))
                    return false;
            }
        }

        const NetInfo *i0 = get_net_or_empty(cell, "I0"),
                      *i1 = get_net_or_empty(cell, "I1"),
                      *i2 = get_net_or_empty(cell, "I2"),
                      *i3 = get_net_or_empty(cell, "I3");
        if (i0 != nullptr)
            locals.insert(i0->name);
        if (i1 != nullptr)
            locals.insert(i1->name);
        if (i2 != nullptr)
            locals.insert(i2->name);
        if (i3 != nullptr)
            locals.insert(i3->name);
    }

    return locals.size() <= 32;
}

bool isBelLocationValid(Context *ctx, BelId bel)
{
    if (ctx->getBelType(bel) == TYPE_ICESTORM_LC) {
        std::vector<const CellInfo *> cells;
        for (auto bel_other : ctx->getBelsAtSameTile(bel)) {
            IdString cell_other = ctx->getBelCell(bel_other, false);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = ctx->cells[cell_other];
                cells.push_back(ci_other);
            }
        }
        return logicCellsCompatible(cells);
    } else {
        IdString cellId = ctx->getBelCell(bel, false);
        if (cellId == IdString())
            return true;
        else
            return isValidBelForCell(ctx, ctx->cells.at(cellId), bel);
    }
}

bool isValidBelForCell(Context *ctx, CellInfo *cell, BelId bel)
{
    if (cell->type == "ICESTORM_LC") {
        assert(ctx->getBelType(bel) == TYPE_ICESTORM_LC);

        std::vector<const CellInfo *> cells;

        for (auto bel_other : ctx->getBelsAtSameTile(bel)) {
            IdString cell_other = ctx->getBelCell(bel_other, false);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = ctx->cells[cell_other];
                cells.push_back(ci_other);
            }
        }

        cells.push_back(cell);
        return logicCellsCompatible(cells);
    } else if (cell->type == "SB_IO") {
        return ctx->getBelPackagePin(bel) != "";
    } else if (cell->type == "SB_GB") {
        bool is_reset = false, is_cen = false;
        assert(cell->ports.at("GLOBAL_BUFFER_OUTPUT").net != nullptr);
        for (auto user : cell->ports.at("GLOBAL_BUFFER_OUTPUT").net->users) {
            if (is_reset_port(user))
                is_reset = true;
            if (is_enable_port(user))
                is_cen = true;
        }
        IdString glb_net = ctx->getWireName(
                ctx->getWireBelPin(bel, PIN_GLOBAL_BUFFER_OUTPUT));
        int glb_id = std::stoi(std::string("") + glb_net.str().back());
        if (is_reset && is_cen)
            return false;
        else if (is_reset)
            return (glb_id % 2) == 0;
        else if (is_cen)
            return (glb_id % 2) == 1;
        else
            return true;
    } else {
        // TODO: IO cell clock checks
        return true;
    }
}

NEXTPNR_NAMESPACE_END