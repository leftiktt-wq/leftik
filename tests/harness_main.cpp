// ============================================================================
// harness_main.cpp — восстановленный набор из 48 assertions (P17 2a: Hotfix 1.1
// + 2a.1..2a.4). Соответствие оригинальному логу:
//   Hotfix 1.1  (6 кейсов):  23 assertions
//     1. Win11 Edge 1044      — 4
//     2. Win10 Edge success   — 5
//     3. Win10 Edge failure   — 4
//     4. VBS 1017 cancel      — 5
//     5. UI Edge Win11        — 2
//     6. HAGS old-GPU guard   — 3   (guard-level, HAGS реально НЕ применяется)
//   2a.1 HVCI 1018 (1 кейс):  7
//   2a.2 HAGS/Turbo (3):      7   (Turbo Win11 3, Turbo Win10 2, HAGS desc 2)
//   2a.3 Xbox mode (5):       5
//   2a.4 Intel APO (4):       4
//   Teardown (2):             2
//   Итого:                   48
// CreateProcessW-стаб — НИКОГДА реальный powershell/bcdedit/sc (защита системы).
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "harness.h"

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static bool MbContains(const wchar_t* sub) {
    for (const auto& m : HarnessIatP()->mbLog)
        if (m.text.find(sub) != std::wstring::npos) return true;
    return false;
}
static void ResetAll() { HarnessIatP()->mbLog.clear(); HarnessIatP()->procCalls = 0; }
static void SetMbReturn(int v) { HarnessIatP()->mbReturn = v; }
static int  IndexOf(int id) { return HarnessTweakIndexById(id); }
static bool InList(const std::vector<int>& v, int id) {
    for (int x : v) if (x == id) return true;
    return false;
}

// Снимок/восстановление HW-полей, которые тесты мутают.
struct HwSnapshot {
    int winBuild = 0;
    bool vbsEnabled = false;
    int gpuVendor = 0;
    std::wstring gpuName;
    int hardwareTier = 0;
    bool isLaptop = false;
};
static HwSnapshot SnapHw() {
    HwSnapshot s;
    s.winBuild      = HarnessHwWinBuildGet();
    s.vbsEnabled    = HarnessHwVbsEnabledGet();
    s.gpuVendor     = HarnessHwGpuVendorGet();
    s.gpuName       = HarnessHwGpuNameGet();
    s.hardwareTier  = HarnessHwHardwareTierGet();
    s.isLaptop      = HarnessHwIsLaptopGet();
    if (s.gpuName.empty()) s.gpuName = L"";
    return s;
}
static void RestoreHw(const HwSnapshot& s) {
    HarnessHwWinBuildSet(s.winBuild);
    HarnessHwVbsEnabledSet(s.vbsEnabled);
    HarnessHwGpuVendorSet(s.gpuVendor);
    HarnessHwGpuNameSet(s.gpuName.c_str());
    HarnessHwHardwareTierSet(s.hardwareTier);
    HarnessHwIsLaptopSet(s.isLaptop);
    HarnessIatP()->buildOverride = 0;
    HarnessIatP()->procStub = HarnessIat::PROC_NONE;
    SetMbReturn(IDYES);
    HarnessIatP()->fakeKeyMap.clear();
}

static std::wstring g_fakeGamingAppPath;  // активный fake-ключ для teardown

// ===========================================================================
// Hotfix 1.1 — 6 кейсов, 23 assertions
// ===========================================================================

// 1) Win11 Edge 1044: T_Edge блокирует на 26300, показывая сообщение.
static void Case_Edge_Win11() {
    const char* g = "Win11 Edge 1044";
    ResetAll();
    SetMbReturn(IDOK);
    HarnessSetTweakState(1044, false);
    HarnessIatP()->buildOverride = 26300;   // детерминированно: путь Win11
    bool ok = HarnessApplyTweakById(1044);
    HARNESS_CASE(g, ok == false,                        "ApplyTweakById(1044) returns false on Win11 gate");
    HARNESS_CASE(g, !HarnessReadTweakState(1044),       "state 1044 == 0 after Win11 gate");
    HARNESS_CASE(g, !HarnessToggleEnabled(1044),        "toggle 1044 stays OFF after Win11 gate");
    HARNESS_CASE(g, MbContains(L"Windows 11"),          "Win11-block message captured");
    HarnessIatP()->buildOverride = 0;
}

// 2) Win10 Edge success: CreateProcessW-стаб возвращает 0 — Edge «удалён».
static void Case_Edge_Win10_Success() {
    const char* g = "Win10 Edge 1044";
    ResetAll();
    SetMbReturn(IDOK);
    HarnessSetTweakState(1044, false);
    HarnessIatP()->buildOverride = 19045;
    HarnessIatP()->procStub = HarnessIat::PROC_OK;
    bool ok = HarnessApplyTweakById(1044);
    HARNESS_CASE(g, ok == true,                                   "ApplyTweakById(1044) returns true on Win10 path");
    HARNESS_CASE(g, HarnessReadTweakState(1044) == true,          "state 1044 == 1 after success");
    HARNESS_CASE(g, HarnessToggleEnabled(1044) == true,           "toggle 1044 ON after success");
    HARNESS_CASE(g, HarnessIatP()->procCalls == 1,                "critical-process attempted exactly once (stub)");
    HARNESS_CASE(g, !MbContains(L"Windows 11"),                   "no Win11-block warning on Win10 path");
    HarnessIatP()->procStub = HarnessIat::PROC_NONE;
    HarnessIatP()->buildOverride = 0;
}

// 3) Win10 Edge failure: stub exit 1 → RunCriticalProcess проваливается →
//    тумблер честно сбрасывается.
static void Case_Edge_Win10_Failure() {
    const char* g = "Win10 Edge fail";
    ResetAll();
    SetMbReturn(IDOK);
    HarnessSetTweakState(1044, true);   // сценарий: тумблер включён, потом команда провалилась
    HarnessIatP()->buildOverride = 19045;
    HarnessIatP()->procStub = HarnessIat::PROC_EXIT1;
    bool ok = HarnessApplyTweakById(1044);
    HARNESS_CASE(g, ok == false,                      "ApplyTweakById(1044) returns false after critical-process fail");
    HARNESS_CASE(g, !HarnessReadTweakState(1044),     "state 1044 == 0 after fail");
    HARNESS_CASE(g, !HarnessToggleEnabled(1044),      "toggle 1044 OFF after fail");
    HARNESS_CASE(g, MbContains(L"PowerShell"),        "critical-process failure message captured");
    HarnessIatP()->procStub = HarnessIat::PROC_NONE;
    HarnessIatP()->buildOverride = 0;
}

// 4) VBS 1017: WSL-путь виден (fakeKeyMap), пользователь отменяет → bcdedit НЕ вызывается.
static void Case_VBS_Cancel() {
    const char* g = "VBS 1017 cancel";
    ResetAll();
    SetMbReturn(IDNO);
    // Суррогат Lxss: реальный ключ в песочнице, открытый путь — через fakeKeyMap.
    HKEY h{};
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK_Test\\LxssDummy", 0, nullptr, 0,
                              KEY_READ | KEY_WRITE, nullptr, &h, nullptr);
    if (rc == ERROR_SUCCESS) RegCloseKey(h);
    HarnessIatP()->fakeKeyMap[L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"] =
        L"Software\\LEFTIK_Test\\LxssDummy";
    HarnessSetTweakState(1017, true);
    bool ok = HarnessApplyTweakById(1017);
    HARNESS_CASE(g, ok == false,                  "ApplyTweakById(1017) returns false on cancel");
    HARNESS_CASE(g, !HarnessReadTweakState(1017), "state 1017 == 0 after cancel");
    HARNESS_CASE(g, !HarnessToggleEnabled(1017),  "toggle 1017 OFF after cancel");
    HARNESS_CASE(g, MbContains(L"WSL"),           "WSL/Docker warning shown");
    HARNESS_CASE(g, HarnessIatP()->procCalls == 0, "bcdedit NOT invoked on cancel");
    HarnessIatP()->fakeKeyMap.clear();
}

// 5) UI-путь: реальный DispatchAction(6000+idx) на Win11 — тумблер остаётся выключенным.
static void Case_UI_Edge_Win11() {
    const char* g = "UI Edge 1044 Win11";
    ResetAll();
    SetMbReturn(IDOK);
    HarnessSetTweakState(1044, false);
    HarnessIatP()->buildOverride = 26300;
    int idx = IndexOf(1044);
    HARNESS_CASE(g, idx >= 0,                     "UI click index for 1044 found");
    HarnessDispatchTweakClick(1044);
    HARNESS_CASE(g, !HarnessReadTweakState(1044), "state 1044 == 0 after UI click on Win11");
    HarnessIatP()->buildOverride = 0;
}

// 6) HAGS 1074 old-GPU guard (guard-level; HAGS реально НЕ применяем).
static void Case_Hags_OldGpu() {
    const char* g = "HAGS 1074 old GPU";
    HarnessHwGpuNameSet(L"NVIDIA GeForce GTX 1060 6GB");
    HarnessHwGpuVendorSet(1 /* GPU_NVIDIA */);
    const wchar_t* reason = HarnessGetTweakBlockReason(1074);
    HARNESS_CASE(g, reason != nullptr, "block reason present for old GTX 10xx");
    HARNESS_CASE(g, reason && wcsstr(reason, L"снижает FPS") != nullptr,
                 "reason names the 5-10% FPS regression honestly");
    HARNESS_CASE(g, !HarnessIsTweakAllowedForHardware(1074), "guard blocks HAGS on old GPU tier");
    // GPU-поля восстановит общий RestoreHw(hw0) после секции.
}

// ===========================================================================
// 2a.1 — HVCI 1018, 7 assertions
// ===========================================================================
static void Case_HVCI() {
    const char* g = "HVCI 1018";
    HarnessHwWinBuildSet(26300);
    HarnessHwVbsEnabledSet(true);
    const wchar_t* reason = HarnessGetTweakBlockReason(1018);
    HARNESS_CASE(g, reason != nullptr,                                  "block/warning text present on 26300+vbs");
    HARNESS_CASE(g, reason && wcsstr(reason, L"Vanguard") != nullptr,   "warning mentions Vanguard anti-cheat");
    HARNESS_CASE(g, HarnessIsTweakWarningOnly(1018) == true,            "warning-only flag true (2a.1)");
    HARNESS_CASE(g, HarnessIsTweakAllowedForHardware(1018) == true,     "NOT hard-blocked (user decides consciously)");
    HarnessHwVbsEnabledSet(false);
    HARNESS_CASE(g, HarnessGetTweakBlockReason(1018) == nullptr,        "no warning when HVCI is off");
    HarnessHwWinBuildSet(19045);
    HarnessHwVbsEnabledSet(true);
    HARNESS_CASE(g, HarnessIsTweakWarningOnly(1018) == false,           "no warning-only on Win10 build");
    HarnessHwWinBuildSet(26300);
    HARNESS_CASE(g, HarnessIsTweakWarningOnly(1018) == true,            "state restored: warning-only again");
}

// ===========================================================================
// 2a.2 — HAGS/Turbo, 7 assertions
// ===========================================================================
static void Case_Turbo_Win11() {
    const char* g = "Turbo Win11";
    HarnessHwWinBuildSet(26300);
    HarnessHwHardwareTierSet(HarnessHardwareTierHighValue());
    HarnessHwIsLaptopSet(false);
    auto list = HarnessBuildTurboTweakList();
    HARNESS_CASE(g, InList(list, 1121),    "contains 1121 Widgets on Win11");
    HARNESS_CASE(g, !InList(list, 1074),   "excludes 1074 HAGS (1 Гц stutter regression)");
    HARNESS_CASE(g, InList(list, 1017),    "contains 1017 VBS on non-Low tier");
}
static void Case_Turbo_Win10() {
    const char* g = "Turbo Win10";
    HarnessHwWinBuildSet(19045);
    auto list = HarnessBuildTurboTweakList();
    HARNESS_CASE(g, InList(list, 1074),    "contains 1074 HAGS on Win10");
    HARNESS_CASE(g, !InList(list, 1121),   "excludes 1121 Widgets on Win10");
}
static void Case_Hags_Desc() {
    const char* g = "HAGS desc";
    std::wstring d = HarnessTweakFullDesc(1074);
    HARNESS_CASE(g, d.find(L"1 Гц") != std::wstring::npos,        "fullDesc mentions 1 Гц stutter");
    HARNESS_CASE(g, d.find(L"Windows 11") != std::wstring::npos,  "fullDesc names Windows 11 regression");
}

// ===========================================================================
// 2a.3 — Xbox mode, 5 assertions
// ===========================================================================
static void Case_Xbox(std::string& ferr) {
    const char* g = "Xbox 2a.3";
    HKEY h{}; std::wstring p1;
    if (!HarnessFakeXboxCreate(L"Microsoft.GamingApp_8wekyb3d8bbwe_FAKE", h, p1, ferr)) {
        HARNESS_CASE(g, false, "fake GamingApp package key created");
        return;
    }
    if (h) RegCloseKey(h);
    HARNESS_CASE(g, true, "fake GamingApp package key created");
    HarnessRefreshHardwareFlags();
    HARNESS_CASE(g, HarnessXboxModeAvailableGet(), "Xbox: detected GamingApp package on 24H2+");

    HarnessFakeXboxDelete(p1, ferr);
    HKEY h2{}; std::wstring p2;
    HarnessFakeXboxCreate(L"Microsoft.Xbox.TCUI_8wekyb3d8bbwe_FAKE", h2, p2, ferr);
    if (h2) RegCloseKey(h2);
    HarnessRefreshHardwareFlags();
    HARNESS_CASE(g, !HarnessXboxModeAvailableGet(), "Xbox: TCUI alone does NOT enable Xbox mode");
    HarnessFakeXboxDelete(p2, ferr);

    HKEY h3{}; std::wstring p3;
    if (!HarnessFakeXboxCreate(L"Microsoft.GamingApp_8wekyb3d8bbwe_FAKE", h3, p3, ferr)) {
        HARNESS_CASE(g, false, "Xbox test: fake package recreated for Win10 check");
    } else {
        if (h3) RegCloseKey(h3);
        HARNESS_CASE(g, true, "Xbox test: fake package recreated for Win10 check");
    }
    g_fakeGamingAppPath = p3;
    // Win10-гейт: fake пакет есть, но build < 26100 → mode недоступен.
    HarnessIatP()->buildOverride = 19045;
    HarnessRefreshHardwareFlags();
    HARNESS_CASE(g, !HarnessXboxModeAvailableGet(), "Xbox: build gate keeps Win10 false");
    HarnessIatP()->buildOverride = 0;
}

// ===========================================================================
// 2a.4 — Intel APO (текстовая рекомендация, детект и клик убраны), 4 assertions
// ===========================================================================
static void Case_APO() {
    std::string sys, ui, coreH, ferr;
    HARNESS_CASE("Intel APO", HarnessReadSourceFile("system.cpp", sys, ferr), "APO: patched sources readable for policy checks");
    HARNESS_CASE("Intel APO", sys.find("Intel APO") != std::string::npos,
                 "APO: service-name detection removed from system.cpp (removal pin present)");
    HARNESS_CASE("Intel APO",
                 HarnessReadSourceFile("core.hpp", coreH, ferr) &&
                     coreH.find("isIntelPlatform") != std::string::npos &&
                     coreH.find("apoInstalled") == std::string::npos,
                 "APO: HardwareInfo keeps isIntelPlatform, no APO-installed field");
    bool bannerTextOnly = false;
    if (HarnessReadSourceFile("ui_main.cpp", ui, ferr)) {
        size_t pos = ui.find("Intel APO");
        if (pos != std::string::npos) {
            // Баннер: литерал apoTxt, затем вперёд — вызов DrawTextGdi (только текст).
            size_t call = ui.find("DrawTextGdi", pos);
            size_t hit  = ui.find("AddHit", pos);
            bannerTextOnly = (call != std::string::npos && call - pos < 600) &&
                             (hit == std::string::npos || hit > call);
        }
    }
    HARNESS_CASE("Intel APO", bannerTextOnly, "APO: Intel+CS2/Dota banner is a text recommendation, no click/detect");
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    std::string err;
    if (!HarnessInstallHooks(err)) { printf("[FATAL] hooks: %s\n", err.c_str()); return 1; }
    if (!HarnessSandboxInit(err))  { printf("[FATAL] sandbox: %s\n", err.c_str()); return 1; }
    HarnessIatP()->adminAllowed = true;
    HarnessInitTweaks();
    HarnessRefreshHardwareFlags();

    printf("[ENV] tweaks=%d winBuild=%d vbs=%d adminHook=%d\n",
           HarnessTweaksCount(), HarnessWindowsBuild(),
           (int)HarnessHwVbsEnabledGet(), (int)HarnessIatP()->installedAdmin);
    fflush(stdout);

    HwSnapshot hw0 = SnapHw();

    // Hotfix 1.1 (23)
    Case_Edge_Win11();         RestoreHw(hw0); HarnessSetTweakState(1044, false);
    Case_Edge_Win10_Success(); RestoreHw(hw0); HarnessSetTweakState(1044, false);
    Case_Edge_Win10_Failure(); RestoreHw(hw0); HarnessSetTweakState(1044, false);
    Case_VBS_Cancel();         RestoreHw(hw0); HarnessSetTweakState(1017, false);
    Case_UI_Edge_Win11();      RestoreHw(hw0); HarnessSetTweakState(1044, false);
    Case_Hags_OldGpu();        RestoreHw(hw0);

    // 2a.1 HVCI (7) — сам восстанавливает состояние
    Case_HVCI();               RestoreHw(hw0);

    // 2a.2 HAGS/Turbo (7)
    Case_Turbo_Win11();
    Case_Turbo_Win10();
    Case_Hags_Desc();          RestoreHw(hw0);

    // 2a.3 Xbox (5)
    Case_Xbox(err);            RestoreHw(hw0);

    // 2a.4 Intel APO (4)
    Case_APO();

    // Teardown (2)
    err.clear();
    if (!g_fakeGamingAppPath.empty()) HarnessFakeXboxDelete(g_fakeGamingAppPath, err);
    HARNESS_CASE("teardown", g_fakeGamingAppPath.empty() || err.find("rc=") == std::string::npos,
                 "fake Xbox package removed");
    err.clear();
    bool keyGone = false;
    if (HarnessSandboxTeardown(err)) keyGone = !HarnessSandboxLeft(err);
    HARNESS_CASE("teardown", keyGone, "LEFTIK_Test state key removed");

    printf("\ntotal assertions: %d (48 expected)\n", gl_pass + gl_fail);
    printf("pass=%d fail=%d\n", gl_pass, gl_fail);
    printf("%s\n", gl_fail == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED");
    printf("test journal: C:\\Temp\\LEFTIK_Test\\p17_journal.log\n");
    fflush(stdout);
    return gl_fail == 0 ? 0 : 1;
}
