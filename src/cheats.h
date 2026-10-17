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
