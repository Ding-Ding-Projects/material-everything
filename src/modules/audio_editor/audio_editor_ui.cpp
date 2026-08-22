#include "audio_editor_ui.hpp"

#include <algorithm>

namespace material_everything::audio_editor {

bool AudioEditorUi::open_file(const std::string& path) {
    const bool opened = editor_.open(path);
    if (opened && !editor_.tracks().empty()) state_.active_track_name = editor_.tracks().back().name;
    return opened;
}

bool AudioEditorUi::export_file(const std::string& path, bool mix) {
    return mix ? editor_.export_mix(path) : editor_.export_clip(path);
}

void AudioEditorUi::request_cut() { editor_.cut(state_.selection); }
void AudioEditorUi::request_copy() { editor_.copy(state_.selection); }
bool AudioEditorUi::request_paste() { return editor_.paste(state_.selection.begin_frame); }
void AudioEditorUi::request_delete() { editor_.erase(state_.selection); }

void AudioEditorUi::request_fade(bool fade_in, double seconds) {
    fade_in ? editor_.fade_in(seconds, state_.selection) : editor_.fade_out(seconds, state_.selection);
}

void AudioEditorUi::request_amplify(double gain) { editor_.amplify(gain, state_.selection); }
void AudioEditorUi::request_normalize() { editor_.normalize(0.98, state_.selection); }
void AudioEditorUi::request_noise_reduction() { editor_.reduce_noise(state_.selection); }

}  // namespace material_everything::audio_editor
