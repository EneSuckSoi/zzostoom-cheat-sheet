#include "pad.h"

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cctype>
#include <mutex>

#include "MinHook.h"
#include "log.h"

namespace zz::pad {

namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

GetStateFn g_original = nullptr;  // trampoline once hooked, plain function otherwise
bool g_installed = false;
std::atomic<bool> g_menu_open{false};
std::atomic<bool> g_fly_active{false};
std::atomic<float> g_fly_up{0.0f};
std::atomic<float> g_fly_down{0.0f};

std::mutex g_mutex;
Events g_events;

constexpr BYTE kTriggerThreshold = 100;
constexpr ULONGLONG kTapMs = 120;  // how long a passed-through L3/R3 tap is shown to the game

struct UserState {
    uint32_t prev = 0;
    bool l3_used = false;
    bool r3_used = false;
    bool menu_latched = false;
    uint32_t consumed = 0;  // buttons used in a shortcut, hidden until released
    ULONGLONG tap_l3_until = 0;
    ULONGLONG tap_r3_until = 0;
    bool was_open = false;
    DWORD last_raw_packet = 0;
    DWORD packet_bias = 0;
    XINPUT_GAMEPAD last_out{};
};
UserState g_users[XUSER_MAX_COUNT];

uint32_t ToMask(const XINPUT_GAMEPAD& g) {
    uint32_t m = g.wButtons & 0xF3FF;
    if (g.bLeftTrigger > kTriggerThreshold) m |= kLT;
    if (g.bRightTrigger > kTriggerThreshold) m |= kRT;
    return m;
}

void Process(DWORD user, XINPUT_STATE* st) {
    UserState& s = g_users[user];
    const uint32_t raw = ToMask(st->Gamepad);
    const uint32_t pressed = raw & ~s.prev;
    const uint32_t released = s.prev & ~raw;
    const bool l3 = raw & kL3;
    const bool r3 = raw & kR3;
    const ULONGLONG now = GetTickCount64();

    // Buttons still held when the menu closes stay hidden until released (e.g. Start used to
    // close the menu must not open the game's own menu).
    if (s.was_open && !g_menu_open) {
        s.consumed |= raw;
        s.l3_used = s.r3_used = true;
    }
    s.was_open = g_menu_open;

    if (pressed & kL3) s.l3_used = false;
    if (pressed & kR3) s.r3_used = false;

    if (l3 && r3) {
        if (!s.menu_latched) {
            s.menu_latched = true;
            g_events.toggle_menu = true;
        }
        s.l3_used = s.r3_used = true;
    } else if (!l3 && !r3) {
        s.menu_latched = false;
    }

    const uint32_t others = pressed & ~(kL3 | kR3);
    if (others && l3 != r3) {
        for (uint32_t bit = 1; bit <= kRT; bit <<= 1) {
            if (!(others & bit)) continue;
            g_events.combos.push_back({l3 ? kModL3 : kModR3, bit});
            s.consumed |= bit;
        }
        (l3 ? s.l3_used : s.r3_used) = true;
    }
    const bool fly = g_fly_active && !g_menu_open;
    // A (Cross) is jump: while flying it rises instead, since a jump without gravity never lands.
    // L3 is crouch: held on its own (not as part of an L3 + button shortcut) it sinks.
    const bool l3_sink = fly && l3 && !r3 && !s.l3_used;
    g_fly_up = fly ? ((raw & kA) ? 1.0f : st->Gamepad.bRightTrigger / 255.0f) : 0.0f;
    g_fly_down = fly ? (l3_sink ? 1.0f : st->Gamepad.bLeftTrigger / 255.0f) : 0.0f;

    // While flying an L3 tap would make the character crouch, so it is not passed on.
    if ((released & kL3) && !s.l3_used && !fly) s.tap_l3_until = now + kTapMs;
    if ((released & kR3) && !s.r3_used) s.tap_r3_until = now + kTapMs;
    s.consumed &= raw;
    s.prev = raw;

    XINPUT_GAMEPAD out = st->Gamepad;
    if (g_menu_open) {
        out = XINPUT_GAMEPAD{};
    } else {
        const uint32_t hide = kL3 | kR3 | s.consumed;
        WORD buttons = out.wButtons & static_cast<WORD>(~(hide & 0xFFFF));
        if (now < s.tap_l3_until) buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (now < s.tap_r3_until) buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
        if (fly) buttons &= static_cast<WORD>(~XINPUT_GAMEPAD_A);
        out.wButtons = buttons;
        if ((hide & kLT) || fly) out.bLeftTrigger = 0;
        if ((hide & kRT) || fly) out.bRightTrigger = 0;
    }
    // Games may skip polls whose packet number did not change; make our edits visible.
    if (st->dwPacketNumber == s.last_raw_packet &&
        std::memcmp(&out, &s.last_out, sizeof(out)) != 0)
        ++s.packet_bias;
    s.last_raw_packet = st->dwPacketNumber;
    s.last_out = out;
    st->Gamepad = out;
    st->dwPacketNumber += s.packet_bias;
}

DWORD WINAPI HookedGetState(DWORD user, XINPUT_STATE* state) {
    DWORD result = g_original(user, state);
    if (result == ERROR_SUCCESS && state && user < XUSER_MAX_COUNT) {
        std::lock_guard lock(g_mutex);
        Process(user, state);
    }
    return result;
}

struct NamedButton {
    uint32_t bit;
    const char* config;
    const char* label;
};
const NamedButton kButtons[] = {
    {kA, "A", "A / Cross"},          {kB, "B", "B / Circle"},
    {kX, "X", "X / Square"},         {kY, "Y", "Y / Triangle"},
    {kLB, "LB", "LB / L1"},          {kRB, "RB", "RB / R1"},
    {kLT, "LT", "LT / L2"},          {kRT, "RT", "RT / R2"},
    {kUp, "UP", "D-pad Up"},         {kDown, "DOWN", "D-pad Down"},
    {kLeft, "LEFT", "D-pad Left"},   {kRight, "RIGHT", "D-pad Right"},
    {kView, "VIEW", "View / Share"}, {kMenu, "MENU", "Menu / Options"},
};

float Stick(SHORT v, SHORT deadzone) {
    if (v > -deadzone && v < deadzone) return 0.0f;
    return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f);
}

}  // namespace

#ifdef ZZ_PAD_TEST
void TestProcess(DWORD user, XINPUT_STATE* state) {
    std::lock_guard lock(g_mutex);
    Process(user, state);
}
#endif

bool Install() {
    HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
    auto target = xinput ? reinterpret_cast<GetStateFn>(GetProcAddress(xinput, "XInputGetState"))
                         : nullptr;
    if (!target) {
        ZZ_LOG("XInput not available, controller support disabled");
        return false;
    }
    g_original = target;
    MH_STATUS s = MH_CreateHook(reinterpret_cast<void*>(target),
                                reinterpret_cast<void*>(HookedGetState),
                                reinterpret_cast<void**>(&g_original));
    if (s != MH_OK) {
        g_original = target;
        ZZ_LOG("XInputGetState hook failed (%d)", static_cast<int>(s));
        return false;
    }
    g_installed = true;
    return true;
}

bool Installed() { return g_installed; }

void SetMenuOpen(bool open) { g_menu_open = open; }
void SetFlyActive(bool active) { g_fly_active = active; }
bool FlyActive() { return g_fly_active && !g_menu_open; }

void FlyInput(float& up, float& down) {
    up = g_fly_up;
    down = g_fly_down;
}

Events TakeEvents() {
    std::lock_guard lock(g_mutex);
    Events out = std::move(g_events);
    g_events = Events{};
    return out;
}

RawState ReadRaw() {
    RawState out;
    if (!g_original) return out;
    for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
        XINPUT_STATE st{};
        if (g_original(user, &st) != ERROR_SUCCESS) continue;
        const auto& g = st.Gamepad;
        out.connected = true;
        out.buttons = ToMask(g);
        out.lx = Stick(g.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        out.ly = Stick(g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        out.rx = Stick(g.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        out.ry = Stick(g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        out.lt = g.bLeftTrigger / 255.0f;
        out.rt = g.bRightTrigger / 255.0f;
        break;
    }
    return out;
}

const char* ButtonName(uint32_t button) {
    for (auto& b : kButtons)
        if (b.bit == button) return b.label;
    return "?";
}

std::string ComboName(const Combo& c) {
    if (!c.valid()) return "-";
    return std::string(c.modifier == kModL3 ? "L3" : "R3") + " + " + ButtonName(c.button);
}

std::string ComboToConfig(const Combo& c) {
    if (!c.valid()) return "";
    for (auto& b : kButtons)
        if (b.bit == c.button)
            return std::string(c.modifier == kModL3 ? "L3" : "R3") + "+" + b.config;
    return "";
}

Combo ComboFromConfig(const std::string& text) {
    std::string s;
    for (char ch : text)
        if (!std::isspace(static_cast<unsigned char>(ch)))
            s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    Combo c;
    if (s.rfind("L3+", 0) == 0)
        c.modifier = kModL3;
    else if (s.rfind("R3+", 0) == 0)
        c.modifier = kModR3;
    else
        return {};
    const std::string rest = s.substr(3);
    for (auto& b : kButtons)
        if (rest == b.config) c.button = b.bit;
    return c.button ? c : Combo{};
}

}  // namespace zz::pad
