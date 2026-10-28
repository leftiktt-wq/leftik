#pragma once

#include "core.hpp"

// ============================================================================
// Benchmark: ETW-замер Present-событий (DXGI / D3D9 / DxgKrnl).
// ============================================================================
namespace Benchmark {
    struct FrameSample {
        uint64_t timestampQpc;     // QPC на момент Present
        uint32_t frametimeUs;      // дельта с предыдущим кадром, микросекунды
        uint32_t processId;
    };

    struct Stats {
        double meanFps = 0;
        double low1PctFps = 0;     // 99th percentile frametime → FPS
        double low01PctFps = 0;    // 99.9th percentile frametime → FPS
        double minFps = 0;
        double maxFps = 0;
        double frametimeP99Us = 0;
        double frametimeVarianceUs2 = 0;
        uint32_t sampleCount = 0;
        uint32_t durationSec = 0;
        bool waitingForGame = false;   // ETW активен, но Present-события игры ещё не пойманы
        bool autoStopped = false;      // замер завершён автоматически по лимиту 5 минут
        std::wstring gameExe;
        std::wstring notes;        // какие твики были активны во время замера
    };

    bool IsRunning();
    bool Start(HWND ownerForErrors);
    void Stop();
    Stats GetLastStats();
    std::vector<FrameSample> GetLastSamples();
    bool ExportGraphPng(const std::wstring& path);
    bool ExportCsv(const std::wstring& path);
    void Shutdown();
    void ResetSamples();
    bool AutoStopDue();
    void RequestStop(HWND notify, bool autoStop = false);
    uint32_t MaxDurationSec();
}

// ============================================================================
// Journal: единый журнал изменений с бэкапом, индексом, HMAC и откатом.
// Все записи реестра/команд — только через этот namespace.
// ============================================================================
namespace Journal {
#pragma pack(push, 1)
    struct JournalIdxEntry {
        uint64_t offset;      // смещение строки в .log
        uint32_t length;      // длина строки (без \n)
        uint32_t crc32;       // CRC32 строки
        uint32_t groupHash;   // CRC32 от group (0 = legacy V2 без группы)
        uint32_t tweakId;     // 0 = не привязано
        uint64_t timestamp;   // FILETIME (GetSystemTimeAsFileTime)
    };
#pragma pack(pop)
    static_assert(sizeof(JournalIdxEntry) == 32, "JournalIdxEntry must be 32 bytes");

    struct DiffEntry {
        std::wstring hive;           // HKLM / HKCU
        std::wstring path;
        std::wstring name;
        std::wstring tweakName;
        bool wouldChange;            // true = текущее отличается от restored
        std::wstring currentValue;   // текущее значение (усечённое до 64 симв.)
        std::wstring restoredValue;  // восстанавливаемое значение
    };

    struct GroupMeta {
        std::wstring group;
        std::wstring title;
        int entryCount = 0;
        uint64_t lastTimestamp = 0;
    };

    // Хуки в слой tweaks: system не включает tweaks.hpp (нет циклической зависимости).
    // Устанавливаются из InitTweaks(); до установки откат работает без них.
    void SetRollbackHooks(void (*revertTweak)(int), bool (*readTweakState)(int));

    std::wstring NewGroupId();
    bool RebuildJournalIndex(bool forceRebuild = false);
    std::vector<std::wstring> GetJournalGroups();
    GroupMeta GetGroupMeta(const std::wstring& group);

    void LogAction(const std::wstring& actionName, const std::wstring& revertCmd = L"");
    void LogActionGrouped(const std::wstring& actionName, const std::wstring& revertCmd, const std::wstring& group);

    bool Backup(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& tweakName);
    bool SetDword(HKEY root, const std::wstring& path, const std::wstring& name, DWORD value,
                  const std::wstring& tweakName = L"Системный твик");
    bool DeleteValue(HKEY root, const std::wstring& path, const std::wstring& name,
                     const std::wstring& tweakName = L"Системный твик");
    bool SetString(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& value,
                   const std::wstring& tweakName = L"Системный твик");

    bool SetDwordGrouped(HKEY root, const std::wstring& path, const std::wstring& name,
                         DWORD value, const std::wstring& tweakName, const std::wstring& group);
    bool SetStringGrouped(HKEY root, const std::wstring& path, const std::wstring& name,
                          const std::wstring& value, const std::wstring& tweakName, const std::wstring& group);
    bool DeleteValueGrouped(HKEY root, const std::wstring& path, const std::wstring& name,
                            const std::wstring& tweakName, const std::wstring& group);

    // Снимки схемы питания и скрытых параметров (нужны tweaks-слою для точного отката).
    bool SavePrevPowerScheme(const std::wstring& guid);
    std::wstring LoadPrevPowerScheme();
    void ClearPrevPowerScheme();
    bool SavePrevPowerAdv(const std::wstring& snapshot);
    std::wstring LoadPrevPowerAdv();
    void ClearPrevPowerAdv();

    bool WriteMarker(const std::wstring& tag);
    uint64_t NowFileTimeU64();

    std::vector<DiffEntry> BuildDiffForGroup(const std::wstring& group);

    int RollbackGroupInternal(const std::wstring& group, bool changedOnly,
                              int& restored, int& registryFailed,
                              int& cmdRestored, int& cmdFailed, int& unsupported);
    int RollbackGroup(const std::wstring& group,
                      int& restored, int& registryFailed,
                      int& cmdRestored, int& cmdFailed, int& unsupported);
    int RollbackToMarker(const std::wstring& tag,
                         int& restored, int& registryFailed,
                         int& cmdRestored, int& cmdFailed, int& unsupported);
    int Rollback(HWND owner);

    bool DeriveHmacKey(BYTE outKey[32]);
    bool HmacSha256(const BYTE key[32], const std::string& data, BYTE outMac[32]);
    bool ExportJournal(const std::wstring& outPath);
    int ImportJournalEx(const std::wstring& inPath, int& validEntries, int& tamperedEntries,
                        bool allowPartialCommit, std::vector<std::string>* outValidLines = nullptr);
    int ImportJournal(const std::wstring& inPath, int& validEntries, int& tamperedEntries);

    void AppendBenchmarkLine(const Benchmark::Stats& st);
}

// ============================================================================
// Monitor: фоновый монитор температур/нагрузки + Auto-Game Guard.
// ============================================================================
namespace Monitor {
    extern std::atomic<int>    cpuTemp;
    extern std::atomic<int>    gpuTemp;
    extern std::atomic<double> junkGB;
    extern std::atomic<bool>   running;
    extern std::atomic<bool>   gameActive;
    extern std::thread         worker;

    void InvalidateGroupsCache();
    std::vector<std::wstring> GetGroupsSnapshot();
    bool IsGameProcessPid(DWORD pid, std::wstring* outName = nullptr);
    void Start();
    void Stop();
}

void TrimBackgroundWorkingSets();

// ============================================================================
// MemoryOptimizer / NetworkTweaker
// ============================================================================
namespace MemoryOptimizer {
    bool EnablePrivilege(const wchar_t* privilegeName);
    bool ClearStandbyList();
}

namespace NetworkTweaker {
    bool IsValidAdapter(const std::wstring& driverDesc);
    std::vector<std::wstring> GetPhysicalAdapters();
    // Реализация — tweaks.cpp (использует Journal::SetDword).
    void OptimizeAdapters();
}

// ============================================================================
// GDI+ примитивы: нужны и UI, и Benchmark::ExportGraphPng (PNG-график).
// ============================================================================
void DrawRoundRect(Graphics& g, RectF rect, float radius, Color fill, Color border);
void DrawTextGdi(Graphics& g, const std::wstring& text, Font* font, RectF rect, Color color,
                 StringAlignment align = StringAlignmentNear, StringAlignment lineAlign = StringAlignmentCenter);
void DrawTextEllipsis(Graphics& g, const std::wstring& text, Font* font, RectF rect, Color color,
                      StringAlignment align = StringAlignmentNear, StringAlignment lineAlign = StringAlignmentCenter);

// ============================================================================
// Hardware / GPU (WMI, DXGI, PDH, nvidia-smi)
// ============================================================================
bool IsNvidiaGpu();
bool IsAmdGpu();
int GetWindowsBuild();
void RefreshHardwareFlags();
void DetectDiskType();
bool IsLowRamSystem();
App::HardwareTier DetectHardwareTier();
const wchar_t* HardwareTierName(App::HardwareTier t);
void GetHardwareInfo();
void UpdateGPUMemory();
int GetGPULoadPDH();
int GetGPUTempNvidiaSmi();
// Пакет #17 (2a.1): неблокирующее предупреждение (сейчас — HVCI для твика 1018).
// GetTweakBlockReason возвращает текст и для warning-only, поэтому UI обязан
// проверять этот флаг, прежде чем показывать «Заблокировано Guard».
bool IsTweakWarningOnly(int id);

// ============================================================================
// Системные операции: процессы, очистка, restore point, статистика.
// ============================================================================
void RunPowerShell(const std::wstring& cmd);
void RunCmd(const std::wstring& cmd);
bool RunCmdVisible(const std::wstring& cmd);
void DisableSvc(const std::wstring& s);
void DeleteFolderRecursive(const std::wstring& folder);
void RunCleaner();
void SmartDiskClean();
void CleanEmptyAppTraces();
void CleanSampCache();
void CreateRestorePoint();
void EnsureFirstRunRestorePoint();
void ClearAllShaderCaches();
void ClearDXCache();
void KillBackgroundApps();
bool IsProcessRunning(const wchar_t* exeName);
void RefreshRealStats();
void UpdateStats();
void ScanGames();

// ============================================================================
// FpsMonitor: нагрузка GPU через PDH (не ETW; отдельный лёгкий монитор).
// Определение статических полей — ровно в одном system.cpp.
// ============================================================================
struct FpsMonitor {
    static std::atomic<double> currentFps;
    static std::atomic<double> avgFps;
    static std::atomic<int>    samples;
    static std::atomic<bool>   running;
    static std::thread         worker;

    static void Start() {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true)) return;
        // Если старый worker уже завершился (например, PdhOpenQueryW fail) —
        // он всё ещё joinable. operator= на joinable thread → std::terminate.
        if (worker.joinable()) worker.join();
        samples.store(0);
        avgFps.store(0.0);
        currentFps.store(0.0);
        worker = std::thread([]() {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            PDH_HQUERY q = nullptr;
            PDH_HCOUNTER c = nullptr;
            if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) {
                samples.store(0);
                avgFps.store(0.0);
                currentFps.store(0.0);
                running.store(false);
                CoUninitialize();
                return;
            }
            PdhAddEnglishCounterW(q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &c);
            PdhCollectQueryData(q);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            double lastAvg = 0.0;
            int n = 0;
            while (running.load()) {
                PdhCollectQueryData(q);
                DWORD size = 0, count = 0;
                PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &size, nullptr, nullptr);
                if (size > 0) {
                    std::vector<BYTE> buf(size);
                    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
                    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
                        double total = 0;
                        for (DWORD i = 0; i < count; ++i)
                            if (items[i].FmtValue.doubleValue > 0)
                                total += items[i].FmtValue.doubleValue;
                        total = std::min(100.0, total);
                        currentFps.store(total);
                        lastAvg = (lastAvg * n + total) / (n + 1);
                        n++;
                        avgFps.store(lastAvg);
                        samples.store(n);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            PdhCloseQuery(q);
            CoUninitialize();
        });
    }

    static void Stop() {
        running.store(false);
        // worker делает sleep 200ms внутри цикла. join() может подождать до 200ms —
        // это ≤2 кадра при 144Hz, визуально незаметно. Не оптимизируем: замена на
        // condition_variable даст +15 строк кода ради 200ms фриза в момент клика.
        if (worker.joinable()) worker.join();
    }
};
