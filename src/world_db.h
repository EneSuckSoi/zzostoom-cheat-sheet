#pragma once
#include <cstdint>

// Event flags generated from WorldMapPieceParam / BonfireWarpParam (see tools/extract_items.py).
namespace zz::world {

struct Grace {
    uint32_t flag;
    const char* name;
    bool dlc;
};

extern const uint32_t kMapFlags[];  // map piece "revealed" + "acquired" flags
extern const int kMapFlagCount;
extern const Grace kGraces[];
extern const int kGraceCount;

}  // namespace zz::world
