#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "log.h"

namespace zz::config {

namespace {

Config g_config;
std::wstring g_path;

struct KeyEntry {
    const char* name;
    int vk;
};

const KeyEntry kKeys[] = {
    {"UNDER_ESC", kKeyUnderEsc}, {"INSERT", VK_INSERT}, {"HOME", VK_HOME},     {"END", VK_END},       {"DELETE", VK_DELETE},
    {"PAGEUP", VK_PRIOR},  {"PAGEDOWN", VK_NEXT}, {"F1", VK_F1},         {"F2", VK_F2},
    {"F3", VK_F3},         {"F4", VK_F4},         {"F5", VK_F5},         {"F6", VK_F6},
    {"F7", VK_F7},         {"F8", VK_F8},         {"F9", VK_F9},         {"F10", VK_F10},
    {"F11", VK_F11},       {"F12", VK_F12},       {"NUMPAD0", VK_NUMPAD0}, {"PAUSE", VK_PAUSE},
    {"SCROLLLOCK", VK_SCROLL},
};

std::string ReadString(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[128];
    GetPrivateProfileStringW(L"zzostoom", key, def, buf, 128, g_path.c_str());
    char out[256];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
    return out;
}

void WriteString(const wchar_t* key, const std::string& value) {
    wchar_t buf[128];
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, buf, 128);
    WritePrivateProfileStringW(L"zzostoom", key, buf, g_path.c_str());
}

}  // namespace

const char* KeyName(int vk) {
    for (auto& k : kKeys)
        if (k.vk == vk) return k.name;
    return "?";
}

int KeyFromName(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    for (auto& k : kKeys)
        if (upper == k.name) return k.vk;
    return 0;
}

Config& Get() { return g_config; }

bool MatchesKey(int key, unsigned long long wparam, long long lparam) {
    if (key == kKeyUnderEsc) return ((lparam >> 16) & 0xFF) == 0x29 && !(lparam & (1 << 24));
    return static_cast<int>(wparam) == key;
}
const std::wstring& IniPath() { return g_path; }

void Load(const std::wstring& dll_dir) {
    g_path = dll_dir + L"\\zzostoom_cheat_sheet.ini";
    bool exists = GetFileAttributesW(g_path.c_str()) != INVALID_FILE_ATTRIBUTES;
    std::string key = ReadString(L"toggle_key", L"INSERT");
    if (int vk = KeyFromName(key)) {
        g_config.toggle_key = vk;
        g_config.toggle_key_name = KeyName(vk);
    }
    g_config.ui_scale = static_cast<float>(std::atof(ReadString(L"ui_scale", L"0").c_str()));
    g_config.block_game_input = ReadString(L"block_game_input", L"1") != "0";
    g_config.auto_backup = ReadString(L"auto_backup", L"1") != "0";
    g_config.show_hint = ReadString(L"show_hint", L"1") != "0";
    if (!exists) Save();
    ZZ_LOG("Config: toggle=%s ui_scale=%.2f block_input=%d auto_backup=%d",
           g_config.toggle_key_name.c_str(), g_config.ui_scale, g_config.block_game_input,
           g_config.auto_backup);
}

void Save() {
    if (g_path.empty()) return;
    WriteString(L"toggle_key", g_config.toggle_key_name);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", g_config.ui_scale);
    WriteString(L"ui_scale", buf);
    WriteString(L"block_game_input", g_config.block_game_input ? "1" : "0");
    WriteString(L"auto_backup", g_config.auto_backup ? "1" : "0");
    WriteString(L"show_hint", g_config.show_hint ? "1" : "0");
}

}  // namespace zz::config
