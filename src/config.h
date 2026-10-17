#pragma once
#include <string>

// zzostoom_cheat_sheet.ini next to the DLL.
namespace zz::config {

// Pseudo key code: the key under Esc (left of 1), matched by its scan code so it works on every
// keyboard layout (" on Turkish Q, ` on US, ^ on German...).
constexpr int kKeyUnderEsc = 0x1000;

struct Config {
    int toggle_key = 0x2D;          // VK_INSERT
    std::string toggle_key_name = "INSERT";
    float ui_scale = 0.0f;          // 0 = automatic (based on screen height)
    bool block_game_input = true;   // block keyboard/mouse game input while the menu is open
    bool auto_backup = true;        // back up the active save before the first item of a session
    bool show_hint = true;          // small "INSERT: menu" hint on screen when the menu is closed
    std::string language = "en";    // menu language code (i18n.cpp)
    bool english_item_names = false;  // item names in English whatever the menu language
};

void Load(const std::wstring& dll_dir);
void Save();
Config& Get();
const std::wstring& IniPath();

const char* KeyName(int vk);
int KeyFromName(const std::string& name);
// True if a WM_KEYDOWN (wparam/lparam) is the configured key.
bool MatchesKey(int key, unsigned long long wparam, long long lparam);

}  // namespace zz::config
