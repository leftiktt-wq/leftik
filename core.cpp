#include "core.hpp"


namespace App {
    const wchar_t TITLE[] = L"LEFTIK | Безопасная оптимизация с откатом";
    const wchar_t CLASS_NAME[] = L"LEFTIK_UI_V59_1";

    // ==== Новая палитра (v57.1) ====
    const Color UI_BG        = Color(255, 15, 15, 18);
    const Color UI_SIDEBAR   = Color(255, 20, 20, 25);
    const Color UI_CARD      = Color(255, 26, 26, 32);
    const Color UI_CARD_HOVER= Color(255, 35, 35, 42);
    const Color UI_ACCENT    = Color(255, 123, 97, 255);
    const Color UI_GREEN     = Color(255, 0, 230, 118);
    const Color UI_TEXT      = Color(255, 240, 240, 245);
    const Color UI_MUTED     = Color(255, 130, 130, 140);

    const Color UI_BORDER      = Color(255, 43, 43, 54);
    const Color UI_ACCENT_DIM  = Color(255, 42, 35, 76);
    const Color UI_GREEN_DIM   = Color(255, 21, 76, 57);
    const Color UI_BODY_TEXT   = Color(255, 202, 202, 214);

    // Алиасы: старые экраны автоматически получают новую палитру.
    const Color BG        = UI_BG;
    const Color SIDEBAR   = UI_SIDEBAR;
    const Color PANEL     = UI_CARD;
    const Color PANEL_HOVER = UI_CARD_HOVER;
    const Color BORDER    = UI_BORDER;
    const Color TEXT      = UI_TEXT;
    const Color MUTED     = UI_MUTED;
    const Color ACCENT    = UI_ACCENT;
    const Color GREEN     = UI_GREEN;
    const Color RED       = Color(255, 226, 84, 96);
    const Color YELLOW    = Color(255, 241, 196, 76);

    HardwareInfo hwInfo;

    HWND hwnd{};
    Page page = Dashboard;
    Category activeCategory = CAT_BASIC;
    std::vector<Hit> hits;
    std::vector<TweakItem> tweaksList;
    std::vector<Game> installedGames;
    int selectedTweakIdx = -1;
    bool isLaptop = false;
    std::vector<Game> steamGames;
    std::map<std::wstring, Image*> gameImages;

    bool cleanTemp = true;
    bool cleanPrefetch = true;
    bool cleanRecycle = true;
    bool cleanLogs = true;

    ULONGLONG totalBytesFreed = 0;
    int totalFilesDeleted = 0;

    bool isMaxActive = false;
    bool isUltraActive = false;
    bool magicBoostActive = false;
    bool turboBoostActive = false;  // v59: Турбо-режим
    bool lastPowerState = true;
    bool backgroundMode = false;
    bool exitRequested = false;

    // v58.0: Feature A/B state
    bool dnsPromptedThisSession = false;   // DNS-диалог показан в этой сессии
    bool autoStandbyClean = false;         // Feature B: авто-очистка standby
    int  standbyIntervalMin = 30;          // Feature B: интервал в минутах

    HardwareTier g_hardwareTier = HardwareTier::Mid;

    FontFamily* fontFamily = nullptr;
    Font* fRegular = nullptr;
    Font* fBold = nullptr;
    Font* fTitle = nullptr;
    Font* fSmall = nullptr;
    Font* fH1 = nullptr;
    Font* fH2 = nullptr;
    Font* fMetric = nullptr;
    Font* fNav = nullptr;
    Font* fBody = nullptr;
    Font* fBodyBold = nullptr;
    FontFamily* iconFamily = nullptr;
    Font* fIcon = nullptr;
    Font* fIconBig = nullptr;

    std::wstring journalPath;
    std::wstring debugLogPath = L"C:\\ProgramData\\LEFTIK\\leftik_debug.log";
    std::set<std::wstring> selectedGroups;   // v60: выбранные группы в Восстановлении
    std::vector<std::wstring> cachedGroups;  // v60: кэш GetJournalGroups()
    std::wstring activeGroupId;              // v61: активный group-id для SetDwordGrouped/SetStringGrouped
    int windowW = 1584, windowH = 861;
    int tweaksScrollY = 0;
    int gamesScrollY = 0;
    bool showPlaceboTweaks = false;  // v58.4: показать "устаревшие" твики

    std::wstring cpuName = L"Загрузка...";
    std::wstring gpuName = L"Загрузка...";
    std::wstring totalRamText = L"0 GB";
    ULONGLONG prevIdle = 0, prevKernel = 0, prevUser = 0;
    int cpuLoad = 0;
    int ramLoad = 0;
    std::wstring cpuText = L"0%";
    std::wstring ramText = L"0.0 / 16.0 GB";

    // Мониторинг GPU / температур (-1 = нет данных, показываем N/A)
    int gpuLoad = -1;
    double gpuVramUsed = 0.0;
    double gpuVramTotal = 0.0;
    int cpuTemp = -1;
    int gpuTemp = -1;

    int realProcessCount = 0;
    int realStartupCount = 0;

    // Результаты последнего вызова KillBackgroundApps — для отчёта в case 3000
    int lastKilledCount = 0;
    int lastFailedCount = 0;

    std::atomic<bool> busyJob{false};    // выполняется фоновая операция — вторую не запускаем
    std::atomic<bool> appClosing{false}; // окно закрывается — воркеры не трогают UI

    float cpuHistory[60] = {0};
    int cpuHistoryIdx = 0;

    HBITMAP hBufferBmp = nullptr;
    HDC hBufferDC = nullptr;
}

namespace Util {
    std::string ToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed <= 0) return std::string();
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    std::wstring FromUtf8(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
        if (size_needed <= 0) return std::wstring();
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    static const char* B64CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    // Base64 для журнала: '|' и переводы строк внутри полей больше не ломают парсер
    std::string Base64Encode(const std::string& in) {
        std::string out; out.reserve(((in.size() + 2) / 3) * 4);
        int val = 0, valb = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c; valb += 8;
            while (valb >= 0) { out.push_back(B64CHARS[(val >> valb) & 0x3F]); valb -= 6; }
        }
        if (valb > -6) out.push_back(B64CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }
    bool Base64Decode(const std::string& in, std::string& out) {
        out.clear();
        int vals[256] = {0}; bool known[256] = {};
        for (int i = 0; i < 64; ++i) { vals[(unsigned char)B64CHARS[i]] = i; known[(unsigned char)B64CHARS[i]] = true; }
        int val = 0, valb = -8;
        for (unsigned char c : in) {
            if (c == '=') break;
            if (!known[c]) return false;
            val = (val << 6) + vals[c]; valb += 6;
            if (valb >= 0) { out.push_back((char)((val >> valb) & 0xFF)); valb -= 8; }
        }
        return true;
    }

    std::wstring ExeDir() {
        wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
        std::wstring p(b); return p.substr(0, p.find_last_of(L"\\/"));
    }
    std::wstring Join(const std::wstring& a, const std::wstring& b) {
        if (a.empty()) return b;
        if (b.empty()) return a;
        return a.back() == L'\\' ? a + b : a + L"\\" + b;
    }
    
    std::wstring ReadRegString(HKEY root, const wchar_t* path, const wchar_t* name) {
        HKEY k{}; if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return L"";
        wchar_t b[32768]{}; DWORD size = sizeof(b); LONG rc = RegQueryValueExW(k, name, nullptr, nullptr, (BYTE*)b, &size);
        RegCloseKey(k); return rc == ERROR_SUCCESS ? std::wstring(b) : L"";
    }

    bool FileExists(const std::wstring& path) {
        DWORD dwAttrib = GetFileAttributesW(path.c_str());
        return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
    }

    std::string ReadFileToString(const std::wstring& path) {
        std::string result;
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize != INVALID_FILE_SIZE && fileSize < 10 * 1024 * 1024) {
                std::vector<char> buffer(fileSize + 1, 0);
                DWORD bytesRead = 0;
                if (ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL)) {
                    result.assign(buffer.data(), bytesRead);
                }
            }
            CloseHandle(hFile);
        }
        return result;
    }

    // Атомарная запись: temp-файл + MoveFileEx(REPLACE_EXISTING). При сбое целевой не тронут.
    bool WriteFileAtomic(const std::wstring& target, const std::string& content) {
        std::wstring tmp = target + L".leftik_tmp";
        {
            std::ofstream out(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out.write(content.data(), (std::streamsize)content.size());
            out.flush();
            if (!out.good()) { out.close(); DeleteFileW(tmp.c_str()); return false; }
        }
        if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmp.c_str());
            return false;
        }
        return true;
    }

    std::wstring GetAcfValue(const std::string& content, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = content.find(searchKey);
        if (pos == std::string::npos) return L"";
        size_t start = content.find('"', pos + searchKey.length());
        if (start == std::string::npos) return L"";
        size_t end = content.find('"', start + 1);
        if (end == std::string::npos) return L"";
        std::string val = content.substr(start + 1, end - start - 1);
        return FromUtf8(val);
    }

    std::vector<std::wstring> GetJournalHistory() {
        std::vector<std::wstring> history;
        std::string content = ReadFileToString(App::journalPath);
        std::stringstream ss(content);
        std::string line;
        std::vector<std::string> allLines;
        while (std::getline(ss, line)) {
            if (!line.empty()) allLines.push_back(line);
        }
        // Новые записи сверху
        int count = 0;
        for (int i = (int)allLines.size() - 1; i >= 0 && count < 15; --i) {
            std::stringstream ls(allLines[i]);
            std::string tok0;
            std::getline(ls, tok0, '|');
            if (tok0 == "V2") {
                std::string kind; std::getline(ls, kind, '|');
                if (kind == "MARK") continue; // служебная граница — не операция
                if (kind == "ACTION") {
                    std::string f1, f2;
                    std::getline(ls, f1, '|'); // b64 actionName
                    std::getline(ls, f2, '|'); // b64 revertCmd
                    std::string d1, d2;
                    if (!Base64Decode(f1, d1) || !Base64Decode(f2, d2)) {
                        history.push_back(L"(поврежденная запись журнала)");
                    } else {
                        history.push_back(FromUtf8(d1));
                    }
                    count++;
                    continue;
                } else if (kind == "REG_VALUE") {
                    // Формат: V2|REG_VALUE|hive|b64path|b64name|keyEx|valEx|type|b64data|b64tweakName (10 токенов)
                    std::string f1, f2, b64name, keyEx, valEx, type, b64data, b64tweakName;
                    std::getline(ls, f1, '|');           // hive (HKLM / HKCU)
                    std::getline(ls, f2, '|');           // b64path
                    std::getline(ls, b64name, '|');      // b64name
                    std::getline(ls, keyEx, '|');        // keyEx
                    std::getline(ls, valEx, '|');        // valEx
                    std::getline(ls, type, '|');         // type
                    std::getline(ls, b64data, '|');      // b64data
                    std::getline(ls, b64tweakName, '|'); // b64tweakName
                    std::string dPath, dName, dTweakName;
                    if (!Base64Decode(f2, dPath) || !Base64Decode(b64name, dName) || !Base64Decode(b64tweakName, dTweakName)) {
                        history.push_back(L"(поврежденная запись журнала)");
                    } else {
                        history.push_back(FromUtf8(dTweakName) + L" [" + FromUtf8(dName) + L"]");
                    }
                    count++;
                    continue;
                } else {
                    history.push_back(L"(неизвестная запись v2)");
                    count++;
                    continue;
                }
            }
            if (tok0 == "V3") {
                std::string kind; std::getline(ls, kind, '|');
                if (kind == "MARK") continue;
                if (kind == "IMPORTED") {
                    std::string rest; std::getline(ls, rest);
                    history.push_back(L"[IMPORTED] " + FromUtf8(rest.substr(0, 48)));
                    count++;
                    continue;
                }
                if (kind == "ACTION") {
                    std::string b64grp, f1, f2;
                    std::getline(ls, b64grp, '|');
                    std::getline(ls, f1, '|');
                    std::getline(ls, f2, '|');
                    std::string dGrp, d1, d2;
                    if (!Base64Decode(b64grp, dGrp) || !Base64Decode(f1, d1) || !Base64Decode(f2, d2)) {
                        history.push_back(L"(поврежденная запись журнала)");
                    } else {
                        history.push_back(FromUtf8(d1));
                    }
                    count++;
                    continue;
                } else if (kind == "REG_VALUE") {
                    std::string b64grp, f1, f2, b64name, keyEx, valEx, type, b64data, b64tweakName;
                    std::getline(ls, b64grp, '|');
                    std::getline(ls, f1, '|');
                    std::getline(ls, f2, '|');
                    std::getline(ls, b64name, '|');
                    std::getline(ls, keyEx, '|');
                    std::getline(ls, valEx, '|');
                    std::getline(ls, type, '|');
                    std::getline(ls, b64data, '|');
                    std::getline(ls, b64tweakName, '|');
                    std::string dGrp, dPath, dName, dTweakName;
                    if (!Base64Decode(b64grp, dGrp) || !Base64Decode(f2, dPath) || !Base64Decode(b64name, dName) || !Base64Decode(b64tweakName, dTweakName)) {
                        history.push_back(L"(поврежденная запись журнала)");
                    } else {
                        history.push_back(FromUtf8(dTweakName) + L" [" + FromUtf8(dName) + L"]");
                    }
                    count++;
                    continue;
                } else if (kind == "BENCHMARK") {
                    std::string b64grp, sMean, sLow1, sLow01, sCnt, b64notes;
                    std::getline(ls, b64grp, '|');
                    std::getline(ls, sMean, '|');
                    std::getline(ls, sLow1, '|');
                    std::getline(ls, sLow01, '|');
                    std::getline(ls, sCnt, '|');
                    std::getline(ls, b64notes, '|');
                    history.push_back(L"[BENCHMARK] Avg " + FromUtf8(sMean) + L" FPS · 1% Low " + FromUtf8(sLow1) + L" · 0.1% Low " + FromUtf8(sLow01) + L" (" + FromUtf8(sCnt) + L" frames)");
                    count++;
                    continue;
                } else {
                    history.push_back(L"(неизвестная запись v3)");
                    count++;
                    continue;
                }
            }
            // Легаси-формат
            std::stringstream ls2(allLines[i]);
            std::string hive, path, name, exists, type, tweakName, data;
            std::getline(ls2, hive, '|');
            if (hive == "CMD") {
                std::getline(ls2, tweakName, '|');
                history.push_back(FromUtf8(tweakName));
            } else {
                std::getline(ls2, path, '|');
                std::getline(ls2, name, '|');
                std::getline(ls2, exists, '|');
                std::getline(ls2, type, '|');
                std::getline(ls2, tweakName, '|');
                history.push_back(FromUtf8(tweakName) + L" [" + FromUtf8(name) + L"]");
            }
            count++;
        }
        return history;
    }
}

bool BackupIfExists(const std::wstring& target) {
    if (!Util::FileExists(target)) return true;  // нечего бэкапить
    wchar_t stamp[32];
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf(stamp, 32, L"%04d%02d%02d_%02d%02d%02d_%03d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::wstring bak = target + L"." + stamp + L".bak";
    return CopyFileW(target.c_str(), bak.c_str(), FALSE) != 0;
}


// Запуск с ожиданием и РАЗЛИЧИМЫМ статусом. При таймауте процесс НЕ убивается
// (DISM/SFC/WU могут продолжать работу) — результат трактуется как "не подтвержден".
// outCapture (опционально) захватывает stdout+stderr.
ProcWaitStatus RunProcessWait(const wchar_t* exePath, const std::wstring& args, DWORD timeoutMs, DWORD& exitCode, std::string* outCapture, DWORD* lastErr) {
    exitCode = 0;
    std::wstring cmd = std::wstring(L"\"") + exePath + L"\" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    HANDLE rd = nullptr, wr = nullptr;
    if (outCapture) {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        if (!CreatePipe(&rd, &wr, &sa, 0)) { if (lastErr) *lastErr = GetLastError(); return PWS_LAUNCH_FAILED; }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = wr; si.hStdError = wr;
    }
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, outCapture ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (lastErr) *lastErr = GetLastError();
        if (rd) CloseHandle(rd);
        if (wr) CloseHandle(wr);
        return PWS_LAUNCH_FAILED;
    }
    if (wr) CloseHandle(wr);
    ProcWaitStatus st = PWS_OK;
    if (outCapture) {
        char buf[4096]; DWORD avail = 0;
        for (;;) {
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break; // канал закрыт
            if (avail > 0) {
                DWORD toRead = (avail < sizeof(buf)) ? avail : sizeof(buf), got = 0;
                if (ReadFile(rd, buf, toRead, &got, nullptr) && got) {
                    outCapture->append(buf, got);
                    if (outCapture->size() > 1024 * 1024) break;
                } else break;
            } else {
                DWORD w = WaitForSingleObject(pi.hProcess, 50);
                if (w == WAIT_OBJECT_0) {
                    while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                        DWORD got = 0;
                        if (!ReadFile(rd, buf, (avail < sizeof(buf)) ? avail : sizeof(buf), &got, nullptr) || !got) break;
                        outCapture->append(buf, got);
                    }
                    break;
                }
                if (w == WAIT_TIMEOUT) {
                    if (timeoutMs <= 50) { st = PWS_TIMEOUT; break; }
                    timeoutMs -= 50;
                    continue;
                }
                if (lastErr) *lastErr = GetLastError();
                st = PWS_WAIT_ERROR; break;
            }
        }
        CloseHandle(rd);
        if (st == PWS_OK) {
            DWORD ec = 1; GetExitCodeProcess(pi.hProcess, &ec); exitCode = ec;
            if (ec != 0) st = PWS_EXIT_CODE;
        }
    } else {
        DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs);
        if (w == WAIT_OBJECT_0) {
            DWORD ec = 1; GetExitCodeProcess(pi.hProcess, &ec); exitCode = ec;
            if (ec != 0) st = PWS_EXIT_CODE;
        } else if (w == WAIT_TIMEOUT) {
            st = PWS_TIMEOUT; // процесс жив, не трогаем
        } else {
            if (lastErr) *lastErr = GetLastError();
            st = PWS_WAIT_ERROR;
        }
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return st;
}

// command БЕЗ "/c" — добавляем "/d /s /c" ровно один раз (защита от /c /c)
ProcWaitStatus RunCmdWait(const std::wstring& command, DWORD timeoutMs, DWORD& exitCode) {
    std::wstring args = L"/d /s /c \"" + command + L"\"";
    return RunProcessWait(L"C:\\Windows\\System32\\cmd.exe", args, timeoutMs, exitCode);
}

ProcWaitStatus RunPowerShellWait(const std::wstring& psCommand, DWORD timeoutMs, DWORD& exitCode, std::string* outCapture) {
    // EncodedCommand требует Base64 от UTF-16LE байтов команды
    std::string raw((const char*)psCommand.c_str(), psCommand.size() * sizeof(wchar_t));
    std::string b64a = Util::Base64Encode(raw);
    std::wstring b64(b64a.begin(), b64a.end());
    std::wstring args = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + b64;
    return RunProcessWait(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", args, timeoutMs, exitCode, outCapture);
}

// Захват stdout (автоматически добавляет префикс C:\Windows\System32\, чтобы не запускались подмененные exe)
std::string RunCaptureStdout(const std::wstring& cmdLine, DWORD timeoutMs) {
    size_t sp = cmdLine.find(L' ');
    if (sp == std::wstring::npos) return "";
    std::wstring exe = cmdLine.substr(0, sp);
    if (exe.find(L'\\') == std::wstring::npos && exe.find(L'/') == std::wstring::npos) {
        exe = L"C:\\Windows\\System32\\" + exe;
        if (exe.size() < 4 || _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") != 0)
            exe += L".exe";
    }
    DWORD ec = 0; std::string out;
    RunProcessWait(exe.c_str(), cmdLine.substr(sp + 1), timeoutMs, ec, &out);
    return out;
}

namespace TimerMgr {
    static std::mutex s_mtx;
    static std::set<std::wstring> s_owners;
    // true = таймер активен после вызова (включен нами или уже был включен)
    bool Acquire(const wchar_t* owner) {
        std::lock_guard<std::mutex> lk(s_mtx);
        bool inserted = s_owners.insert(owner).second;
        if (inserted) {
            if (!SetTimerResolution(true)) {
                s_owners.erase(owner);
                return false;
            }
            return true;
        }
        return true; // уже активен по запросу другого владельца
    }
    void Release(const wchar_t* owner) {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (s_owners.erase(owner) && s_owners.empty())
            SetTimerResolution(false);
    }
    // Факт для дашборда: сколько активных владельцев у таймера 0.5ms
    size_t OwnerCount() {
        std::lock_guard<std::mutex> lk(s_mtx);
        return s_owners.size();
    }
}

void SaveTweakState(int id, bool enabled) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\States", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        wchar_t name[32]; swprintf(name, 32, L"%d", id);
        DWORD val = enabled ? 1 : 0;
        RegSetValueExW(hKey, name, 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

// Безопасная синхронизация тумблера и реестра по ID (без привязки к App::selectedTweakIdx).
void SetTweakStateById(int id, bool enabled) {
    for (auto& tw : App::tweaksList) {
        if (tw.id == id) {
            tw.isEnabled = enabled;
            break;
        }
    }
    SaveTweakState(id, enabled);
}

// Фоновый режим — пользовательская настройка, не системный твик: журнал/бэкап не нужен.
void SaveBackgroundMode(bool enabled) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = enabled ? 1 : 0;
        RegSetValueExW(hKey, L"BackgroundMode", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

void LoadBackgroundMode() {
    HKEY hKey;
    App::backgroundMode = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, sz = sizeof(val), tp = 0;
        if (RegQueryValueExW(hKey, L"BackgroundMode", nullptr, &tp, (BYTE*)&val, &sz) == ERROR_SUCCESS && tp == REG_DWORD)
            App::backgroundMode = (val != 0);
        RegCloseKey(hKey);
    }
}

// v58.0 Feature B: настройки авто-очистки standby-памяти.
void SaveStandbySettings() {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        DWORD on = App::autoStandbyClean ? 1 : 0;
        RegSetValueExW(k, L"AutoStandbyClean", 0, REG_DWORD, (BYTE*)&on, sizeof(on));
        DWORD iv = (DWORD)App::standbyIntervalMin;
        RegSetValueExW(k, L"StandbyIntervalMin", 0, REG_DWORD, (BYTE*)&iv, sizeof(iv));
        RegCloseKey(k);
    }
}
void LoadStandbySettings() {
    App::autoStandbyClean = false;
    App::standbyIntervalMin = 30;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        DWORD v = 0, sz = sizeof(v), tp = 0;
        if (RegQueryValueExW(k, L"AutoStandbyClean", nullptr, &tp, (BYTE*)&v, &sz) == ERROR_SUCCESS && tp == REG_DWORD)
            App::autoStandbyClean = (v != 0);
        v = 0; sz = sizeof(v); tp = 0;
        if (RegQueryValueExW(k, L"StandbyIntervalMin", nullptr, &tp, (BYTE*)&v, &sz) == ERROR_SUCCESS && tp == REG_DWORD) {
            if (v >= 5 && v <= 240) App::standbyIntervalMin = (int)v;
        }
        RegCloseKey(k);
    }
}
