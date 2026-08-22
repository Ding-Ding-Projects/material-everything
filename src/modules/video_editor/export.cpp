#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "video_editor.hpp"

namespace material_everything::video_editor {

namespace {

std::string shell_quote(const std::string& value)
{
    std::string result{"'"};
    for (const char character : value) {
        result += character;
        if (character == '\'') {
            result += "\\'";
        }
    }
    return result + "'";
}

bool run_ffmpeg(const std::string& command)
{
    std::array<char, 128> buffer{};
    std::string output;
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) {
        return false;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output.append(buffer.data());
    }
    const int status = _pclose(pipe);
    return status == 0;
}

std::string build_drawtext_filter(const std::vector<TextOverlay>& overlays)
{
    std::ostringstream filter;
    bool first = true;
    for (const auto& overlay : overlays) {
        if (!first) {
            filter << ',';
        }
        first = false;
        const double seconds_start = overlay.start.count() / 1000.0;
        const double seconds_end = overlay.end.count() / 1000.0;
        filter << "drawtext=text='" << overlay.text
               << "':fontsize=" << overlay.font_size
               << ":fontcolor=white:x=(w-text_w)*" << overlay.x_ratio
               << ":y=(h-text_h)*" << overlay.y_ratio
               << ":enable='between(t," << seconds_start << ',' << seconds_end << ")'";
    }
    return filter.str();
}

} // namespace

VideoEditorModule::ExportResult VideoEditorModule::export_timeline(
    const ExportSettings& settings) const
{
    if (timeline_.clips().empty()) {
        return ExportResult::EmptyTimeline;
    }
    if (settings.output_path.empty() || settings.width <= 0 || settings.height <= 0 ||
        settings.fps <= 0 || settings.video_bitrate_kbps <= 0) {
        return ExportResult::InvalidSettings;
    }

    std::ostringstream command;
    command << "ffmpeg -y -hide_banner -loglevel error";
    for (const auto& clip : timeline_.clips()) {
        command << " -ss " << clip.source_start.count() / 1000.0
                << " -t " << clip.duration.count() / 1000.0
                << ' ' << shell_quote(clip.media_path);
    }
    command << " -filter_complex \"";
    for (std::size_t index = 0; index < timeline_.clips().size(); ++index) {
        if (index > 0) {
            command << ';';
        }
        command << '[' << index << ":v][" << index + 1U << ":a]concat=n="
                << timeline_.clips().size() << ":v=1:a=1[outv][outa]";
    }
    const auto drawtext = build_drawtext_filter(overlays_);
    if (!drawtext.empty()) {
        command << ';' << drawtext << "[finalv]";
        command << "\" -map \"[finalv]\" -map \"[outa]\"";
    } else {
        command << "\" -map \"[outv]\" -map \"[outa]\"";
    }
    command << " -s " << settings.width << 'x' << settings.height
            << " -r " << settings.fps
            << " -c:v " << settings.video_codec
            << " -b:v " << settings.video_bitrate_kbps << 'k'
            << " -c:a " << settings.audio_codec
            << ' ' << shell_quote(settings.output_path);

    return run_ffmpeg(command.str()) ? ExportResult::Success : ExportResult::FfmpegFailed;
}

const char* VideoEditorModule::export_error(ExportResult result) noexcept
{
    switch (result) {
    case ExportResult::Success:
        return "Export succeeded";
    case ExportResult::EmptyTimeline:
        return "Add at least one clip before exporting";
    case ExportResult::InvalidSettings:
        return "Output path and positive dimensions, frame rate, and bitrate are required";
    case ExportResult::FfmpegMissing:
        return "FFmpeg was not found";
    case ExportResult::FfmpegFailed:
        return "FFmpeg reported an export failure";
    }
    return "Unknown export state";
}

} // namespace material_everything::video_editor
