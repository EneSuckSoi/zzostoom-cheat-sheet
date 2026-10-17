#include "game.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "log.h"
#include "memory.h"

namespace zz::game {

namespace {

Addresses g_addr;
bool g_ready = false;
std::string g_version;
std::string g_report;

std::string ReadExeVersion() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) return "?";
    std::vector<uint8_t> buf(size);
    if (!GetFileVersionInfoW(path, 0, size, buf.data())) return "?";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info)
        return "?";
    char out[64];
    std::snprintf(out, sizeof(out), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
                  LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
                  LOWORD(info->dwFileVersionLS));
    return out;
}

struct Target {
    const char* name;
    uintptr_t* slot;
    std::vector<std::pair<const char*, int>> patterns;  // pattern, bytes back to target
    int disp_offset;  // -1 = direct pattern (the match itself, minus the pattern's offset)
    int insn_len;
    uintptr_t known_rva;  // for exe 2.7.1.0 (Elden Ring 1.17.1, worldwide)
    bool critical;
};

}  // namespace

bool Init() {
    const auto module = mem::MainModule();
    g_version = ReadExeVersion();
    const bool known_version = g_version == "2.7.1.0";
    ZZ_LOG("eldenring.exe version %s, base %p, size 0x%zx", g_version.c_str(),
           reinterpret_cast<void*>(module.base), module.size);

    // Each pattern carries how many bytes its match lies after the address we want (direct
    // patterns) or is ignored for RIP-relative ones.
    std::vector<Target> targets = {
        {"WorldChrMan", &g_addr.world_chr_man,
         {{"48 8B 05 ?? ?? ?? ?? 48 85 C0 74 0F 48 39 88 ?? ?? ?? ?? 75 06 89 B1 5C 03 00 00 0F 28 "
           "05 ?? ?? ?? ?? 4C 8D 45 E7", 0},
          {"48 8B 05 ?? ?? ?? ?? 48 85 C0 74 0F 48 39 88", 0}},
         3, 7, 0x3d69ff8, true},
        {"GameDataMan", &g_addr.game_data_man,
         {{"48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3", 0}}, 3, 7, 0x3d61f98, true},
        {"MapItemMan", &g_addr.map_item_man,
         {{"48 8B 0D ?? ?? ?? ?? C7 44 24 50 FF FF FF FF C7 45 A0 FF FF FF FF 48 85 C9 75 2E", 0}},
         3, 7, 0x3d6bac0, false},
        {"CSMenuManImp", &g_addr.cs_menu_man_imp,
         {{"48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8 ?? ?? ?? ??", 0}},
         3, 7, 0x3d6f820, false},
        // cmp byte ptr [rip+disp], 0 (7 bytes) starting one byte after the match.
        {"ChrDbgFlags", &g_addr.dbg_flags,
         {{"?? 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 32 C0 48", 0}}, 3, 8, 0x3d6a210, false},
        // Adds a list of up to 10 items straight to the inventory ("ItemGib").
        {"ItemGive", &g_addr.fn_item_give,
         {{"40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 70 FF FF FF 48 81 EC 90 01 00 00 48 C7 "
           "45 C8 FE FF FF FF 48 89 9C 24 D8", 0},
          {"40 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 ?? ?? ?? ?? 48 81 ec ?? ?? ?? ?? 48 c7 "
           "45 ?? ?? ?? ?? ?? 48 89 9c 24 ?? ?? ?? ?? 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 85 ?? ?? "
           "?? ?? 44 89 4c 24", 0},
          // mov [rsp+x],r9d ... mov eax,[rdx]; cmp eax,0Ah (list size check), 0x3C into the function
          {"44 89 4C 24 ?? 4D 8B F8 4C 89 44 24 ?? 4C 8B E9 33 FF 89 7C 24 ?? 8B 02 83 F8 0A 0F 87",
           0x3C}},
         -1, 0, 0x561400, false},
        {"EventFlagMan", &g_addr.event_flag_man,
         {{"48 8B 3D ?? ?? ?? ?? 48 85 FF 74 ?? 48 8B 49", 0}}, 3, 7, 0x3d6c4b8, false},
        {"GetEventFlag", &g_addr.fn_get_event_flag,
         {{"44 8B 41 1C 44 8B DA 33 D2 41 8B C3 41 F7 F0 4C 8B D1 45 33 C9 44 0F AF C0 45 2B D8 "
           "4C 8B 41 38", 0}},
         -1, 0, 0x5fa250, false},
    };

    bool critical_ok = true;
    g_report.clear();
    for (auto& t : targets) {
        uintptr_t found = 0;
        for (auto [pattern, back] : t.patterns) {
            uintptr_t hit = mem::FindPattern(module, pattern);
            if (!hit) continue;
            found = t.disp_offset < 0 ? hit - back
                                      : mem::ResolveRip(hit, t.disp_offset, t.insn_len);
            break;
        }
        uintptr_t known = known_version ? module.base + t.known_rva : 0;
        const char* source = "pattern";
        if (known && found != known) {
            // On the verified version the known address wins over a mismatching pattern hit.
            if (found)
                ZZ_LOG("  %s: pattern hit RVA 0x%llx differs from verified 0x%llx, using verified",
                       t.name, static_cast<unsigned long long>(found - module.base),
                       static_cast<unsigned long long>(t.known_rva));
            found = known;
            source = "verified RVA";
        }
        *t.slot = found;
        char line[160];
        if (found)
            std::snprintf(line, sizeof(line), "%s: RVA 0x%llx (%s)\n", t.name,
                          static_cast<unsigned long long>(found - module.base), source);
        else
            std::snprintf(line, sizeof(line), "%s: NOT FOUND\n", t.name);
        g_report += line;
        line[std::strlen(line) - 1] = '\0';  // the log adds its own newline
        ZZ_LOG("  %s", line);
        if (!found && t.critical) critical_ok = false;
    }
    g_ready = critical_ok;
    return g_ready;
}

bool Ready() { return g_ready; }
const Addresses& Addr() { return g_addr; }
const std::string& GameVersion() { return g_version; }
const std::string& InitReport() { return g_report; }

uintptr_t WorldChrMan() { return g_ready ? mem::ReadPtr(g_addr.world_chr_man) : 0; }

uintptr_t PlayerIns() {
    uintptr_t wcm = WorldChrMan();
    return wcm ? mem::ReadPtr(wcm + off::kWcmPlayerIns) : 0;
}

uintptr_t ChrModule(uintptr_t chr_ins, ptrdiff_t module_offset) {
    if (!chr_ins) return 0;
    uintptr_t modules = mem::ReadPtr(chr_ins + off::kModules);
    return modules ? mem::ReadPtr(modules + module_offset) : 0;
}

uintptr_t PlayerModule(ptrdiff_t module_offset) { return ChrModule(PlayerIns(), module_offset); }

uintptr_t PlayerGameData() {
    if (!g_ready) return 0;
    uintptr_t gdm = mem::ReadPtr(g_addr.game_data_man);
    return gdm ? mem::ReadPtr(gdm + off::kGdmPlayerGameData) : 0;
}

bool InGame() {
    uintptr_t data = PlayerModule(off::kModData);
    if (!data) return false;
    auto max_hp = mem::Read<int32_t>(data + off::kDataMaxHp);
    return max_hp && *max_hp > 0 && *max_hp < 100000;
}

static bool IsTorrent(uintptr_t chr) {
    if (!chr) return false;
    auto character_id = mem::Read<int32_t>(chr + off::kCharacterId);
    auto npc_id = mem::Read<int32_t>(chr + off::kNpcId);
    // Torrent is model c8000; the character id seen in game is 8002 (8000-8099 accepted).
    auto torrent_id = [](std::optional<int32_t> id) { return id && *id >= 8000 && *id < 8100; };
    bool id_ok = torrent_id(character_id) || torrent_id(npc_id);
    return id_ok && ChrModule(chr, off::kModBehavior) != 0;
}

static bool ChunkPos(uintptr_t chr, float out[3]) {
    uintptr_t physics = ChrModule(chr, off::kModPhysics);
    return physics && mem::ReadRaw(physics + off::kPhysicsPosition, out, sizeof(float) * 3);
}

static float DistanceSq(uintptr_t a, uintptr_t b) {
    float pa[3], pb[3];
    if (!ChunkPos(a, pa) || !ChunkPos(b, pb)) return 1e30f;
    const float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
    return dx * dx + dy * dy + dz * dz;
}

uintptr_t TorrentIns() {
    uintptr_t player = PlayerIns();
    uintptr_t wcm = WorldChrMan();
    if (!player || !wcm) return 0;
    // Torrent lives in the summon buddy ChrSet (with spirit ashes). In co-op every player's horse is
    // there, so take the closest one.
    uintptr_t best = 0;
    float best_dist = 1e30f;
    const uintptr_t set = wcm + off::kWcmSummonBuddyChrSet;
    auto capacity = mem::Read<uint32_t>(set + off::kChrSetCapacity);
    uintptr_t entries = mem::ReadPtr(set + off::kChrSetEntries);
    if (capacity && entries && *capacity < 0x400) {
        for (uint32_t i = 0; i < *capacity; ++i) {
            uintptr_t chr = mem::ReadPtr(entries + i * 0x10);
            if (!IsTorrent(chr)) continue;
            const float d = DistanceSq(player, chr);
            if (d < best_dist) {
                best_dist = d;
                best = chr;
            }
        }
    }
    if (best) return best;
    // Fallback: what the player's ride module mounted this session.
    uintptr_t ride = ChrModule(player, off::kModRide);
    uintptr_t mounted = ride ? mem::ReadPtr(ride + off::kRideLastMounted) : 0;
    return IsTorrent(mounted) ? mounted : 0;
}

bool GetDbgFlag(DbgFlag flag) {
    if (!g_addr.dbg_flags) return false;
    return mem::Read<uint8_t>(g_addr.dbg_flags + flag).value_or(0) != 0;
}

void SetDbgFlag(DbgFlag flag, bool on) {
    if (!g_addr.dbg_flags) return;
    mem::Write<uint8_t>(g_addr.dbg_flags + flag, on ? 1 : 0);
}

namespace {
struct ItemEntry {
    uint32_t item_id;
    uint32_t quantity;
    int32_t durability;  // -1 = default
    int32_t gem;         // Ash of War installed on a weapon, -1 = none
};
// Layout read by the game function: a count (max 10) followed by the entries.
struct ItemList {
    uint32_t count;
    ItemEntry entries[10];
};
static_assert(sizeof(ItemList) == 4 + 10 * 16);
using ItemGiveFn = void (*)(void* map_item_man, ItemList* list, uint32_t* out, uint32_t unk);
}  // namespace

bool GiveItem(uint32_t full_id, uint32_t quantity) {
    if (!g_addr.fn_item_give || !g_addr.map_item_man || !InGame()) return false;
    uintptr_t map_item_man = mem::ReadPtr(g_addr.map_item_man);
    if (!map_item_man) return false;
    ItemList list{};
    for (auto& e : list.entries) e = {0xFFFFFFFFu, 0, -1, -1};
    list.count = 1;
    list.entries[0] = {full_id, std::max<uint32_t>(1, quantity), -1, -1};
    uint32_t out = 0;
    reinterpret_cast<ItemGiveFn>(g_addr.fn_item_give)(reinterpret_cast<void*>(map_item_man), &list,
                                                       &out, 0);
    ZZ_LOG("GiveItem 0x%08X x%u", full_id, list.entries[0].quantity);
    return true;
}

void SetGameCursorVisible(bool visible) {
    if (!g_addr.cs_menu_man_imp) return;
    uintptr_t menu = mem::ReadPtr(g_addr.cs_menu_man_imp);
    if (menu) mem::SetBits(menu + off::kMenuCursorShow, 0x1, visible);
}

static std::string WideToUtf8(const wchar_t* w, size_t max_len) {
    size_t len = 0;
    while (len < max_len && w[len]) ++len;
    if (!len) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), out.data(), n, nullptr, nullptr);
    return out;
}

bool ReadCharacter(CharacterInfo& out) {
    uintptr_t pgd = PlayerGameData();
    if (!pgd || !InGame()) return false;
    wchar_t name[17] = {};
    if (!mem::ReadRaw(pgd + off::kPgdName, name, sizeof(name))) return false;
    out.name = WideToUtf8(name, 16);
    out.level = mem::Read<uint32_t>(pgd + off::kPgdLevel).value_or(0);
    out.runes = mem::Read<uint32_t>(pgd + off::kPgdRunes).value_or(0);
    mem::ReadRaw(pgd + off::kPgdAttributes, out.attributes, sizeof(out.attributes));
    out.scadutree = mem::Read<uint8_t>(pgd + off::kPgdScadutree).value_or(0);
    out.revered_ash = mem::Read<uint8_t>(pgd + off::kPgdReveredAsh).value_or(0);
    if (uintptr_t data = PlayerModule(off::kModData)) {
        out.hp = mem::Read<int32_t>(data + off::kDataHp).value_or(0);
        out.max_hp = mem::Read<int32_t>(data + off::kDataMaxHp).value_or(0);
        out.fp = mem::Read<int32_t>(data + off::kDataFp).value_or(0);
        out.max_fp = mem::Read<int32_t>(data + off::kDataMaxFp).value_or(0);
        out.stamina = mem::Read<int32_t>(data + off::kDataStamina).value_or(0);
        out.max_stamina = mem::Read<int32_t>(data + off::kDataMaxStamina).value_or(0);
    }
    return true;
}

bool AddRunes(int64_t amount) {
    uintptr_t pgd = PlayerGameData();
    if (!pgd || !InGame()) return false;
    auto runes = mem::Read<uint32_t>(pgd + off::kPgdRunes);
    if (!runes) return false;
    int64_t next = std::clamp<int64_t>(static_cast<int64_t>(*runes) + amount, 0, 999999999);
    return mem::Write<uint32_t>(pgd + off::kPgdRunes, static_cast<uint32_t>(next));
}

bool WriteAttributes(const uint32_t attributes[8]) {
    uintptr_t pgd = PlayerGameData();
    if (!pgd || !InGame()) return false;
    uint32_t sum = 0;
    uint32_t clamped[8];
    for (int i = 0; i < 8; ++i) {
        clamped[i] = std::clamp<uint32_t>(attributes[i], 1, 99);
        sum += clamped[i];
    }
    // Every starting class sums to (level + 79).
    uint32_t level = sum > 79 ? sum - 79 : 1;
    return mem::WriteRaw(pgd + off::kPgdAttributes, clamped, sizeof(clamped)) &&
           mem::Write<uint32_t>(pgd + off::kPgdLevel, level);
}

bool WriteBlessings(uint8_t scadutree, uint8_t revered_ash) {
    uintptr_t pgd = PlayerGameData();
    if (!pgd || !InGame()) return false;
    return mem::Write<uint8_t>(pgd + off::kPgdScadutree, std::min<uint8_t>(scadutree, 20)) &&
           mem::Write<uint8_t>(pgd + off::kPgdReveredAsh, std::min<uint8_t>(revered_ash, 10));
}

bool ReadChunkPosition(float out[3]) {
    uintptr_t physics = PlayerModule(off::kModPhysics);
    return physics && mem::ReadRaw(physics + off::kPhysicsPosition, out, sizeof(float) * 3);
}

bool WriteChunkPosition(const float pos[3]) {
    uintptr_t physics = PlayerModule(off::kModPhysics);
    return physics && mem::WriteRaw(physics + off::kPhysicsPosition, pos, sizeof(float) * 3);
}

bool OnGround() {
    uintptr_t physics = PlayerModule(off::kModPhysics);
    if (!physics) return true;
    return mem::Read<uint8_t>(physics + off::kPhysicsStandingOnGround).value_or(1) != 0 ||
           mem::Read<uint8_t>(physics + off::kPhysicsTouchingGround).value_or(1) != 0;
}

// Torrent's physics module while the player is riding, else 0.
static uintptr_t RiddenTorrentPhysics() {
    // The rider sits on the horse: their positions are within a metre of each other.
    uintptr_t player = PlayerIns();
    uintptr_t torrent = TorrentIns();
    if (!torrent || DistanceSq(player, torrent) > 2.5f * 2.5f) return 0;
    return ChrModule(torrent, off::kModPhysics);
}

std::string DescribeTorrentSearch() {
    uintptr_t wcm = WorldChrMan();
    if (!wcm) return "no WorldChrMan";
    const uintptr_t set = wcm + off::kWcmSummonBuddyChrSet;
    auto capacity = mem::Read<uint32_t>(set + off::kChrSetCapacity).value_or(0);
    uintptr_t entries = mem::ReadPtr(set + off::kChrSetEntries);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "summon set capacity %u, ids:", capacity);
    std::string out = buf;
    for (uint32_t i = 0; entries && i < capacity && i < 0x400; ++i) {
        uintptr_t chr = mem::ReadPtr(entries + i * 0x10);
        if (!chr) continue;
        std::snprintf(buf, sizeof(buf), " %d/%d(d=%.1f)", mem::Read<int32_t>(chr + off::kCharacterId).value_or(-1),
                      mem::Read<int32_t>(chr + off::kNpcId).value_or(-1),
                      std::sqrt(DistanceSq(PlayerIns(), chr)));
        out += buf;
    }
    return out;
}

uintptr_t ControlledPhysics(bool* riding) {
    uintptr_t torrent = InGame() ? RiddenTorrentPhysics() : 0;
    if (riding) *riding = torrent != 0;
    return torrent ? torrent : PlayerModule(off::kModPhysics);
}

static void MoveChunk(uintptr_t physics, float dx, float dy, float dz) {
    float pos[3];
    if (!physics || !mem::ReadRaw(physics + off::kPhysicsPosition, pos, sizeof(pos))) return;
    pos[0] += dx;
    pos[1] += dy;
    pos[2] += dz;
    mem::WriteRaw(physics + off::kPhysicsPosition, pos, sizeof(pos));
}

bool ReadTransform(Transform& out) {
    uintptr_t player = PlayerIns();
    uintptr_t physics = PlayerModule(off::kModPhysics);
    if (!player || !physics || !InGame()) return false;
    out.map_id = mem::Read<uint32_t>(player + off::kMapId).value_or(0);
    out.angle1 = mem::Read<float>(physics + off::kPhysicsAngle1).value_or(0);
    out.angle2 = mem::Read<float>(physics + off::kPhysicsAngle2).value_or(0);
    return mem::ReadRaw(player + off::kGlobalPosition, out.global, sizeof(out.global));
}

bool WriteTransform(const Transform& t) {
    uintptr_t player = PlayerIns();
    uintptr_t physics = PlayerModule(off::kModPhysics);
    if (!player || !physics || !InGame()) return false;
    Transform now;
    if (!ReadTransform(now)) return false;
    if ((now.map_id >> 24) != (t.map_id >> 24)) return false;  // different area (m60 vs m10...)
    const float dx = t.global[0] - now.global[0];
    const float dy = t.global[1] - now.global[1];
    const float dz = t.global[2] - now.global[2];
    const uintptr_t torrent = RiddenTorrentPhysics();
    MoveChunk(physics, dx, dy, dz);
    mem::Write<float>(physics + off::kPhysicsAngle1, t.angle1);
    mem::Write<float>(physics + off::kPhysicsAngle2, t.angle2);
    mem::Write<uint32_t>(player + off::kMapId, t.map_id);
    if (torrent) {
        MoveChunk(torrent, dx, dy, dz);
        mem::Write<float>(torrent + off::kPhysicsAngle1, t.angle1);
        mem::Write<float>(torrent + off::kPhysicsAngle2, t.angle2);
    }
    return true;
}

bool NudgePosition(float dx, float dy, float dz) {
    uintptr_t physics = PlayerModule(off::kModPhysics);
    if (!physics) return false;
    MoveChunk(physics, dx, dy, dz);
    MoveChunk(RiddenTorrentPhysics(), dx, dy, dz);
    return true;
}

namespace {
using GetFlagFn = uint32_t (*)(void* flags, uint32_t flag);

uintptr_t EventFlagMan() {
    if (!g_addr.event_flag_man) return 0;
    uintptr_t man = mem::ReadPtr(g_addr.event_flag_man);
    // The divisor (+0x1C) must look sane before handing the object to the game.
    return man && mem::Read<uint32_t>(man + 0x1C).value_or(0) == 1000 ? man : 0;
}
}

bool GetEventFlag(uint32_t flag) {
    uintptr_t man = EventFlagMan();
    if (!man || !g_addr.fn_get_event_flag) return false;
    return reinterpret_cast<GetFlagFn>(g_addr.fn_get_event_flag)(reinterpret_cast<void*>(man),
                                                                 flag) != 0;
}

// Sets the bit directly in the flag block, the same way the game's own GetFlag finds it:
// group = flag / divisor in an MSVC std::map<u32, descriptor> at +0x30 (head pointer at +0x38),
// descriptor mode 1 = block index into the block array (+0x28, block size +0x20), mode 2 =
// pointer to an external block. Bits are numbered from the most significant bit of each byte.
bool SetEventFlag(uint32_t flag, bool on) {
    uintptr_t man = EventFlagMan();
    if (!man || !InGame()) return false;
    const uint32_t divisor = mem::Read<uint32_t>(man + 0x1C).value_or(0);
    if (!divisor) return false;
    const uint32_t group = flag / divisor;
    const uint32_t offset = flag % divisor;

    const uintptr_t head = mem::ReadPtr(man + 0x38);
    if (!head) return false;
    uintptr_t node = mem::ReadPtr(head + 0x08);  // root
    uintptr_t found = head;
    for (int depth = 0; depth < 64 && node && !mem::Read<uint8_t>(node + 0x19).value_or(1); ++depth) {
        const uint32_t key = mem::Read<uint32_t>(node + 0x20).value_or(0);
        if (key >= group) {
            found = node;
            node = mem::ReadPtr(node + 0x00);  // left
        } else {
            node = mem::ReadPtr(node + 0x10);  // right
        }
    }
    if (found == head || mem::Read<uint32_t>(found + 0x20).value_or(~0u) != group) return false;

    uintptr_t block = 0;
    switch (mem::Read<uint32_t>(found + 0x28).value_or(0)) {
        case 1: {
            const uint64_t index = mem::Read<uint32_t>(found + 0x30).value_or(0);
            const uint64_t size = mem::Read<uint32_t>(man + 0x20).value_or(0);
            const uintptr_t blocks = mem::ReadPtr(man + 0x28);
            block = blocks ? blocks + index * size : 0;
            break;
        }
        case 2:
            block = mem::ReadPtr(found + 0x30);
            break;
        default:
            break;
    }
    if (!block) return false;
    const uintptr_t byte = block + (offset >> 3);
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - (offset & 7)));
    if (!mem::SetBits(byte, mask, on)) return false;
    return !g_addr.fn_get_event_flag || GetEventFlag(flag) == on;
}

}  // namespace zz::game
