// ============================================================================
// harness_unit.cpp — объединённый TU харнесса LEFTIK (P17 restored, пакеты #17/#18).
// Механика утерянного P17-харнесса воспроизведена: единица включает РЕАЛЬНЫЕ
// исходники продакшена (core/system/tweaks/ui_main), затем перехватывает IAT
// собственного модуля: MessageBoxW, CreateProcessW, Reg* — эмуляция
// Win10/WSL/реестра. Тесты живут В РЕПОЗИТОРИИ (tests\), а не во временных
// папках — правило после утери исходников P17-харнесса.
// ============================================================================
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>

#include "harness.h"

int gl_pass = 0, gl_fail = 0;
void HarnessReport(const char* group, bool ok, const char* text) {
    if (ok) { ++gl_pass; printf("[PASS] %s: %s\n", group, text); }
    else    { ++gl_fail; printf("[FAIL] %s: %s\n", group, text); }
    fflush(stdout);
}

static HarnessIat g_iat;
HarnessIat* HarnessIatP() { return &g_iat; }

// ---------------------------------------------------------------------------
// 1) Реальные исходники одним TU — static-функции становятся видимы.
// ---------------------------------------------------------------------------
#define wWinMain harness_unused_wWinMain   // точка входа продакшена не нужна харнессу
#include "../core.cpp"
#include "../system.cpp"
#include "../tweaks.cpp"
#include "../ui_main.cpp"
#undef wWinMain

// --- обёртки для main ---
bool HarnessApplyTweakById(int id) {
    // 1044 (Edge) не входит в статический dispatch ApplyTweakById (только UI-switch).
    // P17-харнесс вызывал T_Edge непосредственно — воспроизводим generic-контракт
    // ApplyTweakById для него: включить → T_Edge → сверка живого реестра.
    if (id == 1044) {
        const int idx = TweakIndexById(id);
        if (idx < 0) return false;
        App::tweaksList[idx].isEnabled = true;
        SaveTweakState(id, true);
        T_Edge();
        if (!App::tweaksList[idx].isEnabled) return false;
        if (!ReadTweakState(id)) {
            App::tweaksList[idx].isEnabled = false;
            SaveTweakState(id, false);
            return false;
        }
        return true;
    }
    return ApplyTweakById(id);
}
int     HarnessTweakIndexById(int id) { return TweakIndexById(id); }
bool    HarnessReadTweakState(int id) { return ReadTweakState(id); }
void    HarnessInitTweaks()           { InitTweaks(); }
void    HarnessRefreshHardwareFlags() { RefreshHardwareFlags(); }
bool    HarnessToggleEnabled(int id) {
    for (auto& tw : App::tweaksList) if (tw.id == id) return tw.isEnabled;
    return false;
}
void    HarnessDispatchTweakClick(int id) {
    for (size_t i = 0; i < App::tweaksList.size(); ++i)
        if (App::tweaksList[i].id == id) { DispatchAction((int)(6000 + i)); return; }
}

// --- HW/Guard обёртки для assertions (чтение/запись полей App напрямую) ---
void HarnessHwWinBuildSet(int v)  { App::hwInfo.winBuild = v; }
int  HarnessHwWinBuildGet()       { return App::hwInfo.winBuild; }
void HarnessHwVbsEnabledSet(bool v) { App::hwInfo.vbsEnabled = v; }
bool HarnessHwVbsEnabledGet()     { return App::hwInfo.vbsEnabled; }
void HarnessHwGpuVendorSet(int v) { App::hwInfo.gpuVendor = (decltype(App::hwInfo.gpuVendor))v; }
int  HarnessHwGpuVendorGet()      { return (int)App::hwInfo.gpuVendor; }
void HarnessHwGpuNameSet(const wchar_t* n) { App::gpuName = n; }
std::wstring HarnessHwGpuNameGet() { return App::gpuName; }
void HarnessHwHardwareTierSet(int v) { App::g_hardwareTier = (decltype(App::g_hardwareTier))v; }
int  HarnessHwHardwareTierGet()   { return (int)App::g_hardwareTier; }
int  HarnessHardwareTierHighValue() { return (int)App::HardwareTier::High; }
void HarnessHwIsLaptopSet(bool v) { App::isLaptop = v; }
bool HarnessHwIsLaptopGet() { return App::isLaptop; }
bool HarnessXboxModeAvailableGet() { return App::hwInfo.xboxModeAvailable; }
void HarnessSetTweakState(int id, bool on) { SetTweakStateById(id, on); }
std::vector<int> HarnessBuildTurboTweakList() { return BuildTurboTweakList(); }
const wchar_t* HarnessGetTweakBlockReason(int id) { return GetTweakBlockReason(id); }
bool HarnessIsTweakWarningOnly(int id) { return IsTweakWarningOnly(id); }
bool HarnessIsTweakAllowedForHardware(int id) { return IsTweakAllowedForHardware(id); }
std::wstring HarnessTweakFullDesc(int id) {
    for (auto& tw : App::tweaksList) if (tw.id == id) return tw.fullDesc;
    return L"";
}
int  HarnessTweaksCount() { return (int)App::tweaksList.size(); }
int  HarnessWindowsBuild() { return GetWindowsBuild(); }

#ifndef LEFTIK_SOURCE_DIR
#define LEFTIK_SOURCE_DIR "E:/MyCppApp"
#endif
static std::wstring AsciiToWide(const char* s) {
    std::wstring w;
    while (*s) w += (wchar_t)(unsigned char)*s++;
    return w;
}
bool HarnessReadSourceFile(const char* relName, std::string& out, std::string& err) {
    std::wstring full = AsciiToWide(LEFTIK_SOURCE_DIR) + L"/" + AsciiToWide(relName);
    out = Util::ReadFileToString(full);
    if (out.size() < 500) { err = "source not readable: " + std::string(relName); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// 2) IAT-перехват собственного модуля.
// ---------------------------------------------------------------------------
static void* FindIatSlot(const wchar_t* dll, const char* fn) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return nullptr;
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + rva);
    for (; imp->Name; ++imp) {
        const char* name = (const char*)(base + imp->Name);
        std::wstring wide(name, name + strlen(name));
        if (_wcsicmp(wide.c_str(), dll) != 0) continue;     // имя dll совпало полностью (без регистра)
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA* orig   = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        for (; thunk && thunk->u1.Function; ++thunk, ++orig) {
            if (!(orig->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                IMAGE_IMPORT_BY_NAME* iibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
                if (_stricmp(iibn->Name, fn) == 0) return &thunk->u1.Function;
            }
        }
    }
    return nullptr;
}
static bool PatchIat(const wchar_t* dll, const char* fn, void* repl, void** orig) {
    void* slot = FindIatSlot(dll, fn);
    if (!slot) return false;
    void* cur = *(void**)slot;
    if (!cur) return false;
    if (orig) *orig = cur;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *(void**)slot = repl;
    VirtualProtect(slot, sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

// ---------------------------------------------------------------------------
// 3) Хуки.
// ---------------------------------------------------------------------------
typedef int (WINAPI *PfnMessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);
typedef BOOL (WINAPI *PfnCreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
typedef LONG (WINAPI *PfnRegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *PfnRegCreateKeyExW)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const SECURITY_ATTRIBUTES*, PHKEY, LPDWORD);
typedef LONG (WINAPI *PfnRegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *PfnRegSetValueExW)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD);
typedef BOOL (WINAPI *PfnIsUserAnAdmin)(void);

static PfnMessageBoxW      g_realMessageBoxW = nullptr;
static PfnCreateProcessW   g_realCreateProcessW = nullptr;
static PfnRegOpenKeyExW    g_realRegOpenKeyExW = nullptr;
static PfnRegCreateKeyExW  g_realRegCreateKeyExW = nullptr;
static PfnRegQueryValueExW g_realRegQueryValueExW = nullptr;
static PfnRegSetValueExW   g_realRegSetValueExW = nullptr;
static PfnIsUserAnAdmin    g_realIsUserAnAdmin = nullptr;

static std::wstring SandboxSubstitute(const wchar_t* path) {
    if (!path) return L"";
    std::wstring s(path);
    size_t at = s.find(L"Software\\LEFTIK");
    if (at != std::wstring::npos) {
        size_t chk = at + 15;
        if (chk == s.size() || s[chk] == L'\\')
            s = s.substr(0, at) + L"Software\\LEFTIK_Test" + (chk < s.size() ? s.substr(chk) : L"");
    }
    return s;
}
static bool IsCurrentBuildQuery(LPCWSTR valueName) {
    return valueName && wcscmp(valueName, L"CurrentBuildNumber") == 0;
}

static int WINAPI HookedMessageBoxW(HWND h, LPCWSTR text, LPCWSTR caption, UINT type) {
    HarnessMsg m{ h, text ? text : L"", caption ? caption : L"" };
    g_iat.mbLog.push_back(m);
    return g_iat.mbReturn;
}

static BOOL WINAPI HookedCreateProcessW(LPCWSTR, LPWSTR cmdLine, LPSECURITY_ATTRIBUTES sa1,
                                       LPSECURITY_ATTRIBUTES sa2, BOOL inherit, DWORD flags,
                                       LPVOID env, LPCWSTR cwd, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    ++g_iat.procCalls;
    g_iat.lastCmdLine = cmdLine ? cmdLine : L"";
    if (g_iat.procStub == HarnessIat::PROC_NONE)
        return g_realCreateProcessW(nullptr, cmdLine ? cmdLine : (LPWSTR)L"", sa1, sa2, inherit, flags, env, cwd, si, pi);
    // Stub: настоящий безобидный процесс с заданным кодом выхода (никогда
    // реальный powershell/bcdedit/sc — это защищает живую систему).
    // ВАЖНО: CreateProcessW изменяет командную строку на месте — только
    // изменяемый буфер (const-литерал дал бы access violation).
    std::wstring stub = (g_iat.procStub == HarnessIat::PROC_OK) ? std::wstring(L"cmd.exe /c exit 0")
                                                                : std::wstring(L"cmd.exe /c exit 1");
    return g_realCreateProcessW(nullptr, &stub[0], nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, si, pi);
}

static LONG WINAPI HookedRegOpenKeyExW(HKEY root, LPCWSTR parent, DWORD opts, REGSAM sam, PHKEY out) {
    std::wstring s(parent ? parent : L"");
    // Fake-ключи: перенаправляем запрошенный путь на реально существующий.
    auto fk = g_iat.fakeKeyMap.find(s);
    if (fk != g_iat.fakeKeyMap.end()) return g_realRegOpenKeyExW(root, fk->second.c_str(), opts, sam, out);
    if (g_iat.regSandbox && root == HKEY_CURRENT_USER) {
        std::wstring alt = SandboxSubstitute(s.c_str());
        if (alt != s) return g_realRegOpenKeyExW(root, alt.c_str(), opts, sam, out);
    }
    return g_realRegOpenKeyExW(root, parent, opts, sam, out);
}
static LONG WINAPI HookedRegCreateKeyExW(HKEY root, LPCWSTR parent, DWORD res, LPWSTR cls, DWORD opts,
                                         REGSAM sam, const SECURITY_ATTRIBUTES* sa, PHKEY out, LPDWORD dispo) {
    if (g_iat.regSandbox && root == HKEY_CURRENT_USER) {
        std::wstring alt = SandboxSubstitute(parent ? parent : L"");
        std::wstring orig(parent ? parent : L"");
        if (alt != orig)
            return g_realRegCreateKeyExW(root, alt.c_str(), res, cls, opts, sam, sa, out, dispo);
    }
    return g_realRegCreateKeyExW(root, parent, res, cls, opts, sam, sa, out, dispo);
}
static LONG WINAPI HookedRegQueryValueExW(HKEY k, LPCWSTR name, LPDWORD res, LPDWORD tp, LPBYTE data, LPDWORD sz) {
    if (g_iat.buildOverride && IsCurrentBuildQuery(name)) {
        // Эмуляция сборки: CurrentBuildNumber читается как REG_SZ.
        wchar_t buf[32];
        swprintf(buf, 32, L"%d", g_iat.buildOverride);
        size_t bytes = (wcslen(buf) + 1) * sizeof(wchar_t);
        if (tp) *tp = REG_SZ;
        if (sz && data) { if (bytes <= *sz) { memcpy(data, buf, bytes); *sz = (DWORD)bytes; return ERROR_SUCCESS; } *sz = (DWORD)bytes; return ERROR_MORE_DATA; }
        if (sz) { *sz = (DWORD)bytes; return ERROR_SUCCESS; }
        return ERROR_SUCCESS;
    }
    return g_realRegQueryValueExW(k, name, res, tp, data, sz);
}
static LONG WINAPI HookedRegSetValueExW(HKEY k, LPCWSTR name, DWORD res, DWORD tp, const BYTE* data, DWORD sz) {
    if (name && wcscmp(name, L"CurrentBuildNumber") == 0)
        return ERROR_ACCESS_DENIED; // защита: тесты НЕ переписывают реальный build
    return g_realRegSetValueExW(k, name, res, tp, data, sz);
}
static BOOL WINAPI HookedIsUserAnAdmin(void) {
    if (g_iat.adminAllowed) return TRUE;   // empathy: harness-сценарии требуют админ-контекст
    return g_realIsUserAnAdmin();
}

bool HarnessInstallHooks(std::string& err) {
    if (!PatchIat(L"user32.dll", "MessageBoxW", (void*)&HookedMessageBoxW, (void**)&g_realMessageBoxW)) { err = "MessageBoxW IAT patch failed"; return false; }
    g_iat.installedMessageBox = true;
    if (!PatchIat(L"kernel32.dll", "CreateProcessW", (void*)&HookedCreateProcessW, (void**)&g_realCreateProcessW)) { err = "CreateProcessW IAT patch failed"; return false; }
    g_iat.installedCreateProcess = true;
    if (!PatchIat(L"advapi32.dll", "RegOpenKeyExW", (void*)&HookedRegOpenKeyExW, (void**)&g_realRegOpenKeyExW)) { err = "RegOpenKeyExW IAT patch failed"; return false; }
    if (!PatchIat(L"advapi32.dll", "RegCreateKeyExW", (void*)&HookedRegCreateKeyExW, (void**)&g_realRegCreateKeyExW)) { err = "RegCreateKeyExW IAT patch failed"; return false; }
    if (!PatchIat(L"advapi32.dll", "RegQueryValueExW", (void*)&HookedRegQueryValueExW, (void**)&g_realRegQueryValueExW)) { err = "RegQueryValueExW IAT patch failed"; return false; }
    if (!PatchIat(L"advapi32.dll", "RegSetValueExW", (void*)&HookedRegSetValueExW, (void**)&g_realRegSetValueExW)) { err = "RegSetValueExW IAT patch failed"; return false; }
    g_iat.installedReg = true;
    // IsUserAnAdmin: если не найден в IAT — не фатально (админ-check идет через ui_main).
    if (PatchIat(L"shell32.dll", "IsUserAnAdmin", (void*)&HookedIsUserAnAdmin, (void**)&g_realIsUserAnAdmin))
        g_iat.installedAdmin = true;
    return true;
}

// ---------------------------------------------------------------------------
// 4) Песочница: journal/debug в C:\Temp\LEFTIK_Test + реестр под LEFTIK_Test.
// ---------------------------------------------------------------------------
static const wchar_t* kTestDir     = L"C:\\Temp\\LEFTIK_Test";
static const wchar_t* kSandboxPath = L"Software\\LEFTIK_Test";

bool HarnessSandboxInit(std::string& err) {
    if (!CreateDirectoryW(kTestDir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        err = "CreateDirectory(C:\\Temp\\LEFTIK_Test) failed";
        return false;
    }
    g_iat.regSandbox = true;
    App::journalPath = std::wstring(kTestDir) + L"\\p17_journal.log";
    App::debugLogPath = std::wstring(kTestDir) + L"\\p17_debug.log";
    return true;
}
bool HarnessSandboxTeardown(std::string& err) {
    // regSandbox выключаем: удалять LEFTIK_Test надо РЕАЛЬНЫМ вызовом.
    bool wasSandbox = g_iat.regSandbox;
    g_iat.regSandbox = false;
    LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, kSandboxPath);
    g_iat.regSandbox = wasSandbox;
    return (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND);
}
bool HarnessSandboxLeft(std::string&) { // осталось ли дерево (для teardown-assert)
    HKEY hk{};
    bool wasSandbox = g_iat.regSandbox;
    g_iat.regSandbox = false;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kSandboxPath, 0, KEY_READ, &hk);
    g_iat.regSandbox = wasSandbox;
    if (rc == ERROR_SUCCESS) { RegCloseKey(hk); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// 5) Fake Xbox-пакетные ключи: создаются РЕАЛЬНО в HKCU (RegEnumKeyExW NOT hooked).
//    Имена всегда с суффиксом _FAKE — конфликтовать с настоящими пакетами невозможно.
// ---------------------------------------------------------------------------
static const wchar_t* kRepoPath = L"Software\\Classes\\Local Settings\\Software\\"
                                  L"Microsoft\\Windows\\CurrentVersion\\AppModel\\"
                                  L"Repository\\Packages";

bool HarnessFakeXboxCreate(const wchar_t* pkgName, HKEY& out, std::wstring& createdPath, std::string& err) {
    std::wstring full = std::wstring(kRepoPath) + L"\\" + pkgName;
    HKEY hk{};
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, full.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &hk, nullptr);
    if (rc != ERROR_SUCCESS) { err = "RegCreateKeyExW(fake xbox pkg) rc=" + std::to_string(rc); return false; }
    out = hk;
    createdPath = full;
    return true;
}
bool HarnessFakeXboxDelete(const std::wstring& path, std::string& err) {
    LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND && rc != ERROR_PATH_NOT_FOUND) {
        err = "RegDeleteTreeW(fake xbox pkg) rc=" + std::to_string(rc);
        return false;
    }
    return true;
}
