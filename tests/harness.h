#pragma once
// ============================================================================
// LEFTIK harness (P17 2a restored, пакеты #17/#18 инфраструктура).
// История: исходники harness были утеряны вместе с temp-каталогом. Этот файл —
// восстановленная версия по логу прогона (48 PASS / 0 FAIL) и сводке отчёта.
// Тесты живут В РЕПОЗИТОРИИ (tests\), а не во временных папках — правило после инцидента.
// ============================================================================

#include <windows.h>
#include <string>
#include <vector>
#include <map>

// --- Раннер ---
extern int gl_pass, gl_fail;
void HarnessReport(const char* group, bool ok, const char* text);

#define HARNESS_CASE(group, cond, text) HarnessReport(group, (cond) != 0, text)

// Обёртки над внутренними (static) функциями tweaks.cpp — видны только из
// harness_unit.cpp (который включает tweaks.cpp напрямую).
bool    HarnessApplyTweakById(int id);
int     HarnessTweakIndexById(int id);
bool    HarnessReadTweakState(int id);
void    HarnessInitTweaks();
void    HarnessRefreshHardwareFlags();

// HW/Guard обёртки для assertions.
void  HarnessHwWinBuildSet(int v);
int   HarnessHwWinBuildGet();
void  HarnessHwVbsEnabledSet(bool v);
bool  HarnessHwVbsEnabledGet();
void  HarnessHwGpuVendorSet(int v);   // App::GpuVendor (GPU_NVIDIA=1, GPU_AMD=2)
int   HarnessHwGpuVendorGet();
void  HarnessHwGpuNameSet(const wchar_t* n);
std::wstring HarnessHwGpuNameGet();
void  HarnessHwHardwareTierSet(int v);
int   HarnessHwHardwareTierGet();
int   HarnessHardwareTierHighValue();
void  HarnessHwGpuNameSet(const wchar_t* n);
void  HarnessHwHardwareTierSet(int v);
void  HarnessHwIsLaptopSet(bool v);
bool  HarnessHwIsLaptopGet();
bool  HarnessXboxModeAvailableGet();
void  HarnessSetTweakState(int id, bool on);
std::vector<int> HarnessBuildTurboTweakList();
const wchar_t* HarnessGetTweakBlockReason(int id);
bool  HarnessIsTweakWarningOnly(int id);
bool  HarnessIsTweakAllowedForHardware(int id);
std::wstring HarnessTweakFullDesc(int id);
int   HarnessTweaksCount();
int   HarnessWindowsBuild();
bool  HarnessReadSourceFile(const char* relName, std::string& out, std::string& err);

// Контроль hook-политики (см. harness_unit.cpp).
struct HarnessMsg { HWND hwnd; std::wstring text, caption; };
struct HarnessIat {
    bool installedMessageBox = false;
    bool installedCreateProcess = false;
    bool installedReg = false;
    bool installedAdmin = false;   // IsUserAnAdmin
    bool adminAllowed = true;      // harness-сценарии требуют админ-контекст
    // MessageBoxW: что вернуть + журнал показанных сообщений
    int  mbReturn = IDYES;
    std::vector<HarnessMsg> mbLog;
    // CreateProcessW
    enum ProcStub { PROC_NONE, PROC_OK, PROC_EXIT1 } procStub = PROC_NONE;
    int  procCalls = 0;
    std::wstring lastCmdLine;
    // Registry: песочница HKCU Software\LEFTIK -> LEFTIK_Test
    bool regSandbox = false;
    // Эмуляция сборки Windows (0 = реальный build, иначе CurrentBuildNumber override)
    int  buildOverride = 0;
    // Fake открытые ключи: запрошенный путь -> реальный фейковый путь
    std::map<std::wstring, std::wstring> fakeKeyMap;
};
extern HarnessIat* HarnessIatP();

// Установка хуков / песочница.
bool HarnessInstallHooks(std::string& err);
bool HarnessSandboxInit(std::string& err);
bool HarnessSandboxTeardown(std::string& err);
bool HarnessSandboxLeft(std::string& err);

// Fake Xbox-пакеты (RegEnumKeyExW НЕ хукается — создаём реальные ключи HKCU
// с суффиксом _FAKE, чтобы не пересекаться с настоящими пакетами).
bool HarnessFakeXboxCreate(const wchar_t* pkgName, HKEY& out, std::wstring& createdPath, std::string& err);
bool HarnessFakeXboxDelete(const std::wstring& fullCreatedPath, std::string& err);

// Вспомогательное для состояния тумблера.
bool HarnessToggleEnabled(int id);
// Совершить DispatchAction по клику тумблера (UI-путь 6000+idx).
void HarnessDispatchTweakClick(int id);
