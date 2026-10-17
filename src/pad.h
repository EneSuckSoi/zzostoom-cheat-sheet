#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Controller support. The game reads the pad through XInputGetState (xinput1_4.dll); we hook it to
//  - detect L3+R3 (menu) and "L3/R3 held + button" shortcuts,
//  - hide those buttons from the game (a lone short L3/R3 press is passed on when released),
//  - show the game an idle controller while the menu is open.
// PlayStation pads work the same way through Steam Input (they show up as XInput devices).
namespace zz::pad {

// Our button bits: XInput's wButtons plus the two analog triggers.
enum Button : uint32_t {
    kUp = 0x0001,
    kDown = 0x0002,
    kLeft = 0x0004,
    kRight = 0x0008,
    kMenu = 0x0010,  // Start / Options
    kView = 0x0020,  // Back / Share
    kL3 = 0x0040,
    kR3 = 0x0080,
    kLB = 0x0100,
    kRB = 0x0200,
    kA = 0x1000,
    kB = 0x2000,
    kX = 0x4000,
    kY = 0x8000,
    kLT = 0x10000,
    kRT = 0x20000,
};

enum Modifier : uint8_t { kNone = 0, kModL3 = 1, kModR3 = 2 };

struct Combo {
    uint8_t modifier = kNone;
    uint32_t button = 0;
    bool valid() const { return modifier != kNone && button != 0; }
    bool operator==(const Combo& o) const { return modifier == o.modifier && button == o.button; }
};

struct Events {
    bool toggle_menu = false;
    std::vector<Combo> combos;
};

bool Install();  // creates the MinHook hook (enabled together with the other hooks)
bool Installed();

// Called by the overlay when the menu opens/closes.
void SetMenuOpen(bool open);

// Drains what happened since the last call (render thread).
Events TakeEvents();

// Raw state of the first connected controller, read directly (bypasses our filter).
struct RawState {
    bool connected = false;
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;  // -1..1
    float lt = 0, rt = 0;                  // 0..1
};
RawState ReadRaw();

// While fly mode is on, RT rises and LT sinks (0..1, by trigger pressure); both triggers are
// hidden from the game meanwhile.
void SetFlyActive(bool active);
void FlyInput(float& up, float& down);

std::string ComboName(const Combo& c);
std::string ComboToConfig(const Combo& c);
Combo ComboFromConfig(const std::string& s);
const char* ButtonName(uint32_t button);

}  // namespace zz::pad
