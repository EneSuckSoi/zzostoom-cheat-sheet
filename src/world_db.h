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
struct Enemy {
    uint32_t npc_param;    // NpcParam row
    uint32_t think_param;  // NpcThinkParam row (AI)
    int32_t chr_id;        // model cXXXX
    bool boss;
    int32_t name_fmg;      // NpcName id for the boss health bar, -1 if none
    const char* name;      // English, from Paramdex
};
extern const Enemy kEnemies[];  // spawnable enemies, sorted by name
extern const int kEnemyCount;

extern const uint32_t kEnemyNpcParams[];  // sorted NpcParam ids whose param team is an enemy team
extern const int kEnemyNpcParamCount;

}  // namespace zz::world
