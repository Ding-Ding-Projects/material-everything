#pragma once

#include "audio_editor.hpp"

#include <string>

namespace material_everything::audio_editor {

// Material Design 3 waveform surface and toolbar contract for the unified desktop shell.
struct AudioEditorUiState {
    bool toolbar_visible = true;
    bool waveform_visible = true;
    std::string active_track_name;
    double zoom = 1.0;
    AudioSelection selection;
};

class AudioEditorUi {
public:
    explicit AudioEditorUi(AudioEditor& editor) : editor_(editor) {}
    AudioEditorUiState& state() { return state_; }
    bool open_file(const std::string& path);
    bool export_file(const std::string& path, bool mix);
    void request_cut();
    void request_copy();
    bool request_paste();
    void request_delete();
    void request_fade(bool fade_in, double seconds);
    void request_amplify(double gain);
    void request_normalize();
    void request_noise_reduction();

private:
    AudioEditor& editor_;
    AudioEditorUiState state_;
};

}  // namespace material_everything::audio_editor
