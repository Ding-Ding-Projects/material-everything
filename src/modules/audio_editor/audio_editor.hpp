#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace material_everything::audio_editor {

struct AudioClip {
    std::string name;
    std::uint32_t sample_rate = 44100;
    std::uint16_t channels = 1;
    std::vector<float> samples;

    std::size_t frame_count() const { return samples.size() / channels; }
    double duration_seconds() const {
        return sample_rate == 0 ? 0.0 : static_cast<double>(frame_count()) / sample_rate;
    }
};

struct AudioSelection {
    std::size_t begin_frame = 0;
    std::size_t end_frame = 0;
    bool active() const { return end_frame > begin_frame; }
};

class DspBackend {
public:
    virtual ~DspBackend() = default;
    virtual AudioClip load(const std::string& path) = 0;
    virtual bool save(const std::string& path, const AudioClip& clip) = 0;
    virtual void amplify(AudioClip& clip, double gain, const AudioSelection& selection) const = 0;
    virtual void normalize(AudioClip& clip, double target_peak, const AudioSelection& selection) const = 0;
    virtual void fade(AudioClip& clip, bool fade_in, double seconds, const AudioSelection& selection) const = 0;
    virtual void reduce_noise(AudioClip& clip, const AudioSelection& selection) const = 0;
};

class AudioEditor {
public:
    explicit AudioEditor(std::unique_ptr<DspBackend> backend);

    bool open(const std::string& path);
    bool export_clip(const std::string& path, std::size_t track = 0);
    bool export_mix(const std::string& path);

    void cut(const AudioSelection& selection, std::size_t track = 0);
    void copy(const AudioSelection& selection, std::size_t track = 0);
    bool paste(std::size_t frame, std::size_t track = 0);
    void erase(const AudioSelection& selection, std::size_t track = 0);

    void amplify(double gain, const AudioSelection& selection, std::size_t track = 0);
    void normalize(double target_peak, const AudioSelection& selection, std::size_t track = 0);
    void fade_in(double seconds, const AudioSelection& selection, std::size_t track = 0);
    void fade_out(double seconds, const AudioSelection& selection, std::size_t track = 0);
    void reduce_noise(const AudioSelection& selection, std::size_t track = 0);

    std::size_t add_track(AudioClip clip);
    std::vector<AudioClip>& tracks() { return tracks_; }
    const std::vector<AudioClip>& tracks() const { return tracks_; }
    const AudioClip& clipboard() const { return clipboard_; }
    const std::string& last_error() const { return last_error_; }

private:
    AudioClip mix_tracks() const;
    AudioClip* track(std::size_t index);
    std::unique_ptr<DspBackend> backend_;
    std::vector<AudioClip> tracks_;
    AudioClip clipboard_;
    std::string last_error_;
};

}  // namespace material_everything::audio_editor
