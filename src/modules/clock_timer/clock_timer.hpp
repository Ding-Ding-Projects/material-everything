#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace me::clock_timer {

enum class Weekday : std::uint8_t {
    Sunday,
    Monday,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
};

struct WorldClockEntry {
    std::string city;
    std::string timeZoneId; // IANA identifier, e.g. "America/Toronto"
    int utcOffsetSeconds = 0;
};

struct Alarm {
    std::string id;
    std::string label;
    int hour = 7;
    int minute = 0;
    bool enabled = true;
    std::array<bool, 7> recurringDays{}; // Sunday..Saturday
};

struct Lap {
    std::chrono::milliseconds total{};
    std::chrono::milliseconds split{};
};

struct PomodoroConfig {
    std::chrono::seconds work{25 * 60};
    std::chrono::seconds shortBreak{5 * 60};
    std::chrono::seconds longBreak{15 * 60};
    int longBreakEvery = 4;
};

class ClockTimerModule final {
public:
    // Material Design 3 circular indicator state: 0.0 through 1.0.
    double circularProgress() const { return progress_; }

    const std::vector<WorldClockEntry>& worldClocks() const { return worldClocks_; }
    void addWorldClock(WorldClockEntry entry);
    void removeWorldClock(const std::string& timeZoneId);

    Alarm& addAlarm(Alarm alarm);
    bool removeAlarm(const std::string& id);
    const std::vector<Alarm>& alarms() const { return alarms_; }
    std::optional<Alarm> dueAlarm(std::chrono::system_clock::time_point now) const;

    void startStopwatch();
    Lap recordLap();
    void pauseStopwatch() { stopwatchRunning_ = false; }
    void resumeStopwatch() { stopwatchRunning_ = true; }
    void resetStopwatch();
    std::chrono::milliseconds stopwatchElapsed() const { return stopwatchElapsed_; }
    bool stopwatchRunning() const { return stopwatchRunning_; }
    const std::vector<Lap>& laps() const { return laps_; }

    enum class TimerState { Stopped, Running, Paused, Finished };

    void setCountdown(std::chrono::seconds duration);
    void startCountdown();
    void pauseCountdown() { countdownState_ = TimerState::Paused; }
    void resumeCountdown() { countdownState_ = TimerState::Running; }
    void stopCountdown();
    void tickCountdown(std::chrono::milliseconds delta);
    std::chrono::milliseconds countdownRemaining() const { return countdownRemaining_; }
    TimerState countdownState() const { return countdownState_; }

    static constexpr std::array<std::chrono::seconds, 6> kTimerPresets{
        std::chrono::minutes(1), std::chrono::minutes(3), std::chrono::minutes(5),
        std::chrono::minutes(10), std::chrono::minutes(15), std::chrono::minutes(30),
    };

    void configurePomodoro(const PomodoroConfig& config);
    void startPomodoro();
    void tickPomodoro(std::chrono::milliseconds delta);
    enum class PomodoroPhase { Work, ShortBreak, LongBreak };
    PomodoroPhase pomodoroPhase() const { return phase_; }
    int completedWorkSessions() const { return completedWorkSessions_; }
    std::chrono::milliseconds pomodoroRemaining() const { return pomodoroRemaining_; }
    bool pomodoroRunning() const { return pomodoroRunning_; }
    void stopPomodoro();

private:
    static bool isDueOnDay(const Alarm& alarm, Weekday weekday);
    static Weekday weekdayOf(std::chrono::system_clock::time_point now);

    double progress_{};
    std::vector<WorldClockEntry> worldClocks_;
    std::vector<Alarm> alarms_;

    bool stopwatchRunning_{};
    std::chrono::milliseconds stopwatchElapsed_{};
    std::vector<Lap> laps_;

    TimerState countdownState_ = TimerState::Stopped;
    std::chrono::milliseconds countdownDuration_{};
    std::chrono::milliseconds countdownRemaining_{};

    PomodoroConfig pomodoro_;
    PomodoroPhase phase_ = PomodoroPhase::Work;
    std::chrono::milliseconds pomodoroRemaining_{};
    int completedWorkSessions_ = 0;
    bool pomodoroRunning_ = false;
};

} // namespace me::clock_timer
