#include "clock_timer.hpp"

#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <utility>

namespace me::clock_timer {
namespace {

std::int64_t secondsSinceEpoch(std::chrono::system_clock::time_point now) {
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

void validateClockTime(int hour, int minute) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        throw std::invalid_argument("alarm time must be a valid clock time");
    }
}

} // namespace

void ClockTimerModule::addWorldClock(WorldClockEntry entry) {
    if (entry.city.empty() || entry.timeZoneId.empty()) {
        throw std::invalid_argument("world clock requires city and timezone");
    }
    const auto duplicate = std::any_of(worldClocks_.begin(), worldClocks_.end(),
        [&](const auto& existing) { return existing.timeZoneId == entry.timeZoneId; });
    if (!duplicate) {
        worldClocks_.push_back(std::move(entry));
    }
}

void ClockTimerModule::removeWorldClock(const std::string& timeZoneId) {
    worldClocks_.erase(std::remove_if(worldClocks_.begin(), worldClocks_.end(),
        [&](const auto& entry) { return entry.timeZoneId == timeZoneId; }), worldClocks_.end());
}

Alarm& ClockTimerModule::addAlarm(Alarm alarm) {
    validateClockTime(alarm.hour, alarm.minute);
    if (alarm.id.empty()) {
        alarm.id = "alarm-" + std::to_string(alarms_.size() + 1);
    }
    alarms_.push_back(std::move(alarm));
    return alarms_.back();
}

bool ClockTimerModule::removeAlarm(const std::string& id) {
    const auto before = alarms_.size();
    alarms_.erase(std::remove_if(alarms_.begin(), alarms_.end(),
        [&](const Alarm& alarm) { return alarm.id == id; }), alarms_.end());
    return alarms_.size() != before;
}

bool ClockTimerModule::isDueOnDay(const Alarm& alarm, Weekday weekday) {
    if (!alarm.enabled || !std::any_of(alarm.recurringDays.begin(), alarm.recurringDays.end(),
            [](bool enabled) { return enabled; })) {
        return false;
    }
    return alarm.recurringDays[static_cast<std::size_t>(weekday)];
}

Weekday ClockTimerModule::weekdayOf(std::chrono::system_clock::time_point now) {
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    return static_cast<Weekday>(local.tm_wday);
}

std::optional<Alarm> ClockTimerModule::dueAlarm(std::chrono::system_clock::time_point now) const {
    const Weekday today = weekdayOf(now);
    const std::int64_t currentSeconds = secondsSinceEpoch(now) % 86'400;
    for (const auto& alarm : alarms_) {
        const std::int64_t alarmSeconds =
            static_cast<std::int64_t>(alarm.hour) * 3'600 + alarm.minute * 60;
        if (currentSeconds / 60 == alarmSeconds / 60 && isDueOnDay(alarm, today)) {
            return alarm;
        }
    }
    return std::nullopt;
}

void ClockTimerModule::startStopwatch() {
    resetStopwatch();
    stopwatchRunning_ = true;
}

void ClockTimerModule::resetStopwatch() {
    stopwatchElapsed_ = {};
    laps_.clear();
    stopwatchRunning_ = false;
}

Lap ClockTimerModule::recordLap() {
    if (!stopwatchRunning_) {
        throw std::logic_error("cannot record a lap while the stopwatch is paused");
    }
    const auto previousTotal = laps_.empty() ? std::chrono::milliseconds{} : laps_.back().total;
    laps_.push_back(Lap{stopwatchElapsed_, stopwatchElapsed_ - previousTotal});
    return laps_.back();
}

void ClockTimerModule::setCountdown(std::chrono::seconds duration) {
    if (duration <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("countdown duration must be positive");
    }
    countdownDuration_ = duration;
    countdownRemaining_ = duration;
    countdownState_ = TimerState::Stopped;
}

void ClockTimerModule::startCountdown() {
    if (countdownDuration_ <= std::chrono::milliseconds::zero()) {
        throw std::logic_error("set a countdown before starting it");
    }
    countdownState_ = TimerState::Running;
}

void ClockTimerModule::stopCountdown() {
    countdownRemaining_ = countdownDuration_;
    countdownState_ = TimerState::Stopped;
}

void ClockTimerModule::tickCountdown(std::chrono::milliseconds delta) {
    if (delta < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("timer delta cannot be negative");
    }
    if (countdownState_ != TimerState::Running) {
        return;
    }
    countdownRemaining_ -= std::min(countdownRemaining_, delta);
    progress_ = countdownDuration_.count() == 0
        ? 1.0
        : 1.0 - static_cast<double>(countdownRemaining_.count()) /
                  static_cast<double>(countdownDuration_.count());
    if (countdownRemaining_ == std::chrono::milliseconds::zero()) {
        countdownState_ = TimerState::Finished;
    }
}

void ClockTimerModule::configurePomodoro(const PomodoroConfig& config) {
    if (config.work <= std::chrono::seconds::zero() ||
        config.shortBreak <= std::chrono::seconds::zero() ||
        config.longBreak <= std::chrono::seconds::zero() || config.longBreakEvery <= 0) {
        throw std::invalid_argument("pomodoro intervals and cycle count must be positive");
    }
    pomodoro_ = config;
    phase_ = PomodoroPhase::Work;
    pomodoroRemaining_ = config.work;
    completedWorkSessions_ = 0;
    pomodoroRunning_ = false;
}

void ClockTimerModule::startPomodoro() {
    if (pomodoroRemaining_ <= std::chrono::milliseconds::zero()) {
        configurePomodoro(pomodoro_);
    }
    pomodoroRunning_ = true;
}

namespace {

std::chrono::milliseconds phaseDuration(PomodoroConfig config, ClockTimerModule::PomodoroPhase phase) {
    switch (phase) {
    case ClockTimerModule::PomodoroPhase::Work: return config.work;
    case ClockTimerModule::PomodoroPhase::ShortBreak: return config.shortBreak;
    case ClockTimerModule::PomodoroPhase::LongBreak: return config.longBreak;
    }
    return config.work;
}

} // namespace

void ClockTimerModule::tickPomodoro(std::chrono::milliseconds delta) {
    if (delta < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("pomodoro delta cannot be negative");
    }
    if (!pomodoroRunning_) {
        return;
    }
    while (delta > std::chrono::milliseconds::zero()) {
        const auto consumed = std::min(pomodoroRemaining_, delta);
        pomodoroRemaining_ -= consumed;
        delta -= consumed;
        if (pomodoroRemaining_ > std::chrono::milliseconds::zero()) {
            break;
        }

        if (phase_ == PomodoroPhase::Work) {
            ++completedWorkSessions_;
            phase_ = completedWorkSessions_ % pomodoro_.longBreakEvery == 0
                ? PomodoroPhase::LongBreak
                : PomodoroPhase::ShortBreak;
        } else {
            phase_ = PomodoroPhase::Work;
        }
        pomodoroRemaining_ = phaseDuration(pomodoro_, phase_);
    }
    progress_ = pomodoroRemaining_.count() == 0
        ? 1.0
        : 1.0 - static_cast<double>(pomodoroRemaining_.count()) /
                  static_cast<double>(phaseDuration(pomodoro_, phase_).count());
}

void ClockTimerModule::stopPomodoro() {
    pomodoroRunning_ = false;
    phase_ = PomodoroPhase::Work;
    pomodoroRemaining_ = pomodoro_.work;
    completedWorkSessions_ = 0;
}

} // namespace me::clock_timer
