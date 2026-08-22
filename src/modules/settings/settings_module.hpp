#ifndef MATERIAL_EVERYTHING_SETTINGS_MODULE_HPP
#define MATERIAL_EVERYTHING_SETTINGS_MODULE_HPP

#include <string>

namespace material_everything::settings {

enum class Theme { Light, Dark, Auto };
enum class TabStripPosition { Left, Right, Top, Bottom };
enum class LanguageMode { English, Cantonese, Bilingual };

struct AccentColour final {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
};

struct FontSelection final {
    std::string family{"Segoe UI Variable"};
    int size = 14;
    int weight = 400;
};

struct FunnyLevel final {
    int english_level = 5;
    int cantonese_level = 5;
};

struct NotificationPreferences final {
    bool enabled = true;
    bool show_success_messages = true;
    bool show_progress_messages = true;
    bool persist_dismissed_history = true;
    int corner_timeout_seconds = 6;
};

struct CommandPaletteConfiguration final {
    std::string activation_shortcut{"Ctrl+Shift+F"};
    bool rich_inline_controls = true;
    bool bounded_card_view = true;
};

struct SettingsState final {
    Theme theme = Theme::Auto;
    AccentColour accent_colour{};
    double density = 1.0;
    FontSelection font_selection{};
    TabStripPosition tab_strip_position = TabStripPosition::Left;
    LanguageMode language_mode = LanguageMode::English;
    FunnyLevel funny_level{};
    bool school_mode_enabled = false;
    std::string school_mode_name{"School mode"};
    NotificationPreferences notifications{};
    CommandPaletteConfiguration command_palette{};
};

class SettingsModule final {
public:
    [[nodiscard]] static SettingsModule load();
    void save() const;
    void reset_to_defaults();
    bool export_settings(const std::string& destination) const;
    bool import_settings(const std::string& source);

    [[nodiscard]] const SettingsState& state() const noexcept;
    SettingsState& mutable_state() noexcept;
    void apply_state(const SettingsState& next_state);

private:
    explicit SettingsModule(std::string configuration_path);
    SettingsState state_{};
    std::string configuration_path_;
};

} // namespace material_everything::settings

#endif // MATERIAL_EVERYTHING_SETTINGS_MODULE_HPP
