#include "Module/Modules/Aliases.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "SDK/Types.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<void(__fastcall*)(void*, const void*)> g_executeCommand;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_layout{true};

std::mutex g_rulesMutex;
std::vector<std::pair<std::string, std::string>> g_rules;

struct KeyCap {
    uint32_t cyrillic;
    char latin;
};

constexpr KeyCap kKeyCaps[] = {
    {0x0439, 'q'}, {0x0446, 'w'},  {0x0443, 'e'}, {0x043A, 'r'}, {0x0435, 't'},
    {0x043D, 'y'}, {0x0433, 'u'},  {0x0448, 'i'}, {0x0449, 'o'}, {0x0437, 'p'},
    {0x0445, '['}, {0x044A, ']'},  {0x0444, 'a'}, {0x044B, 's'}, {0x0432, 'd'},
    {0x0430, 'f'}, {0x043F, 'g'},  {0x0440, 'h'}, {0x043E, 'j'}, {0x043B, 'k'},
    {0x0434, 'l'}, {0x0436, ';'},  {0x044D, '\''}, {0x044F, 'z'}, {0x0447, 'x'},
    {0x0441, 'c'}, {0x043C, 'v'},  {0x0438, 'b'}, {0x0442, 'n'}, {0x044C, 'm'},
    {0x0431, ','}, {0x044E, '.'},  {0x0451, '`'},
};

char latinFor(uint32_t codepoint) {
    if (codepoint >= 0x0410 && codepoint <= 0x042F)
        codepoint += 0x20;
    if (codepoint == 0x0401)
        codepoint = 0x0451;

    for (const KeyCap& cap : kKeyCaps) {
        if (cap.cyrillic == codepoint)
            return cap.latin;
    }
    return 0;
}

std::string retype(std::string_view word) {
    std::string out;
    out.reserve(word.size());

    for (size_t i = 0; i < word.size();) {
        const auto lead = static_cast<uint8_t>(word[i]);
        if (lead < 0x80) {
            out.push_back(word[i]);
            ++i;
            continue;
        }

        if ((lead & 0xE0) != 0xC0 || i + 1 >= word.size())
            return {};

        const uint32_t codepoint = ((lead & 0x1Fu) << 6) |
                                   (static_cast<uint8_t>(word[i + 1]) & 0x3Fu);
        i += 2;

        const char latin = latinFor(codepoint);
        if (!latin)
            return {};

        out.push_back(latin);
    }

    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    if (!text.empty() && text.front() == '/')
        text.remove_prefix(1);
    return text;
}

bool sameCommand(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i) {
        char left = a[i];
        char right = b[i];
        if (left >= 'A' && left <= 'Z')
            left = static_cast<char>(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z')
            right = static_cast<char>(right + ('a' - 'A'));
        if (left != right)
            return false;
    }
    return true;
}

bool sameRules(const std::vector<ListSetting::Entry>& entries) {
    std::lock_guard lock(g_rulesMutex);
    if (g_rules.size() > entries.size())
        return false;

    size_t seen = 0;
    for (const auto& entry : entries) {
        const std::string_view from = trim(entry.from);
        const std::string_view to = trim(entry.to);
        if (from.empty() || to.empty())
            continue;
        if (seen >= g_rules.size() || g_rules[seen].first != from || g_rules[seen].second != to)
            return false;
        ++seen;
    }
    return seen == g_rules.size();
}

void loadRules(const std::vector<ListSetting::Entry>& entries) {
    std::lock_guard lock(g_rulesMutex);
    g_rules.clear();

    for (const auto& entry : entries) {
        const std::string_view from = trim(entry.from);
        const std::string_view to = trim(entry.to);
        if (!from.empty() && !to.empty())
            g_rules.emplace_back(from, to);
    }
}

bool rewrite(std::string_view line, std::string& out) {
    if (line.size() < 2 || line.front() != '/')
        return false;

    const size_t space = line.find_first_of(" \t", 1);
    const std::string_view word =
        space == std::string_view::npos ? line.substr(1) : line.substr(1, space - 1);
    const std::string_view rest =
        space == std::string_view::npos ? std::string_view{} : line.substr(space);

    if (word.empty())
        return false;

    std::string replacement;
    {
        std::lock_guard lock(g_rulesMutex);
        for (const auto& rule : g_rules) {
            if (sameCommand(word, rule.first)) {
                replacement = rule.second;
                break;
            }
        }
    }

    if (replacement.empty() && g_layout.load(std::memory_order_relaxed))
        replacement = retype(word);

    if (replacement.empty() || replacement == word)
        return false;

    out.clear();
    out.reserve(1 + replacement.size() + rest.size());
    out.push_back('/');
    out.append(replacement);
    out.append(rest);
    return true;
}

void __fastcall onExecuteCommand(void* self, const void* command) {
    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_executeCommand.call(self, command);
        return;
    }

    const std::string_view line = sdk::gameString(command);

    std::string fixed;
    if (!rewrite(line, fixed)) {
        g_executeCommand.call(self, command);
        return;
    }

    LOG_INFO("Aliases", "sending '{}' instead of '{}'", fixed, line);
    g_executeCommand.call(self, &fixed);
}

bool install() {
    g_executeCommand.attach("MinecraftScreenModel::executeCommand",
                            memory::rva(func::MinecraftScreenModel_executeCommand),
                            &onExecuteCommand);
    return true;
}

const hooks::Installer g_installer{"Aliases", &install};

}

Aliases::Aliases()
    : Module("Aliases", "Fixes a command you typed on the wrong layout, or by your own shorthand",
             Category::Input) {
    m_rules = addList("Rules", "Typed command on the left, what to send on the right", "typed",
                      "send");

    m_layout = addBool("Layout fix", "Read a Cyrillic command off the keyboard as if it were Latin",
                       true);

    listen<Render2DEvent>(&Aliases::onRender);
}

void Aliases::onRender(Render2DEvent& event) {
    (void)event;

    g_layout.store(m_layout->value, std::memory_order_relaxed);

    if (!sameRules(m_rules->entries))
        loadRules(m_rules->entries);
}

void Aliases::onEnable() { g_enabled.store(true, std::memory_order_relaxed); }

void Aliases::onDisable() { g_enabled.store(false, std::memory_order_relaxed); }

}
