#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace material_everything::calendar {

using Date = std::chrono::year_month_day;
using DateTimeMinutes = std::chrono::sys_time<std::chrono::minutes>;

enum class Recurrence { None, Daily, Weekly, Monthly, Yearly, Weekdays };
enum class ViewMode { Month, Week, Day };

struct Category {
    std::string id;
    std::string name;
    std::uint32_t color = 0xFF2196F3; // ARGB
};

struct Event {
    std::string id;
    std::string title;
    std::string location;
    std::string notes;
    DateTimeMinutes start{};
    int duration_minutes = 30;
    bool all_day = false;
    std::string category_id;
    Recurrence recurrence = Recurrence::None;
    int recurrence_interval = 1; // >= 1
    int reminder_minutes_before = 10;
};

struct Occurrence {
    Event event;
    DateTimeMinutes start{};
};

// Pure data/logic module. The shell binds this API to Material Design 3 widgets.
class CalendarModule {
public:
    CalendarModule();

    void set_view(ViewMode mode);
    void set_visible_date(Date date);
    ViewMode view() const;
    Date visible_date() const;

    const Category* add_category(std::string name, std::uint32_t color);
    bool remove_category(const std::string& id);
    const std::vector<Category>& categories() const;
    const Category* category(const std::string& id) const;

    Event* add_event(Event event);
    bool update_event(const std::string& id, const Event& replacement);
    bool delete_event(const std::string& id);
    const std::vector<Event>& events() const;

    std::vector<Occurrence> occurrences(Date from, Date to_inclusive) const;
    std::vector<Occurrence> agenda(Date from, std::size_t max_days = 7) const;
    std::vector<Event*> due_reminders(DateTimeMinutes now);
    static bool is_recurring_on(const Event& event, Date day);
    static Date add_days(Date base, int days);
    static std::chrono::sys_days to_days(DateTimeMinutes value);

private:
    ViewMode mode_ = ViewMode::Month;
    Date visible_{std::chrono::year{1970}, std::chrono::month{1}, std::chrono::day{1}};
    std::vector<Category> categories_;
    std::vector<Event> events_;
    mutable std::uint64_t next_event_id_ = 1;
    std::vector<DateTimeMinutes> acknowledged_reminders_;
};

} // namespace material_everything::calendar
