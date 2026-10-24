#pragma once
#include <cstdint>

namespace zz::cheats {

struct State {
    // Survival
    bool no_death = false;
    bool god_mode = false;
    bool no_hit = false;
    bool inf_hp = false;
    bool inf_fp = false;
    bool inf_stamina = false;
    bool no_stagger = false;
    bool no_fall_damage = false;
    // Items / combat
    bool no_goods_consume = false;
    bool no_arrow_consume = false;
    bool one_shot = false;
    // Stealth (experimental)
    bool hide = false;
    bool silence = false;
    // World
    bool torrent_no_death = false;
    bool freeze_ai = false;
    bool enemies_no_move = false;
    bool enemies_no_attack = false;
    // Killing aura: hostile characters that come within the radius die instantly.
    bool kill_aura = false;
    float kill_aura_radius = 8.0f;   // metres
    bool kill_aura_npcs = false;     // also NPCs that turned hostile (off: a misclick can't cost a quest)
    bool kill_aura_show = true;      // draw the radius as a faint sphere
    // Movement
    bool player_speed_on = false;
    float player_speed = 2.0f;
    bool torrent_speed_on = false;
    float torrent_speed = 2.0f;
    bool jump_boost_on = false;
    float jump_boost = 2.0f;
    bool fly_mode = false;   // no gravity + no collision, RT/LT or Page Up/Down to rise/sink
    float fly_speed = 3.0f;  // multiplies both vertical speed and horizontal movement
};

State& Get();

// Called once per rendered frame; re-applies every enabled toggle.
void Tick(float dt, bool game_focused, bool menu_open);

// Turns everything off but keeps the slider values.
void DisableAll();

// Status line for the speed widgets ("" when OK).
const char* TorrentStatus();

}  // namespace zz::cheats
