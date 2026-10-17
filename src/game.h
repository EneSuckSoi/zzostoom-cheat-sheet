#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Everything that touches Elden Ring's memory lives here.
//
// Static addresses are found with byte patterns (so minor patches keep working); on the exact
// version these offsets were verified for (exe 2.7.1.0 = game 1.17.1) the known RVAs are used.
// Structure offsets were cross-checked against two independent public reverse-engineering
// projects (veeenu/eldenring-practice-tool and vswarte/fromsoftware-rs).
namespace zz::game {

namespace off {
// WorldChrManImp
constexpr ptrdiff_t kWcmPlayerIns = 0x1E508;
constexpr ptrdiff_t kWcmTorrentSlot = 0x1CC90;
constexpr ptrdiff_t kWcmChrSetTable = 0x10DC8;
constexpr ptrdiff_t kWcmChrSetCount = 0x10ED8;
constexpr ptrdiff_t kWcmSummonBuddyChrSet = 0x10F90;  // ChrSet holding spirit ashes and Torrent
constexpr ptrdiff_t kChrSetCapacity = 0x10;
constexpr ptrdiff_t kChrSetEntries = 0x18;  // ChrSetEntry[capacity], 0x10 bytes each
// ChrIns
constexpr ptrdiff_t kChrCtrl = 0x58;
constexpr ptrdiff_t kNpcId = 0x64;
constexpr ptrdiff_t kCharacterId = 0x188;
constexpr ptrdiff_t kModules = 0x190;
constexpr ptrdiff_t kChrDebugFlags = 0x538;  // u32, bit 3 = ignore incoming hits
constexpr ptrdiff_t kGlobalPosition = 0x6C0;  // x, y, z in area coordinates
constexpr ptrdiff_t kMapId = 0x6D0;
constexpr ptrdiff_t kChrCtrlCollision = 0xF0;  // bit 3 = no collision
// ChrIns module container
constexpr ptrdiff_t kModData = 0x00;
constexpr ptrdiff_t kModBehavior = 0x28;
constexpr ptrdiff_t kModSuperArmor = 0x40;
constexpr ptrdiff_t kModPhysics = 0x68;
constexpr ptrdiff_t kModRide = 0xE8;
// CSChrDataModule
constexpr ptrdiff_t kDataArea = 0x7F;  // high byte of block_id_origin
constexpr ptrdiff_t kDataHp = 0x138;
constexpr ptrdiff_t kDataMaxHp = 0x13C;
constexpr ptrdiff_t kDataFp = 0x148;
constexpr ptrdiff_t kDataMaxFp = 0x14C;
constexpr ptrdiff_t kDataStamina = 0x154;
constexpr ptrdiff_t kDataMaxStamina = 0x158;
constexpr ptrdiff_t kDataDebugFlags = 0x19B;  // bit 1 = no damage
// Other modules
constexpr ptrdiff_t kBehaviorAnimSpeed = 0x17C8;
constexpr ptrdiff_t kSuperArmorCurrent = 0x10;
constexpr ptrdiff_t kSuperArmorMax = 0x14;
constexpr ptrdiff_t kPhysicsAngle1 = 0x54;
constexpr ptrdiff_t kPhysicsAngle2 = 0x64;
constexpr ptrdiff_t kPhysicsPosition = 0x70;
constexpr ptrdiff_t kPhysicsStandingOnGround = 0x92;
constexpr ptrdiff_t kPhysicsTouchingGround = 0x93;
constexpr ptrdiff_t kPhysicsGravityMul = 0x1CC;
constexpr ptrdiff_t kPhysicsNoGravity = 0x1D3;  // bit 0
constexpr ptrdiff_t kRideLastMounted = 0x18;
constexpr ptrdiff_t kRideIsMounted = 0x163;
// GameDataMan / PlayerGameData
constexpr ptrdiff_t kGdmPlayerGameData = 0x08;
constexpr ptrdiff_t kPgdAttributes = 0x3C;  // 8 x u32: vig, mnd, end, str, dex, int, fai, arc
constexpr ptrdiff_t kPgdLevel = 0x68;
constexpr ptrdiff_t kPgdRunes = 0x6C;
constexpr ptrdiff_t kPgdRuneMemory = 0x70;
constexpr ptrdiff_t kPgdName = 0x9C;  // wchar_t[17]
constexpr ptrdiff_t kPgdScadutree = 0xFC;
constexpr ptrdiff_t kPgdReveredAsh = 0xFD;
// CSMenuManImp
constexpr ptrdiff_t kMenuCursorShow = 0xAC;  // bit 0
}  // namespace off

// Byte offsets inside the global character debug flag block (WorldChrManDbgFlags).
enum DbgFlag : int {
    kPlayerNoDead = 0x00,
    kHorseNoDead = 0x01,
    kPlayerExterminate = 0x02,
    kPlayerNoGoodsConsume = 0x03,
    kAllNoStaminaConsume = 0x04,
    kAllNoFpConsume = 0x05,
    kAllNoArrowConsume = 0x06,
    kPlayerHide = 0x08,
    kPlayerSilence = 0x09,
    kAllNoAttack = 0x0D,
    kAllNoMove = 0x0E,
    kAllNoUpdateAi = 0x0F,
    kNoArtsFpConsume = 0x11,
};

struct Addresses {
    uintptr_t world_chr_man = 0;    // static slot holding WorldChrManImp*
    uintptr_t game_data_man = 0;    // static slot holding GameDataMan*
    uintptr_t map_item_man = 0;     // static slot holding MapItemMan*
    uintptr_t cs_menu_man_imp = 0;  // static slot holding CSMenuManImp*
    uintptr_t dbg_flags = 0;        // address of the debug flag bytes themselves
    uintptr_t fn_item_give = 0;     // adds items directly to the inventory
    uintptr_t event_flag_man = 0;   // static slot holding CSEventFlagMan*
    uintptr_t fn_get_event_flag = 0;  // CSFD4VirtualMemoryFlag::GetFlag
};

// Scans the executable. Safe to call once the game window exists.
bool Init();
bool Ready();
const Addresses& Addr();
const std::string& GameVersion();
const std::string& InitReport();

// Live instances (0 when not available, e.g. on the title screen).
uintptr_t WorldChrMan();
uintptr_t PlayerIns();
uintptr_t PlayerModule(ptrdiff_t module_offset);
uintptr_t PlayerGameData();
uintptr_t TorrentIns();
uintptr_t ChrModule(uintptr_t chr_ins, ptrdiff_t module_offset);
bool InGame();

bool GetDbgFlag(DbgFlag flag);
void SetDbgFlag(DbgFlag flag, bool on);

// Gives an item to the currently loaded character through the game's own item function.
// `full_id` must include the type prefix (0x10000000 armor, 0x40000000 goods...).
bool GiveItem(uint32_t full_id, uint32_t quantity);

void SetGameCursorVisible(bool visible);

struct CharacterInfo {
    std::string name;
    uint32_t level = 0;
    uint32_t runes = 0;
    uint32_t attributes[8] = {};
    uint8_t scadutree = 0;
    uint8_t revered_ash = 0;
    int hp = 0, max_hp = 0, fp = 0, max_fp = 0, stamina = 0, max_stamina = 0;
};
bool ReadCharacter(CharacterInfo& out);
bool AddRunes(int64_t amount);
bool WriteAttributes(const uint32_t attributes[8]);
bool WriteBlessings(uint8_t scadutree, uint8_t revered_ash);

// Saved position: global (area) coordinates + facing, restored the same way the practice tool
// does it (chunk position shifted by the global delta, Torrent moved along when ridden).
struct Transform {
    float global[3] = {};
    float angle1 = 0, angle2 = 0;
    uint32_t map_id = 0;
};
bool ReadTransform(Transform& out);
// Returns false (and does nothing) when the saved position is in another area.
bool WriteTransform(const Transform& t);
bool NudgePosition(float dx, float dy, float dz);
bool ReadChunkPosition(float out[3]);
bool WriteChunkPosition(const float pos[3]);
bool OnGround();

// Returns true only if reading the flag back gives the requested state.
bool SetEventFlag(uint32_t flag, bool on);
bool GetEventFlag(uint32_t flag);

// Physics module of what the player controls: Torrent while riding, else the player.
uintptr_t ControlledPhysics(bool* riding = nullptr);
// Diagnostic text for the log when Torrent can't be found.
std::string DescribeTorrentSearch();

}  // namespace zz::game
