#include "video_editor.hpp"

#include <algorithm>

namespace material_everything::video_editor {

VideoEditorModule::VideoEditorModule() : implementation_(std::make_unique<Implementation>()) {}
VideoEditorModule::~VideoEditorModule() = default;

bool VideoEditorModule::add_text_overlay(TextOverlay overlay)
{
    if (overlay.id.empty() || overlay.text.empty() || overlay.end <= overlay.start) {
        return false;
    }
    if (std::any_of(overlays_.begin(), overlays_.end(),
                    [&](const TextOverlay& item) { return item.id == overlay.id; })) {
        return false;
    }
    overlays_.push_back(std::move(overlay));
    return true;
}

bool VideoEditorModule::remove_text_overlay(const std::string& overlay_id)
{
    const auto original_size = overlays_.size();
    overlays_.erase(std::remove_if(overlays_.begin(), overlays_.end(),
                                   [&](const TextOverlay& item) {
                                       return item.id == overlay_id;
                                   }),
                    overlays_.end());
    return overlays_.size() != original_size;
}

} // namespace material_everything::video_editor
