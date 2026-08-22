#include "system_monitor.hpp"

#include <algorithm>
#include <cctype>
#include <comdef.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tchar.h>
#include <windows.h>
#include <wbemidl.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace me::system_monitor {
namespace {

std::string narrow(const wchar_t* value) {
    if (!value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (!result.empty()) {
        WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
        result.pop_back();
    }
    return result;
}

bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    const std::string h = haystack, n = needle;
    return std::search(h.begin(), h.end(), n.begin(), n.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != h.end();
}

} // namespace

struct SystemMonitor::Impl {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER totalCpu = nullptr;
    std::vector<PDH_HCOUNTER> coreCpu;
    PDH_HCOUNTER memoryAvailable = nullptr;
    PDH_HCOUNTER diskRead = nullptr;
    PDH_HCOUNTER diskWrite = nullptr;
    PDH_HCOUNTER netReceived = nullptr;
    PDH_HCOUNTER netSent = nullptr;
    PDH_HCOUNTER processCpu = nullptr;
    PDH_HCOUNTER processMemory = nullptr;
    PDH_HCOUNTER processDisk = nullptr;
    PDH_HCOUNTER processNetwork = nullptr;
    Sample current;
    std::vector<ProcessInfo> processRows;
    bool initialized = false;

    bool addCounter(const wchar_t* path, PDH_HCOUNTER* counter) {
        return PdhAddEnglishCounterW(query, path, 0, counter) == ERROR_SUCCESS;
    }
};

SystemMonitor::SystemMonitor() : impl_(new Impl) {
    if (PdhOpenQueryW(nullptr, 0, &impl_->query) != ERROR_SUCCESS) return;

    const bool ok =
        impl_->addCounter(L"\\Processor(_Total)\\% Processor Time", &impl_->totalCpu) &&
        impl_->addCounter(L"\\Memory\\Available Bytes", &impl_->memoryAvailable) &&
        impl_->addCounter(L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", &impl_->diskRead) &&
        impl_->addCounter(L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", &impl_->diskWrite) &&
        impl_->addCounter(L"\\Network Interface(*)\\Bytes Received/sec", &impl_->netReceived) &&
        impl_->addCounter(L"\\Network Interface(*)\\Bytes Sent/sec", &impl_->netSent) &&
        impl_->addCounter(L"\\Process(*)\\% Processor Time", &impl_->processCpu) &&
        impl_->addCounter(L"\\Process(*)\\Working Set", &impl_->processMemory) &&
        impl_->addCounter(L"\\Process(*)\\IO Read Bytes/sec", &impl_->processDisk) &&
        impl_->addCounter(L"\\Process(*)\\IO Data Bytes/sec", &impl_->processNetwork);

    const DWORD coreCount = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    impl_->coreCpu.reserve(coreCount);
    for (DWORD i = 0; i < coreCount; ++i) {
        PDH_HCOUNTER counter = nullptr;
        std::wstring path = L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time";
        if (impl_->addCounter(path.c_str(), &counter)) impl_->coreCpu.push_back(counter);
    }

    impl_->initialized = ok;
}

SystemMonitor::~SystemMonitor() {
    if (impl_->query) PdhCloseQuery(impl_->query);
    delete impl_;
}

bool SystemMonitor::refresh() {
    if (!impl_->initialized) return false;
    if (PdhCollectQueryData(impl_->query) != ERROR_SUCCESS) return false;

    Sample next;
    next.timestamp = std::chrono::system_clock::now();

    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(impl_->totalCpu, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
        next.cpuPercent = std::clamp(value.doubleValue, 0.0, 100.0);

    next.perCoreCpuPercent.reserve(impl_->coreCpu.size());
    for (PDH_HCOUNTER counter : impl_->coreCpu) {
        if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
            next.perCoreCpuPercent.push_back(std::clamp(value.doubleValue, 0.0, 100.0));
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        next.memoryTotalBytes = static_cast<double>(memory.ullTotalPhys);
        next.memoryUsedBytes = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys);
        next.memoryPercent = memory.ullTotalPhys
            ? 100.0 * static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys) /
                  static_cast<double>(memory.ullTotalPhys)
            : 0.0;
    }

    if (PdhGetFormattedCounterValue(impl_->diskRead, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
        next.diskReadBytesPerSecond = std::max(0.0, value.doubleValue);
    if (PdhGetFormattedCounterValue(impl_->diskWrite, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
        next.diskWriteBytesPerSecond = std::max(0.0, value.doubleValue);
    if (PdhGetFormattedCounterValue(impl_->netReceived, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
        next.networkReceivedBytesPerSecond = std::max(0.0, value.doubleValue);
    if (PdhGetFormattedCounterValue(impl_->netSent, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
        next.networkSentBytesPerSecond = std::max(0.0, value.doubleValue);

    // Process table from the native API; PDH per-process counters are used only
    // for aggregate rates when their instance names match the executable name.
    std::vector<DWORD> pids(1024);
    DWORD bytes = 0;
    while (EnumProcesses(pids.data(), static_cast<DWORD>(pids.size() * sizeof(DWORD)), &bytes) &&
           bytes == pids.size() * sizeof(DWORD)) {
        pids.resize(pids.size() * 2);
    }
    const DWORD count = bytes / sizeof(DWORD);

    // PDH instance lookup: build a name->rate map once.
    struct InstanceRate { std::wstring name; double cpu = 0.0; };
    std::vector<InstanceRate> rates;
    PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
    DWORD itemCount = 0, bufferSize = 0;
    if (PdhGetFormattedCounterArrayW(impl_->processCpu, PDH_FMT_DOUBLE, &bufferSize, &itemCount,
                                    static_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(nullptr)) == PDH_MORE_DATA) {
        std::vector<unsigned char> buffer(bufferSize);
        if (PdhGetFormattedCounterArrayW(impl_->processCpu, PDH_FMT_DOUBLE, &bufferSize, &itemCount,
                                        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data())) == ERROR_SUCCESS) {
            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            rates.reserve(itemCount);
            for (DWORD i = 0; i < itemCount && items[i].szName; ++i)
                rates.push_back({items[i].szName, std::clamp(items[i].FmtValue.doubleValue, 0.0, 100.0)});
        }
    }

    impl_->processRows.clear();
    impl_->processRows.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!handle) continue;

        wchar_t imagePath[MAX_PATH] = L"";
        DWORD pathSize = MAX_PATH;
        QueryFullProcessImageNameW(handle, 0, imagePath, &pathSize);

        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));

        ProcessInfo row;
        row.processId = pids[i];
        row.name = narrow(imagePath ? wcsrchr(imagePath, L'\\') ? wcsrchr(imagePath, L'\\') + 1 : imagePath : L"");
        row.memoryBytes = static_cast<double>(pmc.WorkingSetSize);

        // Match by executable stem against PDH instances ("chrome", "chrome#1").
        std::wstring stem = imagePath ? (wcsrchr(imagePath, L'\\') ? wcsrchr(imagePath, L'\\') + 1 : imagePath) : L"";
        const size_t dot = stem.find_last_of(L'.');
        if (dot != std::wstring::npos) stem = stem.substr(0, dot);
        for (const auto& rate : rates) {
            if (rate.name == stem || rate.name.rfind(stem + L"#", 0) == 0)
                row.cpuPercent = rate.cpu; // First match wins; good enough for a sortable table.
        }
        impl_->processRows.push_back(std::move(row));
        CloseHandle(handle);
    }

    // Temperature is optional. WMI thermal zones are read only when available;
    // an unavailable sensor leaves the vector empty rather than inventing data.
    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        bool comInitialized = true;
        if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IWbemLocator, reinterpret_cast<void**>(&locator)))) {
            if (SUCCEEDED(locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0,
                                                 nullptr, nullptr, &services)) && services) {
                CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                  RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
                if (SUCCEEDED(services->CreateInstanceEnum(_bstr_t(L"MSAcpi_ThermalZoneTemperature"),
                                                           WBEM_FLAG_FORWARD_ONLY, nullptr, &enumerator)) && enumerator) {
                    IWbemClassObject* object = nullptr;
                    ULONG returned = 0;
                    while (enumerator->Next(WBEM_INFINITE, 1, &object, &returned) == WBEM_S_NO_ERROR) {
                        VARIANT temperature{};
                        if (SUCCEEDED(object->Get(L"CurrentTemperature", 0, &temperature, nullptr, nullptr)) &&
                            temperature.vt == VT_I4) {
                            // ACPI reports tenths of degrees Kelvin.
                            next.temperatureCelsius.push_back(
                                (static_cast<double>(temperature.lVal) / 10.0) - 273.15);
                        }
                        VariantClear(&temperature);
                        object->Release();
                    }
                }
            }
        }
        if (enumerator) enumerator->Release();
        if (services) services->Release();
        if (locator) locator->Release();
        if (comInitialized) CoUninitialize();
    }

    impl_->current = std::move(next);
    return true;
}

const Sample& SystemMonitor::sample() const { return impl_->current; }

std::vector<ProcessInfo> SystemMonitor::processes(const ProcessQuery& query) const {
    std::vector<ProcessInfo> rows = impl_->processRows;
    if (!query.filter.empty()) {
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const ProcessInfo& row) {
            return !containsCaseInsensitive(row.name, query.filter);
        }), rows.end());
    }

    const bool descending = query.sortOrder == SortOrder::Descending;
    std::stable_sort(rows.begin(), rows.end(), [&](const ProcessInfo& a, const ProcessInfo& b) {
        switch (query.sortKey) {
            case SortKey::Cpu: return descending ? a.cpuPercent > b.cpuPercent : a.cpuPercent < b.cpuPercent;
            case SortKey::Memory: return descending ? a.memoryBytes > b.memoryBytes : a.memoryBytes < b.memoryBytes;
            case SortKey::Disk: return descending ? a.diskBytesPerSecond > b.diskBytesPerSecond : a.diskBytesPerSecond < b.diskBytesPerSecond;
            case SortKey::Network: return descending ? a.networkBytesPerSecond > b.networkBytesPerSecond : a.networkBytesPerSecond < b.networkBytesPerSecond;
            case SortKey::Name: return descending ? a.name > b.name : a.name < b.name;
            case SortKey::ProcessId: return descending ? a.processId > b.processId : a.processId < b.processId;
        }
        return false;
    });
    return rows;
}

bool SystemMonitor::killProcess(std::uint32_t processId, std::string* error) const {
    HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        if (error) *error = "OpenProcess failed with error " + std::to_string(GetLastError());
        return false;
    }
    const BOOL terminated = TerminateProcess(process, 1);
    if (!terminated && error)
        *error = "TerminateProcess failed with error " + std::to_string(GetLastError());
    CloseHandle(process);
    return terminated != FALSE;
}

} // namespace me::system_monitor
