#include "hotkeys.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "config.h"
#include "i18n.h"
#include "log.h"
#include "ui.h"

namespace zz::hotkeys {

namespace {

game::Transform g_saved_transform;
bool g_has_saved = false;

void RunSavePosition() { SavePosition(); }
void RunLoadPosition() { LoadPosition(); }
void RunTeleportUp() { ui::Notify(game::NudgePosition(0, 5, 0) ? i18n::T("Up +5 m") : i18n::T("Teleport failed.")); }
void RunAddRunes() {
    ui::Notify(game::AddRunes(100000) ? i18n::T("+100,000 runes") : i18n::T("Could not add runes."));
}
void RunAllOff() {
    cheats::DisableAll();
    ui::Notify(i18n::T("All cheats OFF"));
}

using S = cheats::State;
const Action kActions[] = {
    {"god_mode", "God Mode", &S::god_mode, nullptr},
    {"no_death", "No Death", &S::no_death, nullptr},
    {"no_hit", "Attacks Pass Through", &S::no_hit, nullptr},
    {"inf_hp", "Infinite HP", &S::inf_hp, nullptr},
    {"inf_fp", "Infinite FP", &S::inf_fp, nullptr},
    {"inf_stamina", "Infinite Stamina", &S::inf_stamina, nullptr},
    {"no_stagger", "No Stagger", &S::no_stagger, nullptr},
    {"no_fall_damage", "No Fall Damage", &S::no_fall_damage, nullptr},
    {"no_goods_consume", "No Item Consumption", &S::no_goods_consume, nullptr},
    {"no_arrow_consume", "No Arrow / Bolt Consumption", &S::no_arrow_consume, nullptr},
    {"one_shot", "One-Shot Kills", &S::one_shot, nullptr},
    {"hide", "Invisible", &S::hide, nullptr},
    {"silence", "Silent", &S::silence, nullptr},
    {"torrent_no_death", "Torrent No Death", &S::torrent_no_death, nullptr},
    {"freeze_ai", "Freeze Enemy AI", &S::freeze_ai, nullptr},
    {"enemies_no_move", "Enemies Can't Move", &S::enemies_no_move, nullptr},
    {"enemies_no_attack", "Enemies Don't Attack", &S::enemies_no_attack, nullptr},
    {"kill_aura", "Killing Aura", &S::kill_aura, nullptr},
    {"player_speed", "Player Speed", &S::player_speed_on, nullptr},
    {"torrent_speed", "Torrent Speed", &S::torrent_speed_on, nullptr},
    {"jump_boost", "Jump Boost", &S::jump_boost_on, nullptr},
    {"fly_mode", "Fly Mode (noclip)", &S::fly_mode, nullptr},
    {"save_position", "Save Position", nullptr, RunSavePosition},
    {"load_position", "Teleport to Saved Position", nullptr, RunLoadPosition},
    {"teleport_up", "Teleport Up (+5 m)", nullptr, RunTeleportUp},
    {"add_runes", "+100,000 Runes", nullptr, RunAddRunes},
    {"all_off", "Turn All Cheats Off", nullptr, RunAllOff},
};
constexpr int kCount = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));

// Left thumb holds L3 -> right-hand buttons; right thumb holds R3 -> D-pad / left bumper.
struct Default {
    const char* id;
    const char* combo;
};
const Default kDefaults[] = {
    {"god_mode", "L3+Y"},          {"inf_fp", "L3+B"},
    {"inf_stamina", "L3+X"},       {"no_stagger", "L3+A"},
    {"player_speed", "R3+UP"},     {"torrent_speed", "R3+RIGHT"},
    {"fly_mode", "R3+DOWN"},       {"save_position", "R3+LEFT"},
    {"load_position", "R3+LB"},
};

Binding g_bindings[kCount];

std::mutex g_queue_mutex;
std::vector<int> g_key_queue;

int g_capture_action = -1;
bool g_capture_controller = false;

void Trigger(int index) {
    const Action& a = kActions[index];
    if (a.toggle) {
        bool& flag = cheats::Get().*a.toggle;
        flag = !flag;
        ui::Notify(std::string(i18n::T(a.label)) + ": " + i18n::T(flag ? "ON" : "OFF"));
    } else if (a.run) {
        a.run();
    }
    ZZ_LOG("Shortcut: %s", a.id);
}

std::wstring Widen(const char* s) {
    wchar_t buf[128];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, 128);
    return buf;
}

}  // namespace

int Count() { return kCount; }
const Action& GetAction(int index) { return kActions[index]; }
Binding& GetBinding(int index) { return g_bindings[index]; }

void ResetDefaults() {
    for (auto& b : g_bindings) b = Binding{};
    for (auto& d : kDefaults)
        for (int i = 0; i < kCount; ++i)
            if (std::string(kActions[i].id) == d.id) g_bindings[i].combo = pad::ComboFromConfig(d.combo);
}

void Load() {
    ResetDefaults();
    const std::wstring& ini = config::IniPath();
    if (ini.empty()) return;
    bool any_missing = false;
    for (int i = 0; i < kCount; ++i) {
        wchar_t buf[128];
        GetPrivateProfileStringW(L"hotkeys", Widen(kActions[i].id).c_str(), L"\x01", buf, 128,
                                 ini.c_str());
        if (buf[0] == L'\x01') {
            any_missing = true;  // keep the default
            continue;
        }
        char value[256];
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, value, sizeof(value), nullptr, nullptr);
        // Format: "kb=0x74;pad=L3+Y" (either part may be empty)
        Binding b;
        std::string v = value;
        size_t kb = v.find("kb=");
        if (kb != std::string::npos) b.key = static_cast<int>(std::strtol(v.c_str() + kb + 3, nullptr, 0));
        size_t pd = v.find("pad=");
        if (pd != std::string::npos) {
            size_t end = v.find(';', pd);
            b.combo = pad::ComboFromConfig(v.substr(pd + 4, end == std::string::npos ? end : end - pd - 4));
        }
        g_bindings[i] = b;
    }
    if (any_missing) Save();
}

void Save() {
    const std::wstring& ini = config::IniPath();
    if (ini.empty()) return;
    for (int i = 0; i < kCount; ++i) {
        char value[64];
        std::snprintf(value, sizeof(value), "kb=0x%02X;pad=%s", g_bindings[i].key,
                      pad::ComboToConfig(g_bindings[i].combo).c_str());
        if (!g_bindings[i].key)
            std::snprintf(value, sizeof(value), "kb=;pad=%s",
                          pad::ComboToConfig(g_bindings[i].combo).c_str());
        WritePrivateProfileStringW(L"hotkeys", Widen(kActions[i].id).c_str(), Widen(value).c_str(),
                                   ini.c_str());
    }
}

void QueueKey(int vk) {
    std::lock_guard lock(g_queue_mutex);
    g_key_queue.push_back(vk);
}

void ProcessQueued() {
    std::vector<int> keys;
    {
        std::lock_guard lock(g_queue_mutex);
        keys.swap(g_key_queue);
    }
    for (int vk : keys)
        for (int i = 0; i < kCount; ++i)
            if (g_bindings[i].key == vk) Trigger(i);
}

void OnCombo(const pad::Combo& combo) {
    for (int i = 0; i < kCount; ++i)
        if (g_bindings[i].combo == combo) Trigger(i);
}

void BeginCapture(int action, bool controller) {
    g_capture_action = action;
    g_capture_controller = controller;
}
void CancelCapture() { g_capture_action = -1; }
int CaptureAction() { return g_capture_action; }
bool CaptureController() { return g_capture_controller; }

bool CaptureKey(int vk) {
    if (g_capture_action < 0 || g_capture_controller) return false;
    if (vk == VK_ESCAPE) {
        CancelCapture();
        return true;
    }
    if (vk == config::Get().toggle_key ||
        (config::Get().toggle_key == config::kKeyUnderEsc && vk == static_cast<int>(MapVirtualKeyW(0x29, MAPVK_VSC_TO_VK))))
        return true;  // reserved for the menu
    Binding& b = g_bindings[g_capture_action];
    b.key = (vk == VK_BACK || vk == VK_DELETE) ? 0 : vk;
    // One key, one action.
    if (b.key)
        for (int i = 0; i < kCount; ++i)
            if (i != g_capture_action && g_bindings[i].key == b.key) g_bindings[i].key = 0;
    CancelCapture();
    Save();
    return true;
}

bool CaptureCombo(const pad::Combo& combo) {
    if (g_capture_action < 0 || !g_capture_controller) return false;
    g_bindings[g_capture_action].combo = combo;
    for (int i = 0; i < kCount; ++i)
        if (i != g_capture_action && g_bindings[i].combo == combo) g_bindings[i].combo = {};
    CancelCapture();
    Save();
    return true;
}

std::string KeyLabel(int vk) {
    if (!vk) return "-";
    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG lparam = static_cast<LONG>(scan << 16);
    // Keys on the extended part of the keyboard need bit 24 for the right name.
    switch (vk) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
            lparam |= 1 << 24;
            break;
        default:
            break;
    }
    wchar_t name[64] = {};
    if (GetKeyNameTextW(lparam, name, 64) > 0) {
        char out[128];
        WideCharToMultiByte(CP_UTF8, 0, name, -1, out, sizeof(out), nullptr, nullptr);
        return out;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

bool SavePosition() {
    g_has_saved = game::ReadTransform(g_saved_transform);
    ui::Notify(g_has_saved ? i18n::T("Position saved.") : i18n::T("Could not read the position."));
    return g_has_saved;
}

bool LoadPosition() {
    if (!g_has_saved) {
        ui::Notify(i18n::T("No saved position yet."));
        return false;
    }
    bool ok = game::WriteTransform(g_saved_transform);
    ui::Notify(ok ? i18n::T("Teleported to the saved position.")
                  : i18n::T("Saved position is in another area; go there first."));
    return ok;
}

bool HasSavedPosition() { return g_has_saved; }
const game::Transform& SavedPosition() { return g_saved_transform; }

}  // namespace zz::hotkeys
