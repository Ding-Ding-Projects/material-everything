#include "video_editor.hpp"
#include <numeric>

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <numeric>

namespace material_everything::video_editor {

void Timeline::add_clip(Clip clip)
{
    if (clip.id.empty()) {
        throw std::invalid_argument("clip id cannot be empty");
    }
    if (clip.duration <= Milliseconds{0}) {
        throw std::invalid_argument("clip duration must be positive");
    }
    if (std::any_of(clips_.begin(), clips_.end(), [&](const Clip& existing) {
            return existing.id == clip.id;
        })) {
        throw std::invalid_argument("clip id already exists");
    }
    clips_.push_back(std::move(clip));
    recompute();
}

bool Timeline::remove_clip(const std::string& clip_id)
{
    auto it = locate(clip_id);
    if (it == clips_.end()) {
        return false;
    }
    clips_.erase(it);
    recompute();
    return true;
}

bool Timeline::split_clip(const std::string& clip_id, Milliseconds timeline_offset)
{
    for (auto it = clips_.begin(); it != clips_.end(); ++it) {
        const auto start = std::next(clips_.begin(), std::distance(clips_.begin(), it));
        (void)start;
        Milliseconds clip_start{0};
        for (auto prior = clips_.begin(); prior != it; ++prior) {
            clip_start += prior->duration;
        }
        if (it->id != clip_id || timeline_offset <= clip_start ||
            timeline_offset >= clip_start + it->duration) {
            continue;
        }
        Clip right{*it};
        right.id = it->id + "_split";
        right.source_start += timeline_offset - clip_start;
        right.duration -= timeline_offset - clip_start;
        right.transition_in = TransitionType::None;
        right.transition_duration = Milliseconds{500};
        it->duration = timeline_offset - clip_start;
        clips_.insert(std::next(it), std::move(right));
        recompute();
        return true;
    }
    return false;
}

bool Timeline::trim_clip(const std::string& clip_id, Milliseconds new_source_start,
                         Milliseconds new_duration)
{
    auto it = locate(clip_id);
    if (it == clips_.end() || new_duration <= Milliseconds{0}) {
        return false;
    }
    it->source_start = new_source_start;
    it->duration = new_duration;
    recompute();
    return true;
}

bool Timeline::set_transition(const std::string& clip_id, TransitionType type,
                              Milliseconds duration)
{
    auto it = locate(clip_id);
    if (it == clips_.end() || duration < Milliseconds{0}) {
        return false;
    }
    it->transition_in = type;
    it->transition_duration = duration;
    return true;
}

std::vector<Clip>::iterator Timeline::locate(const std::string& clip_id)
{
    return std::find_if(clips_.begin(), clips_.end(), [&](const Clip& clip) {
        return clip.id == clip_id;
    });
}

void Timeline::recompute()
{
    total_duration_ = std::accumulate(
        clips_.begin(), clips_.end(), Milliseconds{0},
        [](Milliseconds sum, const Clip& clip) { return sum + clip.duration; });
}

std::size_t Timeline::find_clip_at(Milliseconds timeline_offset) const
{
    Milliseconds elapsed{0};
    for (std::size_t index = 0; index < clips_.size(); ++index) {
        elapsed += clips_[index].duration;
        if (timeline_offset < elapsed || index + 1U == clips_.size()) {
            return index;
        }
    }
    return clips_.empty() ? static_cast<std::size_t>(-1) : clips_.size() - 1U;
}

std::string Timeline::source_for_offset(Milliseconds timeline_offset) const
{
    const auto index = find_clip_at(timeline_offset);
    if (index >= clips_.size()) {
        return {};
    }
    return clips_[index].media_path;
}

} // namespace material_everything::video_editor
