#include "video_editor.hpp"

#include <algorithm>

namespace material_everything::video_editor {

VideoEditorModule::PreviewFrame VideoEditorModule::render_preview(
    Milliseconds timeline_offset, int width, int height) const
{
    PreviewFrame frame;
    frame.width = std::max(1, width);
    frame.height = std::max(1, height);
    frame.presentation_time = timeline_offset;
    frame.rgba.resize(static_cast<std::size_t>(frame.width * frame.height * 4U));

    // Deterministic scrub feedback: a Material 3 primary gradient with a
    // playhead stripe. A decoder adapter replaces this in production while
    // keeping the API and layout unchanged.
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            auto& pixel = *reinterpret_cast<unsigned*>(&frame.rgba[static_cast<std::size_t>(
                (y * frame.width + x) * 4U)]);
            const unsigned t = static_cast<unsigned>(x * 255U / frame.width);
            pixel = 0xFF000000U | (t << 16U) | (t / 2U << 8U) |
                    static_cast<unsigned>(t / 3U + 40U);
        }
    }
    const int playhead_x =
        static_cast<int>((static_cast<double>(timeline_offset.count()) /
                          static_cast<double>(std::max<Milliseconds::rep>(1, timeline_.total_duration().count()))) *
                         static_cast<double>(frame.width));
    if (playhead_x >= 0 && playhead_x < frame.width) {
        for (int y = 0; y < frame.height; ++y) {
            auto& pixel = *reinterpret_cast<unsigned*>(&frame.rgba[static_cast<std::size_t>(
                (y * frame.width + playhead_x) * 4U)]);
            pixel = 0xFF6750A4U; // M3 primary purple.
        }
    }
    return frame;
}

} // namespace material_everything::video_editor
