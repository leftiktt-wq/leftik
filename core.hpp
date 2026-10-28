#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winternl.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <objidl.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <map>
#include <psapi.h>
#include <knownfolders.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <gdiplus.h>
#include <filesystem>
#include <dxgi1_4.h>
#ifdef __MINGW32__
#include <wbemcli.h>   // WMI-интерфейсы в MinGW
#else
#include <wbemidl.h>   // WMI-интерфейсы в MSVC
#endif
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <set>
#include <unordered_set>
#include <cctype>
#include <cwctype>
#include <mutex>
#include <cstdint>
#include <iomanip>
#include <bcrypt.h>
#include <commdlg.h>
#include <sddl.h>
#include <cmath>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "pdh.lib")

using namespace Gdiplus;

#define WM_TRAYICON       (WM_USER + 1)
#define HOTKEY_BOOST      1
#define WM_APP_JOBRESULT  (WM_APP + 2)
#define WM_LEFTIK_BENCH_STOPPED (WM_APP + 12)  // ETW-замер остановлен (wParam: 1 = авто-стоп по лимиту)

// Результат фоновой операции. Живет в heap: создает воркер, освобождает UI-поток.
struct AsyncJobResult { int jobId = 0; int status = 0; bool verified = false; std::wstring detail; };

// ============================================================================
// App: типы, константы палитры и объявления глобального состояния.
// Определения переменных — строго в core.cpp (extern + одно определение).
// ============================================================================
namespace App {
    extern const wchar_t TITLE[];
    extern const wchar_t CLASS_NAME[];

    // ==== Новая палитра (v57.1) ====
    extern const Color UI_BG;
    extern const Color UI_SIDEBAR;
    extern const Color UI_CARD;
    extern const Color UI_CARD_HOVER;
    extern const Color UI_ACCENT;
    extern const Color UI_GREEN;
    extern const Color UI_TEXT;
    extern const Color UI_MUTED;

    extern const Color UI_BORDER;
    extern const Color UI_ACCENT_DIM;
    extern const Color UI_GREEN_DIM;
    extern const Color UI_BODY_TEXT;

    // Алиасы: старые экраны автоматически получают новую палитру.
    extern const Color BG;
    extern const Color SIDEBAR;
    extern const Color PANEL;
    extern const Color PANEL_HOVER;
    extern const Color BORDER;
    extern const Color TEXT;
    extern const Color MUTED;
    extern const Color ACCENT;
    extern const Color GREEN;
    extern const Color RED;
    extern const Color YELLOW;

    constexpr int SIDE_W = 260;
    enum Page { Dashboard, Tweaks, Games, Cleaner, Restore, Diagnostics, Sensors, BenchmarkPage };
    enum Category { CAT_BASIC, CAT_SECURITY, CAT_POWER, CAT_NETWORK, CAT_DEBLOAT, CAT_PRIVACY, CAT_SERVICES, CAT_FIXES, CAT_COUNT };

    struct Hit { RECT r{}; int id=0; };
    struct TweakItem {
        std::wstring name;
        std::wstring shortDesc;
        std::wstring fullDesc;
        int id;
        Category cat;
        bool isDangerous;
        bool isLaptopSafe;
        bool isEnabled;
        bool requiresReboot;
        bool isPlacebo = false;   // не даёт эффекта на Win10/11 — скрыт по умолчанию
    };
    struct Game { std::wstring name; bool isSteam; std::wstring path; int appid = 0; std::wstring installdir = L""; };

    // Вендор GPU из WMI Win32_VideoController (PNPDeviceID: VEN_10DE / 1002 / 8086).
    // Строковый поиск по названию остаётся только fallback'ом.
    enum GpuVendor { GPU_UNKNOWN = 0, GPU_NVIDIA = 1, GPU_AMD = 2, GPU_INTEL = 3 };

    struct HardwareInfo {
        std::wstring cpuName, cpuCores, cpuThreads, cpuMaxClock;
        std::wstring gpuName, gpuMemory, gpuDriver;
        std::wstring ramTotal, ramSpeed, ramType;
        std::wstring diskInfo;
        // Новые поля для Guard 2.0
        int cpuPhysicalCores = 0;
        double ramTotalGB = 0;
        int ramLoadPct = 0;
        bool diskIsSSD = false;
        bool diskTypeKnown = false;
        double diskFreeGB = 0;
        bool vbsEnabled = false;
        bool tamperProtection = false;
        bool onACPower = true;
        int winBuild = 0;
        GpuVendor gpuVendor = GPU_UNKNOWN;
        // Пакет #17 (2a.3): штатный Xbox mode доступен только на Win11 24H2+
        // и только при установленном приложении Xbox (Microsoft.GamingApp).
        bool xboxModeAvailable = false;
        // Пакет #17 (2a.4): Intel APO — внешний компонент, LEFTIK его не эмулирует
        // и не детектирует (имя службы не подтверждено). Флаг нужен только для
        // текстовой рекомендации на Intel-платформе (Intel GPU или Intel CPU).
        bool isIntelPlatform = false;
    };

    enum class HardwareTier { Low, Mid, High };

    extern HardwareInfo hwInfo;

    extern HWND hwnd;
    extern Page page;
    extern Category activeCategory;
    extern std::vector<Hit> hits;
    extern std::vector<TweakItem> tweaksList;
    extern std::vector<Game> installedGames;
    extern int selectedTweakIdx;
    extern bool isLaptop;
    extern std::vector<Game> steamGames;
    extern std::map<std::wstring, Image*> gameImages;

    extern bool cleanTemp;
    extern bool cleanPrefetch;
    extern bool cleanRecycle;
    extern bool cleanLogs;

    extern ULONGLONG totalBytesFreed;
    extern int totalFilesDeleted;

    extern bool isMaxActive;
    extern bool isUltraActive;
    extern bool magicBoostActive;
    extern bool turboBoostActive;  // v59: Турбо-режим
    extern bool lastPowerState;
    extern bool backgroundMode;
    extern bool exitRequested;

    // v58.0: Feature A/B state
    extern bool dnsPromptedThisSession;   // DNS-диалог показан в этой сессии
    extern bool autoStandbyClean;         // Feature B: авто-очистка standby
    extern int  standbyIntervalMin;       // Feature B: интервал в минутах

    extern HardwareTier g_hardwareTier;

    extern FontFamily* fontFamily;
    extern Font* fRegular;
    extern Font* fBold;
    extern Font* fTitle;
    extern Font* fSmall;
    extern Font* fH1;
    extern Font* fH2;
    extern Font* fMetric;
    extern Font* fNav;
    extern Font* fBody;
    extern Font* fBodyBold;
    extern FontFamily* iconFamily;
    extern Font* fIcon;
    extern Font* fIconBig;

    extern std::wstring journalPath;
    extern std::wstring debugLogPath;
    extern std::set<std::wstring> selectedGroups;   // v60: выбранные группы в Восстановлении
    extern std::vector<std::wstring> cachedGroups;  // v60: кэш GetJournalGroups()
    extern std::wstring activeGroupId;              // v61: активный group-id для SetDwordGrouped/SetStringGrouped
    extern int windowW, windowH;
    extern int tweaksScrollY;
    extern int gamesScrollY;
    extern bool showPlaceboTweaks;  // v58.4: показать "устаревшие" твики

    extern std::wstring cpuName;
    extern std::wstring gpuName;
    extern std::wstring totalRamText;
    extern ULONGLONG prevIdle, prevKernel, prevUser;
    extern int cpuLoad;
    extern int ramLoad;
    extern std::wstring cpuText;
    extern std::wstring ramText;

    // Мониторинг GPU / температур (-1 = нет данных, показываем N/A)
    extern int gpuLoad;
    extern double gpuVramUsed;
    extern double gpuVramTotal;
    extern int cpuTemp;
    extern int gpuTemp;

    extern int realProcessCount;
    extern int realStartupCount;

    // Результаты последнего вызова KillBackgroundApps — для отчёта в case 3000
    extern int lastKilledCount;
    extern int lastFailedCount;

    extern std::atomic<bool> busyJob;    // выполняется фоновая операция — вторую не запускаем
    extern std::atomic<bool> appClosing; // окно закрывается — воркеры не трогают UI

    extern float cpuHistory[60];
    extern int cpuHistoryIdx;

    extern HBITMAP hBufferBmp;
    extern HDC hBufferDC;
}

// ============================================================================
// Util: базовые файловые/строковые/реестровые утилиты.
// ============================================================================
namespace Util {
    std::string ToUtf8(const std::wstring& wstr);
    std::wstring FromUtf8(const std::string& str);
    std::string Base64Encode(const std::string& in);
    bool Base64Decode(const std::string& in, std::string& out);
    std::wstring ExeDir();
    std::wstring Join(const std::wstring& a, const std::wstring& b);
    std::wstring ReadRegString(HKEY root, const wchar_t* path, const wchar_t* name);
    bool FileExists(const std::wstring& path);
    std::string ReadFileToString(const std::wstring& path);
    bool WriteFileAtomic(const std::wstring& target, const std::string& content);
    std::wstring GetAcfValue(const std::string& content, const std::string& key);
    std::vector<std::wstring> GetJournalHistory();
}

// ============================================================================
// Запуск процессов (whitelisted wrappers). Реализация — core.cpp.
// ============================================================================
enum ProcWaitStatus { PWS_OK = 0, PWS_LAUNCH_FAILED = 1, PWS_EXIT_CODE = 2, PWS_TIMEOUT = 3, PWS_WAIT_ERROR = 4 };

ProcWaitStatus RunProcessWait(const wchar_t* exePath, const std::wstring& args, DWORD timeoutMs, DWORD& exitCode,
                              std::string* outCapture = nullptr, DWORD* lastErr = nullptr);
ProcWaitStatus RunCmdWait(const std::wstring& command, DWORD timeoutMs, DWORD& exitCode);
ProcWaitStatus RunPowerShellWait(const std::wstring& psCommand, DWORD timeoutMs, DWORD& exitCode,
                                 std::string* outCapture = nullptr);
std::string RunCaptureStdout(const std::wstring& cmdLine, DWORD timeoutMs);

bool BackupIfExists(const std::wstring& target);

// ============================================================================
// TimerMgr: общий счётчик владельцев таймера 0.5 ms.
// ============================================================================
namespace TimerMgr {
    bool Acquire(const wchar_t* owner);
    void Release(const wchar_t* owner);
    size_t OwnerCount();
}

// Реализация в system.cpp (низкоуровневый NtSetTimerResolution + журнал).
bool SetTimerResolution(bool enable);

// ============================================================================
// Состояние твиков / пользовательские настройки (реестр HKCU\Software\LEFTIK).
// ============================================================================
void SaveTweakState(int id, bool enabled);
void SetTweakStateById(int id, bool enabled);
void SaveBackgroundMode(bool enabled);
void LoadBackgroundMode();
void SaveStandbySettings();
void LoadStandbySettings();
