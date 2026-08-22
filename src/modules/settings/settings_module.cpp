#include "settings_module.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace material_everything::settings {
namespace {

constexpr std::string_view kDefaultConfigurationPath =
#ifdef _WIN32
    "C:\\ProgramData\\MaterialEverything\\settings.json";
#else
    "/var/lib/material-everything/settings.json";
#endif

std::string escape_json(const std::string& value)
{
    std::ostringstream escaped;
    for (unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20U) {
                constexpr char hex[] = "0123456789abcdef";
                escaped << "\\u00" << hex[character >> 4] << hex[character & 0x0FU];
            } else {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

std::string trim(std::string value)
{
    auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

template <typename Number>
std::optional<Number> parse_number(const std::string& text)
{
    Number number{};
    if (text.empty()) return std::nullopt;
    const auto* first = text.data();
    const auto result = std::from_chars(first, first + text.size(), number);
    if (result.ec == std::errc{} && result.ptr == first + text.size()) return number;
    return std::nullopt;
}

std::string decode_json_string(const std::string& raw)
{
    std::string decoded;
    for (std::size_t cursor = 0; cursor < raw.size(); ++cursor) {
        if (raw[cursor] != '\\') {
            decoded.push_back(raw[cursor]);
            continue;
        }
        if (++cursor >= raw.size()) break;
        switch (raw[cursor]) {
        case 'b': decoded.push_back('\b'); break;
        case 'f': decoded.push_back('\f'); break;
        case 'n': decoded.push_back('\n'); break;
        case 'r': decoded.push_back('\r'); break;
        case 't': decoded.push_back('\t'); break;
        case 'u': {
            if (cursor + 4 >= raw.size()) return {};
            unsigned int codepoint = 0;
            if (std::from_chars(raw.data() + cursor + 1, raw.data() + cursor + 5, codepoint, 16).ec != std::errc{})
                return {};
            if (codepoint <= 0x7FU) decoded.push_back(static_cast<char>(codepoint));
            else if (codepoint <= 0x7FFU) {
                decoded.push_back(static_cast<char>(0xC0U | (codepoint >> 6)));
                decoded.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
            } else {
                decoded.push_back(static_cast<char>(0xE0U | (codepoint >> 12)));
                decoded.push_back(static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU)));
                decoded.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
            }
            cursor += 4;
            break;
        }
        default: decoded.push_back(raw[cursor]); break;
        }
    }
    return decoded;
}

std::string string_member(const std::string& object, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const auto key_position = object.find(needle);
    if (key_position == std::string::npos) return {};
    const auto colon = object.find(':', key_position + needle.size());
    if (colon == std::string::npos) return {};
    const auto opening_quote = object.find('"', colon + 1);
    if (opening_quote == std::string::npos) return {};
    std::string raw;
    for (auto cursor = opening_quote + 1; cursor < object.size(); ++cursor) {
        if (object[cursor] == '\\') {
            raw.push_back(object[cursor]);
            if (++cursor < object.size()) raw.push_back(object[cursor]);
            continue;
        }
        if (object[cursor] == '"') break;
        raw.push_back(object[cursor]);
    }
    return decode_json_string(raw);
}

std::string nested_member(const std::string& object, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const auto key_position = object.find(needle);
    if (key_position == std::string::npos) return {};
    const auto colon = object.find(':', key_position + needle.size());
    if (colon == std::string::npos) return {};
    auto start = colon + 1;
    while (start < object.size() && std::isspace(static_cast<unsigned char>(object[start])) != 0) ++start;
    if (start >= object.size() || (object[start] != '{' && object[start] != '[')) return {};
    int depth = 0;
    bool inside_string = false;
    bool escaped = false;
    for (auto cursor = start; cursor < object.size(); ++cursor) {
        const char current = object[cursor];
        if (inside_string) {
            if (escaped) escaped = false;
            else if (current == '\\') escaped = true;
            else if (current == '"') inside_string = false;
            continue;
        }
        if (current == '"') inside_string = true;
        else if (current == '{' || current == '[') ++depth;
        else if (current == '}' || current == ']') {
            if (--depth == 0) return object.substr(start, cursor - start + 1);
        }
    }
    return {};
}

std::optional<int> integer_member(const std::string& object, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const auto position = object.find(needle);
    if (position == std::string::npos) return std::nullopt;
    const auto colon = object.find(':', position + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    auto end = colon + 1;
    while (end < object.size() && (std::isdigit(static_cast<unsigned char>(object[end])) != 0 ||
                                   object[end] == '-' || object[end] == '+')) ++end;
    return parse_number<int>(trim(object.substr(colon + 1, end - colon - 1)));
}

std::optional<double> real_member(const std::string& object, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const auto position = object.find(needle);
    if (position == std::string::npos) return std::nullopt;
    const auto colon = object.find(':', position + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    auto end = colon + 1;
    while (end < object.size() && (std::isdigit(static_cast<unsigned char>(object[end])) != 0 ||
                                   std::tolower(static_cast<unsigned char>(object[end])) == 'e' ||
                                   object[end] == '-' || object[end] == '+' || object[end] == '.')) ++end;
    const auto digits = trim(object.substr(colon + 1, end - colon - 1));
    try {
        return digits.empty() ? std::optional<double>{} : std::stod(digits);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> boolean_member(const std::string& object, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const auto position = object.find(needle);
    if (position == std::string::npos) return std::nullopt;
    const auto colon = object.find(':', position + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    const auto value = trim(object.substr(colon + 1));
    if (value.rfind("true", 0) == 0) return true;
    if (value.rfind("false", 0) == 0) return false;
    return std::nullopt;
}

SettingsState deserialize(const std::string& json)
{
    SettingsState state{};
    const std::string theme_text = string_member(json, "theme");
    if (theme_text == "light") state.theme = Theme::Light;
    else if (theme_text == "dark") state.theme = Theme::Dark;

    const std::string tab_text = string_member(json, "tab_strip_position");
    if (tab_text == "right") state.tab_strip_position = TabStripPosition::Right;
    else if (tab_text == "top") state.tab_strip_position = TabStripPosition::Top;
    else if (tab_text == "bottom") state.tab_strip_position = TabStripPosition::Bottom;

    const std::string language_text = string_member(json, "language_mode");
    if (language_text == "english") state.language_mode = LanguageMode::English;
    else if (language_text == "cantonese") state.language_mode = LanguageMode::Cantonese;
    else if (language_text == "bilingual") state.language_mode = LanguageMode::Bilingual;

    if (const auto accent = nested_member(json, "accent_colour"); !accent.empty()) {
        if (const auto red = real_member(accent, "red")) state.accent_colour.red = static_cast<float>(*red);
        if (const auto green = real_member(accent, "green")) state.accent_colour.green = static_cast<float>(*green);
        if (const auto blue = real_member(accent, "blue")) state.accent_colour.blue = static_cast<float>(*blue);
        if (const auto alpha = real_member(accent, "alpha")) state.accent_colour.alpha = static_cast<float>(*alpha);
    }
    if (const auto density = real_member(json, "density")) state.density = *density;
    if (const auto font = nested_member(json, "font_selection"); !font.empty()) {
        if (!string_member(font, "family").empty()) state.font_selection.family = string_member(font, "family");
        if (const auto size = integer_member(font, "size")) state.font_selection.size = *size;
        if (const auto weight = integer_member(font, "weight")) state.font_selection.weight = *weight;
    }
    if (const auto english = integer_member(json, "funny_english_level"))
        state.funny_level.english_level = *english;
    if (const auto cantonese = integer_member(json, "funny_cantonese_level"))
        state.funny_level.cantonese_level = *cantonese;
    if (const auto enabled = boolean_member(json, "school_mode_enabled")) state.school_mode_enabled = *enabled;
    if (!string_member(json, "school_mode_name").empty())
        state.school_mode_name = string_member(json, "school_mode_name");

    if (const auto notifications_object = nested_member(json, "notifications"); !notifications_object.empty()) {
        if (const auto enabled = boolean_member(notifications_object, "enabled"))
            state.notifications.enabled = *enabled;
        if (const auto success = boolean_member(notifications_object, "show_success_messages"))
            state.notifications.show_success_messages = *success;
        if (const auto progress = boolean_member(notifications_object, "show_progress_messages"))
            state.notifications.show_progress_messages = *progress;
        if (const auto history = boolean_member(notifications_object, "persist_dismissed_history"))
            state.notifications.persist_dismissed_history = *history;
        if (const auto timeout = integer_member(notifications_object, "corner_timeout_seconds"))
            state.notifications.corner_timeout_seconds = *timeout;
    }
    if (const auto palette_object = nested_member(json, "command_palette"); !palette_object.empty()) {
        if (!string_member(palette_object, "activation_shortcut").empty())
            state.command_palette.activation_shortcut = string_member(palette_object, "activation_shortcut");
        if (const auto rich = boolean_member(palette_object, "rich_inline_controls"))
            state.command_palette.rich_inline_controls = *rich;
        if (const auto card = boolean_member(palette_object, "bounded_card_view"))
            state.command_palette.bounded_card_view = *card;
    }
    return state;
}

void normalise(SettingsState& state)
{
    state.accent_colour.red = std::clamp(state.accent_colour.red, 0.0F, 1.0F);
    state.accent_colour.green = std::clamp(state.accent_colour.green, 0.0F, 1.0F);
    state.accent_colour.blue = std::clamp(state.accent_colour.blue, 0.0F, 1.0F);
    state.accent_colour.alpha = std::clamp(state.accent_colour.alpha, 0.0F, 1.0F);
    state.density = std::clamp(state.density, 0.75, 2.0);
    state.font_selection.size = std::clamp(state.font_selection.size, 8, 72);
    state.font_selection.weight = std::clamp(state.font_selection.weight, 100, 900);
    state.funny_level.english_level = std::clamp(state.funny_level.english_level, 1, 5);
    state.funny_level.cantonese_level = std::clamp(state.funny_level.cantonese_level, 1, 5);
    state.notifications.corner_timeout_seconds = std::max(1, state.notifications.corner_timeout_seconds);
    state.school_mode_name = state.school_mode_name.empty() ? "School mode" : state.school_mode_name;
    state.command_palette.activation_shortcut = state.command_palette.activation_shortcut.empty()
                                                    ? "Ctrl+Shift+F"
                                                    : state.command_palette.activation_shortcut;
}

} // namespace

SettingsModule::SettingsModule(std::string configuration_path)
    : configuration_path_(std::move(configuration_path))
{
}

SettingsModule SettingsModule::load()
{
    return SettingsModule{std::string(kDefaultConfigurationPath)};
}

void SettingsModule::save() const
{
    namespace fs = std::filesystem;
    const fs::path target(configuration_path_);
    std::error_code directory_error;
    fs::create_directories(target.parent_path(), directory_error);

    std::ostringstream json;
    const auto theme = [theme_value = state_.theme]() {
        if (theme_value == Theme::Light) return "light";
        if (theme_value == Theme::Dark) return "dark";
        return "auto";
    }();
    const auto tabs = [position = state_.tab_strip_position]() {
        if (position == TabStripPosition::Right) return "right";
        if (position == TabStripPosition::Top) return "top";
        if (position == TabStripPosition::Bottom) return "bottom";
        return "left";
    }();
    const auto language = [mode = state_.language_mode]() {
        if (mode == LanguageMode::English) return "english";
        if (mode == LanguageMode::Cantonese) return "cantonese";
        return "bilingual";
    }();

    json << "{\n  \"theme\":\"" << theme << "\",\n  \"accent_colour\":{"
         << "\"red\":" << state_.accent_colour.red << ",\"green\":" << state_.accent_colour.green
         << ",\"blue\":" << state_.accent_colour.blue << ",\"alpha\":" << state_.accent_colour.alpha
         << "},\n  \"density\":" << state_.density
         << ",\n  \"font_selection\":{\"family\":\"" << escape_json(state_.font_selection.family)
         << "\",\"size\":" << state_.font_selection.size << ",\"weight\":" << state_.font_selection.weight
         << "},\n  \"tab_strip_position\":\"" << tabs << "\",\n  \"language_mode\":\"" << language
         << "\",\n  \"funny_english_level\":" << state_.funny_level.english_level
         << ",\n  \"funny_cantonese_level\":" << state_.funny_level.cantonese_level
         << ",\n  \"school_mode_enabled\":" << (state_.school_mode_enabled ? "true" : "false")
         << ",\n  \"school_mode_name\":\"" << escape_json(state_.school_mode_name)
         << "\",\n  \"notifications\":{\"enabled\":" << (state_.notifications.enabled ? "true" : "false")
         << ",\"show_success_messages\":" << (state_.notifications.show_success_messages ? "true" : "false")
         << ",\"show_progress_messages\":" << (state_.notifications.show_progress_messages ? "true" : "false")
         << ",\"persist_dismissed_history\":"
         << (state_.notifications.persist_dismissed_history ? "true" : "false")
         << ",\"corner_timeout_seconds\":" << state_.notifications.corner_timeout_seconds
         << "},\n  \"command_palette\":{\"activation_shortcut\":\""
         << escape_json(state_.command_palette.activation_shortcut)
         << "\",\"rich_inline_controls\":" << (state_.command_palette.rich_inline_controls ? "true" : "false")
         << ",\"bounded_card_view\":" << (state_.command_palette.bounded_card_view ? "true" : "false")
         << "}\n}\n";

    const fs::path temporary = target.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !(output.write(json.str().data(), static_cast<std::streamsize>(json.str().size()))) ||
        !(output.flush())) throw std::runtime_error("Unable to write temporary settings file");
    output.close();

    std::error_code rename_error;
    fs::rename(temporary, target, rename_error);
    if (rename_error) {
        std::error_code copy_error;
        fs::copy_file(temporary, target, fs::copy_options::overwrite_existing, copy_error);
        fs::remove(temporary);
        if (copy_error) throw std::runtime_error("Unable to replace settings file");
    }
}

void SettingsModule::reset_to_defaults()
{
    state_ = SettingsState{};
}

bool SettingsModule::export_settings(const std::string& destination) const
{
    try {
        save();
        std::filesystem::copy_file(
            configuration_path_, destination, std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

bool SettingsModule::import_settings(const std::string& source)
{
    try {
        std::ifstream input(source, std::ios::binary);
        if (!input) return false;
        std::ostringstream contents;
        contents << input.rdbuf();
        const std::string imported_json = contents.str();
        if (imported_json.find('{') == std::string::npos ||
            imported_json.find('}') == std::string::npos) return false;
        apply_state(deserialize(imported_json));
        save();
        return true;
    } catch (...) {
        return false;
    }
}

const SettingsState& SettingsModule::state() const noexcept
{
    return state_;
}

SettingsState& SettingsModule::mutable_state() noexcept
{
    return state_;
}

void SettingsModule::apply_state(const SettingsState& next_state)
{
    state_ = next_state;
    normalise(state_);
}

} // namespace material_everything::settings

