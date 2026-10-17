#include "cheats.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "game.h"
#include "log.h"
#include "memory.h"
#include "pad.h"
#include "ui.h"

namespace zz::cheats {

namespace {

State g_state;
const char* g_torrent_status = "";

using game::DbgFlag;
namespace off = game::off;

constexpr float kFlyVerticalSpeed = 8.0f;  // m/s at fly speed 1x

// A boolean toggle that owns one global debug flag byte. The flag is re-applied every frame while
// enabled and cleared exactly once when the toggle is switched off.
struct FlagBinding {
    bool State::*toggle;
    DbgFlag flag;
    bool applied = false;
};

FlagBinding g_flag_bindings[] = {
    {&State::no_death, game::kPlayerNoDead},
    {&State::god_mode, game::kPlayerNoDead},
    {&State::torrent_no_death, game::kHorseNoDead},
    {&State::one_shot, game::kPlayerExterminate},
    {&State::no_goods_consume, game::kPlayerNoGoodsConsume},
    {&State::inf_fp, game::kAllNoFpConsume},
    {&State::inf_fp, game::kNoArtsFpConsume},
    {&State::no_arrow_consume, game::kAllNoArrowConsume},
    {&State::hide, game::kPlayerHide},
    {&State::silence, game::kPlayerSilence},
    {&State::enemies_no_attack, game::kAllNoAttack},
    {&State::enemies_no_move, game::kAllNoMove},
    {&State::freeze_ai, game::kAllNoUpdateAi},
};

void ApplyFlags() {
    // Several toggles can share one flag (no_death and god_mode both need "no dead").
    for (auto& b : g_flag_bindings) {
        bool want = false;
        for (auto& other : g_flag_bindings)
            if (other.flag == b.flag && g_state.*other.toggle) want = true;
        if (want) {
            if (!game::GetDbgFlag(b.flag)) game::SetDbgFlag(b.flag, true);
            b.applied = true;
        } else if (b.applied) {
            game::SetDbgFlag(b.flag, false);
            b.applied = false;
        }
    }
}

// Bit toggle on a per-character structure (re-resolved every frame because the player
// instance changes on loading screens).
struct BitBinding {
    bool applied = false;
    void Apply(bool want, uintptr_t address, uint8_t mask) {
        if (want) {
            if (address) mem::SetBits(address, mask, true);
            applied = true;
        } else if (applied) {
            if (address) mem::SetBits(address, mask, false);
            applied = false;
        }
    }
};

BitBinding g_god_bit, g_no_hit_bit, g_no_gravity_bit, g_no_collision_bit;
bool g_stagger_applied = false;
uintptr_t g_last_player = 0;
ULONGLONG g_torrent_check = 0;

// Movement cheats scale what the game itself does each frame: the controlled body's position
// change since the previous frame is multiplied. This works with Seamless Coop, which ignores
// the animation speed field other tools use.
struct Motion {
    uintptr_t body = 0;
    float prev[3] = {};
    bool valid = false;
    bool jumping = false;
};
Motion g_motion;

void Refill(uintptr_t data, ptrdiff_t cur, ptrdiff_t max) {
    auto m = mem::Read<int32_t>(data + max);
    auto c = mem::Read<int32_t>(data + cur);
    if (m && c && *m > 0 && *c < *m) mem::Write<int32_t>(data + cur, *m);
}

// Records the raw trigger values while flying, to see how analog the controller really is.
void LogTriggers() {
    static ULONGLONG last = 0;
    static int last_up = -1, last_down = -1;
    float up = 0.0f, down = 0.0f;
    pad::FlyInput(up, down);
    const int u = static_cast<int>(up * 255.0f), d = static_cast<int>(down * 255.0f);
    const ULONGLONG now = GetTickCount64();
    if ((u != last_up || d != last_down) && now - last > 250) {
        last = now;
        last_up = u;
        last_down = d;
        ZZ_LOG("Fly triggers: RT/A %d  LT %d", u, d);
    }
}

// Trigger pressure -> speed: squared so that light presses give fine control.
float Curve(float v) {
    v = (v - 0.05f) / 0.95f;
    return v <= 0.0f ? 0.0f : v * v;
}

void MoveBody(uintptr_t body, float horizontal, float jump, float vertical, float dt) {
    float pos[3];
    if (!body || !mem::ReadRaw(body + off::kPhysicsPosition, pos, sizeof(pos))) {
        g_motion.valid = false;
        return;
    }
    if (body != g_motion.body) {
        g_motion = Motion{};
        g_motion.body = body;
    }
    bool changed = false;
    if (g_motion.valid) {
        const float dx = pos[0] - g_motion.prev[0];
        const float dy = pos[1] - g_motion.prev[1];
        const float dz = pos[2] - g_motion.prev[2];
        // Ignore teleports / loading jumps.
        const bool sane = std::fabs(dx) < 2.0f && std::fabs(dz) < 2.0f && std::fabs(dy) < 2.0f;
        if (sane && horizontal != 1.0f && (dx != 0.0f || dz != 0.0f)) {
            pos[0] += dx * (horizontal - 1.0f);
            pos[2] += dz * (horizontal - 1.0f);
            changed = true;
        }
        // A jump starts with a fast rise; keep boosting until the body stops going up.
        if (sane && jump > 1.0f && dt > 0.0f) {
            if (dy / dt > 4.5f) g_motion.jumping = true;
            if (dy <= 0.0f) g_motion.jumping = false;
            if (g_motion.jumping) {
                pos[1] += dy * (jump - 1.0f);
                changed = true;
            }
        }
    }
    if (vertical != 0.0f && dt > 0.0f && dt < 0.25f) {
        pos[1] += vertical * dt;
        changed = true;
    }
    if (changed) mem::WriteRaw(body + off::kPhysicsPosition, pos, sizeof(pos));
    for (int i = 0; i < 3; ++i) g_motion.prev[i] = pos[i];
    g_motion.valid = true;
}

}  // namespace

State& Get() { return g_state; }
const char* TorrentStatus() { return g_torrent_status; }

void Tick(float dt, bool game_focused, bool menu_open) {
    if (!game::Ready()) return;
    ApplyFlags();

    const uintptr_t player = game::PlayerIns();
    const bool in_game = game::InGame();
    const uintptr_t data = in_game ? game::ChrModule(player, off::kModData) : 0;
    const uintptr_t physics = in_game ? game::ChrModule(player, off::kModPhysics) : 0;
    const uintptr_t super_armor = in_game ? game::ChrModule(player, off::kModSuperArmor) : 0;
    const uintptr_t chr_ctrl = in_game ? mem::ReadPtr(player + off::kChrCtrl) : 0;

    // Death / loading screen: the player instance changes. Fly mode would leave collision off on
    // the new character and drop it through the floor at the grace, so it switches itself off.
    const bool dead = data && mem::Read<int32_t>(data + off::kDataHp).value_or(1) <= 0;
    if ((player != g_last_player && g_last_player) || dead || !in_game) {
        if (g_state.fly_mode && (dead || g_last_player)) {
            g_state.fly_mode = false;
            ui::Notify("Fly mode: OFF (death / loading screen)");
        }
        g_motion.valid = false;
    }
    g_last_player = player;

    // God mode: per-character "no damage" bit + full HP + no death (flag above).
    g_god_bit.Apply(g_state.god_mode, data ? data + off::kDataDebugFlags : 0, 0x02);
    // No hit: attacks pass through the player (ChrIns debug flags, bit 3).
    g_no_hit_bit.Apply(g_state.no_hit, in_game ? player + off::kChrDebugFlags : 0, 0x08);

    if (data) {
        if (g_state.inf_hp || g_state.god_mode) Refill(data, off::kDataHp, off::kDataMaxHp);
        if (g_state.inf_fp) Refill(data, off::kDataFp, off::kDataMaxFp);
        if (g_state.inf_stamina) Refill(data, off::kDataStamina, off::kDataMaxStamina);
    }

    // No stagger: keep poise (super armor) far above anything an attack can remove.
    if (super_armor) {
        if (g_state.no_stagger) {
            if (mem::Read<float>(super_armor + off::kSuperArmorCurrent).value_or(0) < 9999.0f)
                mem::Write<float>(super_armor + off::kSuperArmorCurrent, 99999.0f);
            g_stagger_applied = true;
        } else if (g_stagger_applied) {
            auto max = mem::Read<float>(super_armor + off::kSuperArmorMax);
            if (max) mem::Write<float>(super_armor + off::kSuperArmorCurrent, *max);
            g_stagger_applied = false;
        }
    }

    const bool fly = g_state.fly_mode && in_game && !dead;
    pad::SetFlyActive(fly);
    g_no_gravity_bit.Apply(fly, physics ? physics + off::kPhysicsNoGravity : 0, 0x01);
    g_no_collision_bit.Apply(fly, chr_ctrl ? chr_ctrl + off::kChrCtrlCollision : 0, 0x08);

    const bool moving_cheat =
        g_state.player_speed_on || g_state.torrent_speed_on || g_state.jump_boost_on;
    if (!in_game || dead || (!fly && !moving_cheat)) {
        g_motion.valid = false;
        g_torrent_status = "";
        return;
    }
    if (fly) {
        float up = 0.0f, down = 0.0f;
        pad::FlyInput(up, down);  // RT / LT (A also rises)
        up = Curve(up);
        down = Curve(down);
        if (game_focused && !menu_open) {
            if (GetAsyncKeyState(VK_PRIOR) & 0x8000) up = 1.0f;   // Page Up
            if (GetAsyncKeyState(VK_NEXT) & 0x8000) down = 1.0f;  // Page Down
        }
        // Ease the vertical speed in: a short or light press moves slowly even on controllers whose
        // triggers jump straight to full value.
        static float vertical = 0.0f;
        const float target = (up - down) * kFlyVerticalSpeed * g_state.fly_speed;
        const float blend = (dt > 0.0f && dt < 0.25f) ? std::min(1.0f, dt * 2.5f) : 1.0f;
        vertical += (target - vertical) * (target == 0.0f ? std::min(1.0f, blend * 4.0f) : blend);
        LogTriggers();
        MoveBody(physics, g_state.fly_speed, 1.0f, vertical, dt);
        return;
    }
    bool riding = false;
    const uintptr_t body = game::ControlledPhysics(&riding);
    float horizontal = 1.0f;
    if (riding && g_state.torrent_speed_on) horizontal = g_state.torrent_speed;
    if (!riding && g_state.player_speed_on) horizontal = g_state.player_speed;
    g_torrent_status = (g_state.torrent_speed_on && !riding)
                           ? "Torrent speed applies while you ride Torrent."
                           : "";
    // Only worth logging when no Torrent exists at all (not merely "not riding right now").
    if (g_state.torrent_speed_on && !riding && GetTickCount64() - g_torrent_check > 60000 &&
        !game::TorrentIns()) {
        g_torrent_check = GetTickCount64();
        ZZ_LOG("Torrent lookup: %s", game::DescribeTorrentSearch().c_str());
    }
    MoveBody(body, horizontal, g_state.jump_boost_on ? g_state.jump_boost : 1.0f, 0.0f, dt);
}

void DisableAll() {
    const State keep = g_state;
    g_state = State{};
    g_state.player_speed = keep.player_speed;
    g_state.torrent_speed = keep.torrent_speed;
    g_state.jump_boost = keep.jump_boost;
    g_state.fly_speed = keep.fly_speed;
}

}  // namespace zz::cheats
