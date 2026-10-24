#include "cheats.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "game.h"
#include "i18n.h"
#include "log.h"
#include "memory.h"
#include "pad.h"
#include "ui.h"
#include "world_db.h"

namespace zz::cheats {

namespace {

State g_state;
const char* g_torrent_status = "";

using game::DbgFlag;
namespace off = game::off;

constexpr float kFlyVerticalSpeed = 8.0f;    // m/s at fly speed 1x
constexpr float kFlyHorizontalSpeed = 6.0f;  // m/s at fly speed 1x, full stick

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


// Fly mode moves the body itself instead of scaling the game's own movement: in a fall or jump
// animation the game doesn't move the character at all, so there would be nothing to scale.
// Left stick / WASD steer relative to the camera; the game's own displacement is cancelled so
// both don't add up.
void FlyMove(uintptr_t body, float vertical, float dt, bool input_allowed) {
    float pos[3];
    if (!body || !mem::ReadRaw(body + off::kPhysicsPosition, pos, sizeof(pos))) {
        g_motion.valid = false;
        return;
    }
    if (body != g_motion.body) {
        g_motion = Motion{};
        g_motion.body = body;
    }
    if (g_motion.valid && dt > 0.0f && dt < 0.25f) {
        const float dx = pos[0] - g_motion.prev[0], dz = pos[2] - g_motion.prev[2];
        if (std::fabs(dx) < 2.0f && std::fabs(dz) < 2.0f) {  // not a teleport / load
            pos[0] = g_motion.prev[0];
            pos[2] = g_motion.prev[2];
        }
        float mx = 0.0f, my = 0.0f;  // stick: x right, y forward
        if (input_allowed) {
            const pad::RawState pad_state = pad::ReadRaw();
            mx = pad_state.lx;
            my = pad_state.ly;
            if (GetAsyncKeyState('W') & 0x8000) my += 1.0f;
            if (GetAsyncKeyState('S') & 0x8000) my -= 1.0f;
            if (GetAsyncKeyState('D') & 0x8000) mx += 1.0f;
            if (GetAsyncKeyState('A') & 0x8000) mx -= 1.0f;
        }
        const float len = std::sqrt(mx * mx + my * my);
        game::Camera cam;
        if (len > 0.05f && game::ReadCamera(cam)) {
            if (len > 1.0f) mx /= len, my /= len;
            float fx = cam.forward[0], fz = cam.forward[2];
            // The camera looks at the player: "forward" must point from the camera to the body.
            if (fx * (pos[0] - cam.pos[0]) + fz * (pos[2] - cam.pos[2]) < 0) fx = -fx, fz = -fz;
            float rx = cam.right[0], rz = cam.right[2];
            const float fl = std::sqrt(fx * fx + fz * fz), rl = std::sqrt(rx * rx + rz * rz);
            if (fl > 0.01f && rl > 0.01f) {
                fx /= fl, fz /= fl, rx /= rl, rz /= rl;
                const float speed = kFlyHorizontalSpeed * g_state.fly_speed * dt;
                pos[0] += (fx * my + rx * mx) * speed;
                pos[2] += (fz * my + rz * mx) * speed;
            }
        }
        pos[1] += vertical * dt;
        mem::WriteRaw(body + off::kPhysicsPosition, pos, sizeof(pos));
    }
    for (int i = 0; i < 3; ++i) g_motion.prev[i] = pos[i];
    g_motion.valid = true;
}

// TEAM_TYPE values the aura never touches, whatever the param says: players and phantoms,
// allies and spirit summons, friendly / co-op NPCs, neutral characters and objects.
bool ProtectedTeam(uint8_t team) {
    switch (team) {
        case 1: case 2: case 4: case 5: case 8: case 12: case 14: case 15: case 19: case 20:
        case 22: case 26: case 28: case 30: case 31: case 47:
            return true;
        default:
            return false;
    }
}

// Whitelist: only characters whose NpcParam row is an enemy (see ENEMY_NPC_TEAMS in
// tools/extract_items.py). NPCs such as Hewg, Enia or Ranni have param team 0 and are never in it.
bool EnemyParam(uintptr_t chr) {
    const uint32_t param = mem::Read<uint32_t>(chr + off::kNpcParamId).value_or(0);
    return std::binary_search(world::kEnemyNpcParams,
                              world::kEnemyNpcParams + world::kEnemyNpcParamCount, param);
}

// "Also NPCs that turned hostile": NPCs currently on a hostile team (attacked merchants, NPC
// invaders). An NPC that is not angry at you keeps a friendly or neutral team and is skipped.
bool AngryNpc(uint8_t team) { return team == 9 || team == 21 || team == 27; }

void KillAura(uintptr_t player) {
    float me[3];
    if (!game::ChrPosition(player, me)) return;
    const uintptr_t player_vtable = mem::ReadPtr(player);
    const float r2 = g_state.kill_aura_radius * g_state.kill_aura_radius;
    static uintptr_t chrs[1024];
    const int n = game::ListCharacters(chrs, 1024);
    for (int i = 0; i < n; ++i) {
        const uintptr_t chr = chrs[i];
        if (chr == player || mem::ReadPtr(chr) == player_vtable) continue;  // any PlayerIns (co-op partners too)
        const int32_t npc_id = mem::Read<int32_t>(chr + off::kNpcId).value_or(0);
        if (npc_id >= 8000 && npc_id < 8100) continue;  // Torrent
        const uint8_t team = mem::Read<uint8_t>(chr + off::kTeamType).value_or(0);
        float p[3];
        if (!game::ChrPosition(chr, p)) continue;
        const float dx = p[0] - me[0], dy = p[1] - me[1], dz = p[2] - me[2];
        if (dx * dx + dy * dy + dz * dz > r2) continue;
        const bool hostile =
            !ProtectedTeam(team) && (EnemyParam(chr) || (g_state.kill_aura_npcs && AngryNpc(team)));
        const uintptr_t data = game::ChrModule(chr, off::kModData);
        const int32_t hp = data ? mem::Read<int32_t>(data + off::kDataHp).value_or(-1) : -1;
        if (hostile && hp > 0) mem::Write<int32_t>(data + off::kDataHp, 0);
    }
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
            ui::Notify(i18n::T("Fly mode: OFF (death / loading screen)"));
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

    if (g_state.kill_aura && in_game && !dead) KillAura(player);

    const bool fly = g_state.fly_mode && in_game && !dead;
    // No fall damage (always on while flying): the game measures a fall from the position where it
    // started (physics + 0x150, found by logging take-offs: the y at +0x154 keeps the take-off
    // height during the whole fall). Keeping it at the current position makes every landing a
    // zero-metre fall: no damage and no lethal fall.
    if (in_game && physics && (fly || g_state.no_fall_damage)) {
        float pos[3];
        if (mem::ReadRaw(physics + off::kPhysicsPosition, pos, sizeof(pos)))
            mem::WriteRaw(physics + off::kPhysicsFallStart, pos, sizeof(pos));
    }
    // Fall module: without gravity a fall or jump never lands, and the airborne state blocks
    // walking and attacks. While flying the fall motion is off; switching fly off restores it.
    if (const uintptr_t fall = in_game ? game::ChrModule(player, off::kModFall) : 0) {
        static bool fall_overridden = false;
        if (fly) {
            mem::Write<float>(fall + off::kFallTimer, 0.0f);
            mem::Write<uint8_t>(fall + off::kFallDisableMotion, 1);
            fall_overridden = true;
        } else if (fall_overridden) {
            mem::Write<float>(fall + off::kFallTimer, 0.0f);
            mem::Write<uint8_t>(fall + off::kFallDisableMotion, 0);
            fall_overridden = false;
        }
    }
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
            // Page Up / Page Down, and the jump (Space) and crouch (C) keys.
            if ((GetAsyncKeyState(VK_PRIOR) & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000)) up = 1.0f;
            if ((GetAsyncKeyState(VK_NEXT) & 0x8000) || (GetAsyncKeyState('C') & 0x8000)) down = 1.0f;
        }
        // Ease the vertical speed in: a short or light press moves slowly even on controllers whose
        // triggers jump straight to full value.
        static float vertical = 0.0f;
        const float target = (up - down) * kFlyVerticalSpeed * g_state.fly_speed;
        const float blend = (dt > 0.0f && dt < 0.25f) ? std::min(1.0f, dt * 2.5f) : 1.0f;
        vertical += (target - vertical) * (target == 0.0f ? std::min(1.0f, blend * 4.0f) : blend);
        FlyMove(physics, vertical, dt, game_focused && !menu_open);
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
    g_state.kill_aura_radius = keep.kill_aura_radius;
    g_state.kill_aura_npcs = keep.kill_aura_npcs;
    g_state.kill_aura_show = keep.kill_aura_show;
}

}  // namespace zz::cheats
