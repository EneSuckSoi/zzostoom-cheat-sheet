#include "ui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "cheats.h"
#include "config.h"
#include "game.h"
#include "hotkeys.h"
#include "imgui.h"
#include "item_db.h"
#include "log.h"
#include "overlay.h"
#include "pad.h"
#include "saves.h"
#include "world_db.h"

namespace zz::ui {

namespace {

constexpr const char* kModVersion = "1.3.0";

const char* const kAffinityNames[13] = {
    "Standard", "Heavy",  "Keen", "Quality", "Fire",  "Flame Art", "Lightning",
    "Sacred",   "Magic",  "Cold", "Poison",  "Blood", "Occult",
};

const char* const kTabs[] = {"Items",     "Cheats",    "Movement", "World",
                             "Character", "Shortcuts", "Saves",    "Settings"};
constexpr int kTabCount = 8;

const ImVec4 kGold(0.92f, 0.78f, 0.42f, 1.0f);
const ImVec4 kGreen(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kRed(0.95f, 0.40f, 0.35f, 1.0f);
const ImVec4 kDim(0.62f, 0.60f, 0.56f, 1.0f);

float g_scale = 1.0f;

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---- Notifications ------------------------------------------------------------------------

struct Toast {
    std::string text;
    double expires;
};
std::mutex g_toast_mutex;
std::vector<Toast> g_toasts;
double g_start_time = 0.0;

// ---- Give queue (drained a few items per frame on the render thread) -----------------------

struct GiveRequest {
    uint32_t id;
    uint32_t quantity;
    std::string label;
};
std::deque<GiveRequest> g_queue;
int g_batch_done = 0;
int g_batch_failed = 0;
std::string g_batch_label;

// ---- UI state -----------------------------------------------------------------------------

std::vector<std::vector<int>> g_items_by_sub;
std::vector<std::vector<int>> g_subs_by_top;
int g_selected_sub = -1;
int g_selected_item = -1;
char g_search[96] = {};
std::string g_applied_search;
std::vector<int> g_search_results;
int g_quantity = 1;
int g_upgrade = 0;
int g_affinity = 0;
int g_give_all_sub = -1;

int g_current_tab = 0;
int g_tab_request = -1;
bool g_focus_request = false;

int g_attr_edit[8] = {};
bool g_attr_loaded = false;
int g_scadutree_edit = 0;
int g_revered_edit = 0;
bool g_bless_loaded = false;
int g_custom_runes = 50000;

std::vector<saves::SaveFile> g_saves;
bool g_saves_scanned = false;

// Used only by test/harness.cpp to screenshot each tab; inert unless the variable is set.
int TestEnvInt(const wchar_t* name) {
    wchar_t buf[16];
    DWORD n = GetEnvironmentVariableW(name, buf, 16);
    return (n > 0 && n < 16) ? _wtoi(buf) : -1;
}

std::string FormatNumber(uint64_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string FormatPlaytime(uint32_t seconds) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%uh %02um", seconds / 3600, (seconds / 60) % 60);
    return buf;
}

std::string Lower(const char* s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool Stackable(const db::ItemDef& item) {
    return item.kind == db::kKindGoods || item.kind == db::kKindAmmo;
}

int MaxQuantity(const db::ItemDef& item) { return Stackable(item) ? 999 : 10; }

uint32_t BuildItemId(const db::ItemDef& item, int upgrade, int affinity) {
    uint32_t id = item.id;
    if (item.kind == db::kKindWeapon) {
        if (affinity > 0 && affinity < 13 && (item.affinities & (1u << affinity)))
            id += static_cast<uint32_t>(affinity) * 100;
        id += static_cast<uint32_t>(std::clamp(upgrade, 0, static_cast<int>(item.max_upgrade)));
    } else if (item.kind == db::kKindSpiritAsh) {
        id += static_cast<uint32_t>(std::clamp(upgrade, 0, static_cast<int>(item.max_upgrade)));
    }
    return id;
}

std::string ItemLabel(const db::ItemDef& item, int upgrade, int affinity) {
    std::string label = item.name;
    if (item.kind == db::kKindWeapon) {
        if (affinity > 0 && (item.affinities & (1u << affinity)))
            label = std::string(kAffinityNames[affinity]) + " " + label;
        if (upgrade > 0 && item.max_upgrade)
            label += " +" + std::to_string(std::min<int>(upgrade, item.max_upgrade));
    } else if (item.kind == db::kKindSpiritAsh && upgrade > 0 && item.max_upgrade) {
        label += " +" + std::to_string(std::min<int>(upgrade, item.max_upgrade));
    }
    return label;
}

const char* KindLabel(uint8_t kind) {
    switch (kind) {
        case db::kKindWeapon: return "Weapon";
        case db::kKindAmmo: return "Ammunition";
        case db::kKindProtector: return "Armor";
        case db::kKindAccessory: return "Talisman";
        case db::kKindGem: return "Ash of War";
        case db::kKindSpiritAsh: return "Spirit Ash";
        default: return "Item";
    }
}

void Enqueue(const db::ItemDef& item, int quantity, int upgrade, int affinity) {
    quantity = std::clamp(quantity, 1, MaxQuantity(item));
    g_queue.push_back({BuildItemId(item, upgrade, affinity), static_cast<uint32_t>(quantity),
                       ItemLabel(item, upgrade, affinity)});
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("%s", text);
}

void Toggle(const char* label, bool* value, const char* help = nullptr) {
    ImGui::Checkbox(label, value);
    if (help) HelpMarker(help);
}

// Checkbox + slider on one line, the slider aligned in a column.
void ToggleSlider(const char* label, bool* on, const char* slider_id, float* value, float min,
                  float max, const char* format, const char* help) {
    ImGui::Checkbox(label, on);
    ImGui::SameLine(230 * g_scale);
    ImGui::SetNextItemWidth(260 * g_scale);
    ImGui::SliderFloat(slider_id, value, min, max, format);
    if (help) HelpMarker(help);
}

void Header(const char* text) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    ImGui::TextColored(kGold, "%s", text);
    ImGui::PopFont();
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    ImVec4* c = style.Colors;
    const ImVec4 gold_dark(0.36f, 0.29f, 0.14f, 1.0f);
    const ImVec4 gold_mid(0.55f, 0.44f, 0.20f, 1.0f);
    const ImVec4 gold_hi(0.72f, 0.58f, 0.26f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.91f, 0.86f, 1.0f);
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.07f, 0.96f);
    c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.60f);
    c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.42f, 0.35f, 0.20f, 0.55f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.14f, 0.12f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.21f, 0.15f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.26f, 0.17f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.07f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.16f, 0.09f, 1.0f);
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_CheckboxSelectedBg] = ImVec4(0.30f, 0.24f, 0.11f, 1.0f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.55f, 0.44f, 0.20f, 0.35f);
    c[ImGuiCol_ResizeGripHovered] = gold_mid;
    c[ImGuiCol_ResizeGripActive] = gold_hi;
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.28f, 0.20f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = gold_mid;
    c[ImGuiCol_ScrollbarGrabActive] = gold_hi;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.44f, 0.20f, 0.45f);
    c[ImGuiCol_NavCursor] = kGold;
    c[ImGuiCol_InputTextCursor] = kGold;
    c[ImGuiCol_SliderGrab] = gold_mid;
    c[ImGuiCol_SliderGrabActive] = gold_hi;
    c[ImGuiCol_Button] = gold_dark;
    c[ImGuiCol_ButtonHovered] = gold_mid;
    c[ImGuiCol_ButtonActive] = gold_hi;
    c[ImGuiCol_Header] = ImVec4(0.30f, 0.25f, 0.13f, 0.85f);
    c[ImGuiCol_HeaderHovered] = gold_mid;
    c[ImGuiCol_HeaderActive] = gold_hi;
    c[ImGuiCol_Separator] = ImVec4(0.42f, 0.35f, 0.20f, 0.6f);
    c[ImGuiCol_Tab] = ImVec4(0.16f, 0.14f, 0.10f, 1.0f);
    c[ImGuiCol_TabHovered] = gold_mid;
    c[ImGuiCol_TabSelected] = gold_dark;
    c[ImGuiCol_TabSelectedOverline] = kGold;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.14f, 0.10f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
}

// ---- Items --------------------------------------------------------------------------------

void RebuildSearch() {
    g_applied_search = g_search;
    g_search_results.clear();
    std::string needle = Lower(g_search);
    if (needle.empty()) return;
    for (int i = 0; i < db::kItemCount; ++i)
        if (Lower(db::kItems[i].name).find(needle) != std::string::npos) g_search_results.push_back(i);
}

void DrawItemDetails(float height) {
    ImGui::BeginChild("details", ImVec2(0, height), ImGuiChildFlags_Borders);
    if (g_selected_item < 0) {
        ImGui::TextDisabled("Pick a category on the left, then click an item.");
        ImGui::TextDisabled("Double-clicking an item gives it right away with the options below.");
        ImGui::EndChild();
        return;
    }
    const db::ItemDef& item = db::kItems[g_selected_item];
    Header(item.name);
    ImGui::TextDisabled("%s  |  ID 0x%08X  |  %s", KindLabel(item.kind),
                        BuildItemId(item, g_upgrade, g_affinity),
                        db::kSubCategories[item.sub].name);
    if (item.info) ImGui::TextWrapped("%s", item.info);

    const int max_qty = MaxQuantity(item);
    ImGui::SetNextItemWidth(130 * g_scale);
    ImGui::InputInt("Quantity", &g_quantity, 1, 10);
    g_quantity = std::clamp(g_quantity, 1, max_qty);
    ImGui::SameLine();
    if (ImGui::SmallButton("1")) g_quantity = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("10")) g_quantity = std::min(10, max_qty);
    if (Stackable(item)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("99")) g_quantity = 99;
        ImGui::SameLine();
        char max_label[32];
        std::snprintf(max_label, sizeof(max_label), "Max (%d)", std::max<int>(1, item.max_num));
        if (ImGui::SmallButton(max_label)) g_quantity = std::max<int>(1, item.max_num);
        HelpMarker("Anything above the inventory limit goes to the Sort Chest, as in the game.");
    }

    if ((item.kind == db::kKindWeapon || item.kind == db::kKindSpiritAsh) && item.max_upgrade) {
        ImGui::SetNextItemWidth(220 * g_scale);
        ImGui::SliderInt("Upgrade", &g_upgrade, 0, item.max_upgrade, "+%d");
        ImGui::SameLine();
        if (ImGui::SmallButton("Max##upgrade")) g_upgrade = item.max_upgrade;
    }
    if (item.kind == db::kKindWeapon && item.affinities) {
        if (!(item.affinities & (1u << g_affinity))) g_affinity = 0;
        ImGui::SetNextItemWidth(220 * g_scale);
        if (ImGui::BeginCombo("Affinity", kAffinityNames[g_affinity])) {
            for (int a = 0; a < 13; ++a) {
                if (a && !(item.affinities & (1u << a))) continue;
                if (ImGui::Selectable(kAffinityNames[a], a == g_affinity)) g_affinity = a;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.38f, 0.12f, 1.0f));
    if (ImGui::Button("GIVE", ImVec2(160 * g_scale, 0))) Enqueue(item, g_quantity, g_upgrade, g_affinity);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s  x%d", ItemLabel(item, g_upgrade, g_affinity).c_str(), g_quantity);
    ImGui::EndChild();
}

void DrawItemList(const std::vector<int>& items, bool show_category) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("items", show_category ? 3 : 2, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    if (show_category) ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 2.5f);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(items.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = items[row];
            const db::ItemDef& item = db::kItems[index];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(index);
            if (ImGui::Selectable(item.name, index == g_selected_item,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                if (g_selected_item != index) {
                    g_selected_item = index;
                    g_upgrade = 0;
                    g_affinity = 0;
                    if (!Stackable(item)) g_quantity = 1;
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    Enqueue(item, g_quantity, g_upgrade, g_affinity);
            }
            if (item.info) ImGui::SetItemTooltip("%s", item.info);
            ImGui::PopID();
            if (show_category) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", db::kSubCategories[item.sub].name);
            }
            ImGui::TableNextColumn();
            if (item.info)
                ImGui::TextDisabled("%s", item.info);
            else if (item.max_upgrade)
                ImGui::TextDisabled("up to +%d", item.max_upgrade);
        }
    }
    ImGui::EndTable();
}

void DrawItemsTab() {
    const float left_width = 330 * g_scale;
    ImGui::BeginChild("categories", ImVec2(left_width, 0), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search items (e.g. rune, smithing, lily)", g_search,
                             sizeof(g_search));
    if (g_applied_search != g_search) RebuildSearch();
    ImGui::Spacing();
    for (int top = 0; top < db::kTopCategoryCount; ++top) {
        const auto& subs = g_subs_by_top[top];
        if (subs.empty()) continue;
        ImGui::PushID(top);
        int total = 0;
        for (int s : subs) total += static_cast<int>(g_items_by_sub[s].size());
        char label[160];
        std::snprintf(label, sizeof(label), "%s (%d)###top", db::kTopCategories[top], total);
        if (g_selected_sub >= 0 && db::kSubCategories[g_selected_sub].top == top)
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (int s : subs) {
                std::snprintf(label, sizeof(label), "%s (%d)", db::kSubCategories[s].name,
                              static_cast<int>(g_items_by_sub[s].size()));
                if (ImGui::Selectable(label, s == g_selected_sub && g_search[0] == 0)) {
                    g_selected_sub = s;
                    g_search[0] = 0;
                    RebuildSearch();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    const float details_height = 205 * g_scale;
    const float list_height =
        ImGui::GetContentRegionAvail().y - details_height - ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("list", ImVec2(0, list_height), ImGuiChildFlags_Borders);
    if (g_search[0]) {
        ImGui::Text("Search results: %d items", static_cast<int>(g_search_results.size()));
        DrawItemList(g_search_results, true);
    } else if (g_selected_sub >= 0) {
        const auto& items = g_items_by_sub[g_selected_sub];
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kGold, "%s", db::kSubCategories[g_selected_sub].name);
        ImGui::SameLine();
        ImGui::TextDisabled("(%d items)", static_cast<int>(items.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Give all...")) {
            g_give_all_sub = g_selected_sub;
            ImGui::OpenPopup("give_all");
        }
        if (ImGui::BeginPopupModal("give_all", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto& all = g_items_by_sub[g_give_all_sub];
            ImGui::Text("Give all %d items in \"%s\"?", static_cast<int>(all.size()),
                        db::kSubCategories[g_give_all_sub].name);
            ImGui::TextDisabled("Stackable items: x%d, everything else: x1. Weapons at +%d.",
                                g_quantity, g_upgrade);
            if (ImGui::Button("Yes, give", ImVec2(140 * g_scale, 0))) {
                for (int i : all) {
                    const auto& it = db::kItems[i];
                    Enqueue(it, Stackable(it) ? g_quantity : 1, g_upgrade, 0);
                }
                g_batch_label = db::kSubCategories[g_give_all_sub].name;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(140 * g_scale, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        DrawItemList(items, false);
    } else {
        Header("Give Items");
        ImGui::TextWrapped(
            "Categories follow the game's own inventory groups (%d items, DLC included). Open a "
            "category or search above.",
            db::kItemCount);
        ImGui::TextWrapped(
            "Items go to the character that is currently loaded, through the game's own "
            "\"add to inventory\" function, so normal and Seamless Coop saves work the same way.");
    }
    ImGui::EndChild();
    DrawItemDetails(details_height);
    ImGui::EndGroup();
}

// ---- Cheats -------------------------------------------------------------------------------

void DrawCheatsTab() {
    auto& s = cheats::Get();
    // One column on purpose: controller navigation (D-pad down) walks the list top to bottom.
    ImGui::SeparatorText("Survival");
    Toggle("No Death", &s.no_death, "You take damage but never die; HP stops at 1.");
    Toggle("God Mode", &s.god_mode, "You take no damage at all (also keeps HP full and prevents death).");
    Toggle("Attacks Pass Through", &s.no_hit,
           "Enemy attacks don't connect at all: no hit reaction, no status buildup.");
    Toggle("Infinite HP", &s.inf_hp, "HP is refilled every frame.");
    Toggle("Infinite FP", &s.inf_fp, "Spells and skills cost no FP.");
    Toggle("Infinite Stamina", &s.inf_stamina, "Stamina stays full.");
    Toggle("No Stagger", &s.no_stagger, "Poise is kept extremely high, so hits don't stagger you.");

    ImGui::SeparatorText("Combat & items");
    Toggle("No Item Consumption", &s.no_goods_consume, "Flasks, pots, runes... are not used up.");
    Toggle("No Arrow / Bolt Consumption", &s.no_arrow_consume);
    Toggle("One-Shot Kills", &s.one_shot, "Every enemy you hit dies in one hit.");

    ImGui::SeparatorText("Stealth (experimental)");
    Toggle("Invisible", &s.hide, "Enemies don't notice you by sight.");
    Toggle("Silent", &s.silence, "Enemies don't notice you by sound.");

    ImGui::SeparatorText("World & enemies");
    Toggle("Torrent No Death", &s.torrent_no_death);
    Toggle("Freeze Enemy AI", &s.freeze_ai, "Enemies stop thinking and stand still.");
    Toggle("Enemies Can't Move", &s.enemies_no_move);
    Toggle("Enemies Don't Attack (experimental)", &s.enemies_no_attack,
           "A game debug flag; in some situations it can affect your own attacks too.");

    ImGui::Spacing();
    if (ImGui::Button("Turn all cheats off")) cheats::DisableAll();
    ImGui::TextDisabled("Takes effect the moment you tick a box. Everything starts off each time "
                        "the game starts.");
}

// ---- Movement -----------------------------------------------------------------------------

void DrawMovementTab() {
    auto& s = cheats::Get();
    ImGui::SeparatorText("Speed");
    ToggleSlider("Player Speed", &s.player_speed_on, "##player_speed", &s.player_speed, 1.0f, 5.0f,
                 "%.2fx", "Multiplies how far you move each frame (walking, running, rolling). "
                 "Attacks keep their normal speed. Works with Seamless Coop.");
    ToggleSlider("Torrent Speed", &s.torrent_speed_on, "##torrent_speed", &s.torrent_speed, 1.0f,
                 5.0f, "%.2fx", "Multiplies Torrent's movement while you ride.");
    if (const char* status = cheats::TorrentStatus(); status && *status)
        ImGui::TextColored(kRed, "%s", status);

    ImGui::SeparatorText("Jump");
    ToggleSlider("Jump Boost", &s.jump_boost_on, "##jump_boost", &s.jump_boost, 1.0f, 5.0f, "%.1fx",
                 "Multiplies the rise of a jump (also Torrent's jumps while riding).");

    ImGui::SeparatorText("Fly mode (noclip)");
    ToggleSlider("Fly Mode", &s.fly_mode, "##fly_speed", &s.fly_speed, 1.0f, 10.0f, "%.1fx",
                 "No gravity and no collision: you float and go through walls. The speed multiplies "
                 "both rising/sinking and your normal running. Switching it off makes you fall "
                 "normally.");
    ImGui::TextDisabled("Rise: RT (R2), A (Cross) or Page Up    Sink: LT (L2) or Page Down    "
                        "Move: run as usual");
    ImGui::TextDisabled("Triggers are analog: press lightly to move slowly.");
    ImGui::TextDisabled("Turns itself off when you die or hit a loading screen.");

    ImGui::SeparatorText("Teleport");
    const bool in_game = game::InGame();
    if (!in_game) ImGui::BeginDisabled();
    if (ImGui::Button("Up +5 m")) game::NudgePosition(0, 5, 0);
    ImGui::SameLine();
    if (ImGui::Button("Up +50 m")) game::NudgePosition(0, 50, 0);
    ImGui::SameLine();
    if (ImGui::Button("Down -5 m")) game::NudgePosition(0, -5, 0);
    if (ImGui::Button("Save position")) hotkeys::SavePosition();
    ImGui::SameLine();
    const bool has_saved = hotkeys::HasSavedPosition();
    if (!has_saved) ImGui::BeginDisabled();
    if (ImGui::Button("Teleport to saved position")) hotkeys::LoadPosition();
    if (!has_saved) ImGui::EndDisabled();
    if (has_saved) {
        const auto& t = hotkeys::SavedPosition();
        ImGui::TextDisabled("Saved: X %.1f  Y %.1f  Z %.1f", t.global[0], t.global[1], t.global[2]);
    }
    if (!in_game) ImGui::EndDisabled();
}

// ---- World --------------------------------------------------------------------------------

void RevealMaps() {
    int set = 0;
    for (int i = 0; i < world::kMapFlagCount; ++i) set += game::SetEventFlag(world::kMapFlags[i], true);
    char text[160];
    std::snprintf(text, sizeof(text),
                  "Map flags set: %d/%d. Rest at a grace or reload the save if the map hasn't "
                  "updated yet.",
                  set, world::kMapFlagCount);
    Notify(set ? text : "Could not set the map flags (are you in game?).");
    ZZ_LOG("RevealMaps: %d/%d flags verified", set, world::kMapFlagCount);
}

void UnlockGraces(int which) {  // 0 = base game, 1 = DLC
    int set = 0, total = 0;
    for (int i = 0; i < world::kGraceCount; ++i) {
        const auto& g = world::kGraces[i];
        if ((which == 1) != g.dlc) continue;
        ++total;
        set += game::SetEventFlag(g.flag, true);
    }
    char text[160];
    std::snprintf(text, sizeof(text),
                  "Sites of Grace unlocked: %d/%d. Rest at a grace or reload the save, then fast "
                  "travel from the map.",
                  set, total);
    Notify(set ? text : "Could not unlock Sites of Grace (are you in game?).");
    ZZ_LOG("UnlockGraces(%d): %d/%d flags verified", which, set, total);
}

void DrawWorldTab() {
    const bool in_game = game::InGame();
    if (!in_game && TestEnvInt(L"ZZ_UI_TEST_SHOWCASE") <= 0) ImGui::TextDisabled("Load a character first.");
    if (!in_game) ImGui::BeginDisabled();

    ImGui::SeparatorText("Map");
    if (ImGui::Button("Reveal the whole map")) RevealMaps();
    HelpMarker("Sets the same flags as picking up every map fragment (base game and Shadow of the "
               "Erdtree). Giving yourself the Map items alone does not reveal anything.");

    ImGui::SeparatorText("Sites of Grace");
    ImGui::TextWrapped("Unlocks Sites of Grace so you can fast travel to them from the map. "
                       "Some lie past bosses or story events, so this can skip progress.");
    int dlc = 0;
    for (int i = 0; i < world::kGraceCount; ++i) dlc += world::kGraces[i].dlc;
    char label[64];
    std::snprintf(label, sizeof(label), "Unlock base game (%d)", world::kGraceCount - dlc);
    if (ImGui::Button(label)) ImGui::OpenPopup("graces_base");
    ImGui::SameLine();
    std::snprintf(label, sizeof(label), "Unlock Shadow of the Erdtree (%d)", dlc);
    if (ImGui::Button(label)) ImGui::OpenPopup("graces_dlc");
    for (int which = 0; which < 2; ++which) {
        if (!ImGui::BeginPopupModal(which ? "graces_dlc" : "graces_base", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            continue;
        ImGui::Text("Unlock every %s Site of Grace?", which ? "Shadow of the Erdtree" : "base game");
        ImGui::TextDisabled("Your save is backed up first if automatic backups are on.");
        if (ImGui::Button("Unlock", ImVec2(140 * g_scale, 0))) {
            if (config::Get().auto_backup) saves::BackupActive(false);
            UnlockGraces(which);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140 * g_scale, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!in_game) ImGui::EndDisabled();
}

// ---- Character ----------------------------------------------------------------------------

void DrawCharacterTab(const game::CharacterInfo& ci, bool in_game) {
    if (!in_game) {
        ImGui::TextDisabled("Runes, stats and blessings show up here once a character is loaded.");
        g_attr_loaded = g_bless_loaded = false;
        return;
    }
    ImGui::SeparatorText("Runes");
    ImGui::Text("Current runes: %s", FormatNumber(ci.runes).c_str());
    const int64_t presets[] = {10000, 100000, 1000000, 10000000};
    const char* preset_labels[] = {"+10,000", "+100,000", "+1,000,000", "+10,000,000"};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(preset_labels[i])) {
            game::AddRunes(presets[i]);
            Notify(std::string(preset_labels[i]) + " runes");
        }
    }
    ImGui::SetNextItemWidth(160 * g_scale);
    ImGui::InputInt("##custom_runes", &g_custom_runes, 1000, 100000);
    g_custom_runes = std::clamp(g_custom_runes, 0, 999999999);
    ImGui::SameLine();
    if (ImGui::Button("Add")) game::AddRunes(g_custom_runes);
    ImGui::SameLine();
    if (ImGui::Button("Remove")) game::AddRunes(-static_cast<int64_t>(g_custom_runes));
    ImGui::SameLine();
    if (ImGui::Button("Set to 0")) game::AddRunes(-static_cast<int64_t>(ci.runes));

    ImGui::SeparatorText("Attributes");
    static const char* names[8] = {"Vigor",     "Mind",         "Endurance", "Strength",
                                   "Dexterity", "Intelligence", "Faith",     "Arcane"};
    if (!g_attr_loaded) {
        for (int i = 0; i < 8; ++i) g_attr_edit[i] = static_cast<int>(ci.attributes[i]);
        g_attr_loaded = true;
    }
    for (int i = 0; i < 8; ++i) {
        ImGui::SetNextItemWidth(140 * g_scale);
        ImGui::InputInt(names[i], &g_attr_edit[i], 1, 5);
        g_attr_edit[i] = std::clamp(g_attr_edit[i], 1, 99);
    }
    int sum = 0;
    for (int v : g_attr_edit) sum += v;
    ImGui::Text("Level: %u  ->  %d", ci.level, std::max(1, sum - 79));
    if (ImGui::Button("Apply attributes")) {
        uint32_t attrs[8];
        for (int i = 0; i < 8; ++i) attrs[i] = static_cast<uint32_t>(g_attr_edit[i]);
        Notify(game::WriteAttributes(attrs) ? "Attributes applied." : "Could not write attributes.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from game")) g_attr_loaded = false;
    ImGui::TextDisabled("Max HP/FP/stamina update after changing equipment or resting at a grace.");

    ImGui::SeparatorText("Shadow of the Erdtree blessings");
    if (!g_bless_loaded) {
        g_scadutree_edit = ci.scadutree;
        g_revered_edit = ci.revered_ash;
        g_bless_loaded = true;
    }
    ImGui::SetNextItemWidth(220 * g_scale);
    ImGui::SliderInt("Scadutree Blessing", &g_scadutree_edit, 0, 20);
    ImGui::SetNextItemWidth(220 * g_scale);
    ImGui::SliderInt("Revered Spirit Ash Blessing", &g_revered_edit, 0, 10);
    if (ImGui::Button("Apply blessings")) {
        bool ok = game::WriteBlessings(static_cast<uint8_t>(g_scadutree_edit),
                                       static_cast<uint8_t>(g_revered_edit));
        Notify(ok ? "Blessings applied." : "Could not write blessings.");
    }

    ImGui::SeparatorText("Status");
    ImGui::Text("HP %d / %d    FP %d / %d    Stamina %d / %d", ci.hp, ci.max_hp, ci.fp, ci.max_fp,
                ci.stamina, ci.max_stamina);
}

// ---- Shortcuts ----------------------------------------------------------------------------

void DrawShortcutsTab() {
    const pad::RawState raw = pad::ReadRaw();
    ImGui::TextWrapped(
        "Controller: L3 + R3 (click both sticks) opens/closes the menu. Shortcuts: hold L3 and "
        "press A/B/X/Y/RB/RT, or hold R3 and press the D-pad/LB/LT. A short L3 or R3 press on its "
        "own still reaches the game (crouch / lock-on).");
    ImGui::TextWrapped(
        "In the menu: D-pad / left stick to move, A to select, B to go back, LB/RB to switch tabs, "
        "Start to close. In fly mode RT rises and LT sinks.");
    ImGui::TextDisabled(
        "PlayStation: A=Cross, B=Circle, X=Square, Y=Triangle, LB/RB=L1/R1, LT/RT=L2/R2.");
    ImGui::Text("Controller: ");
    ImGui::SameLine(0, 0);
    if (!pad::Installed())
        ImGui::TextColored(kRed, "XInput not available");
    else if (raw.connected)
        ImGui::TextColored(kGreen, "connected");
    else
        ImGui::TextDisabled("not detected");

    const int capturing = hotkeys::CaptureAction();
    if (capturing >= 0) {
        ImGui::Spacing();
        ImGui::TextColored(kGold, "\"%s\": %s", hotkeys::GetAction(capturing).label,
                           hotkeys::CaptureController()
                               ? "hold L3 or R3 on the controller and press a button..."
                               : "press a key... (Esc: cancel, Backspace: clear)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) hotkeys::CancelCapture();
    }
    ImGui::Spacing();
    const float row_button = 220 * g_scale;
    if (ImGui::BeginTable("shortcuts", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 40 * g_scale))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < hotkeys::Count(); ++i) {
            const auto& action = hotkeys::GetAction(i);
            auto& binding = hotkeys::GetBinding(i);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (action.toggle && cheats::Get().*action.toggle)
                ImGui::TextColored(kGreen, "%s  (on)", action.label);
            else
                ImGui::TextUnformatted(action.label);

            ImGui::TableNextColumn();
            const bool cap_kb = capturing == i && !hotkeys::CaptureController();
            const std::string kb_label = cap_kb ? "...##kb" : hotkeys::KeyLabel(binding.key) + "##kb";
            if (ImGui::Button(kb_label.c_str(), ImVec2(row_button * 0.6f, 0)))
                hotkeys::BeginCapture(i, false);
            if (binding.key) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x##kbclear")) {
                    binding.key = 0;
                    hotkeys::Save();
                }
            }

            ImGui::TableNextColumn();
            const bool cap_pad = capturing == i && hotkeys::CaptureController();
            const std::string pad_label = cap_pad ? "...##pad" : pad::ComboName(binding.combo) + "##pad";
            if (ImGui::Button(pad_label.c_str(), ImVec2(row_button, 0))) hotkeys::BeginCapture(i, true);
            if (binding.combo.valid()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x##padclear")) {
                    binding.combo = {};
                    hotkeys::Save();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Restore default shortcuts")) {
        hotkeys::ResetDefaults();
        hotkeys::Save();
        Notify("Shortcuts restored to defaults.");
    }
}

// ---- Saves --------------------------------------------------------------------------------

void OpenFolder(const std::wstring& file) {
    std::wstring folder = file.substr(0, file.find_last_of(L"\\/"));
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DrawSavesTab(const game::CharacterInfo& ci, bool in_game) {
    ImGui::TextWrapped(
        "Items and cheats apply to the character that is loaded in the game right now. To give "
        "items to another character, load that character. The game writes the save itself, so "
        "normal (.sl2) and Seamless Coop (.%s) saves behave the same.",
        saves::SeamlessLoaded() ? saves::ActiveExtension().c_str() : "co2");
    ImGui::Text("Save type this session: ");
    ImGui::SameLine(0, 0);
    ImGui::TextColored(kGold, "%s", saves::SeamlessLoaded() ? "Seamless Coop" : "Normal (vanilla)");
    if (ImGui::Button("Refresh") || !g_saves_scanned) {
        g_saves = saves::Scan();
        g_saves_scanned = true;
    }
    ImGui::Spacing();
    if (g_saves.empty()) ImGui::TextDisabled("No save files found (%%APPDATA%%\\EldenRing).");

    std::string current_steam;
    for (const auto& save : g_saves) {
        ImGui::PushID(save.file_name.c_str());
        ImGui::PushID(save.steam_id.c_str());
        if (save.steam_id != current_steam) {
            current_steam = save.steam_id;
            ImGui::SeparatorText(("Steam account " + save.steam_id).c_str());
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kGold, "%s", save.file_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  last saved %s", save.seamless ? "Seamless Coop" : "Normal",
                            save.modified.c_str());
        if (save.active) {
            ImGui::SameLine();
            ImGui::TextColored(kGreen, "[IN USE]");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Back up")) {
            std::wstring dest = saves::Backup(save);
            Notify(dest.empty() ? "Backup failed." : "Backup written to zzostoom_backups.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open folder")) OpenFolder(save.path);

        if (!save.error.empty()) {
            ImGui::TextColored(kRed, "Unreadable: %s", save.error.c_str());
        } else if (save.characters.empty()) {
            ImGui::TextDisabled("No characters in this save.");
        } else if (ImGui::BeginTable("chars", 4,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                         ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 50 * g_scale);
            ImGui::TableSetupColumn("Character");
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 70 * g_scale);
            ImGui::TableSetupColumn("Play time");
            ImGui::TableHeadersRow();
            for (const auto& c : save.characters) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", c.slot + 1);
                ImGui::TableNextColumn();
                if (in_game && save.active && c.name == ci.name)
                    ImGui::TextColored(kGreen, "%s  (loaded)", c.name.c_str());
                else
                    ImGui::TextUnformatted(c.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", c.level);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatPlaytime(c.seconds_played).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::SeparatorText("About backups");
    ImGui::TextWrapped(
        "Backups go to the 'zzostoom_backups' folder next to the save, named with date and time. "
        "To restore one, close the game, copy it back into the save folder and rename it to the "
        "original file name (e.g. ER0000.%s).",
        saves::ActiveExtension().c_str());
}

// ---- Settings -----------------------------------------------------------------------------

void DrawSettingsTab() {
    auto& cfg = config::Get();
    static const char* keys[] = {"UNDER_ESC", "INSERT", "HOME", "END", "DELETE", "PAGEUP", "PAGEDOWN",
                                 "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
                                 "F10", "F11", "F12", "PAUSE", "SCROLLLOCK"};
    ImGui::SeparatorText("Menu");
    ImGui::SetNextItemWidth(180 * g_scale);
    auto key_label = [](const std::string& name) {
        return name == "UNDER_ESC" ? std::string("\" / ~  (key under Esc)") : name;
    };
    if (ImGui::BeginCombo("Menu key (keyboard)", key_label(cfg.toggle_key_name).c_str())) {
        for (const char* k : keys) {
            if (ImGui::Selectable(key_label(k).c_str(), cfg.toggle_key_name == k)) {
                cfg.toggle_key = config::KeyFromName(k);
                cfg.toggle_key_name = k;
                config::Save();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Controller: L3 + R3");
    float scale = ImGui::GetStyle().FontScaleMain;
    ImGui::SetNextItemWidth(180 * g_scale);
    if (ImGui::SliderFloat("Text size", &scale, 0.75f, 2.5f, "%.2fx"))
        ImGui::GetStyle().FontScaleMain = scale;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        cfg.ui_scale = scale;
        config::Save();
    }
    if (ImGui::Checkbox("Block game keyboard/mouse input while the menu is open",
                        &cfg.block_game_input))
        config::Save();
    if (ImGui::Checkbox("Back up the save before the first item of a session", &cfg.auto_backup))
        config::Save();
    if (ImGui::Checkbox("Show a short hint when the game starts", &cfg.show_hint)) config::Save();

    ImGui::SeparatorText("About");
    ImGui::Text("zzostoom cheat sheet v%s", kModVersion);
    ImGui::Text("Game version (exe): %s", game::GameVersion().c_str());
    ImGui::Text("Memory addresses: ");
    ImGui::SameLine(0, 0);
    if (game::Ready())
        ImGui::TextColored(kGreen, "ready");
    else
        ImGui::TextColored(kRed, "missing (the game may have been updated)");
    ImGui::TextDisabled("%s", game::InitReport().c_str());
    ImGui::TextWrapped(
        "No Cheat Engine needed; loaded by Mod Engine 3 as a DLL. UI: Dear ImGui, hooks: MinHook. "
        "Game structures were checked against veeenu/eldenring-practice-tool and "
        "vswarte/fromsoftware-rs. Offline / Seamless Coop only; don't use it online.");
}

// ---- Overlay bits -------------------------------------------------------------------------

void DrawToasts() {
    std::lock_guard lock(g_toast_mutex);
    const double now = Now();
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(),
                                  [&](const Toast& t) { return t.expires < now; }),
                   g_toasts.end());
    if (g_toasts.empty()) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float pad = 10 * g_scale;
    float y = display.y - 40 * g_scale;
    for (auto it = g_toasts.rbegin(); it != g_toasts.rend(); ++it) {
        const ImVec2 size = ImGui::CalcTextSize(it->text.c_str());
        const ImVec2 max(display.x - 20 * g_scale, y);
        const ImVec2 min(max.x - size.x - pad * 2, y - size.y - pad * 2);
        dl->AddRectFilled(min, max, IM_COL32(16, 14, 10, 230), 6 * g_scale);
        dl->AddRect(min, max, IM_COL32(200, 165, 90, 200), 6 * g_scale);
        dl->AddText(ImVec2(min.x + pad, min.y + pad), IM_COL32(240, 230, 210, 255), it->text.c_str());
        y = min.y - 6 * g_scale;
    }
}

void DrawHint() {
    if (!config::Get().show_hint || Now() - g_start_time > 20.0) return;
    char text[128];
    const std::string& key = config::Get().toggle_key_name;
    std::snprintf(text, sizeof(text), "zzostoom cheat sheet ready  -  menu: %s / L3 + R3",
                  key == "UNDER_ESC" ? "\" (under Esc)" : key.c_str());
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 pos(18 * g_scale, 14 * g_scale);
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(pos, IM_COL32(235, 200, 110, 255), text);
}

}  // namespace

void SwitchTab(int delta) { g_tab_request = (g_current_tab + delta + kTabCount) % kTabCount; }

void OnMenuToggled(bool open) {
    if (open) g_focus_request = true;
}

void Notify(const std::string& text) {
    std::lock_guard lock(g_toast_mutex);
    g_toasts.push_back({text, Now() + 4.0});
    if (g_toasts.size() > 6) g_toasts.erase(g_toasts.begin());
    ZZ_LOG("Notify: %s", text.c_str());
}

void Init(float scale) {
    g_scale = scale;
    g_start_time = Now();

    g_items_by_sub.assign(db::kSubCategoryCount, {});
    g_subs_by_top.assign(db::kTopCategoryCount, {});
    for (int i = 0; i < db::kItemCount; ++i) g_items_by_sub[db::kItems[i].sub].push_back(i);
    for (int s = 0; s < db::kSubCategoryCount; ++s)
        g_subs_by_top[db::kSubCategories[s].top].push_back(s);

    ImGuiIO& io = ImGui::GetIO();
    wchar_t windir[MAX_PATH];
    GetWindowsDirectoryW(windir, MAX_PATH);
    std::wstring font_path = std::wstring(windir) + L"\\Fonts\\segoeui.ttf";
    char font_utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, font_path.c_str(), -1, font_utf8, sizeof(font_utf8), nullptr,
                        nullptr);
    ImFont* font = nullptr;
    if (GetFileAttributesW(font_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        font = io.Fonts->AddFontFromFileTTF(font_utf8, 18.0f);
    if (!font) io.Fonts->AddFontDefault();

    ApplyStyle();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontSizeBase = 18.0f;
    float saved = config::Get().ui_scale;
    style.FontScaleMain = saved > 0.0f ? saved : scale;
    ZZ_LOG("UI ready (%d items, %d categories, %d graces, font %s)", db::kItemCount,
           db::kSubCategoryCount, world::kGraceCount, font ? "segoeui" : "default");
}

void Update() {
    if (g_queue.empty()) return;
    if (!game::InGame()) {
        g_queue.clear();
        g_batch_done = g_batch_failed = 0;
        g_batch_label.clear();
        Notify("Could not give items: load a character first.");
        return;
    }
    static bool backup_checked = false;
    if (!backup_checked) {
        backup_checked = true;
        if (config::Get().auto_backup && !saves::BackupActive(false).empty())
            Notify("Save backed up first (zzostoom_backups).");
    }
    std::string last_label;
    uint32_t last_qty = 0;
    for (int n = 0; n < 3 && !g_queue.empty(); ++n) {
        GiveRequest r = g_queue.front();
        g_queue.pop_front();
        if (game::GiveItem(r.id, r.quantity)) {
            ++g_batch_done;
            last_label = r.label;
            last_qty = r.quantity;
        } else {
            ++g_batch_failed;
        }
    }
    if (g_queue.empty()) {
        if (g_batch_failed)
            Notify(std::to_string(g_batch_failed) + " item(s) could not be given (are you in game?).");
        if (g_batch_done == 1)
            Notify("Given: " + last_label + " x" + std::to_string(last_qty));
        else if (g_batch_done > 1)
            Notify(std::to_string(g_batch_done) + " items given" +
                   (g_batch_label.empty() ? "." : " (" + g_batch_label + ")."));
        g_batch_done = g_batch_failed = 0;
        g_batch_label.clear();
    }
}

void Draw(bool menu_open) {
    DrawToasts();
    if (!menu_open) {
        DrawHint();
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(1040 * g_scale, 720 * g_scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    if (g_focus_request) {
        ImGui::SetNextWindowFocus();
        g_focus_request = false;
    }
    bool open = true;
    if (ImGui::Begin("zzostoom cheat sheet###zz_main", &open, ImGuiWindowFlags_NoCollapse)) {
        game::CharacterInfo ci;
        const bool in_game = game::ReadCharacter(ci);
        if (TestEnvInt(L"ZZ_UI_TEST_SHOWCASE") > 0) {
            // Nexus screenshots: the harness has no game, so show a sample header instead of the warning.
            ImGui::TextColored(kGold, "Tarnished");
            ImGui::SameLine();
            ImGui::Text("  Level 150    Runes 1,250,000");
        } else if (!game::Ready()) {
            ImGui::TextColored(kRed, "Game addresses not found; the game may have been updated. "
                                     "See Settings > About.");
        } else if (in_game) {
            ImGui::TextColored(kGold, "%s", ci.name.c_str());
            ImGui::SameLine();
            ImGui::Text("  Level %u    Runes %s", ci.level, FormatNumber(ci.runes).c_str());
        } else {
            ImGui::TextDisabled("No character loaded: items and cheats work once you are in game.");
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 260 * g_scale);
        ImGui::TextDisabled("Save: ER0000.%s (%s)", saves::ActiveExtension().c_str(),
                            saves::SeamlessLoaded() ? "Seamless" : "Normal");
        ImGui::Separator();

        static int last_test_tab = -1;
        const int test_tab = TestEnvInt(L"ZZ_UI_TEST_TAB");
        int requested = g_tab_request;
        g_tab_request = -1;
        if (test_tab >= 0 && test_tab != last_test_tab) requested = test_tab;
        last_test_tab = test_tab;
        if (const int test_sub = TestEnvInt(L"ZZ_UI_TEST_SUB");
            test_sub >= 0 && test_sub < db::kSubCategoryCount && g_selected_sub != test_sub) {
            g_selected_sub = test_sub;
            g_selected_item = g_items_by_sub[test_sub].empty() ? -1 : g_items_by_sub[test_sub][0];
        }
        if (ImGui::BeginTabBar("tabs")) {
            for (int t = 0; t < kTabCount; ++t) {
                const ImGuiTabItemFlags flags =
                    requested == t ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                if (!ImGui::BeginTabItem(kTabs[t], nullptr, flags)) {
                    if (t == 6) g_saves_scanned = false;  // rescan next time the tab opens
                    continue;
                }
                g_current_tab = t;
                switch (t) {
                    case 0: DrawItemsTab(); break;
                    case 1: DrawCheatsTab(); break;
                    case 2: DrawMovementTab(); break;
                    case 3: DrawWorldTab(); break;
                    case 4: DrawCharacterTab(ci, in_game); break;
                    case 5: DrawShortcutsTab(); break;
                    case 6: DrawSavesTab(ci, in_game); break;
                    case 7: DrawSettingsTab(); break;
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!open) overlay::SetMenuOpen(false);
}

}  // namespace zz::ui
