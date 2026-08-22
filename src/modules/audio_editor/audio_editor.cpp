#include "audio_editor.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace material_everything::audio_editor {
namespace {

constexpr std::array<unsigned char, 4> kRiff = {'R', 'I', 'F', 'F'};
constexpr std::array<unsigned char, 4> kWave = {'W', 'A', 'V', 'E'};
constexpr std::array<unsigned char, 4> kFmt = {'f', 'm', 't', ' '};
constexpr std::array<unsigned char, 4> kData = {'d', 'a', 't', 'a'};

std::uint16_t read_u16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t read_u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u16(std::ofstream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void write_u32(std::ofstream& out, std::uint32_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>((value >> 16) & 0xff));
    out.put(static_cast<char>((value >> 24) & 0xff));
}

void write_tag(std::ofstream& out, const std::array<unsigned char, 4>& tag) {
    for (unsigned char c : tag) out.put(static_cast<char>(c));
}

double clamp_sample(double value) { return std::clamp(value, -1.0, 1.0); }

class NativeWavBackend final : public DspBackend {
public:
    AudioClip load(const std::string& path) override {
        AudioClip clip;
        clip.name = path;
        std::ifstream in(path, std::ios::binary);
        if (!in) return clip;

        unsigned char header[12];
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        if (in.gcount() != sizeof(header) || !std::equal(header, header + 4, kRiff.begin()) ||
            !std::equal(header + 8, header + 12, kWave.begin()))
            return clip;

        bool fmt_found = false;
        while (in) {
            unsigned char chunk[8];
            in.read(reinterpret_cast<char*>(chunk), sizeof(chunk));
            if (in.gcount() != sizeof(chunk)) break;
            const std::uint32_t size = read_u32(chunk + 4);
            if (std::equal(chunk, chunk + 4, kFmt.begin()) && size >= 16) {
                unsigned char fmt[16];
                in.read(reinterpret_cast<char*>(fmt), sizeof(fmt));
                if (in.gcount() != sizeof(fmt)) break;
                clip.channels = read_u16(fmt + 2);
                clip.sample_rate = read_u32(fmt + 4);
                const std::uint16_t bits = read_u16(fmt + 14);
                if (clip.channels == 0 || (bits != 16 && bits != 24 && bits != 32)) {
                    clip.channels = 0;
                    break;
                }
                fmt_found = true;
                if (size > 16) in.seekg(static_cast<std::streamoff>(size - 16), std::ios::cur);
            } else if (std::equal(chunk, chunk + 4, kData.begin()) && fmt_found) {
                const std::uint32_t bytes = size;
                std::vector<unsigned char> bytes_buffer(bytes);
                in.read(reinterpret_cast<char*>(bytes_buffer.data()), static_cast<std::streamsize>(bytes));
                const std::size_t read = static_cast<std::size_t>(in.gcount());
                clip.samples.reserve(read / 4);
                for (std::size_t i = 0; i + 2 < read; i += 2) {
                    const auto raw = static_cast<std::int16_t>(read_u16(bytes_buffer.data() + i));
                    clip.samples.push_back(static_cast<float>(raw) / 32768.0f);
                }
                break;
            } else {
                in.seekg(static_cast<std::streamoff>(size), std::ios::cur);
            }
        }
        return clip;
    }

    bool save(const std::string& path, const AudioClip& clip) override {
        if (clip.channels == 0 || clip.sample_rate == 0 || clip.samples.empty()) return false;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::uint32_t data_bytes = static_cast<std::uint32_t>(clip.samples.size() * 2);
        write_tag(out, kRiff);
        write_u32(out, 36 + data_bytes);
        write_tag(out, kWave);
        write_tag(out, kFmt);
        write_u32(out, 16);
        write_u16(out, 1);
        write_u16(out, clip.channels);
        write_u32(out, clip.sample_rate);
        write_u32(out, clip.sample_rate * clip.channels * 2);
        write_u16(out, static_cast<std::uint16_t>(clip.channels * 2));
        write_u16(out, 16);
        write_tag(out, kData);
        write_u32(out, data_bytes);
        for (float sample : clip.samples) {
            const auto raw = static_cast<std::int16_t>(std::lround(clamp_sample(sample) * 32767.0));
            write_u16(out, static_cast<std::uint16_t>(raw));
        }
        return static_cast<bool>(out);
    }

    void amplify(AudioClip& clip, double gain, const AudioSelection& selection) const override {
        if (gain <= 0.0) return;
        const std::size_t begin = std::min(selection.begin_frame * clip.channels, clip.samples.size());
        const std::size_t end = std::min(selection.active() ? selection.end_frame * clip.channels : clip.samples.size(), clip.samples.size());
        for (std::size_t i = begin; i < end; ++i) clip.samples[i] = static_cast<float>(clamp_sample(clip.samples[i] * gain));
    }

    void normalize(AudioClip& clip, double target_peak, const AudioSelection& selection) const override {
        const std::size_t begin = std::min(selection.begin_frame * clip.channels, clip.samples.size());
        const std::size_t end = std::min(selection.active() ? selection.end_frame * clip.channels : clip.samples.size(), clip.samples.size());
        if (begin >= end) return;
        float peak = 0.0f;
        for (std::size_t i = begin; i < end; ++i) peak = std::max(peak, std::abs(clip.samples[i]));
        if (peak <= std::numeric_limits<float>::epsilon()) return;
        amplify(clip, target_peak / peak, selection);
    }

    void fade(AudioClip& clip, bool fade_in, double seconds, const AudioSelection& selection) const override {
        if (seconds <= 0.0) return;
        const std::size_t begin = std::min(selection.begin_frame * clip.channels, clip.samples.size());
        const std::size_t end = std::min(selection.active() ? selection.end_frame * clip.channels : clip.samples.size(), clip.samples.size());
        const std::size_t frames = (end - begin) / std::max<std::size_t>(clip.channels, 1);
        const double rate = clip.sample_rate == 0 ? 44100.0 : clip.sample_rate;
        const std::size_t fade_frames = std::min(frames, static_cast<std::size_t>(seconds * rate));
        for (std::size_t frame = 0; frame < fade_frames; ++frame) {
            const double factor = fade_in ? static_cast<double>(frame) / fade_frames : 1.0 - static_cast<double>(frame) / fade_frames;
            for (std::uint16_t channel = 0; channel < clip.channels; ++channel)
                clip.samples[begin + frame * clip.channels + channel] = static_cast<float>(clip.samples[begin + frame * clip.channels + channel] * factor);
        }
    }

    void reduce_noise(AudioClip& clip, const AudioSelection& selection) const override {
        // Pluggable placeholder: conservative one-pole spectral smoothing until a dedicated DSP adapter is installed.
        const std::size_t begin = std::min(selection.begin_frame * clip.channels, clip.samples.size());
        const std::size_t end = std::min(selection.active() ? selection.end_frame * clip.channels : clip.samples.size(), clip.samples.size());
        for (std::uint16_t channel = 0; channel < clip.channels; ++channel) {
            float previous = 0.0f;
            for (std::size_t i = begin + channel; i < end; i += clip.channels) {
                previous = 0.85f * previous + 0.15f * clip.samples[i];
                clip.samples[i] = previous;
            }
        }
    }
};

}  // namespace

AudioEditor::AudioEditor(std::unique_ptr<DspBackend> backend) : backend_(std::move(backend)) {
    if (!backend_) backend_ = std::make_unique<NativeWavBackend>();
}

AudioClip* AudioEditor::track(std::size_t index) {
    if (index >= tracks_.size()) {
        last_error_ = "Track index out of range";
        return nullptr;
    }
    return &tracks_[index];
}

bool AudioEditor::open(const std::string& path) {
    AudioClip clip = backend_->load(path);
    if (clip.channels == 0 || clip.samples.empty()) {
        last_error_ = "Unsupported or empty audio file";
        return false;
    }
    tracks_.push_back(std::move(clip));
    last_error_.clear();
    return true;
}

bool AudioEditor::export_clip(const std::string& path, std::size_t track_index) {
    const AudioClip* clip = track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
    if (!clip) {
        last_error_ = "Track index out of range";
        return false;
    }
    const bool ok = backend_->save(path, *clip);
    last_error_ = ok ? std::string() : std::string("Export failed");
    return ok;
}

bool AudioEditor::export_mix(const std::string& path) { return backend_->save(path, mix_tracks()); }

AudioClip AudioEditor::mix_tracks() const {
    if (tracks_.empty()) return {};
    std::uint32_t rate = 0;
    std::uint16_t channels = 0;
    std::size_t frames = 0;
    for (const AudioClip& clip : tracks_) {
        rate = std::max(rate, clip.sample_rate);
        channels = std::max(channels, clip.channels);
        frames = std::max(frames, clip.frame_count());
    }
    AudioClip mixed;
    mixed.name = "Material Everything Mix";
    mixed.sample_rate = rate;
    mixed.channels = channels;
    mixed.samples.assign(frames * channels, 0.0f);
    for (const AudioClip& clip : tracks_) {
        for (std::size_t frame = 0; frame < clip.frame_count(); ++frame)
            for (std::uint16_t channel = 0; channel < clip.channels; ++channel)
                mixed.samples[frame * channels + channel] += clip.samples[frame * clip.channels + channel];
    }
    for (float& sample : mixed.samples) sample = static_cast<float>(clamp_sample(sample));
    return mixed;
}

void AudioEditor::cut(const AudioSelection& selection, std::size_t track_index) {
    copy(selection, track_index);
    erase(selection, track_index);
}

void AudioEditor::copy(const AudioSelection& selection, std::size_t track_index) {
    AudioClip* clip = track(track_index);
    if (!clip || !selection.active()) return;
    const std::size_t begin = std::min(selection.begin_frame, clip->frame_count());
    const std::size_t end = std::min(selection.end_frame, clip->frame_count());
    if (begin >= end) return;
    clipboard_ = *clip;
    clipboard_.samples.assign(clip->samples.begin() + static_cast<std::ptrdiff_t>(begin * clip->channels),
                              clip->samples.begin() + static_cast<std::ptrdiff_t>(end * clip->channels));
    clipboard_.name = "Clipboard";
}

bool AudioEditor::paste(std::size_t frame, std::size_t track_index) {
    AudioClip* clip = track(track_index);
    if (!clip || clipboard_.samples.empty()) return false;
    frame = std::min(frame, clip->frame_count());
    clip->samples.insert(clip->samples.begin() + static_cast<std::ptrdiff_t>(frame * clip->channels),
                         clipboard_.samples.begin(), clipboard_.samples.end());
    return true;
}

void AudioEditor::erase(const AudioSelection& selection, std::size_t track_index) {
    AudioClip* clip = track(track_index);
    if (!clip || !selection.active()) return;
    const std::size_t begin = std::min(selection.begin_frame, clip->frame_count());
    const std::size_t end = std::min(selection.end_frame, clip->frame_count());
    if (begin >= end) return;
    clip->samples.erase(clip->samples.begin() + static_cast<std::ptrdiff_t>(begin * clip->channels),
                        clip->samples.begin() + static_cast<std::ptrdiff_t>(end * clip->channels));
}

void AudioEditor::amplify(double gain, const AudioSelection& selection, std::size_t track_index) {
    if (AudioClip* clip = track(track_index)) backend_->amplify(*clip, gain, selection);
}

void AudioEditor::normalize(double target_peak, const AudioSelection& selection, std::size_t track_index) {
    if (AudioClip* clip = track(track_index)) backend_->normalize(*clip, target_peak, selection);
}

void AudioEditor::fade_in(double seconds, const AudioSelection& selection, std::size_t track_index) {
    if (AudioClip* clip = track(track_index)) backend_->fade(*clip, true, seconds, selection);
}

void AudioEditor::fade_out(double seconds, const AudioSelection& selection, std::size_t track_index) {
    if (AudioClip* clip = track(track_index)) backend_->fade(*clip, false, seconds, selection);
}

void AudioEditor::reduce_noise(const AudioSelection& selection, std::size_t track_index) {
    if (AudioClip* clip = track(track_index)) backend_->reduce_noise(*clip, selection);
}

std::size_t AudioEditor::add_track(AudioClip clip) {
    tracks_.push_back(std::move(clip));
    return tracks_.size() - 1;
}

}  // namespace material_everything::audio_editor
