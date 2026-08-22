#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace me::system_monitor {

enum class SortKey {
    Cpu,
    Memory,
    Disk,
    Network,
    ProcessId,
    Name,
};

enum class SortOrder {
    Ascending,
    Descending,
};

struct Sample {
    double cpuPercent = 0.0;          // Total CPU, 0..100.
    std::vector<double> perCoreCpuPercent;
    double memoryUsedBytes = 0.0;
    double memoryTotalBytes = 0.0;
    double memoryPercent = 0.0;
    double diskReadBytesPerSecond = 0.0;
    double diskWriteBytesPerSecond = 0.0;
    double networkReceivedBytesPerSecond = 0.0;
    double networkSentBytesPerSecond = 0.0;
    std::vector<double> temperatureCelsius; // Empty when sensors are unavailable.
    std::chrono::system_clock::time_point timestamp{};
};

struct ProcessInfo {
    std::uint32_t processId = 0;
    std::string name;
    double cpuPercent = 0.0;
    double memoryBytes = 0.0;
    double diskBytesPerSecond = 0.0;
    double networkBytesPerSecond = 0.0;
};

struct ProcessQuery {
    SortKey sortKey = SortKey::Cpu;
    SortOrder sortOrder = SortOrder::Descending;
    std::string filter; // Case-insensitive substring match on the process name.
};

// Material Design 3 color roles used by chart consumers. Values are opaque
// presentation tokens; this module never renders directly.
struct ChartPalette {
    static constexpr const char* kPrimary = "#D0BCFF";
    static constexpr const char* kSecondary = "#CCC2DC";
    static constexpr const char* kTertiary = "#EFB8C8";
    static constexpr const char* kError = "#F2B8B5";
    static constexpr const char* kSurface = "#141218";
    static constexpr const char* kOnSurface = "#E6E0E9";
    static constexpr const char* kCpu = kPrimary;
    static constexpr const char* kMemory = kSecondary;
    static constexpr const char* kDisk = kTertiary;
    static constexpr const char* kNetwork = kPrimary;
    static constexpr const char* kTemperature = kError;
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    SystemMonitor(const SystemMonitor&) = delete;
    SystemMonitor& operator=(const SystemMonitor&) = delete;

    // Refreshes all counters. Returns false when the Windows performance
    // counters could not be read; the previous sample remains valid.
    bool refresh();

    const Sample& sample() const;

    // Returns a sorted copy of the current process table.
    std::vector<ProcessInfo> processes(const ProcessQuery& query = {}) const;

    // Requests termination. Returns false when the process could not be
    // opened or termination failed; it never reports success optimistically.
    bool killProcess(std::uint32_t processId, std::string* error = nullptr) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace me::system_monitor
