#pragma once
#include <cstdint>

// Item list generated from the game's own params and text (see tools/extract_items.py).
namespace zz::db {

enum ItemKind : uint8_t {
    kKindWeapon = 0,     // id + upgrade level + affinity * 100
    kKindAmmo = 1,
    kKindProtector = 2,
    kKindAccessory = 3,
    kKindGoods = 4,
    kKindGem = 5,        // Ash of War
    kKindSpiritAsh = 6,  // id + upgrade level
};

struct SubCategory {
    int16_t top;
    const char* name;
};

struct ItemDef {
    uint32_t id;            // full item id including the type prefix (0x4... goods etc.)
    uint16_t sub;           // index into kSubCategories
    uint8_t kind;           // ItemKind
    uint8_t max_upgrade;    // 0 = not upgradable
    uint16_t affinities;    // bit k set => affinity k (k * 100 added to id) exists
    int16_t max_num;        // inventory stack limit from the param
    const char* name;
    const char* info;       // short in-game description, may be null
};

extern const char* const kTopCategories[];
extern const int kTopCategoryCount;
extern const SubCategory kSubCategories[];
extern const int kSubCategoryCount;
extern const ItemDef kItems[];
extern const int kItemCount;

}  // namespace zz::db
