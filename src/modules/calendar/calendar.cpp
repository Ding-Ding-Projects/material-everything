#include "calendar.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace material_everything::calendar {

namespace {

constexpr auto kMaxDuration = std::numeric_limits<int>::max();

DateTimeMinutes at_date_time(Date day, int hour, int minute) {
    const std::chrono::sys_days days(day);
    return DateTimeMinutes(days) + std::chrono::hours(hour) + std::chrono::minutes(minute);
}

bool same_day(DateTimeMinutes value, Date day) {
    return std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(value)) == day;
}

unsigned weekday_index(Date day) {
    // Monday == 0 through Sunday == 6.
    const auto sysday = std::chrono::sys_days(day).time_since_epoch().count();
    const long long normalized = ((sysday + 3LL) % 7LL + 7LL) % 7LL;
    return static_cast<unsigned>(normalized);
}

Date normalize(Date candidate) {
    if (!candidate.ok()) throw std::invalid_argument("invalid calendar date");
    return candidate;
}

} // namespace

CalendarModule::CalendarModule()
    : visible_(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())) {
    categories_.push_back({"work", "Work", 0xFF2196F3});
    categories_.push_back({"personal", "Personal", 0xFFF06292});
    categories_.push_back({"health", "Health", 0xFF66BB6A});
}

void CalendarModule::set_view(ViewMode mode) {
    mode_ = mode;
}

void CalendarModule::set_visible_date(Date date) {
    visible_ = normalize(date);
}

ViewMode CalendarModule::view() const { return mode_; }
Date CalendarModule::visible_date() const { return visible_; }

const Category* CalendarModule::add_category(std::string name, std::uint32_t color) {
    if (name.empty()) return nullptr;
    categories_.push_back({name, std::move(name), color});
    return &categories_.back();
}

bool CalendarModule::remove_category(const std::string& id) {
    if (id == "work" || id == "personal" || id == "health") return false;
    const auto it = std::remove_if(categories_.begin(), categories_.end(),
                                   [&](const Category& c) { return c.id == id; });
    if (it == categories_.end()) return false;
    for (auto& event : events_) {
        if (event.category_id == id) event.category_id = "personal";
    }
    categories_.erase(it, categories_.end());
    return true;
}

const std::vector<Category>& CalendarModule::categories() const { return categories_; }

const Category* CalendarModule::category(const std::string& id) const {
    const auto it = std::find_if(categories_.begin(), categories_.end(),
                                 [&](const Category& c) { return c.id == id; });
    return it == categories_.end() ? nullptr : &*it;
}

Event* CalendarModule::add_event(Event event) {
    if (event.title.empty()) return nullptr;
    if (event.duration_minutes <= 0 || event.duration_minutes > kMaxDuration) return nullptr;
    if (event.recurrence_interval < 1) event.recurrence_interval = 1;
    if (event.category_id.empty()) event.category_id = "personal";
    if (!category(event.category_id)) return nullptr;
    event.id = "evt-" + std::to_string(next_event_id_++);
    events_.push_back(std::move(event));
    return &events_.back();
}

bool CalendarModule::update_event(const std::string& id, const Event& replacement) {
    Event copy = replacement;
    copy.id = id;
    if (copy.title.empty()) return false;
    if (copy.duration_minutes <= 0 || copy.duration_minutes > kMaxDuration) return false;
    if (!category(copy.category_id)) return false;
    const auto it = std::find_if(events_.begin(), events_.end(),
                                 [&](const Event& e) { return e.id == id; });
    if (it == events_.end()) return false;
    *it = copy;
    return true;
}

bool CalendarModule::delete_event(const std::string& id) {
    const auto it = std::remove_if(events_.begin(), events_.end(),
                                   [&](const Event& e) { return e.id == id; });
    if (it == events_.end()) return false;
    events_.erase(it, events_.end());
    return true;
}

const std::vector<Event>& CalendarModule::events() const { return events_; }

bool CalendarModule::is_recurring_on(const Event& event, Date day) {
    if (event.recurrence == Recurrence::None) return same_day(event.start, day);
    const Date first = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(event.start));
    if (day < first) return false;
    const int interval = std::max(1, event.recurrence_interval);
    const std::chrono::sys_days first_days(first);
    const std::chrono::sys_days target_days(day);
    switch (event.recurrence) {
        case Recurrence::None:
            break;
        case Recurrence::Daily:
            return (target_days - first_days).count() % interval == 0;
        case Recurrence::Weekdays:
            return weekday_index(day) < 5 && (target_days - first_days).count() % interval == 0;
        case Recurrence::Weekly: {
            const long long weeks = (target_days - first_days).count() / 7;
            return weekday_index(day) == weekday_index(first) &&
                   weeks % interval == 0 && (target_days - first_days).count() >= weeks * 7;
        }
        case Recurrence::Monthly: {
            const auto months = static_cast<long long>((static_cast<int>(day.year()) -
                static_cast<int>(first.year())) * 12 + unsigned(day.month()) - unsigned(first.month()));
            return unsigned(day.day()) == unsigned(first.day()) && months % interval == 0;
        }
        case Recurrence::Yearly:
            return day.month() == first.month() && day.day() == first.day() &&
                   (static_cast<int>(day.year()) - static_cast<int>(first.year())) % interval == 0;
    }
    return false;
}

Date CalendarModule::add_days(Date base, int days) {
    normalize(base);
    const std::chrono::sys_days shifted =
        std::chrono::sys_days(base) + std::chrono::days(days);
    return std::chrono::year_month_day(shifted);
}

std::chrono::sys_days CalendarModule::to_days(DateTimeMinutes value) {
    return std::chrono::floor<std::chrono::days>(value);
}

std::vector<Occurrence> CalendarModule::occurrences(Date from, Date to_inclusive) const {
    std::vector<Occurrence> result;
    normalize(from);
    normalize(to_inclusive);
    if (to_inclusive < from) return result;
    for (Date day = from; day <= to_inclusive; day = add_days(day, 1)) {
        for (const auto& event : events_) {
            if (!is_recurring_on(event, day)) continue;
            const auto start_day = std::chrono::sys_days(day);
            const DateTimeMinutes original_start = event.start;
            const auto time_of_day = original_start - DateTimeMinutes(to_days(original_start));
            result.push_back({event, DateTimeMinutes(start_day) + time_of_day});
        }
    }
    std::sort(result.begin(), result.end(), [](const Occurrence& a, const Occurrence& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.event.title < b.event.title;
    });
    return result;
}

std::vector<Occurrence> CalendarModule::agenda(Date from, std::size_t max_days) const {
    return occurrences(from, add_days(from, static_cast<int>(max_days) - 1));
}

std::vector<Event*> CalendarModule::due_reminders(DateTimeMinutes now) {
    std::vector<Event*> due;
    const Date today(std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(now)));
    for (Date day = add_days(today, -1); day <= add_days(today, 1); day = add_days(day, 1)) {
        for (const auto& occurrence : occurrences(day, day)) {
            const auto reminder_at = occurrence.start - std::chrono::minutes(occurrence.event.reminder_minutes_before);
            if (now < reminder_at || now > occurrence.start) continue;
            const bool already_acknowledged = std::any_of(acknowledged_reminders_.begin(),
                acknowledged_reminders_.end(), [&](const DateTimeMinutes& t) { return t == reminder_at; });
            if (already_acknowledged) continue;
            acknowledged_reminders_.push_back(reminder_at);
            due.push_back(&events_[static_cast<std::size_t>(&occurrence.event - &events_.front())]);
        }
    }
    std::sort(due.begin(), due.end(), [](const Event* a, const Event* b) { return a->start < b->start; });
    return due;
}

} // namespace material_everything::calendar
