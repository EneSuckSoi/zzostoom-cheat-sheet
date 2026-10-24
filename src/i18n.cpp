#include "i18n.h"

#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "item_db.h"
#include "world_db.h"

namespace zz::i18n {

namespace {

const LangInfo kLanguages[kLangCount] = {
    {"en", "English", nullptr},
    {"de", "Deutsch", "deude"},
    {"fr", "Français", "frafr"},
    {"es", "Español", "spaes"},
    {"pt", "Português (Brasil)", "porbr"},
    {"ru", "Русский", "rusru"},
    {"ja", "日本語", "jpnjp"},
    {"ko", "한국어", "korkr"},
    {"zh", "简体中文", "zhocn"},
    {"tr", "Türkçe", nullptr},
};

std::mutex g_mutex;
int g_lang = kEnglish;
bool g_english_items = false;
const GameText* g_game = nullptr;  // game text of the current language, null = English
std::unordered_map<std::string_view, const char*> g_by_text;
std::unordered_map<const char*, const char*> g_by_pointer;  // cache: same literal, same answer

void Rebuild() {
    g_by_text.clear();
    g_by_pointer.clear();
    g_game = nullptr;
    if (const char* gt = kLanguages[g_lang].game_text)
        for (int i = 0; i < kGameTextCount; ++i)
            if (std::strcmp(kGameText[i].lang, gt) == 0) g_game = &kGameText[i];
    if (g_lang == kEnglish) return;
    for (int r = 0; r < kUiTextCount; ++r)
        if (const char* t = kUiText[r][g_lang]) g_by_text[kUiText[r][0]] = t;
    if (g_game)  // the game's own words win (weapon types, attributes, affinities...)
        for (int i = 0; i < kGameTermCount; ++i)
            if (g_game->terms[i]) g_by_text[kGameTermKeys[i]] = g_game->terms[i];
}

}  // namespace

const LangInfo& Info(int lang) { return kLanguages[(lang >= 0 && lang < kLangCount) ? lang : 0]; }

int Language() { return g_lang; }

void SetLanguage(int lang) {
    std::lock_guard lock(g_mutex);
    g_lang = (lang >= 0 && lang < kLangCount) ? lang : kEnglish;
    Rebuild();
}

int FromCode(const char* code) {
    for (int i = 0; i < kLangCount; ++i)
        if (std::strcmp(kLanguages[i].code, code) == 0) return i;
    return -1;
}

const char* T(const char* english) {
    if (!english || g_lang == kEnglish) return english;
    std::lock_guard lock(g_mutex);
    if (auto it = g_by_pointer.find(english); it != g_by_pointer.end()) return it->second;
    auto it = g_by_text.find(english);
    const char* out = it != g_by_text.end() ? it->second : english;
    g_by_pointer.emplace(english, out);
    return out;
}

void SetEnglishItemNames(bool on) { g_english_items = on; }

const char* ItemName(int item) {
    if (g_game && !g_english_items && g_game->items[item]) return g_game->items[item];
    return db::kItems[item].name;
}

const char* ItemInfo(int item) {
    const char* en = db::kItems[item].info;
    if (!en) return nullptr;
    if (g_game && !g_english_items && g_game->infos[item]) return g_game->infos[item];
    return en;
}

const char* GraceName(int grace) {
    if (g_game && !g_english_items && g_game->graces[grace]) return g_game->graces[grace];
    return world::kGraces[grace].name;
}

}  // namespace zz::i18n
