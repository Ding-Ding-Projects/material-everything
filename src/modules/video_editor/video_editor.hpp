#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace material_everything::video_editor {

using Milliseconds = std::chrono::milliseconds;

enum class TransitionType {
    None,
    Crossfade,
    Wipe,
    Slide,
};

struct Clip {
    std::string id;
    std::string media_path;
    Milliseconds source_start{0};
    Milliseconds duration{0};
    TransitionType transition_in{TransitionType::None};
    Milliseconds transition_duration{500};
};

struct TextOverlay {
    std::string id;
    std::string text;
    Milliseconds start{0};
    Milliseconds end{1000};
    double x_ratio = 0.5;
    double y_ratio = 0.9;
    int font_size = 36;
};

struct ExportSettings {
    std::string output_path;
    int width = 1280;
    int height = 720;
    int fps = 30;
    std::string video_codec = "libx264";
    std::string audio_codec = "aac";
    int video_bitrate_kbps = 4000;
};

class Timeline {
public:
    void add_clip(Clip clip);
    bool remove_clip(const std::string& clip_id);
    bool split_clip(const std::string& clip_id, Milliseconds timeline_offset);
    bool trim_clip(const std::string& clip_id, Milliseconds new_source_start,
                   Milliseconds new_duration);
    bool set_transition(const std::string& clip_id, TransitionType type,
                        Milliseconds duration);

    const std::vector<Clip>& clips() const noexcept { return clips_; }
    Milliseconds total_duration() const noexcept { return total_duration_; }
    std::size_t find_clip_at(Milliseconds timeline_offset) const;
    std::string source_for_offset(Milliseconds timeline_offset) const;

private:
    std::vector<Clip>::iterator locate(const std::string& clip_id);
    void recompute();

    std::vector<Clip> clips_;
    Milliseconds total_duration_{0};
};

class VideoEditorModule {
public:
    VideoEditorModule();
    ~VideoEditorModule();

    Timeline& timeline() noexcept { return timeline_; }
    const Timeline& timeline() const noexcept { return timeline_; }

    std::vector<TextOverlay>& overlays() noexcept { return overlays_; }
    const std::vector<TextOverlay>& overlays() const noexcept { return overlays_; }
    bool add_text_overlay(TextOverlay overlay);
    bool remove_text_overlay(const std::string& overlay_id);

    struct PreviewFrame {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
        Milliseconds presentation_time{0};
    };

    // Renders a placeholder frame for the scrub position. The production
    // decoder plugs into this seam without changing the module API.
    PreviewFrame render_preview(Milliseconds timeline_offset, int width, int height) const;

    enum class ExportResult {
        Success,
        EmptyTimeline,
        InvalidSettings,
        FfmpegMissing,
        FfmpegFailed,
    };

    ExportResult export_timeline(const ExportSettings& settings) const;
    static const char* export_error(ExportResult result) noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
    Timeline timeline_;
    std::vector<TextOverlay> overlays_;
};

} // namespace material_everything::video_editor
