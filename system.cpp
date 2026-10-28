#include "system.hpp"


namespace Journal {
    static void (*s_revertTweakHook)(int) = nullptr;
    static bool (*s_readTweakStateHook)(int) = nullptr;
    void SetRollbackHooks(void (*revertTweak)(int), bool (*readTweakState)(int)) {
        s_revertTweakHook = revertTweak;
        s_readTweakStateHook = readTweakState;
    }


    // Пакет #12: recursive_mutex позволяет Rollback вызывать RevertTweak
    // (внутри — AppendJournalLine с повторным захватом) без дедлока,
    // сохраняя атомарность всего процесса отката для фоновых потоков.
    static std::recursive_mutex s_journalMtx;



    static std::wstring GetIdxPath() {
        if (App::journalPath.size() >= 4 &&
            _wcsicmp(App::journalPath.c_str() + App::journalPath.size() - 4, L".log") == 0) {
            return App::journalPath.substr(0, App::journalPath.size() - 4) + L".idx";
        }
        return App::journalPath + L".idx";
    }

    static uint32_t CalcCrc32(const void* data, size_t len) {
        uint32_t crc = 0xFFFFFFFFu;
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i) {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }

    static uint32_t CalcGroupHash(const std::wstring& group) {
        if (group.empty()) return 0;
        std::string u8 = Util::ToUtf8(group);
        if (u8.empty()) return 0;
        return CalcCrc32(u8.data(), u8.size());
    }

    uint64_t NowFileTimeU64() {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    // Извлекает group-id (в UTF-8) из строки без переводов строк. Пустая строка = нет группы.
    static std::string ExtractGroupUtf8FromRawLine(const std::string& rawLine) {
        if (rawLine.rfind("V3|REG_VALUE|", 0) == 0) {
            size_t p1 = 13;
            size_t p2 = rawLine.find('|', p1);
            if (p2 != std::string::npos) {
                std::string grpDecoded;
                if (Util::Base64Decode(rawLine.substr(p1, p2 - p1), grpDecoded)) return grpDecoded;
            }
        } else if (rawLine.rfind("V3|ACTION|", 0) == 0) {
            size_t p1 = 10;
            size_t p2 = rawLine.find('|', p1);
            if (p2 != std::string::npos) {
                std::string grpDecoded;
                if (Util::Base64Decode(rawLine.substr(p1, p2 - p1), grpDecoded)) return grpDecoded;
            }
        } else if (rawLine.rfind("V3|BENCHMARK|", 0) == 0) {
            size_t p1 = 13;
            size_t p2 = rawLine.find('|', p1);
            if (p2 != std::string::npos) {
                std::string grpDecoded;
                if (Util::Base64Decode(rawLine.substr(p1, p2 - p1), grpDecoded)) return grpDecoded;
            }
        }
        return std::string();
    }

    // Генерирует новый group-id (GUID v4 через CoCreateGuid, формат
    // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx").
    std::wstring NewGroupId() {
        GUID g{};
        if (FAILED(CoCreateGuid(&g))) return L"00000000-0000-0000-0000-000000000000";
        wchar_t buf[64]{};
        swprintf(buf, 64, L"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 (unsigned long)g.Data1, (unsigned int)g.Data2, (unsigned int)g.Data3,
                 (unsigned int)g.Data4[0], (unsigned int)g.Data4[1],
                 (unsigned int)g.Data4[2], (unsigned int)g.Data4[3],
                 (unsigned int)g.Data4[4], (unsigned int)g.Data4[5],
                 (unsigned int)g.Data4[6], (unsigned int)g.Data4[7]);
        return std::wstring(buf);
    }

    // Дописывает готовую строку в журнал и синхронно дописывает 32-байтную запись в journal.idx.
    static bool AppendJournalLine(const std::string& line, uint32_t tweakId = 0) {
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);
        HANDLE hFile = CreateFileW(App::journalPath.c_str(), FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER fsz{};
        uint64_t offset = 0;
        if (GetFileSizeEx(hFile, &fsz) && fsz.QuadPart >= 0) offset = static_cast<uint64_t>(fsz.QuadPart);
        DWORD written = 0;
        BOOL ok = WriteFile(hFile, line.data(), (DWORD)line.size(), &written, NULL);
        CloseHandle(hFile);
        if (!ok || written != (DWORD)line.size()) return false;

        std::string clean = line;
        while (!clean.empty() && (clean.back() == '\n' || clean.back() == '\r')) clean.pop_back();
        std::string grpU8 = ExtractGroupUtf8FromRawLine(clean);

        JournalIdxEntry idx{};
        idx.offset = offset;
        idx.length = static_cast<uint32_t>(clean.size());
        idx.crc32 = CalcCrc32(clean.data(), clean.size());
        idx.groupHash = grpU8.empty() ? 0u : CalcCrc32(grpU8.data(), grpU8.size());
        idx.tweakId = tweakId;
        idx.timestamp = NowFileTimeU64();

        std::wstring idxPath = GetIdxPath();
        HANDLE hIdx = CreateFileW(idxPath.c_str(), FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hIdx != INVALID_HANDLE_VALUE) {
            DWORD idxWritten = 0;
            WriteFile(hIdx, &idx, sizeof(idx), &idxWritten, NULL);
            CloseHandle(hIdx);
        }
        return true;
    }

    // Восстанавливает journal.idx из journal.log, если .idx отсутствует,
    // повреждён или размер не кратен 32.
    bool RebuildJournalIndex(bool forceRebuild) {
        std::wstring idxPath = GetIdxPath();
        if (!Util::FileExists(App::journalPath)) {
            if (Util::FileExists(idxPath)) DeleteFileW(idxPath.c_str());
            return true;
        }
        std::string logContent = Util::ReadFileToString(App::journalPath);
        if (logContent.empty()) {
            if (Util::FileExists(idxPath)) DeleteFileW(idxPath.c_str());
            return true;
        }

        // Разбираем строки .log с точными смещениями
        struct LineSpan { uint64_t offset; uint32_t length; uint32_t crc; uint32_t groupHash; };
        std::vector<LineSpan> spans;
        size_t pos = 0;
        while (pos < logContent.size()) {
            size_t eol = logContent.find('\n', pos);
            size_t nextPos = (eol == std::string::npos) ? logContent.size() : (eol + 1);
            size_t len = (eol == std::string::npos) ? (logContent.size() - pos) : (eol - pos);
            if (len > 0 && logContent[pos + len - 1] == '\r') --len;
            if (len > 0) {
                std::string sub = logContent.substr(pos, len);
                std::string grpU8 = ExtractGroupUtf8FromRawLine(sub);
                spans.push_back({
                    static_cast<uint64_t>(pos),
                    static_cast<uint32_t>(len),
                    CalcCrc32(sub.data(), sub.size()),
                    grpU8.empty() ? 0u : CalcCrc32(grpU8.data(), grpU8.size())
                });
            }
            pos = nextPos;
        }

        std::vector<JournalIdxEntry> oldEntries;
        if (Util::FileExists(idxPath)) {
            std::string idxRaw = Util::ReadFileToString(idxPath);
            if (!idxRaw.empty() && (idxRaw.size() % sizeof(JournalIdxEntry) == 0)) {
                size_t cnt = idxRaw.size() / sizeof(JournalIdxEntry);
                oldEntries.resize(cnt);
                memcpy(oldEntries.data(), idxRaw.data(), idxRaw.size());
            }
        }

        if (!forceRebuild && oldEntries.size() == spans.size() && !spans.empty()) {
            bool valid = true;
            for (size_t i = 0; i < spans.size(); ++i) {
                if (oldEntries[i].offset != spans[i].offset ||
                    oldEntries[i].length != spans[i].length ||
                    oldEntries[i].crc32  != spans[i].crc ||
                    oldEntries[i].groupHash != spans[i].groupHash) {
                    valid = false;
                    break;
                }
            }
            if (valid) return true;
        }

        uint64_t fallbackTs = NowFileTimeU64();
        HANDLE hLog = CreateFileW(App::journalPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hLog != INVALID_HANDLE_VALUE) {
            FILETIME ftWrite{};
            if (GetFileTime(hLog, nullptr, nullptr, &ftWrite)) {
                fallbackTs = (static_cast<uint64_t>(ftWrite.dwHighDateTime) << 32) | ftWrite.dwLowDateTime;
            }
            CloseHandle(hLog);
        }

        std::string outIdx;
        outIdx.resize(spans.size() * sizeof(JournalIdxEntry));
        for (size_t i = 0; i < spans.size(); ++i) {
            JournalIdxEntry e{};
            e.offset = spans[i].offset;
            e.length = spans[i].length;
            e.crc32 = spans[i].crc;
            e.groupHash = spans[i].groupHash;
            e.tweakId = 0;
            e.timestamp = fallbackTs;
            for (const auto& oldE : oldEntries) {
                if (oldE.crc32 == e.crc32 && oldE.groupHash == e.groupHash && oldE.timestamp != 0) {
                    e.timestamp = oldE.timestamp;
                    e.tweakId = oldE.tweakId;
                    break;
                }
            }
            memcpy(&outIdx[i * sizeof(JournalIdxEntry)], &e, sizeof(JournalIdxEntry));
        }
        return Util::WriteFileAtomic(idxPath, outIdx);
    }

    // Читает .idx (с авто-rebuild при отсутствии/повреждении) и возвращает список
    // уникальных group-id в порядке появления в .log.
    std::vector<std::wstring> GetJournalGroups() {
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);
        std::vector<std::wstring> result;
        if (!Util::FileExists(App::journalPath)) return result;
        RebuildJournalIndex(false);

        std::string idxRaw = Util::ReadFileToString(GetIdxPath());
        std::string logRaw = Util::ReadFileToString(App::journalPath);
        if (idxRaw.empty() || (idxRaw.size() % sizeof(JournalIdxEntry) != 0) || logRaw.empty()) return result;

        size_t cnt = idxRaw.size() / sizeof(JournalIdxEntry);
        const auto* entries = reinterpret_cast<const JournalIdxEntry*>(idxRaw.data());
        std::set<std::wstring> seen;
        for (size_t i = 0; i < cnt; ++i) {
            if (entries[i].groupHash == 0) continue;
            if (entries[i].offset + entries[i].length > logRaw.size()) continue;
            std::string line = logRaw.substr(static_cast<size_t>(entries[i].offset), entries[i].length);
            std::string grpU8 = ExtractGroupUtf8FromRawLine(line);
            if (grpU8.empty()) continue;
            std::wstring grpW = Util::FromUtf8(grpU8);
            if (!grpW.empty() && seen.insert(grpW).second) {
                result.push_back(grpW);
            }
        }
        return result;
    }

    // Метаданные по группе для отображения в UI DrawRestore
    GroupMeta GetGroupMeta(const std::wstring& group) {
        // Пакет #11: RebuildJournalIndex пишет .idx — нельзя параллельно с AppendJournalLine.
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);
        GroupMeta meta;
        meta.group = group;
        meta.title = group;
        if (!Util::FileExists(App::journalPath)) return meta;
        RebuildJournalIndex(false);
        std::string idxRaw = Util::ReadFileToString(GetIdxPath());
        std::string logRaw = Util::ReadFileToString(App::journalPath);
        if (idxRaw.empty() || (idxRaw.size() % sizeof(JournalIdxEntry) != 0) || logRaw.empty()) return meta;

        uint32_t targetHash = CalcGroupHash(group);
        size_t cnt = idxRaw.size() / sizeof(JournalIdxEntry);
        const auto* entries = reinterpret_cast<const JournalIdxEntry*>(idxRaw.data());
        for (size_t i = 0; i < cnt; ++i) {
            if (entries[i].groupHash != targetHash) continue;
            if (entries[i].offset + entries[i].length > logRaw.size()) continue;
            std::string line = logRaw.substr(static_cast<size_t>(entries[i].offset), entries[i].length);
            if (Util::FromUtf8(ExtractGroupUtf8FromRawLine(line)) != group) continue;
            meta.entryCount++;
            if (entries[i].timestamp > meta.lastTimestamp) meta.lastTimestamp = entries[i].timestamp;

            // Достаём человекочитаемое имя твика из первой записи группы
            if (meta.title == group) {
                std::stringstream ls(line);
                std::string v, kind, b64grp;
                std::getline(ls, v, '|');
                std::getline(ls, kind, '|');
                std::getline(ls, b64grp, '|');
                if (kind == "REG_VALUE") {
                    std::string hive, b64p, b64n, ke, ve, tp, b64d, b64tw;
                    std::getline(ls, hive, '|');
                    std::getline(ls, b64p, '|');
                    std::getline(ls, b64n, '|');
                    std::getline(ls, ke, '|');
                    std::getline(ls, ve, '|');
                    std::getline(ls, tp, '|');
                    std::getline(ls, b64d, '|');
                    std::getline(ls, b64tw, '|');
                    std::string dTw;
                    if (Util::Base64Decode(b64tw, dTw) && !dTw.empty()) meta.title = Util::FromUtf8(dTw);
                } else if (kind == "ACTION") {
                    std::string b64act;
                    std::getline(ls, b64act, '|');
                    std::string dAct;
                    if (Util::Base64Decode(b64act, dAct) && !dAct.empty()) meta.title = Util::FromUtf8(dAct);
                } else if (kind == "BENCHMARK") {
                    std::string sMean, sLow1;
                    std::getline(ls, sMean, '|');
                    std::getline(ls, sLow1, '|');
                    meta.title = L"Benchmark: " + Util::FromUtf8(sMean) + L" FPS (1% Low " + Util::FromUtf8(sLow1) + L")";
                }
            }
        }
        return meta;
    }

    // V2 ACTION: имя и обратная команда в Base64 — '|' и переводы строк внутри не ломают парсер.
    void LogAction(const std::wstring& actionName, const std::wstring& revertCmd) {
        std::stringstream ss;
        ss << "V2|ACTION|" << Util::Base64Encode(Util::ToUtf8(actionName)) << '|'
           << Util::Base64Encode(Util::ToUtf8(revertCmd)) << "\n";
        AppendJournalLine(ss.str());
    }

    void LogActionGrouped(const std::wstring& actionName, const std::wstring& revertCmd, const std::wstring& group) {
        std::stringstream ss;
        ss << "V3|ACTION|" << Util::Base64Encode(Util::ToUtf8(group)) << '|'
           << Util::Base64Encode(Util::ToUtf8(actionName)) << '|'
           << Util::Base64Encode(Util::ToUtf8(revertCmd)) << "\n";
        AppendJournalLine(ss.str());
    }

    // 2.1/2.2: Backup сохраняет ВСЕ факты исходного состояния. Различает
    // «значения не было» (нормальное состояние) и «не удалось прочитать» (ошибка — менять нельзя).
    bool Backup(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& tweakName) {
        const char* hiveStr = (root == HKEY_LOCAL_MACHINE) ? "HKLM" : ((root == HKEY_CURRENT_USER) ? "HKCU" : nullptr);
        if (!hiveStr) return false; // неизвестный hive — изменение не разрешаем

        bool keyExists = false, valExists = false;
        DWORD type = 0; std::string data;
        HKEY k{};
        LONG open = RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k);
        if (open == ERROR_SUCCESS) {
            keyExists = true;
            DWORD size = 0;
            LONG rq = RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &size);
            if (rq == ERROR_SUCCESS) {
                data.resize(size);
                rq = RegQueryValueExW(k, name.c_str(), nullptr, &type, data.empty() ? nullptr : (BYTE*)&data[0], &size);
                if (rq == ERROR_SUCCESS) { data.resize(size); valExists = true; }
            }
            RegCloseKey(k);
            if (rq != ERROR_SUCCESS && rq != ERROR_FILE_NOT_FOUND) return false; // ACCESS_DENIED и прочее
        } else if (open != ERROR_FILE_NOT_FOUND && open != ERROR_PATH_NOT_FOUND) {
            return false; // ключ есть, но не читается — не рискуем
        }

        std::stringstream ss;
        ss << "V2|REG_VALUE|" << hiveStr << '|'
           << Util::Base64Encode(Util::ToUtf8(path)) << '|'
           << Util::Base64Encode(Util::ToUtf8(name)) << '|'
           << (keyExists ? 1 : 0) << '|' << (valExists ? 1 : 0) << '|'
           << (valExists ? (int)type : 0) << '|'
           << Util::Base64Encode(data) << '|'
           << Util::Base64Encode(Util::ToUtf8(tweakName)) << "\n";
        return AppendJournalLine(ss.str());
    }

    static bool BackupGrouped(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& tweakName, const std::wstring& group) {
        const char* hiveStr = (root == HKEY_LOCAL_MACHINE) ? "HKLM" : ((root == HKEY_CURRENT_USER) ? "HKCU" : nullptr);
        if (!hiveStr) return false;

        bool keyExists = false, valExists = false;
        DWORD type = 0; std::string data;
        HKEY k{};
        LONG open = RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k);
        if (open == ERROR_SUCCESS) {
            keyExists = true;
            DWORD size = 0;
            LONG rq = RegQueryValueExW(k, name.c_str(), nullptr, &type, nullptr, &size);
            if (rq == ERROR_SUCCESS) {
                data.resize(size);
                rq = RegQueryValueExW(k, name.c_str(), nullptr, &type, data.empty() ? nullptr : (BYTE*)&data[0], &size);
                if (rq == ERROR_SUCCESS) { data.resize(size); valExists = true; }
            }
            RegCloseKey(k);
            if (rq != ERROR_SUCCESS && rq != ERROR_FILE_NOT_FOUND) return false;
        } else if (open != ERROR_FILE_NOT_FOUND && open != ERROR_PATH_NOT_FOUND) {
            return false;
        }

        std::stringstream ss;
        ss << "V3|REG_VALUE|" << Util::Base64Encode(Util::ToUtf8(group)) << '|'
           << hiveStr << '|'
           << Util::Base64Encode(Util::ToUtf8(path)) << '|'
           << Util::Base64Encode(Util::ToUtf8(name)) << '|'
           << (keyExists ? 1 : 0) << '|' << (valExists ? 1 : 0) << '|'
           << (valExists ? (int)type : 0) << '|'
           << Util::Base64Encode(data) << '|'
           << Util::Base64Encode(Util::ToUtf8(tweakName)) << "\n";
        return AppendJournalLine(ss.str());
    }

    // 2.3: сначала успешный backup, потом запись, потом обратная проверка. Любая ошибка — false.
    bool SetDword(HKEY root, const std::wstring& path, const std::wstring& name, DWORD value, const std::wstring& tweakName) {
        if (!Backup(root, path, name, tweakName)) return false;
        HKEY k{};
        if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        LONG rc = RegSetValueExW(k, name.c_str(), 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(k);
        if (rc != ERROR_SUCCESS) return false;
        DWORD chk = 0, sz = sizeof(chk), tp = 0; bool ok = false; HKEY k2{};
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k2) == ERROR_SUCCESS) {
            ok = (RegQueryValueExW(k2, name.c_str(), nullptr, &tp, (BYTE*)&chk, &sz) == ERROR_SUCCESS && tp == REG_DWORD && chk == value);
            RegCloseKey(k2);
        }
        return ok;
    }

    // Удаление значения с бэкапом: при откате Journal восстановит прежнее значение.
    bool DeleteValue(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& tweakName) {
        if (!Backup(root, path, name, tweakName)) return false;
        HKEY k{};
        LONG open = RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE, &k);
        if (open == ERROR_FILE_NOT_FOUND || open == ERROR_PATH_NOT_FOUND) return true; // значения уже нет
        if (open != ERROR_SUCCESS) return false;
        LONG rc = RegDeleteValueW(k, name.c_str());
        RegCloseKey(k);
        if (rc == ERROR_FILE_NOT_FOUND) return true;
        if (rc != ERROR_SUCCESS) return false;
        // Обратная проверка: значение действительно удалено.
        HKEY k2{};
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k2) == ERROR_SUCCESS) {
            LONG q = RegQueryValueExW(k2, name.c_str(), nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(k2);
            return q == ERROR_FILE_NOT_FOUND;
        }
        return true;
    }

    bool SetString(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& value, const std::wstring& tweakName) {
        if (!Backup(root, path, name, tweakName)) return false;
        HKEY k{};
        if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        DWORD bytes = (DWORD)((value.length() + 1) * sizeof(wchar_t));
        LONG rc = RegSetValueExW(k, name.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), bytes);
        RegCloseKey(k);
        if (rc != ERROR_SUCCESS) return false;
        std::vector<BYTE> buf(bytes); DWORD sz = bytes, tp = 0; bool ok = false; HKEY k2{};
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k2) == ERROR_SUCCESS) {
            if (RegQueryValueExW(k2, name.c_str(), nullptr, &tp, buf.data(), &sz) == ERROR_SUCCESS &&
                tp == REG_SZ && sz == bytes && memcmp(buf.data(), value.c_str(), bytes) == 0) ok = true;
            RegCloseKey(k2);
        }
        return ok;
    }

    // Парные V3-версии существующих функций (Journal 2.0): пишут V3-строку с group-id.
    bool SetDwordGrouped(HKEY root, const std::wstring& path, const std::wstring& name,
                         DWORD value, const std::wstring& tweakName, const std::wstring& group) {
        if (!BackupGrouped(root, path, name, tweakName, group)) return false;
        HKEY k{};
        if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        LONG rc = RegSetValueExW(k, name.c_str(), 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(k);
        if (rc != ERROR_SUCCESS) return false;
        DWORD chk = 0, sz = sizeof(chk), tp = 0; bool ok = false; HKEY k2{};
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k2) == ERROR_SUCCESS) {
            ok = (RegQueryValueExW(k2, name.c_str(), nullptr, &tp, (BYTE*)&chk, &sz) == ERROR_SUCCESS && tp == REG_DWORD && chk == value);
            RegCloseKey(k2);
        }
        return ok;
    }

    bool SetStringGrouped(HKEY root, const std::wstring& path, const std::wstring& name,
                          const std::wstring& value, const std::wstring& tweakName, const std::wstring& group) {
        if (!BackupGrouped(root, path, name, tweakName, group)) return false;
        HKEY k{};
        if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        DWORD bytes = (DWORD)((value.length() + 1) * sizeof(wchar_t));
        LONG rc = RegSetValueExW(k, name.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), bytes);
        RegCloseKey(k);
        if (rc != ERROR_SUCCESS) return false;
        std::vector<BYTE> buf(bytes); DWORD sz = bytes, tp = 0; bool ok = false; HKEY k2{};
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k2) == ERROR_SUCCESS) {
            if (RegQueryValueExW(k2, name.c_str(), nullptr, &tp, buf.data(), &sz) == ERROR_SUCCESS &&
                tp == REG_SZ && sz == bytes && memcmp(buf.data(), value.c_str(), bytes) == 0) ok = true;
            RegCloseKey(k2);
        }
        return ok;
    }

    bool DeleteValueGrouped(HKEY root, const std::wstring& path, const std::wstring& name,
                            const std::wstring& tweakName, const std::wstring& group) {
        if (!BackupGrouped(root, path, name, tweakName, group)) return false;
        HKEY k{};
        LONG open = RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE, &k);
        if (open == ERROR_FILE_NOT_FOUND || open == ERROR_PATH_NOT_FOUND) return true;
        if (open != ERROR_SUCCESS) return false;
        LONG rc = RegDeleteValueW(k, name.c_str());
        RegCloseKey(k);
        if (rc == ERROR_FILE_NOT_FOUND) return true;
        if (rc != ERROR_SUCCESS) return false;
        HKEY k2{};
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &k2) == ERROR_SUCCESS) {
            LONG q = RegQueryValueExW(k2, name.c_str(), nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(k2);
            return q == ERROR_FILE_NOT_FOUND;
        }
        return true;
    }

    // Постоянный "откат" для Ultimate Performance: сохраняем GUID предыдущей
    // схемы в HKCU\Software\LEFTIK\PrevPowerScheme при захвате, читаем при откате.
    bool SavePrevPowerScheme(const std::wstring& guid) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        DWORD bytes = (DWORD)((guid.length() + 1) * sizeof(wchar_t));
        LONG rc = RegSetValueExW(k, L"PrevPowerScheme", 0, REG_SZ, (const BYTE*)guid.c_str(), bytes);
        RegCloseKey(k);
        return rc == ERROR_SUCCESS;
    }
    std::wstring LoadPrevPowerScheme() {
        std::wstring v = Util::ReadRegString(HKEY_CURRENT_USER, L"Software\\LEFTIK", L"PrevPowerScheme");
        return v;
    }
    void ClearPrevPowerScheme() {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, L"PrevPowerScheme");
            RegCloseKey(k);
        }
    }

    // 1.2: снимок скрытых параметров процессора для точного отката 1102.
    // Строка формата V1|GUID|AC,DC|... ; запись проверяется обратным чтением.
    std::wstring LoadPrevPowerAdv() {
        return Util::ReadRegString(HKEY_CURRENT_USER, L"Software\\LEFTIK", L"PrevPowerAdv");
    }
    bool SavePrevPowerAdv(const std::wstring& snapshot) {
        HKEY k{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        DWORD bytes = (DWORD)((snapshot.length() + 1) * sizeof(wchar_t));
        LONG rc = RegSetValueExW(k, L"PrevPowerAdv", 0, REG_SZ, (const BYTE*)snapshot.c_str(), bytes);
        RegCloseKey(k);
        return rc == ERROR_SUCCESS && LoadPrevPowerAdv() == snapshot;
    }
    void ClearPrevPowerAdv() {
        HKEY k{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, L"PrevPowerAdv");
            RegCloseKey(k);
        }
    }

    // Точный whitelist: только команды из исходника с известным безопасным эффектом.
    // Возврат: 0 — выполнено успешно; 1 — выполнено, но неудачно; -1 — НЕ в whitelist (не исполнять!).
    static int RunWhitelistedRevert(const std::wstring& cmd) {
        DWORD ec = 0; ProcWaitStatus st;
        if (cmd == L"netsh advfirewall set allprofiles state on")
            st = RunProcessWait(L"C:\\Windows\\System32\\netsh.exe", L"advfirewall set allprofiles state on", 20000, ec);
        else if (cmd == L"bcdedit /set hypervisorlaunchtype auto")
            st = RunProcessWait(L"C:\\Windows\\System32\\bcdedit.exe", L"/set hypervisorlaunchtype auto", 20000, ec);
        else if (cmd == L"powercfg -h on")
            st = RunProcessWait(L"C:\\Windows\\System32\\powercfg.exe", L"-h on", 20000, ec);
        else if (cmd == L"netsh int tcp set global autotuninglevel=normal")
            st = RunProcessWait(L"C:\\Windows\\System32\\netsh.exe", L"int tcp set global autotuninglevel=normal", 20000, ec);
        else if (cmd == L"powershell -command Enable-MMAgent -MemoryCompression")
            st = RunPowerShellWait(L"Enable-MMAgent -MemoryCompression", 30000, ec);
        else if (cmd == L"bcdedit /deletevalue disabledynamictick")
            st = RunProcessWait(L"C:\\Windows\\System32\\bcdedit.exe", L"/deletevalue disabledynamictick", 20000, ec);
        else if (cmd == L"bcdedit /set disabledynamictick yes")
            st = RunProcessWait(L"C:\\Windows\\System32\\bcdedit.exe", L"/set disabledynamictick yes", 20000, ec);
        else if (cmd == L"bcdedit /set disabledynamictick no")
            st = RunProcessWait(L"C:\\Windows\\System32\\bcdedit.exe", L"/set disabledynamictick no", 20000, ec);
        else
            return -1; // неизвестная команда из журнала НЕ исполняется
        return (st == PWS_OK && ec == 0) ? 0 : 1;
    }

    static void SplitTokens(const std::string& line, std::vector<std::string>& out) {
        out.clear();
        std::stringstream ss(line); std::string t;
        while (std::getline(ss, t, '|')) out.push_back(t);
        if (!line.empty() && line.back() == '|') out.push_back(""); // хвостовой пустой токен (Base64(""))
    }
    static std::string D64(const std::string& s, bool& ok) {
        std::string out; ok = Util::Base64Decode(s, out); return out;
    }
    static bool IsValidHex(const std::string& s) {
        if (s.size() % 2 != 0) return false;
        for (char c : s) if (!isxdigit((unsigned char)c)) return false;
        return true;
    }

    // ===== Области журнала (профили v57.14) =====
    // Служебная строка-граница V2|MARK|b64(тег): не операция, полным Rollback
    // пропускается, в истории не показывается.
    static std::string MarkerLine(const std::wstring& tag) {
        return "V2|MARK|" + Util::Base64Encode(Util::ToUtf8(tag));
    }
    static std::string MarkerLineV3(const std::wstring& tag) {
        return "V3|MARK|" + Util::Base64Encode(Util::ToUtf8(tag));
    }
    // false = журнал недоступен для записи (профиль без точки отката запрещён)
    bool WriteMarker(const std::wstring& tag) {
        return AppendJournalLine(MarkerLine(tag) + "\n");
    }

    // Обработка одной строки журнала при откате — общая для полного и scoped-отката.
    static void RestoreEntryLine(const std::string& line,
                                 int& restored, int& registryFailed,
                                 int& cmdRestored, int& cmdFailed, int& unsupported) {
        std::vector<std::string> tok;
        SplitTokens(line, tok);
        if (tok.empty()) return;

        // ---- V3 (Journal 2.0 с group-id) ----
        if (tok[0] == "V3" && tok.size() >= 2) {
            if (tok[1] == "MARK" || tok[1] == "IMPORTED" || tok[1] == "BENCHMARK") return; // служебные/метрические записи не откатываются
            if (tok[1] == "REG_VALUE" && tok.size() == 11) {
                bool okGrp = true, ok1 = true, ok2 = true, ok3 = true, ok4 = true;
                D64(tok[2], okGrp); // group-id игнорируется при восстановлении значения
                std::string hive = tok[3];
                std::wstring wpath = Util::FromUtf8(D64(tok[4], ok1));
                std::wstring wname = Util::FromUtf8(D64(tok[5], ok2));
                bool keyEx = (tok[6] == "1"), valEx = (tok[7] == "1");
                DWORD type = (DWORD)atoi(tok[8].c_str());
                std::string data = D64(tok[9], ok3);
                D64(tok[10], ok4);
                static const DWORD kAllowedTypes[] = { REG_SZ, REG_EXPAND_SZ, REG_BINARY, REG_DWORD, REG_MULTI_SZ, REG_QWORD };
                bool validType = !valEx;
                for (DWORD allowed : kAllowedTypes) {
                    if (type == allowed) { validType = true; break; }
                }
                if (!okGrp || !ok1 || !ok2 || !ok3 || !ok4 || !validType || (hive != "HKLM" && hive != "HKCU")) { unsupported++; return; }
                HKEY root = (hive == "HKLM") ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
                HKEY k{};
                if (RegOpenKeyExW(root, wpath.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                    LONG rc;
                    if (valEx) {
                        rc = RegSetValueExW(k, wname.c_str(), 0, type, data.empty() ? nullptr : (BYTE*)data.data(), (DWORD)data.size());
                    } else {
                        rc = RegDeleteValueW(k, wname.c_str());
                        if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) rc = ERROR_SUCCESS;
                    }
                    RegCloseKey(k);
                    if (rc == ERROR_SUCCESS) {
                        restored++;
                        if (!valEx && !keyEx) RegDeleteKeyW(root, wpath.c_str());
                    } else registryFailed++;
                } else {
                    if (!valEx) restored++;
                    else registryFailed++;
                }
                return;
            }
            if (tok[1] == "ACTION" && tok.size() == 5) {
                bool okGrp = true, okAct = true, okRev = true;
                D64(tok[2], okGrp);
                D64(tok[3], okAct);
                std::wstring revert = Util::FromUtf8(D64(tok[4], okRev));
                if (!okGrp || !okAct || !okRev) { unsupported++; return; }
                if (revert.empty()) return;
                int r = RunWhitelistedRevert(revert);
                if (r < 0) unsupported++;
                else if (r == 0) cmdRestored++; else cmdFailed++;
                return;
            }
            unsupported++;
            return;
        }

        // ---- V2 (версионированный формат) ----
        if (tok[0] == "V2" && tok.size() >= 2) {
            if (tok[1] == "MARK") return; // граница области — не операция отката
            if (tok[1] == "REG_VALUE" && tok.size() == 10) {
                bool ok1 = true, ok2 = true, ok3 = true, ok4 = true;
                std::string hive = tok[2];
                std::wstring wpath = Util::FromUtf8(D64(tok[3], ok1));
                std::wstring wname = Util::FromUtf8(D64(tok[4], ok2));
                bool keyEx = (tok[5] == "1"), valEx = (tok[6] == "1");
                DWORD type = (DWORD)atoi(tok[7].c_str());
                std::string data = D64(tok[8], ok3);
                D64(tok[9], ok4);
                static const DWORD kAllowedTypes[] = { REG_SZ, REG_EXPAND_SZ, REG_BINARY, REG_DWORD, REG_MULTI_SZ, REG_QWORD };
                bool validType = !valEx;
                for (DWORD allowed : kAllowedTypes) {
                    if (type == allowed) { validType = true; break; }
                }
                if (!ok1 || !ok2 || !ok3 || !ok4 || !validType || (hive != "HKLM" && hive != "HKCU")) { unsupported++; return; }
                HKEY root = (hive == "HKLM") ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
                HKEY k{};
                if (RegOpenKeyExW(root, wpath.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                    LONG rc;
                    if (valEx) {
                        rc = RegSetValueExW(k, wname.c_str(), 0, type, data.empty() ? nullptr : (BYTE*)data.data(), (DWORD)data.size());
                    } else {
                        rc = RegDeleteValueW(k, wname.c_str());
                        if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) rc = ERROR_SUCCESS; // уже нет — цель достигнута
                    }
                    RegCloseKey(k);
                    if (rc == ERROR_SUCCESS) {
                        restored++;
                        if (!valEx && !keyEx) RegDeleteKeyW(root, wpath.c_str()); // ключ создан нами и опустел
                    } else registryFailed++;
                } else {
                    if (!valEx) restored++; // ключа нет, значения быть не должно — успех
                    else registryFailed++;
                }
                return;
            }
            if (tok[1] == "ACTION" && tok.size() == 4) {
                bool ok = true;
                std::wstring revert = Util::FromUtf8(D64(tok[3], ok));
                if (!ok) { unsupported++; return; }
                if (revert.empty()) return; // действие без обратной команды — не операция отката
                int r = RunWhitelistedRevert(revert);
                if (r < 0) unsupported++;
                else if (r == 0) cmdRestored++; else cmdFailed++;
                return;
            }
            unsupported++;
            return;
        }

        // ---- легаси (старый формат) ----
        if (tok[0] == "CMD") {
            std::string revert = (tok.size() >= 3) ? tok[2] : "";
            if (revert.empty() || revert == "EXEC") return; // старые записи без обратной команды
            int r = RunWhitelistedRevert(Util::FromUtf8(revert));
            if (r < 0) unsupported++;
            else if (r == 0) cmdRestored++; else cmdFailed++;
            return;
        }
        if ((tok[0] == "HKLM" || tok[0] == "HKCU") && tok.size() >= 7) {
            HKEY root = (tok[0] == "HKLM") ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            std::wstring wpath = Util::FromUtf8(tok[1]);
            std::wstring wname = Util::FromUtf8(tok[2]);
            bool existed = (tok[3] == "1");
            std::string typeStr = tok[4];
            std::string hexdata = tok.back(); // data — последнее поле
            HKEY k{};
            if (RegOpenKeyExW(root, wpath.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                LONG rc;
                if (!existed) {
                    rc = RegDeleteValueW(k, wname.c_str());
                    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) rc = ERROR_SUCCESS;
                } else if (typeStr == "4" && hexdata.size() == 8 && IsValidHex(hexdata)) {
                    DWORD v = 0;
                    for (size_t j = 0; j < 8; j += 2) v = (v << 8) | (DWORD)std::stoi(hexdata.substr(j, 2), nullptr, 16);
                    rc = RegSetValueExW(k, wname.c_str(), 0, REG_DWORD, (BYTE*)&v, sizeof(v));
                } else if (typeStr == "1" && IsValidHex(hexdata)) {
                    std::vector<BYTE> bytes;
                    for (size_t j = 0; j + 1 < hexdata.size(); j += 2)
                        bytes.push_back((BYTE)std::stoi(hexdata.substr(j, 2), nullptr, 16));
                    rc = RegSetValueExW(k, wname.c_str(), 0, REG_SZ, bytes.empty() ? nullptr : bytes.data(), (DWORD)bytes.size());
                } else {
                    rc = -1; // неизвестный/поврежденный формат — не рискуем
                }
                RegCloseKey(k);
                if (rc == ERROR_SUCCESS) restored++;
                else if (rc == -1) unsupported++;
                else registryFailed++;
            } else {
                if (!existed) restored++; else registryFailed++;
            }
            return;
        }
        unsupported++; // полностью неизвестная запись
    }

    // МОДУЛЬ 3 — Diff-просмотр перед откатом
    static std::wstring TruncateDiffVal(const std::wstring& s) {
        if (s.size() <= 64) return s;
        return s.substr(0, 61) + L"...";
    }

    std::vector<DiffEntry> BuildDiffForGroup(const std::wstring& group) {
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);
        std::vector<DiffEntry> diffs;
        if (group.empty()) return diffs;
        if (!Util::FileExists(App::journalPath)) return diffs;
        RebuildJournalIndex(false);

        std::string idxRaw = Util::ReadFileToString(GetIdxPath());
        std::string logRaw = Util::ReadFileToString(App::journalPath);
        if (idxRaw.empty() || (idxRaw.size() % sizeof(JournalIdxEntry) != 0) || logRaw.empty()) return diffs;

        uint32_t targetHash = CalcGroupHash(group);
        size_t cnt = idxRaw.size() / sizeof(JournalIdxEntry);
        const auto* entries = reinterpret_cast<const JournalIdxEntry*>(idxRaw.data());

        for (size_t i = 0; i < cnt; ++i) {
            if (entries[i].groupHash != targetHash) continue;
            if (entries[i].offset + entries[i].length > logRaw.size()) continue;
            std::string line = logRaw.substr(static_cast<size_t>(entries[i].offset), entries[i].length);

            std::vector<std::string> tok;
            SplitTokens(line, tok);
            if (tok.size() == 5 && tok[0] == "V3" && tok[1] == "ACTION") {
                bool okGrp = true, okAct = true, okRev = true;
                std::wstring lineGrp = Util::FromUtf8(D64(tok[2], okGrp));
                std::wstring actName = Util::FromUtf8(D64(tok[3], okAct));
                std::wstring revCmd  = Util::FromUtf8(D64(tok[4], okRev));
                if (!okGrp || !okAct || !okRev || lineGrp != group) continue;
                DiffEntry de;
                de.hive = L"CMD";
                de.path = L"(команда)";
                de.name = actName;
                de.tweakName = actName;
                de.wouldChange = !revCmd.empty();
                de.currentValue = L"(выполнено)";
                de.restoredValue = revCmd.empty() ? L"(без обратной команды)" : TruncateDiffVal(revCmd);
                diffs.push_back(de);
                continue;
            }
            if (tok.size() != 11 || tok[0] != "V3" || tok[1] != "REG_VALUE") continue;

            bool okGrp = true, ok1 = true, ok2 = true, ok3 = true, ok4 = true;
            std::wstring lineGrp = Util::FromUtf8(D64(tok[2], okGrp));
            if (!okGrp || lineGrp != group) continue;

            std::string hiveStr = tok[3];
            std::wstring wpath = Util::FromUtf8(D64(tok[4], ok1));
            std::wstring wname = Util::FromUtf8(D64(tok[5], ok2));
            bool valEx = (tok[7] == "1");
            DWORD type = (DWORD)atoi(tok[8].c_str());
            std::string data = D64(tok[9], ok3);
            std::wstring twName = Util::FromUtf8(D64(tok[10], ok4));
            if (!ok1 || !ok2 || !ok3 || !ok4 || (hiveStr != "HKLM" && hiveStr != "HKCU")) continue;

            HKEY root = (hiveStr == "HKLM") ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            DiffEntry de;
            de.hive = (hiveStr == "HKLM") ? L"HKLM" : L"HKCU";
            de.path = wpath;
            de.name = wname;
            de.tweakName = twName;

            // Форматируем восстанавливаемое значение
            DWORD restDword = 0;
            std::wstring restStr;
            if (!valEx) {
                de.restoredValue = L"(удалено)";
            } else if (type == REG_DWORD && data.size() == sizeof(DWORD)) {
                memcpy(&restDword, data.data(), sizeof(DWORD));
                wchar_t b[32]; swprintf(b, 32, L"%lu (0x%lX)", (unsigned long)restDword, (unsigned long)restDword);
                de.restoredValue = b;
            } else if (type == REG_SZ) {
                if (data.size() >= sizeof(wchar_t)) {
                    size_t wlen = data.size() / sizeof(wchar_t);
                    const wchar_t* wp = reinterpret_cast<const wchar_t*>(data.data());
                    while (wlen > 0 && wp[wlen - 1] == L'\0') --wlen;
                    restStr.assign(wp, wlen);
                }
                de.restoredValue = TruncateDiffVal(restStr);
            } else {
                de.restoredValue = L"(type=" + std::to_wstring(type) + L", " + std::to_wstring(data.size()) + L" B)";
            }

            // Читаем текущее состояние в реестре
            bool curValEx = false;
            DWORD curType = 0;
            std::vector<BYTE> curData;
            HKEY hk{};
            if (RegOpenKeyExW(root, wpath.c_str(), 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
                DWORD sz = 0;
                if (RegQueryValueExW(hk, wname.c_str(), nullptr, &curType, nullptr, &sz) == ERROR_SUCCESS) {
                    curData.resize(sz);
                    if (RegQueryValueExW(hk, wname.c_str(), nullptr, &curType, curData.empty() ? nullptr : curData.data(), &sz) == ERROR_SUCCESS) {
                        curData.resize(sz);
                        curValEx = true;
                    }
                }
                RegCloseKey(hk);
            }

            if (!curValEx) {
                de.currentValue = L"(удалено)";
                de.wouldChange = valEx;
            } else if (curType == REG_DWORD && curData.size() == sizeof(DWORD)) {
                DWORD curDword = 0;
                memcpy(&curDword, curData.data(), sizeof(DWORD));
                wchar_t b[32]; swprintf(b, 32, L"%lu (0x%lX)", (unsigned long)curDword, (unsigned long)curDword);
                de.currentValue = b;
                if (!valEx || type != REG_DWORD || data.size() != sizeof(DWORD)) de.wouldChange = true;
                else de.wouldChange = (curDword != restDword);
            } else if (curType == REG_SZ) {
                std::wstring curStr;
                if (curData.size() >= sizeof(wchar_t)) {
                    size_t wlen = curData.size() / sizeof(wchar_t);
                    const wchar_t* wp = reinterpret_cast<const wchar_t*>(curData.data());
                    while (wlen > 0 && wp[wlen - 1] == L'\0') --wlen;
                    curStr.assign(wp, wlen);
                }
                de.currentValue = TruncateDiffVal(curStr);
                if (!valEx || type != REG_SZ) de.wouldChange = true;
                else de.wouldChange = (curStr != restStr);
            } else {
                de.currentValue = L"(type=" + std::to_wstring(curType) + L", " + std::to_wstring(curData.size()) + L" B)";
                de.wouldChange = true;
            }
            diffs.push_back(de);
        }
        return diffs;
    }

    // МОДУЛЬ 2 — Selective Rollback по группе
    // Откатывает все записи с указанным group-id (от конца файла к началу).
    // Возвращает:
    //   0 — полный откат, группа удалена из .log и .idx
    //   1 — частичный (некоторые записи не откатились), .log не тронут, создан .failed
    //   2 — группа не найдена
    //   3 — реестр откачен, .log переписать не удалось (безопасно повторить)
    int RollbackGroupInternal(const std::wstring& group, bool changedOnly,
                                     int& restored, int& registryFailed,
                                     int& cmdRestored, int& cmdFailed, int& unsupported) {
        restored = registryFailed = cmdRestored = cmdFailed = unsupported = 0;
        if (group.empty()) return 2;

        // Пакет #11: весь откат группы атомарен относительно AppendJournalLine/GetJournalGroups.
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);

        auto allGroups = GetJournalGroups();
        if (std::find(allGroups.begin(), allGroups.end(), group) == allGroups.end()) return 2;

        std::string idxRaw = Util::ReadFileToString(GetIdxPath());
        std::string logRaw = Util::ReadFileToString(App::journalPath);
        if (idxRaw.empty() || (idxRaw.size() % sizeof(JournalIdxEntry) != 0) || logRaw.empty()) return 2;

        uint32_t targetHash = CalcGroupHash(group);
        size_t cnt = idxRaw.size() / sizeof(JournalIdxEntry);
        const auto* entries = reinterpret_cast<const JournalIdxEntry*>(idxRaw.data());

        std::vector<JournalIdxEntry> matched;
        std::set<uint64_t> matchedOffsets;
        for (size_t i = 0; i < cnt; ++i) {
            if (entries[i].groupHash != targetHash) continue;
            if (entries[i].offset + entries[i].length > logRaw.size()) continue;
            std::string line = logRaw.substr(static_cast<size_t>(entries[i].offset), entries[i].length);
            if (Util::FromUtf8(ExtractGroupUtf8FromRawLine(line)) != group) continue;
            matched.push_back(entries[i]);
            matchedOffsets.insert(entries[i].offset);
        }
        if (matched.empty()) return 2;

        std::sort(matched.begin(), matched.end(), [](const JournalIdxEntry& a, const JournalIdxEntry& b) {
            return a.offset > b.offset;
        });

        std::set<std::wstring> changedKeys;
        if (changedOnly) {
            auto diffs = BuildDiffForGroup(group);
            for (const auto& d : diffs) {
                if (d.wouldChange) changedKeys.insert(d.hive + L"\\" + d.path + L"\\" + d.name);
            }
        }

        for (const auto& m : matched) {
            std::string line = logRaw.substr(static_cast<size_t>(m.offset), m.length);
            if (changedOnly) {
                std::vector<std::string> tok;
                SplitTokens(line, tok);
                if (tok.size() == 11 && tok[0] == "V3" && tok[1] == "REG_VALUE") {
                    bool ok1 = true, ok2 = true;
                    std::wstring h = (tok[3] == "HKLM") ? L"HKLM" : L"HKCU";
                    std::wstring p = Util::FromUtf8(D64(tok[4], ok1));
                    std::wstring n = Util::FromUtf8(D64(tok[5], ok2));
                    if (changedKeys.find(h + L"\\" + p + L"\\" + n) == changedKeys.end()) {
                        continue; // значение уже совпадает с восстанавливаемым
                    }
                }
            }
            RestoreEntryLine(line, restored, registryFailed, cmdRestored, cmdFailed, unsupported);
        }

        if (registryFailed != 0 || cmdFailed != 0 || unsupported != 0) {
            CopyFileW(App::journalPath.c_str(), (App::journalPath + L".failed").c_str(), FALSE);
            return 1;
        }

        // Собираем .log без откаченных строк этой группы
        std::string keep;
        size_t pos = 0;
        while (pos < logRaw.size()) {
            size_t eol = logRaw.find('\n', pos);
            size_t nextPos = (eol == std::string::npos) ? logRaw.size() : (eol + 1);
            if (matchedOffsets.find(static_cast<uint64_t>(pos)) == matchedOffsets.end()) {
                keep.append(logRaw, pos, nextPos - pos);
            }
            pos = nextPos;
        }

        bool wroteOk = false;
        if (keep.empty()) {
            wroteOk = (DeleteFileW(App::journalPath.c_str()) != 0);
            if (wroteOk) {
                DeleteFileW(GetIdxPath().c_str());
            } else {
                return 3; // Лог заблокирован — индекс НЕ трогаем
            }
        } else {
            wroteOk = Util::WriteFileAtomic(App::journalPath, keep);
            if (wroteOk) RebuildJournalIndex(true);
        }
        return wroteOk ? 0 : 3;
    }

    int RollbackGroup(const std::wstring& group,
                      int& restored, int& registryFailed,
                      int& cmdRestored, int& cmdFailed, int& unsupported) {
        return RollbackGroupInternal(group, false, restored, registryFailed, cmdRestored, cmdFailed, unsupported);
    }

    // МОДУЛЬ 4 — Export / Import с HMAC
    // Ключ: SHA256(MachineGuid + UserSID).
    bool DeriveHmacKey(BYTE outKey[32]) {
        memset(outKey, 0, 32);
        std::wstring machineGuid;
        HKEY hk{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
            wchar_t buf[256]{}; DWORD sz = sizeof(buf), tp = 0;
            if (RegQueryValueExW(hk, L"MachineGuid", nullptr, &tp, (BYTE*)buf, &sz) == ERROR_SUCCESS && (tp == REG_SZ || tp == REG_EXPAND_SZ)) {
                machineGuid = buf;
            }
            RegCloseKey(hk);
        }
        if (machineGuid.empty()) return false;

        std::wstring userSidStr;
        HANDLE hTok = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok) && hTok) {
            DWORD len = 0;
            GetTokenInformation(hTok, TokenUser, nullptr, 0, &len);
            if (len > 0) {
                std::vector<BYTE> tbuf(len);
                if (GetTokenInformation(hTok, TokenUser, tbuf.data(), len, &len)) {
                    auto* tu = reinterpret_cast<TOKEN_USER*>(tbuf.data());
                    LPWSTR sidW = nullptr;
                    if (ConvertSidToStringSidW(tu->User.Sid, &sidW) && sidW) {
                        userSidStr = sidW;
                        LocalFree(sidW);
                    }
                }
            }
            CloseHandle(hTok);
        }
        if (userSidStr.empty()) return false;

        std::string material = Util::ToUtf8(machineGuid + userSidStr);
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        bool ok = false;
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && hAlg) {
            if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0 && hHash) {
                if (BCryptHashData(hHash, (PUCHAR)material.data(), (ULONG)material.size(), 0) == 0 &&
                    BCryptFinishHash(hHash, outKey, 32, 0) == 0) {
                    ok = true;
                }
                BCryptDestroyHash(hHash);
            }
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
        return ok;
    }

    // HMAC-SHA256 над строкой с machine-specific ключом.
    bool HmacSha256(const BYTE key[32], const std::string& data, BYTE outMac[32]) {
        memset(outMac, 0, 32);
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        bool ok = false;
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0 && hAlg) {
            if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, (PUCHAR)key, 32, 0) == 0 && hHash) {
                if (BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
                    BCryptFinishHash(hHash, outMac, 32, 0) == 0) {
                    ok = true;
                }
                BCryptDestroyHash(hHash);
            }
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
        return ok;
    }

    static std::string BytesToHexLower(const BYTE* bytes, size_t len) {
        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; ++i) {
            out.push_back(hex[(bytes[i] >> 4) & 0x0F]);
            out.push_back(hex[bytes[i] & 0x0F]);
        }
        return out;
    }

    bool ExportJournal(const std::wstring& outPath) {
        BYTE key[32]{};
        if (!DeriveHmacKey(key)) return false;

        // Пакет #11: экспорт читает консистентный снимок .log (без параллельной дозаписи).
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);
        std::string logRaw = Util::ReadFileToString(App::journalPath);
        std::stringstream ss(logRaw);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }

        std::string body = "LEFTIK-EXPORT-V1\nCOUNT:" + std::to_string(lines.size()) + "\n";
        for (const auto& l : lines) {
            BYTE mac[32]{};
            if (!HmacSha256(key, l, mac)) return false;
            body += Util::Base64Encode(l) + "|" + BytesToHexLower(mac, 32) + "\n";
        }

        BYTE bodyMac[32]{};
        if (!HmacSha256(key, body, bodyMac)) return false;
        std::string fullContent = body + "SIGNATURE:" + BytesToHexLower(bodyMac, 32) + "\n";
        return Util::WriteFileAtomic(outPath, fullContent);
    }

    // Возвращает:
    //   0 — успех, записи добавлены в .log (append)
    //   1 — файл повреждён / HMAC не сошёлся
    //   2 — файл с другой машины (все HMAC невалидны)
    //   3 — файл открыт, но записей 0
    int ImportJournalEx(const std::wstring& inPath, int& validEntries, int& tamperedEntries, bool allowPartialCommit, std::vector<std::string>* outValidLines) {
        validEntries = 0;
        tamperedEntries = 0;
        BYTE key[32]{};
        if (!DeriveHmacKey(key)) return 1;

        std::string content = Util::ReadFileToString(inPath);
        if (content.empty()) return 1;

        std::stringstream ss(content);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
        if (lines.size() < 3) return 1;
        if (lines[0] != "LEFTIK-EXPORT-V1") return 1;
        if (lines[1].rfind("COUNT:", 0) != 0) return 1;
        if (lines.back().rfind("SIGNATURE:", 0) != 0) return 1;

        int declaredCount = atoi(lines[1].c_str() + 6);
        std::string fileSigHex = lines.back().substr(10);

        std::string reconstructedBody = lines[0] + "\n" + lines[1] + "\n";
        std::vector<std::string> validDecoded;
        int totalRecords = (int)lines.size() - 3;

        for (size_t i = 2; i + 1 < lines.size(); ++i) {
            reconstructedBody += lines[i] + "\n";
            size_t bar = lines[i].find('|');
            if (bar == std::string::npos) {
                tamperedEntries++;
                continue;
            }
            std::string b64Part = lines[i].substr(0, bar);
            std::string macHex = lines[i].substr(bar + 1);
            std::string decodedLine;
            if (macHex.size() != 64 || !Util::Base64Decode(b64Part, decodedLine)) {
                tamperedEntries++;
                continue;
            }
            BYTE mac[32]{};
            if (!HmacSha256(key, decodedLine, mac)) {
                tamperedEntries++;
                continue;
            }
            if (BytesToHexLower(mac, 32) == macHex) {
                validEntries++;
                validDecoded.push_back(decodedLine);
            } else {
                tamperedEntries++;
            }
        }

        BYTE expectedBodyMac[32]{};
        bool sigOk = HmacSha256(key, reconstructedBody, expectedBodyMac) &&
                     (BytesToHexLower(expectedBodyMac, 32) == fileSigHex) &&
                     (declaredCount == totalRecords);

        if (totalRecords == 0) {
            return sigOk ? 3 : 2;
        }
        if (validEntries == 0 && tamperedEntries > 0) {
            return 2; // все HMAC невалидны — файл с другой машины
        }
        if (outValidLines) *outValidLines = validDecoded;
        if (tamperedEntries > 0 || !sigOk) {
            if (!allowPartialCommit) return 1;
        }

        for (const auto& vl : validDecoded) {
            AppendJournalLine("V3|IMPORTED|" + vl + "\n");
        }
        return (tamperedEntries == 0 && sigOk) ? 0 : 1;
    }

    int ImportJournal(const std::wstring& inPath, int& validEntries, int& tamperedEntries) {
        return ImportJournalEx(inPath, validEntries, tamperedEntries, false, nullptr);
    }

    // Scoped-откат области журнала: от конца файла до маркера tag (включая его удаление).
    // Используется профилем 3102 при выключении: восстанавливает состояние ДО профиля.
    // Возврат: 0 — полный; 1 — частичный (журнал сохранён, .failed-копия); 2 — маркер не найден;
    // 3 — реестр откачен, но журнал переписать не удалось (область осталась, повтор безопасен).
    int RollbackToMarker(const std::wstring& tag,
                                int& restored, int& registryFailed,
                                int& cmdRestored, int& cmdFailed, int& unsupported) {
        restored = registryFailed = cmdRestored = cmdFailed = unsupported = 0;
        // Пакет #11: блокировка покрывает чтение, RestoreEntryLine и перезапись журнала.
        std::lock_guard<std::recursive_mutex> lk(s_journalMtx);
        HANDLE hFile = CreateFileW(App::journalPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return 2;
        DWORD size = GetFileSize(hFile, NULL); std::string content(size, '\0'); DWORD rd_ = 0;
        ReadFile(hFile, &content[0], size, &rd_, NULL); CloseHandle(hFile);

        std::stringstream ss(content); std::string line; std::vector<std::string> lines;
        while (std::getline(ss, line)) if (!line.empty()) lines.push_back(line);

        std::string marker = MarkerLine(tag);
        std::string markerV3 = MarkerLineV3(tag);
        int markerIdx = -1;
        for (int i = (int)lines.size() - 1; i >= 0; --i) {
            std::string trimmed = lines[i];
            if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
            if (trimmed == marker || trimmed == markerV3) { markerIdx = i; break; }
        }
        if (markerIdx < 0) return 2; // граница не найдена — НИЧЕГО не трогаем

        for (int i = (int)lines.size() - 1; i > markerIdx; --i)
            RestoreEntryLine(lines[i], restored, registryFailed, cmdRestored, cmdFailed, unsupported);

        if (registryFailed != 0 || cmdFailed != 0 || unsupported != 0) {
            // Правило: журнал не сокращается, пока есть необработанные/неподдержанные записи
            CopyFileW(App::journalPath.c_str(), (App::journalPath + L".failed").c_str(), FALSE);
            return 1;
        }
        // Полный успех: область (маркер включительно) удаляется из журнала атомарно
        std::string keep;
        for (int i = 0; i < markerIdx; ++i) { keep += lines[i]; keep += "\n"; }
        if (keep.empty()) {
            bool delOk = (DeleteFileW(App::journalPath.c_str()) != 0);
            if (delOk) DeleteFileW(GetIdxPath().c_str());
            return delOk ? 0 : 3;
        }
        bool wrOk = Util::WriteFileAtomic(App::journalPath, keep);
        if (wrOk) RebuildJournalIndex(true);
        return wrOk ? 0 : 3;
    }

    // 2.4: обратный порядок, точное восстановление байтов/типа, whitelist для команд,
    // отдельные счетчики, журнал НЕ удаляется при любом неполном результате.
    // Записи V3|IMPORTED|... сохраняются и не откатываются общим Rollback().
    int Rollback(HWND owner) { // 0 = полный успех, 1 = частичный, 2 = отмена, 3 = журнал пуст, 4 = хуки tweaks не готовы
        if (MessageBoxW(owner, L"Восстановить настройки реестра и системы?", L"Откат", MB_YESNO) != IDYES) return 2;
        // Пакет #14 hardening: 1102/3010 и обновление тумблеров идут через хуки слоя tweaks.
        // Если InitTweaks() ещё не выполнен, молча пропускать их нельзя — откат рапортовал бы
        // «полный успех», не восстановив схему питания и DNS. Поэтому явный отказ.
        if (!s_revertTweakHook || !s_readTweakStateHook) return 4;
        bool full = false;
        wchar_t rbMsg[512]{};
        {
            // Пакет #12: единая блокировка на весь откат. recursive_mutex допускает
            // повторный захват из RevertTweak -> AppendJournalLine; фоновые потоки ждут.
            std::lock_guard<std::recursive_mutex> lk(s_journalMtx);

            HANDLE hFile = CreateFileW(App::journalPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                MessageBoxW(owner, L"Журнал пуст — откатывать нечего.", L"LEFTIK", MB_OK);
                return 3; // Ничего не меняли — не трогаем активные профили в вызывающем коде
            }
            DWORD size = GetFileSize(hFile, NULL); std::string content(size, '\0'); DWORD rd_ = 0;
            ReadFile(hFile, &content[0], size, &rd_, NULL); CloseHandle(hFile);

            std::stringstream ss(content); std::string line; std::vector<std::string> lines;
            std::string importedKeep;
            while (std::getline(ss, line)) {
                if (!line.empty()) {
                    std::string trimmed = line;
                    if (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
                    if (trimmed.rfind("V3|IMPORTED|", 0) == 0) {
                        importedKeep += trimmed + "\n";
                    } else {
                        lines.push_back(trimmed);
                    }
                }
            }

            // 1.2/3: powercfg и DNS не восстанавливаются строками журнала — откатываем их
            // явно ДО цикла, чтобы новые записи от RevertTweak не попали в этот же проход.
            if (!LoadPrevPowerAdv().empty() && s_revertTweakHook) s_revertTweakHook(1102);
            {
                HKEY hDns{};
                if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\PrevDNS", 0, KEY_READ, &hDns) == ERROR_SUCCESS) {
                    RegCloseKey(hDns);
                    if (s_revertTweakHook) s_revertTweakHook(3010);
                }
            }

            int restored = 0, registryFailed = 0, cmdRestored = 0, cmdFailed = 0, unsupported = 0;
            for (auto it = lines.rbegin(); it != lines.rend(); ++it)
                RestoreEntryLine(*it, restored, registryFailed, cmdRestored, cmdFailed, unsupported);

            full = (registryFailed == 0 && cmdFailed == 0 && unsupported == 0);
            if (full) {
                // 2.7: журнал и кэш удаляем ТОЛЬКО при полном подтвержденном откате (сохраняя IMPORTED записи)
                if (importedKeep.empty()) {
                    bool delOk = (DeleteFileW(App::journalPath.c_str()) != 0);
                    if (delOk) DeleteFileW(GetIdxPath().c_str());
                } else {
                    Util::WriteFileAtomic(App::journalPath, importedKeep);
                    RebuildJournalIndex(true);
                }
                App::selectedGroups.clear();
                App::cachedGroups = GetJournalGroups();
                RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\States");
                // ReadTweakState читает живой реестр для большинства твиков (кэш — только для тиков-команд)
                for (auto& tw : App::tweaksList) if (s_readTweakStateHook) tw.isEnabled = s_readTweakStateHook(tw.id);
                InvalidateRect(owner, nullptr, FALSE);
                swprintf(rbMsg, 512, L"Откат завершён полностью.\nРеестр: восстановлено значений: %d\nКоманд: выполнено: %d", restored, cmdRestored);
            } else {
                // Исходный журнал ОСТАЁТСЯ на месте; .failed — дополнительная копия, не замена
                BOOL copied = CopyFileW(App::journalPath.c_str(), (App::journalPath + L".failed").c_str(), FALSE);
                swprintf(rbMsg, 512,
                    L"ОТКАТ НЕПОЛНЫЙ — журнал сохранён.\n\n"
                    L"Реестр: восстановлено %d, ошибок %d\n"
                    L"Команды: выполнено %d, ошибок %d\n"
                    L"Неподдерживаемых записей: %d (не выполнялись)\n\n"
                    L"Журнал: %s%s%s\nПроверьте эти операции вручную.",
                    restored, registryFailed, cmdRestored, cmdFailed, unsupported,
                    App::journalPath.c_str(), copied ? L"\nКопия: " : L"", copied ? (App::journalPath + L".failed").c_str() : L"");
            }
        }
        MessageBoxW(owner, rbMsg, L"LEFTIK", MB_OK);
        return full ? 0 : 1;
    }
}

namespace NetworkTweaker {
    const std::wstring NET_CLASS_PATH = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";

    bool IsValidAdapter(const std::wstring& driverDesc) {
        if (driverDesc.empty()) return false;
        if (driverDesc.find(L"Virtual") != std::wstring::npos || driverDesc.find(L"WAN Miniport") != std::wstring::npos || 
            driverDesc.find(L"Bluetooth") != std::wstring::npos || driverDesc.find(L"TAP-") != std::wstring::npos) {
            return false;
        }
        return true;
    }

    std::vector<std::wstring> GetPhysicalAdapters() {
        std::vector<std::wstring> adapters;
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NET_CLASS_PATH.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t subKeyName[256];
            DWORD subKeyNameSize;
            DWORD index = 0;

            while (true) {
                subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
                if (RegEnumKeyExW(hKey, index, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;

                std::wstring fullSubKey = NET_CLASS_PATH + L"\\" + subKeyName;
                std::wstring driverDesc = Util::ReadRegString(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), L"DriverDesc");

                if (IsValidAdapter(driverDesc)) {
                    adapters.push_back(fullSubKey);
                }
                index++;
            }
            RegCloseKey(hKey);
        }
        return adapters;
    }

}

namespace MemoryOptimizer {
    typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
        MemoryCaptureAccessedBits,
        MemoryCaptureAndResetAccessedBits,
        MemoryEmptyWorkingSets,
        MemoryFlushModifiedList,
        MemoryPurgeStandbyList,
        MemoryPurgeLowPriorityStandbyList,
        MemoryCommandMax
    } SYSTEM_MEMORY_LIST_COMMAND;

    typedef NTSTATUS(WINAPI *NtSetSystemInformationPtr)(INT SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength);

    bool EnablePrivilege(const wchar_t* privilegeName) {
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (!LookupPrivilegeValueW(nullptr, privilegeName, &luid)) { CloseHandle(hToken); return false; }
        tp.PrivilegeCount = 1; tp.Privileges[0].Luid = luid; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        bool result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr);
        CloseHandle(hToken);
        return result && (GetLastError() == ERROR_SUCCESS);
    }

    bool ClearStandbyList() {
        if (!EnablePrivilege(L"SeProfileSingleProcessPrivilege")) return false;
        HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtDll) return false;
        auto NtSetSystemInformation = (NtSetSystemInformationPtr)GetProcAddress(hNtDll, "NtSetSystemInformation");
        if (!NtSetSystemInformation) return false;

        SYSTEM_MEMORY_LIST_COMMAND command1 = MemoryEmptyWorkingSets;
        NTSTATUS status1 = NtSetSystemInformation(80, &command1, sizeof(command1));

        SYSTEM_MEMORY_LIST_COMMAND command2 = MemoryPurgeStandbyList;
        NTSTATUS status2 = NtSetSystemInformation(80, &command2, sizeof(command2));

        return (status1 == 0 && status2 == 0);
    }
}

bool IsNvidiaGpu() {
    // WMI-вендор — основной источник истины; имя используем только как fallback.
    if (App::hwInfo.gpuVendor != App::GPU_UNKNOWN)
        return App::hwInfo.gpuVendor == App::GPU_NVIDIA;
    std::wstring g = App::gpuName;
    std::transform(g.begin(), g.end(), g.begin(), ::towlower);
    return g.find(L"nvidia") != std::wstring::npos || g.find(L"geforce") != std::wstring::npos;
}
bool IsAmdGpu() {
    if (App::hwInfo.gpuVendor != App::GPU_UNKNOWN)
        return App::hwInfo.gpuVendor == App::GPU_AMD;
    std::wstring g = App::gpuName;
    std::transform(g.begin(), g.end(), g.begin(), ::towlower);
    return g.find(L"amd") != std::wstring::npos || g.find(L"radeon") != std::wstring::npos;
}

// GTX 10xx и старее: HAGS на них снижает FPS. Проверяем вендор, а не только строку.
static bool IsOldNvidiaGpu() {
    if (App::hwInfo.gpuVendor != App::GPU_UNKNOWN && App::hwInfo.gpuVendor != App::GPU_NVIDIA)
        return false;
    std::wstring g = App::gpuName;
    std::transform(g.begin(), g.end(), g.begin(), ::towlower);
    return g.find(L"gtx 10") != std::wstring::npos ||
           g.find(L"gtx 9")  != std::wstring::npos ||
           g.find(L"gtx 7")  != std::wstring::npos ||
           g.find(L"gtx 6")  != std::wstring::npos;
}

// AMD GCN (RX 400/500) не поддерживает HAGS. Модели GCN — трёхзначные
// (RX 470/580/590), RDNA — четырёхзначные (RX 5500/5700). Поэтому "rx 5"
// без разбора цифр ошибочно блокировал бы RX 5300/5500/5700.
static bool IsOldGcnAmdGpu() {
    if (App::hwInfo.gpuVendor != App::GPU_UNKNOWN && App::hwInfo.gpuVendor != App::GPU_AMD)
        return false;
    std::wstring g = App::gpuName;
    std::transform(g.begin(), g.end(), g.begin(), ::towlower);
    if (g.find(L"rx 4") != std::wstring::npos) return true; // RX 460/470/480
    size_t p = 0;
    while ((p = g.find(L"rx 5", p)) != std::wstring::npos) {
        size_t q = p + 3; // указывает на ведущую "5" в номере модели
        int digits = 0;
        while (q < g.size() && g[q] >= L'0' && g[q] <= L'9') { ++digits; ++q; }
        if (digits == 3) return true; // RX 550/580/590
        p = q;
    }
    return false;
}

// Windows build из реестра. CurrentBuildNumber — REG_SZ: чтение как DWORD всегда
// давало бы 0 и блокировало бы твики «только Win11» на ВСЕХ системах, включая Win11.
int GetWindowsBuild() {
    std::wstring s = Util::ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    return _wtoi(s.c_str());
}

// Guard 2.0: быстрый снимок состояния системы. Вызывается при старте и раз в 30 секунд.
void RefreshHardwareFlags() {
    HKEY k;
    DWORD v = 0, sz = sizeof(v);
    // VBS
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard", 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        sz = sizeof(v);
        if (RegQueryValueExW(k, L"EnableVirtualizationBasedSecurity", nullptr, nullptr, (BYTE*)&v, &sz) == ERROR_SUCCESS)
            App::hwInfo.vbsEnabled = (v == 1);
        RegCloseKey(k);
    }
    // Tamper Protection (MSDN: 1 = Disabled, 2 = Pending, 4 = Enabled, 5 = Enabled+blocked)
    v = 0; sz = sizeof(v);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Features", 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        sz = sizeof(v);
        if (RegQueryValueExW(k, L"TamperProtection", nullptr, nullptr, (BYTE*)&v, &sz) == ERROR_SUCCESS) {
            bool active = (v == 2 || v == 4 || v == 5);
            App::hwInfo.tamperProtection = active;
        }
        RegCloseKey(k);
    }
    // AC Power
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) App::hwInfo.onACPower = (sps.ACLineStatus == 1);
    // RAM
    MEMORYSTATUSEX mem; mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        App::hwInfo.ramLoadPct = (int)mem.dwMemoryLoad;
        App::hwInfo.ramTotalGB = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    }
    // Disk Free
    ULARGE_INTEGER fb, tb, tf;
    if (GetDiskFreeSpaceExW(L"C:\\", &fb, &tb, &tf)) App::hwInfo.diskFreeGB = (double)tf.QuadPart / (1024.0 * 1024.0 * 1024.0);
    // Build
    App::hwInfo.winBuild = GetWindowsBuild();

    // Xbox mode (2a.3): Win11 24H2+ и установленное приложение Xbox.
    // Намеренно НЕ матчим Microsoft.Xbox.TCUI / XboxIdentityProvider / GameBar:
    // они есть почти на каждом ПК и не дают Xbox mode (FSE). Нужен Microsoft.GamingApp.
    App::hwInfo.xboxModeAvailable = false;
    if (App::hwInfo.winBuild >= 26100) {
        HKEY hk{};
        const wchar_t* repo = L"Software\\Classes\\Local Settings\\Software\\"
                              L"Microsoft\\Windows\\CurrentVersion\\AppModel\\"
                              L"Repository\\Packages";
        if (RegOpenKeyExW(HKEY_CURRENT_USER, repo, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
            wchar_t name[512];
            for (DWORD idx = 0;; ++idx) {
                DWORD len = 512;
                if (RegEnumKeyExW(hk, idx, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                    break;
                std::wstring n(name, len);
                std::transform(n.begin(), n.end(), n.begin(), ::towlower);
                if (n == L"microsoft.gamingapp" || n.rfind(L"microsoft.gamingapp_", 0) == 0) {
                    App::hwInfo.xboxModeAvailable = true;
                    break;
                }
            }
            RegCloseKey(hk);
        }
    }

    // 2a.4: детект Intel APO по имени службы убран — имя не подтверждено.
    // LEFTIK только рекомендует APO текстом (Intel DSA), без эмуляции и без клика.
}

// Guard 2.0: тип системного диска. При ошибке diskIsSSD остаётся false (консервативно),
// но diskTypeKnown=false — UI и причина блокировки честно сообщают, что тип не определён.
void DetectDiskType() {
    App::hwInfo.diskTypeKnown = false;
    DWORD diskNumber = 0;
    HANDLE hVol = CreateFileW(L"\\\\.\\C:", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hVol != INVALID_HANDLE_VALUE) {
        VOLUME_DISK_EXTENTS vde{};
        DWORD ret = 0;
        if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &vde, sizeof(vde), &ret, nullptr) &&
            vde.NumberOfDiskExtents > 0) {
            diskNumber = vde.Extents[0].DiskNumber;
        }
        CloseHandle(hVol);
    }
    wchar_t physPath[64];
    swprintf(physPath, 64, L"\\\\.\\PhysicalDrive%lu", (unsigned long)diskNumber);
    HANDLE h = CreateFileW(physPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    STORAGE_PROPERTY_QUERY q{}; q.PropertyId = StorageDeviceSeekPenaltyProperty; q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR d{}; DWORD ret = 0;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &ret, nullptr)) {
        App::hwInfo.diskIsSSD = !d.IncursSeekPenalty;
        App::hwInfo.diskTypeKnown = true;
    }
    CloseHandle(h);
}

// ОЗУ < 15.5 ГБ ИЛИ объём не определен (16 ГБ в Windows = ~15.9 ГБ). App::totalRamText имеет формат "%.1f GB" — парсим число.
// Политика v57.13: неизвестные данные НЕ считаются безопасной совместимостью.
bool IsLowRamSystem() {
    wchar_t* end = nullptr;
    double gb = wcstod(App::totalRamText.c_str(), &end);
    return !(gb >= 15.5); // 0 (не распознано) тоже блокирует
}

// Hardware Guard: централизованная проверка «можно ли применять твик на этой системе».
// Явные вендорские ID вместо диапазона: 1102 (Core Parking) попадает в 1100-1109,
// но вендорским не является — диапазонная проверка блокировала бы его с чужой ошибкой.
static bool IsVendorNvidiaId(int id) { return id == 1100 || id == 1101; }
static bool IsVendorAmdId(int id) { return id == 1110 || id == 1111; }

// Определение tier системы для фильтрации твиков
App::HardwareTier DetectHardwareTier() {
    int cores = App::hwInfo.cpuPhysicalCores;
    double ramGB = App::hwInfo.ramTotalGB;

    // Low: 4 ядра и меньше, или <8 ГБ RAM, или HDD
    if (cores <= 4 || ramGB < 8.0 ||
        (App::hwInfo.diskTypeKnown && !App::hwInfo.diskIsSSD)) {
        return App::HardwareTier::Low;
    }
    // High: 8+ ядер и 16+ ГБ RAM и SSD
    if (cores >= 8 && ramGB >= 16.0 && App::hwInfo.diskIsSSD) {
        return App::HardwareTier::High;
    }
    return App::HardwareTier::Mid;
}

const wchar_t* HardwareTierName(App::HardwareTier t) {
    switch (t) {
        case App::HardwareTier::Low:  return L"Слабая (Low)";
        case App::HardwareTier::Mid:  return L"Средняя (Mid)";
        case App::HardwareTier::High: return L"Мощная (High)";
    }
    return L"?";
}

bool IsTweakAllowedForHardware(int id) {
    if (IsVendorNvidiaId(id) && !IsNvidiaGpu()) return false;
    if (IsVendorAmdId(id) && !IsAmdGpu()) return false;

    // ID 1098/1099/1102 reserved (удалены мёртвые ветки без твиков в InitTweaks)

    // 1120: блокировать при RAM < 16GB ИЛИ загрузке > 70%
    if (id == 1120 && (IsLowRamSystem() || App::hwInfo.ramLoadPct > 70)) return false;

    // 1104: удержание ядра в ОЗУ + 64MB IO lock при малой/загруженной памяти
    if (id == 1104 && (IsLowRamSystem() || App::hwInfo.ramLoadPct > 70)) return false;

    // 1121: только Win11 (при winBuild == 0 также блокируем)
    if (id == 1121 && App::hwInfo.winBuild < 22000) return false;

    // 1010: Tamper Protection активна
    if (id == 1010 && App::hwInfo.tamperProtection) return false;

    // 1021/1073: ноут от батареи (нагрев + разряд)
    if ((id == 1021 || id == 1073) && App::isLaptop && !App::hwInfo.onACPower) return false;

    // 1102: на ноутбуках блокируем всегда (в т.ч. от сети). Твик выставляет
    // PROCTHROTTLEMIN=100 и PERFBOOSTMODE=2 — охлаждение/VRM ноутбука этого
    // не выдерживают, начинается троттлинг и шум. Аналогично 1133.
    if (id == 1102 && App::isLaptop) return false;

    if (id == 1133 && App::isLaptop) return false;

    // 1040/1075: на HDD отключение SysMain/Prefetch = сильное замедление
    if ((id == 1040 || id == 1075) && !App::hwInfo.diskIsSSD) return false;

    // === Новые правила по tier ===

    // 1074 (HAGS) — на GTX 10xx и старее даёт -5-10% FPS
    if (id == 1074) { // 🔴 [HIGH-END] HAGS
        if (IsOldNvidiaGpu() || IsOldGcnAmdGpu()) return false;
    }

    // 1102 (CPU Unpark) — на Low-tier системах даёт +2%, но риск троттлинга высок
    if (id == 1102 && App::g_hardwareTier == App::HardwareTier::Low) return false;

    // 1097 (MSI Mode) — НЕ блокируем на Low-tier: он снижает DPC latency / инпут-лаг,
    // даже если не даёт прироста среднего FPS.

    // 1098 (MMCSS Games) — требует 4+ физических ядра
    if (id == 1098 && App::hwInfo.cpuPhysicalCores < 4) return false;

    // 1094 (NetworkThrottling) — на WiFi/ADSL риск джиттера, оставить, но пометить
    // (не блокируем, но UI предупредит)

    return true;
}

// Человекочитаемая причина блокировки. nullptr = не заблокировано.
bool IsTweakWarningOnly(int id) {
    // 2a.1: HVCI включена системой — это предупреждение, а не запрет.
    // Пользователь вправе осознанно отключить Memory Integrity (например, ради FPS),
    // но должен знать про Vanguard/Riot. Автоотключения HVCI нигде нет.
    return id == 1018 && App::hwInfo.winBuild >= 26100 && App::hwInfo.vbsEnabled;
}

const wchar_t* GetTweakBlockReason(int id) {
    if (IsVendorNvidiaId(id) && !IsNvidiaGpu()) return L"Твик доступен только на видеокартах NVIDIA.";
    if (IsVendorAmdId(id) && !IsAmdGpu()) return L"Твик доступен только на видеокартах AMD.";
    // ID 1098/1099/1102 reserved
    if (IsTweakWarningOnly(id))
        return L"Memory Integrity (HVCI) включена системой автоматически "
               L"(с октября 2026 Windows делает это на подходящем железе). "
               L"Отключение вернёт до 10–11% FPS в CPU-limited играх, "
               L"но Vanguard и часть античитов могут потребовать её обратно. "
               L"Решение осознанное: выключайте только если не играете в игры "
               L"с Vanguard/Riot.";
    if (id == 1120 && IsLowRamSystem()) return L"Управление компрессией памяти заблокировано: объём ОЗУ меньше 16 ГБ или не определён. Отключение сжатия на малой памяти вызывает своп и фризы.";
    if (id == 1120 && App::hwInfo.ramLoadPct > 70) return L"Заблокировано: оперативная память загружена более чем на 70%. Компрессия сейчас необходима.";
    if (id == 1104 && IsLowRamSystem()) return L"Kernel Memory Lock заблокирован: объём ОЗУ меньше 16 ГБ или не определён. Удержание ядра и лок 64 МБ при малой памяти вызывают вылеты игр.";
    if (id == 1104 && App::hwInfo.ramLoadPct > 70) return L"Заблокировано: оперативная память загружена более чем на 70%. Удержание ядра в ОЗУ сейчас вызывает вытеснение рабочих наборов игр.";
    if (id == 1121 && App::hwInfo.winBuild < 22000) return L"Отключение Widgets доступно только на Windows 11 (build 22000+).";
    if (id == 1010 && App::hwInfo.tamperProtection) return L"Заблокировано: Windows Defender защищён Tamper Protection. Отключите её вручную.";
    if ((id == 1021 || id == 1073) && App::isLaptop && !App::hwInfo.onACPower) return L"Заблокировано: ноутбук работает от батареи. Отключение троттлинга вызовет нагрев и быстрый разряд.";
    if (id == 1102 && App::isLaptop)
        return L"Заблокировано на ноутбуках: отключение парковки ядер и принудительный Turbo Boost вызывают перегрев, шум вентиляторов и троттлинг. Даже при питании от сети охлаждение ноутбука не рассчитано на постоянные 100% частоты.";
    if (id == 1133 && App::isLaptop)
        return L"Заблокировано на ноутбуках: отключение динамического тика заставляет процессор работать без остановок — перегрев и разряд батареи.";
    if ((id == 1040 || id == 1075) && !App::hwInfo.diskIsSSD) {
        if (!App::hwInfo.diskTypeKnown)
            return L"Тип диска не определен (возможно, SSD). Проверьте вручную.";
        return L"Заблокировано: у вас HDD. Отключение SysMain/Prefetch сильно замедлит работу системы.";
    }
    if (id == 1074) {
        if (IsOldNvidiaGpu())
            return L"HAGS на GTX 10xx и старее снижает FPS на 5-10%. Не включайте.";
        if (IsOldGcnAmdGpu())
            return L"HAGS не поддерживается на архитектуре AMD GCN (RX 400/500).";
    }
    if (id == 1102 && App::g_hardwareTier == App::HardwareTier::Low)
        return L"CPU Unpark на слабых системах: +2% FPS, но риск троттлинга и шума. Не рекомендуется.";
    if (id == 1098 && App::hwInfo.cpuPhysicalCores < 4)
        return L"MMCSS Games требует минимум 4 физических ядра.";

    return nullptr;
}

// Безопасный WMI-запрос числа (без _bstr_t, с инициализацией VARIANT).
// sumAll=true — суммирует по всем экземплярам (для счетчиков типа GPU Engine).
static bool WmiGetInt(const wchar_t* wmiNamespace, const wchar_t* query, const wchar_t* propName, int& outVal, bool sumAll) {
    outVal = -1;
    IWbemLocator* pLoc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc)) || !pLoc) return false;
    BSTR ns = SysAllocString(wmiNamespace), q = SysAllocString(query), wql = SysAllocString(L"WQL"), prop = SysAllocString(propName);
    IWbemServices* pSvc = nullptr;
    bool ok = false;
    if (ns && q && wql && prop && pLoc->ConnectServer(ns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &pSvc) == WBEM_S_NO_ERROR && pSvc) {
        CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        IEnumWbemClassObject* pEnum = nullptr;
        HRESULT hrExec = pSvc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
        if (hrExec == WBEM_S_NO_ERROR && pEnum) {
            int sum = 0; bool got = false; bool timedOut = false;
            for (;;) {
                IWbemClassObject* obj = nullptr; ULONG ret = 0;
                HRESULT hrNext = pEnum->Next(2000, 1, &obj, &ret);
                if (hrNext == WBEM_S_TIMEDOUT) { timedOut = true; break; }
                if (hrNext != WBEM_S_NO_ERROR || ret == 0) break;
                VARIANT vt; VariantInit(&vt);
                if (SUCCEEDED(obj->Get(prop, 0, &vt, nullptr, nullptr))) {
                    int v = -1;
                    if (vt.vt == VT_I4) v = vt.lVal;
                    else if (vt.vt == VT_UI4) v = (int)vt.ulVal;
                    else if (vt.vt == VT_R8) v = (int)vt.dblVal;
                    if (v >= 0) { got = true; sum += v; }
                    VariantClear(&vt);
                }
                obj->Release();
                if (!sumAll) break;
            }
            if (!timedOut && got) { outVal = sum; ok = true; }
        }
        if (pEnum) pEnum->Release();
        pSvc->Release();
    }
    if (ns) SysFreeString(ns);
    if (q) SysFreeString(q);
    if (wql) SysFreeString(wql);
    if (prop) SysFreeString(prop);
    pLoc->Release();
    return ok;
}

// Занятость VRAM через DXGI (документированный API, без внешних DLL)
void UpdateGPUMemory() {
    static IDXGIAdapter3* s_adapter3 = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
            IDXGIAdapter1* adapter1 = nullptr;
            if (SUCCEEDED(factory->EnumAdapters1(0, &adapter1)) && adapter1) {
                adapter1->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&s_adapter3);
                adapter1->Release();
            }
            factory->Release();
        }
    }
    if (!s_adapter3) return;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (SUCCEEDED(s_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) && info.Budget > 0) {
        App::gpuVramUsed = (double)info.CurrentUsage / (1024.0 * 1024.0 * 1024.0);
        App::gpuVramTotal = (double)info.Budget / (1024.0 * 1024.0 * 1024.0);
    }
}

// Загрузка GPU через PDH-счетчики "GPU Engine" (документированный API Windows 10+)
int GetGPULoadPDH() {
    static PDH_HQUERY query = nullptr;
    static PDH_HCOUNTER counter = nullptr;
    static bool inited = false, haveSample = false;
    if (!inited) {
        inited = true;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) { query = nullptr; return -1; }
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query); query = nullptr; counter = nullptr; return -1;
        }
        return -1; // первый проход — счетчик еще не собран
    }
    if (!query || !counter) return -1;
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) return -1;
    if (!haveSample) { haveSample = true; return -1; } // PDH нужны две выборки

    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, nullptr, &size, nullptr) != (PDH_STATUS)PDH_MORE_DATA || size == 0) return -1;
    std::vector<BYTE> buf(size);
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) return -1;
    double total = 0;
    for (DWORD i = 0; i < count; ++i)
        if (items[i].FmtValue.doubleValue > 0) total += items[i].FmtValue.doubleValue;
    return (int)std::min(100.0, total);
}

// Температура GPU только для NVIDIA: читаем вывод утилиты nvidia-smi (поставляется с драйвером).
// Прямые вызовы nvapi64.dll/atiadlxx.dll НЕ используем: структуры реверс-инжинирные,
// ошибка в размере любой структуры = порча памяти и краш прямо в DLL драйвера.
int GetGPUTempNvidiaSmi() {
    const wchar_t* candidates[] = {
        L"C:\\Windows\\System32\\nvidia-smi.exe",
        L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvidia-smi.exe"
    };
    for (const wchar_t* exe : candidates) {
        if (!Util::FileExists(exe)) continue;
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0)) continue;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        si.hStdOutput = wr; si.hStdError = wr;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = std::wstring(L"\"") + exe + L"\" --query-gpu=temperature.gpu --format=csv,noheader,nounits";
        int result = -1;
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(wr);
            char buf[128] = {}; DWORD rd_ = 0;
            // Сначала ждём завершения с таймаутом — иначе ReadFile блокирует поток навсегда,
            // если nvidia-smi завис на мёртвом драйвере.
            DWORD w = WaitForSingleObject(pi.hProcess, 3000);
            if (w == WAIT_OBJECT_0) {
                ReadFile(rd, buf, 127, &rd_, nullptr);
                int t = atoi(buf);
                if (t > 0 && t < 150) result = t;
            } else {
                TerminateProcess(pi.hProcess, 1);
            }
            CloseHandle(rd);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else {
            CloseHandle(wr); CloseHandle(rd);
        }
        if (result > 0) return result;
    }
    return -1;
}

// Фоновый монитор медленных датчиков (WMI CPU temp, nvidia-smi GPU temp).
// UI-поток читает atomic значения без блокировок и фризов.
namespace Monitor {
    std::atomic<int>    cpuTemp{-1};
    std::atomic<int>    gpuTemp{-1};
    std::atomic<double> junkGB{0.0};
    std::atomic<bool>   running{true};
    std::atomic<bool>   gameActive{false};   // Auto-Game Guard: игра запущена
    std::thread         worker;

    // v61 / 1.3: потокобезопасный кэш групп журнала, обновляемый в SlowLoop и по флагу.
    static std::mutex s_groupsMtx;
    static std::vector<std::wstring> s_groupsSnapshot;
    static std::atomic<bool> s_groupsDirty{true};

    void InvalidateGroupsCache() {
        s_groupsDirty.store(true, std::memory_order_release);
    }
    std::vector<std::wstring> GetGroupsSnapshot() {
        if (s_groupsDirty.exchange(false, std::memory_order_acq_rel)) {
            auto fresh = Journal::GetJournalGroups();
            std::lock_guard<std::mutex> lk(s_groupsMtx);
            s_groupsSnapshot = std::move(fresh);
        }
        std::lock_guard<std::mutex> lk(s_groupsMtx);
        return s_groupsSnapshot;
    }

    // ===== Auto-Game Guard =====
    static const wchar_t* s_gameExes[] = {
        L"cs2.exe", L"dota2.exe", L"gta_sa.exe", L"samp.exe", L"rustclient.exe",
        L"VALORANT-Win64-Shipping.exe", L"r5apex.exe", L"FortniteClient-Win64-Shipping.exe",
        L"TslGame.exe", L"GTA5.exe", L"FiveM_b2802_GTAProcess.exe", L"FiveM_b2944_GTAProcess.exe",
        L"FiveM_b3095_GTAProcess.exe", L"Overwatch.exe", L"RainbowSix.exe", L"project8.exe",
        L"Deadlock.exe", L"PathOfExileSteam.exe", L"EscapeFromTarkov.exe", L"hl2.exe"
    };
    static std::set<DWORD>    s_boostedPids;   // играм уже подняли приоритет — не повторяем каждый цикл
    static std::vector<DWORD> s_throttledPids; // фону уже снизили приоритет — восстановим при выходе из игры
    static std::map<DWORD, DWORD> s_throttledOldClass; // исходный класс приоритета каждого фонового PID

    bool IsGameProcessPid(DWORD pid, std::wstring* outName) {
        if (pid == 0) return false;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;
        bool matched = false;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    for (const wchar_t* g : s_gameExes) {
                        if (_wcsicmp(pe.szExeFile, g) == 0) {
                            matched = true;
                            if (outName) *outName = pe.szExeFile;
                            break;
                        }
                    }
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
        return matched;
    }

    static void ThrottleBackgroundForGame(bool on) {
        const wchar_t* bg[] = {
            L"steamwebhelper.exe", L"Discord.exe", L"chrome.exe", L"msedge.exe",
            L"firefox.exe", L"YandexBrowser.exe", L"opera.exe", L"EpicGamesLauncher.exe",
            L"Battle.net.exe", L"Spotify.exe"
        };
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    for (const wchar_t* b : bg) {
                        if (_wcsicmp(pe.szExeFile, b) == 0) {
                            HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, false, pe.th32ProcessID);
                            if (h) {
                                if (on) {
                                    DWORD oldCls = GetPriorityClass(h);
                                    if (SetPriorityClass(h, IDLE_PRIORITY_CLASS)) {
                                        s_throttledOldClass[pe.th32ProcessID] = oldCls;
                                        s_throttledPids.push_back(pe.th32ProcessID);
                                    }
                                    PROCESS_POWER_THROTTLING_STATE pts{};
                                    pts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                                    pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                                    pts.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                                    SetProcessInformation(h, ProcessPowerThrottling, &pts, sizeof(pts));
                                } else {
                                    DWORD oldCls = NORMAL_PRIORITY_CLASS;
                                    auto itC = s_throttledOldClass.find(pe.th32ProcessID);
                                    if (itC != s_throttledOldClass.end()) { oldCls = itC->second; s_throttledOldClass.erase(itC); }
                                    SetPriorityClass(h, oldCls);
                                    PROCESS_POWER_THROTTLING_STATE pts{};
                                    pts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                                    pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                                    pts.StateMask = 0;
                                    SetProcessInformation(h, ProcessPowerThrottling, &pts, sizeof(pts));
                                }
                                CloseHandle(h);
                            }
                            break;
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        if (!on) { s_throttledPids.clear(); s_throttledOldClass.clear(); }
    }

    static void TrimBackgroundWorkingSets() {
        const wchar_t* bgProcesses[] = {
            L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"brave.exe",
            L"yandex.exe", L"browser.exe", L"YandexBrowser.exe", L"vivaldi.exe",
            L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe",
            L"Telegram.exe", L"TelegramDesktop.exe",
            L"steamwebhelper.exe", L"epicgameslauncher.exe", L"spotify.exe"
        };
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    for (const wchar_t* bg : bgProcesses) {
                        if (_wcsicmp(pe.szExeFile, bg) == 0) {
                            HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                            if (hProc) {
                                SetProcessWorkingSetSize(hProc, (SIZE_T)-1, (SIZE_T)-1);
                                CloseHandle(hProc);
                            }
                            break;
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    static bool ScanGamesAndBoost() {
        bool found = false;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    for (const wchar_t* g : s_gameExes) {
                        if (_wcsicmp(pe.szExeFile, g) == 0) {
                            found = true;
                            if (s_boostedPids.find(pe.th32ProcessID) == s_boostedPids.end()) {
                                HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                                if (h) {
                                    SetPriorityClass(h, HIGH_PRIORITY_CLASS);
                                    PROCESS_POWER_THROTTLING_STATE pts{};
                                    pts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                                    pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                                    pts.StateMask = 0; // 0 = гарантированно высокая частота и P-ядра
                                    SetProcessInformation(h, ProcessPowerThrottling, &pts, sizeof(pts));
                                    CloseHandle(h);
                                }
                                s_boostedPids.insert(pe.th32ProcessID);
                            }
                            break;
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        return found;
    }

    static void GameGuardTick(bool& wasActive) {
        bool nowActive = ScanGamesAndBoost();
        gameActive.store(nowActive, std::memory_order_relaxed);
        if (nowActive && !wasActive) {
            // Игра только что запустилась — разовая активация буста
            TimerMgr::Acquire(L"gameguard");
            MemoryOptimizer::ClearStandbyList();
            TrimBackgroundWorkingSets();
            ThrottleBackgroundForGame(true);

            // v59: Hanami-style авто-профиль
            // Если Turbo/Ultra профили выключены, принудительно ставим High Priority для игры
            // (уже делается в ScanGamesAndBoost) и скидываем GameDVR на лету.
            // Применять тяжелые твики (VBS, MSI) на лету опасно, они уже есть в профилях.
            Journal::LogAction(L"Auto-Game Guard: игра запущена, профиль активирован");
        } else if (!nowActive && wasActive) {
            // Игра закрылась — возвращаем таймер в дефолт, ЕСЛИ её запрос был единственным
            // (не глушим таймер, включенный профилем или тумблером 1080)
            TimerMgr::Release(L"gameguard");
            ThrottleBackgroundForGame(false);
            s_boostedPids.clear();
        }
        wasActive = nowActive;
    }

    static void SlowLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool wasGameActive = false;
        int cycle = 0;
        // v58.0 Feature B: отслеживание авто-очистки standby
        ULONGLONG lastStandbyClean = GetTickCount64();
        while (running.load(std::memory_order_relaxed)) {
            ULONGLONG iterStart = GetTickCount64();
            // Температуры: 6 с при видимом окне/игре; 15 с, если игра не запущена
            // и окно скрыто в трее (WMI и nvidia-smi — дорогие опросы).
            bool hiddenIdle = !gameActive.load(std::memory_order_relaxed) &&
                              App::hwnd && !IsWindowVisible(App::hwnd);
            int tempPeriod = hiddenIdle ? 5 : 2;
            if (cycle % tempPeriod == 0) {
                int t = -1;
                if (WmiGetInt(L"ROOT\\WMI",
                              L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                              L"CurrentTemperature", t, false)) {
                    int c = (t - 2732) / 10;
                    cpuTemp.store((c > 0 && c < 150) ? c : -1, std::memory_order_relaxed);
                } else {
                    cpuTemp.store(-1, std::memory_order_relaxed);
                }
                if (!running.load(std::memory_order_relaxed)) break;
                // Не блокируем итерацию дольше ~5 сек суммарно
                if (GetTickCount64() - iterStart < 3000) {
                    int gt = IsNvidiaGpu() ? GetGPUTempNvidiaSmi() : -1;
                    gpuTemp.store(gt, std::memory_order_relaxed);
                }
            }
            if (!running.load(std::memory_order_relaxed)) break;

            // Auto-Game Guard — каждые 3 с
            GameGuardTick(wasGameActive);

            // ETW-бенчмарк: жёсткий лимит 5 минут. Остановка идёт в фоне,
            // UI получает WM_LEFTIK_BENCH_STOPPED и обновляет экран.
            if (Benchmark::AutoStopDue()) Benchmark::RequestStop(App::hwnd, true);

            // v61 / 1.3: фоновое обновление кэша групп журнала в SlowLoop (без блокировки UI-потока)
            {
                auto freshGroups = Journal::GetJournalGroups();
                std::lock_guard<std::mutex> lk(s_groupsMtx);
                s_groupsSnapshot = std::move(freshGroups);
                s_groupsDirty.store(false, std::memory_order_release);
            }

            // Подсчёт временного мусора (%TEMP% + Корзина) раз в 30 с (10 циклов по 3 с)
            if (cycle % 10 == 0) {
                double rbGB = 0.0;
                SHQUERYRBINFO rbInfo = { sizeof(SHQUERYRBINFO) };
                if (SHQueryRecycleBinW(L"C:\\", &rbInfo) == S_OK)
                    rbGB = (double)rbInfo.i64Size / (1024.0 * 1024.0 * 1024.0);
                double tempGB = 0.0;
                wchar_t tempPath[MAX_PATH];
                if (GetTempPathW(MAX_PATH, tempPath) > 0) {
                    std::error_code ecScan;
                    std::filesystem::recursive_directory_iterator itT(tempPath, std::filesystem::directory_options::skip_permission_denied, ecScan);
                    std::filesystem::recursive_directory_iterator endT;
                    while (!ecScan && itT != endT && running.load(std::memory_order_relaxed)) {
                        std::error_code ec2;
                        if (itT->is_regular_file(ec2) && !ec2) tempGB += (double)itT->file_size(ec2);
                        itT.increment(ecScan);
                    }
                    tempGB /= (1024.0 * 1024.0 * 1024.0);
                }
                junkGB.store(rbGB + tempGB, std::memory_order_relaxed);
            }

            // v58.0 Feature B: авто-очистка standby-памяти по таймеру.
            // Guard: только на SSD (на HDD это замедляет файловый кэш).
            if (App::autoStandbyClean && App::hwInfo.diskIsSSD) {
                ULONGLONG now = GetTickCount64();
                ULONGLONG intervalMs = (ULONGLONG)App::standbyIntervalMin * 60ULL * 1000ULL;
                if (now - lastStandbyClean >= intervalMs) {
                    MemoryOptimizer::ClearStandbyList();
                    lastStandbyClean = now;
                }
            }

            ++cycle;
            for (int i = 0; i < 30; ++i) {
                if (!running.load(std::memory_order_relaxed)) break;
                if (GetTickCount64() - iterStart >= 5000) break;
                // Проверка лимита 5 минут каждые 100 мс — авто-стоп без опоздания на весь цикл.
                if (Benchmark::AutoStopDue()) Benchmark::RequestStop(App::hwnd, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        CoUninitialize();
    }
    void Start() {
        if (worker.joinable()) {
            running.store(false);
            worker.join(); // Безопасно убиваем старый поток, если он был
        }
        running.store(true);
        worker = std::thread(SlowLoop); 
    }
    void Stop() {
        running.store(false, std::memory_order_relaxed);
        if (worker.joinable()) worker.join();
    }
}

void TrimBackgroundWorkingSets() {
    Monitor::TrimBackgroundWorkingSets();
}

void RunPowerShell(const std::wstring& cmd) { 
    Journal::LogAction(L"PowerShell: " + cmd.substr(0, 30) + L"...");
    ShellExecuteW(nullptr, L"runas", L"powershell.exe", (L"-NoProfile -ExecutionPolicy Bypass -Command \"" + cmd + L"\"").c_str(), nullptr, SW_HIDE); 
}
void RunCmd(const std::wstring& cmd) { 
    Journal::LogAction(L"CMD: " + cmd.substr(0, 30) + L"...");
    ShellExecuteW(nullptr, L"runas", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE); 
}
// Для долгих операций (Store/SFC/CompactOS): окно консоли видно,
// пользователь видит прогресс и финальное сообщение. Без этого `pause`
// оставлял бы невидимый процесс cmd.exe висеть в ожидании клавиши.
bool RunCmdVisible(const std::wstring& cmd) {
    Journal::LogAction(L"CMD (visible): " + cmd.substr(0, 30) + L"...");
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", L"cmd.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}
void DisableSvc(const std::wstring& s) { 
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\" + s, L"Start", 4, L"Служба: " + s); 
}

// Прокачивает очередь сообщений, чтобы окно не висело "Не отвечает"
// во время долгой очистки в UI-потоке.
static void PumpMessages() {
    MSG msg;
    // Прокачиваем ТОЛЬКО WM_PAINT: окно перерисовывается ("дышит") во время долгой
    // очистки, но клики/крестик/хоткеи не доходят — приложение нельзя случайно
    // закрыть посреди удаления файлов.
    while (PeekMessageW(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) {
        DispatchMessageW(&msg);
    }
}

void DeleteFolderRecursive(const std::wstring& folder) {
    std::wstring pattern = folder + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    static int s_pumpCounter = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring fullPath = folder + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(fullPath.c_str());
            } else {
                SetFileAttributesW(fullPath.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(fullPath.c_str());
            }
            continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteFolderRecursive(fullPath);
            RemoveDirectoryW(fullPath.c_str());
        } else {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = fd.nFileSizeLow;
            fileSize.HighPart = fd.nFileSizeHigh;

            SetFileAttributesW(fullPath.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(fullPath.c_str())) {
                App::totalBytesFreed += fileSize.QuadPart;
                App::totalFilesDeleted++;
            }
        }

        // Каждые 200 файлов — прокачиваем сообщения, чтобы окно дышало.
        if (++s_pumpCounter >= 200) {
            s_pumpCounter = 0;
            PumpMessages();
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

void RunCleaner() {
    if (!IsUserAnAdmin()) {
        MessageBoxW(App::hwnd, L"Очистка требует прав Администратора.", L"Нет прав", MB_OK);
        return;
    }
    if (!App::cleanTemp && !App::cleanPrefetch && !App::cleanLogs && !App::cleanRecycle) {
        MessageBoxW(App::hwnd, L"Ничего не выбрано!", L"LEFTIK", MB_OK);
        return;
    }
    HCURSOR hOldCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    static bool s_busyRC = false; if (s_busyRC) return; s_busyRC = true; // PumpMessages может привести к повторному клику

    App::totalBytesFreed = 0;
    App::totalFilesDeleted = 0;

    if (App::cleanTemp) {
        wchar_t tempPath[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tempPath) > 0) DeleteFolderRecursive(tempPath);
        wchar_t winDir[MAX_PATH];
        GetWindowsDirectoryW(winDir, MAX_PATH);
        DeleteFolderRecursive(std::wstring(winDir) + L"\\Temp");
    }
    if (App::cleanPrefetch) {
        wchar_t winDir[MAX_PATH];
        GetWindowsDirectoryW(winDir, MAX_PATH);
        DeleteFolderRecursive(std::wstring(winDir) + L"\\Prefetch");
    }
    if (App::cleanLogs) {
        wchar_t winDir[MAX_PATH];
        GetWindowsDirectoryW(winDir, MAX_PATH);
        DeleteFolderRecursive(std::wstring(winDir) + L"\\Logs");
        DeleteFolderRecursive(std::wstring(winDir) + L"\\Panther");
    }
    if (App::cleanRecycle) {
        SHEmptyRecycleBinW(App::hwnd, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    }

    double freedMB = App::totalBytesFreed / (1024.0 * 1024.0);
    wchar_t msg[256];
    swprintf(msg, 256, L"Очистка выполнена!\n\nУдалено файлов: %d\nОсвобождено: %.2f МБ", App::totalFilesDeleted, freedMB);
    s_busyRC = false;
    SetCursor(hOldCur);
    MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK);
}

// Глубокая очистка системного мусора: кэш обновлений, кэш Delivery Optimization,
// дампы, WER-отчеты, темпы UWP-пакетов. Личные файлы не трогаются.
void SmartDiskClean() {
    if (!IsUserAnAdmin()) {
        MessageBoxW(App::hwnd, L"Глубокая очистка требует прав Администратора.", L"Нет прав", MB_OK);
        return;
    }
    HCURSOR hOldCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    static bool s_busySDC = false; if (s_busySDC) return; s_busySDC = true; // PumpMessages может привести к повторному клику
    App::totalBytesFreed = 0;
    App::totalFilesDeleted = 0;

    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);

    // 1. Кэш обновлений Windows
    DeleteFolderRecursive(std::wstring(winDir) + L"\\SoftwareDistribution\\Download");

    // 2. Кэш Delivery Optimization (реальный путь — в профиле сетевой службы)
    DeleteFolderRecursive(std::wstring(winDir) + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache");

    // 3. Минидампы BSOD
    DeleteFolderRecursive(std::wstring(winDir) + L"\\Minidump");

    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData))) {
        std::wstring lad = localAppData;
        CoTaskMemFree(localAppData);

        // 4. Крэш-дампы приложений
        DeleteFolderRecursive(lad + L"\\CrashDumps");

        // 5. Отчеты об ошибках Windows (WER)
        DeleteFolderRecursive(lad + L"\\Microsoft\\Windows\\WER\\ReportQueue");
        DeleteFolderRecursive(lad + L"\\Microsoft\\Windows\\WER\\ReportArchive");

        // 6. Временные папки внутри UWP-пакетов (у каждого пакета своя AC\Temp)
        std::error_code ecIt;
        std::filesystem::directory_iterator it(lad + L"\\Packages", std::filesystem::directory_options::skip_permission_denied, ecIt);
        std::filesystem::directory_iterator end;
        while (!ecIt && it != end) {
            std::error_code ec;
            if (it->is_directory(ec)) {
                std::wstring t = (it->path() / L"AC\\Temp").wstring();
                if (std::filesystem::exists(t)) DeleteFolderRecursive(t);
            }
            it.increment(ecIt);
        }
    }

    double freedMB2 = App::totalBytesFreed / (1024.0 * 1024.0);
    wchar_t msg2[256];
    swprintf(msg2, 256, L"Умная очистка выполнена!\n\nУдалено файлов: %d\nОсвобождено: %.2f МБ\n\n(Кэш обновлений, ДО-кэш, дампы, WER-отчеты)", App::totalFilesDeleted, freedMB2);
    Journal::LogAction(L"Умная очистка диска (Smart Disk Clean)");
    s_busySDC = false;
    SetCursor(hOldCur);
    MessageBoxW(App::hwnd, msg2, L"LEFTIK", MB_OK);
}

// Удаляет только ПУСТЫЕ папки-«хвосты» от удаленных программ в AppData/ProgramData.
// Скрытые/системные папки и известные критичные каталоги не трогаются.
void CleanEmptyAppTraces() {
    if (!IsUserAnAdmin()) {
        MessageBoxW(App::hwnd, L"Очистка хвостов требует прав Администратора.", L"Нет прав", MB_OK);
        return;
    }
    HCURSOR hOldCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    static bool s_busyCAT = false; if (s_busyCAT) return; s_busyCAT = true; // PumpMessages может привести к повторному клику

    std::vector<std::wstring> baseDirs;
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &known))) { baseDirs.push_back(known); CoTaskMemFree(known); known = nullptr; }
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &known))) { baseDirs.push_back(known); CoTaskMemFree(known); known = nullptr; }
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, NULL, &known))) { baseDirs.push_back(known); CoTaskMemFree(known); known = nullptr; }

    // Не трогаем критичные каталоги, даже если они пустые
    const wchar_t* skipList[] = { L"temp", L"microsoft", L"packages", L"windows", L"system", L"comms", L"squirreltemp", L"leftik" };

    int deletedFolders = 0;
    for (const auto& base : baseDirs) {
        std::error_code ecIt;
        std::filesystem::directory_iterator it(base, std::filesystem::directory_options::skip_permission_denied, ecIt);
        std::filesystem::directory_iterator end;
        while (!ecIt && it != end) {
            std::error_code ec;
            bool isDir = it->is_directory(ec);
            std::wstring fnameLower = it->path().filename().wstring();
            std::transform(fnameLower.begin(), fnameLower.end(), fnameLower.begin(), ::towlower);
            bool skipped = false;
            for (const wchar_t* s : skipList) if (fnameLower == s) { skipped = true; break; }
            DWORD attr = GetFileAttributesW(it->path().c_str());
            if (isDir && !skipped && !(attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
                std::error_code ecEmpty;
                if (std::filesystem::is_empty(it->path(), ecEmpty) && !ecEmpty) {
                    std::error_code ecRem;
                    if (std::filesystem::remove(it->path(), ecRem) && !ecRem) deletedFolders++;
                }
            }
            it.increment(ecIt);
        }
    }

    wchar_t msg3[128];
    swprintf(msg3, 128, L"Удалено пустых папок (хвостов): %d", deletedFolders);
    Journal::LogAction(L"Очистка хвостов удаленных приложений");
    s_busyCAT = false;
    SetCursor(hOldCur);
    MessageBoxW(App::hwnd, msg3, L"LEFTIK", MB_OK);
}

void CleanSampCache() {
    PWSTR userDocs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &userDocs))) return;
    std::wstring cacheDir = std::wstring(userDocs) + L"\\GTA San Andreas User Files\\SAMP\\cache";
    CoTaskMemFree(userDocs);

    if (GetFileAttributesW(cacheDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(App::hwnd, L"Кэш SAMP не найден.", L"LEFTIK", MB_OK);
        return;
    }
    if (MessageBoxW(App::hwnd,
        L"Очистить кэш SAMP (модели, скачанные с серверов)?\n\n"
        L"При следующем заходе на Arizona RP они скачаются заново\n"
        L"(первый заход будет дольше).",
        L"LEFTIK", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    App::totalBytesFreed = 0; App::totalFilesDeleted = 0;
    DeleteFolderRecursive(cacheDir);
    double mb = App::totalBytesFreed / (1024.0 * 1024.0);
    wchar_t msg[256];
    swprintf(msg, 256, L"Кэш SAMP очищен.\nФайлов: %d\nОсвобождено: %.1f МБ",
             App::totalFilesDeleted, mb);
    MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK);
}

void CreateRestorePoint() {
    RunPowerShell(L"Enable-ComputerRestore -Drive 'C:\\'");
    Sleep(500);
    RunPowerShell(L"Checkpoint-Computer -Description 'LEFTIK Optimizer' -RestorePointType 'MODIFY_SETTINGS'");
    MessageBoxW(App::hwnd,
        L"Команда создания точки восстановления отправлена.\n\n"
        L"Если ничего не произошло — проверь:\n"
        L"1) Панель управления → Восстановление системы\n"
        L"2) Защита системы → диск C: включен\n\n"
        L"Создание может занять 1-3 минуты.", L"LEFTIK", MB_OK);
}

void EnsureFirstRunRestorePoint() {
    // Маркер первого запуска
    PWSTR localAppData = nullptr;
    std::wstring markerPath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData))) {
        markerPath = Util::Join(localAppData, L"LEFTIK_firstrun.marker");
        CoTaskMemFree(localAppData);
    } else {
        markerPath = Util::Join(Util::ExeDir(), L"leftik_firstrun.marker");
    }
    if (Util::FileExists(markerPath)) return;

    int choice = MessageBoxW(App::hwnd,
        L"Это первый запуск LEFTIK Optimizer.\n\n"
        L"Рекомендуется создать точку восстановления Windows\n"
        L"перед первым применением твиков. Занимает 1-3 минуты.\n\n"
        L"Создать точку сейчас?",
        L"LEFTIK — Первый запуск", MB_YESNOCANCEL | MB_ICONQUESTION);

    if (choice == IDYES) {
        if (App::busyJob.load(std::memory_order_relaxed)) {
            MessageBoxW(App::hwnd, L"Дождитесь завершения предыдущей операции.", L"LEFTIK", MB_OK);
        } else {
            App::busyJob.store(true);
            std::thread([]() {
                DWORD ec = 0;
                RunPowerShellWait(L"Enable-ComputerRestore -Drive 'C:\\'", 10000, ec);
                // Увеличиваем таймаут до 2 минут, чтобы Checkpoint-Computer успел
                ProcWaitStatus st = RunPowerShellWait(L"Checkpoint-Computer -Description 'LEFTIK Optimizer (first run)' -RestorePointType 'MODIFY_SETTINGS'", 120000, ec);
                App::busyJob.store(false);
                if (!App::appClosing.load() && App::hwnd) {
                    PostMessageW(App::hwnd, WM_APP + 11, (WPARAM)st, (LPARAM)ec);
                }
            }).detach();
        }
    }

    // Маркер пишем в любом случае — не надоедать повторно
    std::ofstream mf{std::filesystem::path(markerPath)};
    if (mf.is_open()) {
        mf << "LEFTIK first run at " << GetTickCount64() << "\n";
        mf.close();
    }
}

bool SetTimerResolution(bool enable) {
    // 1.4: Monitor::GameGuardTick вызывает эту функцию из фонового потока.
    // Journal::SetDword не потокобезопасен и при выключении сделал бы бэкап 1,
    // поэтому пишем напрямую и всегда возвращаем 0 при выключении.
    if (App::hwInfo.winBuild >= 22000) {
        HKEY hKey{};
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
                0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            DWORD val = enable ? 1 : 0;
            RegSetValueExW(hKey, L"GlobalTimerResolutionRequests", 0, REG_DWORD,
                (const BYTE*)&val, sizeof(val));
            RegCloseKey(hKey);
        }
    }
    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtDll) return false;
    typedef NTSTATUS(WINAPI* NtSetTimerResolutionPtr)(ULONG, BOOLEAN, PULONG);
    auto fn = (NtSetTimerResolutionPtr)GetProcAddress(hNtDll, "NtSetTimerResolution");
    if (!fn) return false;
    ULONG currentRes = 0;
    NTSTATUS st = fn(5000, enable ? TRUE : FALSE, &currentRes);
    if (enable && st == 0) Journal::LogAction(L"Таймер высокого разрешения (0.5ms) включен");
    return st == 0;
}

void ClearAllShaderCaches() {
    PWSTR lad = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &lad))) {
        std::wstring base(lad); CoTaskMemFree(lad);
        DeleteFolderRecursive(base + L"\\D3DSCache");
        DeleteFolderRecursive(base + L"\\NVIDIA\\DXCache");
        DeleteFolderRecursive(base + L"\\NVIDIA\\GLCache");
        DeleteFolderRecursive(base + L"\\AMD\\DxCache");
        DeleteFolderRecursive(base + L"\\AMD\\DxcCache");
    }
}

void ClearDXCache() {
    ClearAllShaderCaches();
    Journal::LogAction(L"Очистка кэша шейдеров DirectX / NVIDIA / AMD");
    MessageBoxW(App::hwnd, L"Кэш шейдеров DirectX, NVIDIA и AMD успешно очищен!\nЭто уберет статтеры в играх.", L"LEFTIK", MB_OK);
}

// Включает SeDebugPrivilege в текущем процессе — без этого OpenProcess падает
// на процессах SYSTEM / другого пользователя, даже если запущено от админа.
static bool EnableDebugPrivilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    LUID luid;
    bool ok = false;
    if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
             GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(hToken);
    return ok;
}

// Убивает процесс: сначала TerminateProcess, при неудаче — fallback через
// taskkill /F [/T] /PID (работает на stubborn UWP-приложениях и защищённых процессах).
// Для лаунчеров killTree=false (по умолчанию), чтобы /T не закрыл дочерний процесс самой игры.
// Логирует провалы в leftik_debug.log строкой "KILL FAIL ...".
static bool ForceKillProcess(const wchar_t* exeName, DWORD pid, bool killTree = false) {
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        BOOL ok = TerminateProcess(hProc, 0);
        DWORD err = ok ? 0 : GetLastError();
        CloseHandle(hProc);
        if (ok) return true;
        HANDLE hf = CreateFileW(App::debugLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            std::wstring wline = L"KILL FAIL TerminateProcess(" + std::wstring(exeName) + L" pid=" + std::to_wstring(pid) + L") err=" + std::to_wstring(err) + L" -> taskkill\r\n";
            std::string u8 = Util::ToUtf8(wline);
            DWORD w = 0; WriteFile(hf, u8.data(), (DWORD)u8.size(), &w, NULL);
            CloseHandle(hf);
        }
    } else {
        DWORD err = GetLastError();
        HANDLE hf = CreateFileW(App::debugLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            std::wstring wline = L"KILL FAIL OpenProcess(" + std::wstring(exeName) + L" pid=" + std::to_wstring(pid) + L") err=" + std::to_wstring(err) + L" -> taskkill\r\n";
            std::string u8 = Util::ToUtf8(wline);
            DWORD w = 0; WriteFile(hf, u8.data(), (DWORD)u8.size(), &w, NULL);
            CloseHandle(hf);
        }
    }

    // Fallback: taskkill /F [/T] /PID <pid> — по идентификатору: работает и для имен
    // с пробелами ("Razer Synapse 3.exe"). Для лаунчеров БЕЗ /T, иначе закроется дочерняя игра.
    std::wstring cmd = std::wstring(L"taskkill /F ") + (killTree ? L"/T " : L"") + L"/PID " + std::to_wstring(pid);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    bool killed = false;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        DWORD ec = 1;
        GetExitCodeProcess(pi.hProcess, &ec);
        killed = (ec == 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return killed;
}

bool IsProcessRunning(const wchar_t* exeName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = true; break; }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

void KillBackgroundApps() {
    // Без SeDebugPrivilege половина процессов (SYSTEM / elevated) не откроется,
    // даже если приложение запущено от админа — в токене привилегия выключена по умолчанию.
    bool debugPriv = EnableDebugPrivilege();

    const wchar_t* targets[] = {
        // Браузеры
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"YandexBrowser.exe", L"opera.exe",
        L"brave.exe", L"vivaldi.exe", L"tor.exe",
        // Мессенджеры (включая варианты Store и Canary/PTB сборки Discord)
        L"Discord.exe", L"DiscordCanary.exe", L"DiscordPTB.exe", L"DiscordDevelopment.exe",
        L"Telegram.exe", L"TelegramDesktop.exe", L"WhatsApp.exe",
        L"Skype.exe", L"SkypeApp.exe", L"Lync.exe",
        L"Zoom.exe", L"Teams.exe", L"ms-teams.exe", L"Slack.exe", L"Viber.exe",
        // Фоновый хлам Windows / Phone Link / Xbox Game Bar
        L"YourPhone.exe", L"GameBar.exe", L"GameBarFTServer.exe",
        // Игровые лаунчеры (Steam и его webhelper не трогаем — иначе сломается оверлей в CS2/Dota)
        L"EpicGamesLauncher.exe", L"Battle.net.exe", L"Origin.exe",
        L"EAapp.exe", L"EADesktop.exe", L"UbisoftConnect.exe", L"GalaxyClient.exe",
        // Медиа и стриминг
        L"Spotify.exe", L"iTunes.exe", L"vlc.exe", L"obs64.exe",
        // v58.0: Облачные хранилища ОСТАВЛЕНЫ (синхронизация, риск битых файлов).
        // node.exe убран (VS Code, WebStorm, dev-серверы — потеря работы).
        // Adobe ОСТАВЛЕН (лицензионные проверки, фоновые синхронизации).
        // RGB-софт, оверлеи, NVIDIA/AMD фоновое
        L"iCUE.exe", L"Razer Synapse 3.exe", L"RazerCentralService.exe",
        L"LogiOptions.exe", L"LogiOptionsMgr.exe", L"LightingService.exe",
        L"NVIDIA Share.exe", L"NVIDIA Web Helper.exe",
        // Хвосты обновляторов
        L"Update.exe", L"Squirrel.exe", L"SquirrelTemp.exe"
    };

    // Для лаунчеров НЕ используем /T (killTree = false): игры, запущенные через
    // EpicGamesLauncher / Battle.net / Origin / EAapp / EADesktop / UbisoftConnect / GalaxyClient,
    // являются дочерними процессами лаунчера, и /T убил бы саму игру посреди сессии.
    const wchar_t* launchersNoTree[] = {
        L"EpicGamesLauncher.exe", L"Battle.net.exe", L"Origin.exe",
        L"EAapp.exe", L"EADesktop.exe", L"UbisoftConnect.exe", L"GalaxyClient.exe"
    };

    auto ToLowerW = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    };

    static const std::unordered_set<std::wstring> targetSet = [&]() {
        std::unordered_set<std::wstring> s;
        for (const wchar_t* t : targets) s.insert(ToLowerW(t));
        return s;
    }();
    static const std::unordered_set<std::wstring> launcherSet = [&]() {
        std::unordered_set<std::wstring> s;
        for (const wchar_t* l : launchersNoTree) s.insert(ToLowerW(l));
        return s;
    }();

    int killedCount = 0;
    int failedCount = 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == GetCurrentProcessId()) continue;

                std::wstring exeLow = ToLowerW(pe.szExeFile);
                if (targetSet.find(exeLow) != targetSet.end()) {
                    bool killTree = (launcherSet.find(exeLow) == launcherSet.end());
                    if (ForceKillProcess(pe.szExeFile, pe.th32ProcessID, killTree)) {
                        killedCount++;
                    } else {
                        HANDLE hCheck = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                        bool alive;
                        if (hCheck) {
                            alive = (WaitForSingleObject(hCheck, 0) == WAIT_TIMEOUT);
                            CloseHandle(hCheck);
                        } else {
                            alive = (GetLastError() == ERROR_ACCESS_DENIED);
                        }
                        if (alive) failedCount++; else killedCount++;
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // Сохраняем результаты для отчёта в case 3000 — без MessageBox
    App::lastKilledCount = killedCount;
    App::lastFailedCount = failedCount;

    // Итог в debug-лог — по нему сразу видно, что упало и почему
    HANDLE hf = CreateFileW(App::debugLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        char buf[256];
        int len = sprintf_s(buf, "KillBackgroundApps: killed=%d failed=%d debugPriv=%d\r\n",
                            killedCount, failedCount, (int)debugPriv);
        DWORD w; WriteFile(hf, buf, len, &w, NULL);
        CloseHandle(hf);
    }
}

// WMI Win32_VideoController: точное имя + вендор по PNPDeviceID.
// EnumDisplayDevicesW оставляем fallback'ом: он не отдаёт VendorID, а на
// гибридных ноутбуках порядок адаптеров не гарантирует дискретную карту.
static App::GpuVendor VendorFromGpuName(const std::wstring& name) {
    std::wstring g = name;
    std::transform(g.begin(), g.end(), g.begin(), ::towlower);
    if (g.find(L"nvidia") != std::wstring::npos || g.find(L"geforce") != std::wstring::npos)
        return App::GPU_NVIDIA;
    if (g.find(L"amd") != std::wstring::npos || g.find(L"radeon") != std::wstring::npos)
        return App::GPU_AMD;
    if (g.find(L"intel") != std::wstring::npos)
        return App::GPU_INTEL;
    return App::GPU_UNKNOWN;
}

static void DetectGpuWmi() {
    App::hwInfo.gpuVendor = App::GPU_UNKNOWN;
    App::gpuName.clear();

    IWbemLocator* pLoc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator, (LPVOID*)&pLoc)) || !pLoc) return;

    IWbemServices* pSvc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    BSTR wql = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Name, PNPDeviceID FROM Win32_VideoController");

    if (ns && wql && query &&
        pLoc->ConnectServer(ns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                            nullptr, nullptr, &pSvc) == WBEM_S_NO_ERROR && pSvc) {
        CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

        IEnumWbemClassObject* pEnum = nullptr;
        if (pSvc->ExecQuery(wql, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                            nullptr, &pEnum) == WBEM_S_NO_ERROR && pEnum) {
            int bestPrio = 0;
            for (;;) {
                IWbemClassObject* obj = nullptr; ULONG ret = 0;
                if (pEnum->Next(2000, 1, &obj, &ret) != WBEM_S_NO_ERROR || ret == 0) break;

                std::wstring name, pnp;
                VARIANT vt; VariantInit(&vt);
                if (SUCCEEDED(obj->Get(L"Name", 0, &vt, nullptr, nullptr)) &&
                    vt.vt == VT_BSTR && vt.bstrVal)
                    name = vt.bstrVal;
                VariantClear(&vt);
                VariantInit(&vt);
                if (SUCCEEDED(obj->Get(L"PNPDeviceID", 0, &vt, nullptr, nullptr)) &&
                    vt.vt == VT_BSTR && vt.bstrVal)
                    pnp = vt.bstrVal;
                VariantClear(&vt);
                obj->Release();
                if (name.empty()) continue;

                std::wstring up = pnp;
                std::transform(up.begin(), up.end(), up.begin(), ::towupper);
                App::GpuVendor v = App::GPU_UNKNOWN;
                int prio = 0;
                if (up.find(L"VEN_10DE") != std::wstring::npos)      { v = App::GPU_NVIDIA; prio = 3; }
                else if (up.find(L"VEN_1002") != std::wstring::npos) { v = App::GPU_AMD;    prio = 2; }
                else if (up.find(L"VEN_8086") != std::wstring::npos) { v = App::GPU_INTEL;  prio = 1; }
                else {
                    v = VendorFromGpuName(name);
                    prio = (v == App::GPU_NVIDIA) ? 3 : (v == App::GPU_AMD) ? 2 :
                           (v == App::GPU_INTEL) ? 1 : 0;
                }
                // Дискретная карта важнее встройки — независимо от порядка перечисления.
                if (prio > bestPrio) {
                    bestPrio = prio;
                    App::hwInfo.gpuVendor = v;
                    App::gpuName = name;
                }
            }
            pEnum->Release();
        }
        pSvc->Release();
    }
    if (ns) SysFreeString(ns);
    if (wql) SysFreeString(wql);
    if (query) SysFreeString(query);
    pLoc->Release();
}

void GetHardwareInfo() {
    App::cpuName = Util::ReadRegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    if (App::cpuName.empty()) App::cpuName = L"Неизвестный CPU";

    DWORD bufferSize = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufferSize);
    if (bufferSize > 0) {
        std::vector<BYTE> buf(bufferSize);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &bufferSize)) {
            int physicalCores = 0;
            DWORD offset = 0;
            while (offset < bufferSize) {
                auto info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf.data() + offset);
                if (info->Relationship == RelationProcessorCore) physicalCores++;
                offset += info->Size;
            }
            SYSTEM_INFO si; GetSystemInfo(&si);
            wchar_t cbuf[64]; swprintf(cbuf, 64, L"%d / %d", physicalCores, (int)si.dwNumberOfProcessors);
            App::hwInfo.cpuCores = cbuf;
        }
    }

    // 1) WMI: PNPDeviceID даёт точного производителя, Name — точное имя.
    DetectGpuWmi();

    // 2) Fallback: EnumDisplayDevicesW, если WMI недоступен или не дал имени.
    if (App::gpuName.empty()) {
        DISPLAY_DEVICEW dd{}; dd.cb = sizeof(dd);
        std::wstring firstGpu;
        for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); ++i) {
            std::wstring name = dd.DeviceString;
            if (name.empty()) continue;
            if (firstGpu.empty()) firstGpu = name;
            std::wstring low = name;
            std::transform(low.begin(), low.end(), low.begin(), ::towlower);
            if (low.find(L"nvidia") != std::wstring::npos ||
                low.find(L"geforce") != std::wstring::npos ||
                low.find(L"radeon rx") != std::wstring::npos ||
                low.find(L"radeon pro") != std::wstring::npos) {
                App::gpuName = name;
                break;
            }
        }
        if (App::gpuName.empty() && !firstGpu.empty()) App::gpuName = firstGpu;
    }
    if (App::hwInfo.gpuVendor == App::GPU_UNKNOWN)
        App::hwInfo.gpuVendor = VendorFromGpuName(App::gpuName);
    if (App::gpuName.empty()) App::gpuName = L"Неизвестный GPU";

    // 2a.4: Intel-платформа = Intel GPU или Intel CPU (у ноутбуков часто Intel CPU + dGPU).
    // Используется только для баннера-рекомендации Intel APO.
    App::hwInfo.isIntelPlatform =
        App::hwInfo.gpuVendor == App::GPU_INTEL ||
        App::cpuName.find(L"Intel") != std::wstring::npos;

    MEMORYSTATUSEX mem; mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        double totalGB = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        wchar_t rbuf[64]; swprintf(rbuf, 64, L"%.1f GB", totalGB);
        App::totalRamText = rbuf;
        App::hwInfo.ramTotal = rbuf;
    }

    wchar_t drives[256];
    DWORD len = GetLogicalDriveStringsW(256, drives);
    std::wstring info;
    for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
        wchar_t root[4] = { p[0], L':', L'\\', 0 };
        ULARGE_INTEGER freeBytes, totalBytes, totalFree;
        if (GetDiskFreeSpaceExW(root, &freeBytes, &totalBytes, &totalFree)) {
            double totalGB = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
            double freeGB = totalFree.QuadPart / (1024.0 * 1024.0 * 1024.0);
            wchar_t buf[128];
            swprintf(buf, 128, L"%ls: %.1f GB из %.1f GB\n", root, freeGB, totalGB);
            info += buf;
        }
    }
    App::hwInfo.diskInfo = info;

    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        App::isLaptop = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255) ||
                        (sps.BatteryFlag == 255 && sps.BatteryLifeTime != (DWORD)-1);
    }

    // Guard 2.0: cpuCores имеет формат "8 / 16" — _wtoi берёт физические ядра
    App::hwInfo.cpuPhysicalCores = _wtoi(App::hwInfo.cpuCores.c_str());
    RefreshHardwareFlags();
    DetectDiskType();
    App::g_hardwareTier = DetectHardwareTier();
}

void RefreshRealStats() {
    DWORD pids[2048]; DWORD needed = 0;
    if (EnumProcesses(pids, sizeof(pids), &needed)) {
        App::realProcessCount = needed / sizeof(DWORD);
    }

    int startup = 0;
    const wchar_t* runKeys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
    };
    auto CountRunKey = [](HKEY root, const wchar_t* key) -> int {
        int n = 0;
        HKEY hk;
        if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
            wchar_t name[512]; DWORD idx = 0;
            for (;;) {
                DWORD sz = 512;
                if (RegEnumValueW(hk, idx, name, &sz, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                n++; idx++;
            }
            RegCloseKey(hk);
        }
        return n;
    };
    for (const wchar_t* key : runKeys) {
        startup += CountRunKey(HKEY_CURRENT_USER, key);
        startup += CountRunKey(HKEY_LOCAL_MACHINE, key);
    }
    App::realStartupCount = startup;
}

void UpdateStats() {
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        auto cv = [](FILETIME x) { return ((ULONGLONG)x.dwHighDateTime << 32) | x.dwLowDateTime; };
        ULONGLONG i = cv(idle), k = cv(kernel), u = cv(user);
        if (App::prevKernel) {
            ULONGLONG total = (k - App::prevKernel) + (u - App::prevUser);
            ULONGLONG id = i - App::prevIdle;
            if (total) App::cpuLoad = (int)std::clamp(100.0 * (double)(total - id) / (double)total, 0.0, 100.0);
        }
        App::prevIdle = i; App::prevKernel = k; App::prevUser = u;
    }
    App::cpuHistory[App::cpuHistoryIdx] = (float)App::cpuLoad;
    App::cpuHistoryIdx = (App::cpuHistoryIdx + 1) % 60;

    wchar_t buf[32]; swprintf(buf, 32, L"%d%%", App::cpuLoad); App::cpuText = buf;

    MEMORYSTATUSEX mem; mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        App::ramLoad = (int)mem.dwMemoryLoad;
        double totalGB = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        double usedGB = (mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        wchar_t rbuf[64]; swprintf(rbuf, 64, L"%.1f / %.1f GB", usedGB, totalGB); App::ramText = rbuf;
    }

    // Мониторинг GPU/температур: DXGI и PDH каждый тик, медленные — из воркера
    UpdateGPUMemory();
    App::gpuLoad = GetGPULoadPDH();
    App::cpuTemp = Monitor::cpuTemp.load(std::memory_order_relaxed);
    App::gpuTemp = Monitor::gpuTemp.load(std::memory_order_relaxed);

    RefreshRealStats();
}

void ScanGames() {
    App::installedGames.clear();
    App::steamGames.clear();

    std::wstring steamPath = Util::ReadRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steamPath.empty()) steamPath = L"C:\\Program Files (x86)\\Steam";

    std::vector<std::wstring> libraryPaths = { steamPath };
    std::wstring vdfPath = Util::Join(Util::Join(steamPath, L"steamapps"), L"libraryfolders.vdf");

    if (Util::FileExists(vdfPath)) {
        std::string content = Util::ReadFileToString(vdfPath);
        size_t pos = 0;
        while ((pos = content.find("\"path\"", pos)) != std::string::npos) {
            size_t start = content.find('"', pos + 6);
            if (start == std::string::npos) break;
            size_t end = content.find('"', start + 1);
            if (end == std::string::npos) break;
            std::string p = content.substr(start + 1, end - start - 1);
            size_t slashPos = 0;
            while ((slashPos = p.find("\\\\", slashPos)) != std::string::npos) { p.replace(slashPos, 2, "\\"); slashPos++; }
            std::wstring wp = Util::FromUtf8(p);
            if (!wp.empty() && std::find(libraryPaths.begin(), libraryPaths.end(), wp) == libraryPaths.end()) {
                libraryPaths.push_back(wp);
            }
            pos = end;
        }
    }

    for (const auto& libPath : libraryPaths) {
        WIN32_FIND_DATAW fd;
        std::wstring pattern = Util::Join(Util::Join(libPath, L"steamapps"), L"appmanifest_*.acf");
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring mf = Util::Join(Util::Join(libPath, L"steamapps"), fd.cFileName);
            std::wstring fname = fd.cFileName;
            int appid = 0;
            size_t p1 = fname.find(L'_'), p2 = fname.find(L'.');
            if (p1 != std::wstring::npos && p2 != std::wstring::npos && p2 > p1) {
                appid = _wtoi(fname.substr(p1 + 1, p2 - p1 - 1).c_str());
            }
            std::string content = Util::ReadFileToString(mf);
            std::wstring name = Util::GetAcfValue(content, "name");
            std::wstring installdir = Util::GetAcfValue(content, "installdir");
            if (!name.empty() && name != L"Steamworks Common Redistributables") {
                App::steamGames.push_back({name, true, libPath, appid, installdir});
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // Fallback: поиск Dota 2 и CS2
    std::vector<std::pair<std::wstring, std::wstring>> knownGames = {
        {L"D:\\SteamLibrary\\steamapps\\common\\dota 2 beta", L"Dota 2"},
        {L"E:\\SteamLibrary\\steamapps\\common\\dota 2 beta", L"Dota 2"},
        {L"C:\\SteamLibrary\\steamapps\\common\\dota 2 beta", L"Dota 2"},
        {L"D:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive", L"Counter-Strike 2"},
        {L"E:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive", L"Counter-Strike 2"},
        {L"C:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive", L"Counter-Strike 2"},
    };
    for (const auto& kg : knownGames) {
        if (GetFileAttributesW(kg.first.c_str()) != INVALID_FILE_ATTRIBUTES) {
            bool alreadyExists = false;
            for (const auto& g : App::steamGames) {
                if (g.name == kg.second) { alreadyExists = true; break; }
            }
            if (!alreadyExists) {
                // kg.first = <library>\steamapps\common\<game>. Для единообразия с ACF
                // храним корень библиотеки, а не ...\steamapps (иначе получается
                // steamapps\steamapps\common при склейке пути).
                size_t steamappsPos = kg.first.find(L"\\steamapps\\");
                std::wstring libPath = (steamappsPos == std::wstring::npos)
                    ? kg.first : kg.first.substr(0, steamappsPos);
                App::steamGames.push_back({kg.second, true, libPath, 0, L""});
            }
        }
    }

    std::vector<std::wstring> arizonaPaths = {
        L"C:\\Arizona RP", L"D:\\Arizona RP", L"E:\\Arizona RP",
        L"C:\\Games\\Arizona RP", L"D:\\Games\\Arizona RP", L"E:\\Games\\Arizona RP",
        L"C:\\Program Files\\Arizona RP", L"C:\\Program Files (x86)\\Arizona RP",
        L"D:\\Games\\Arizona Games Launcher", L"E:\\Games\\Arizona Games Launcher",
        L"C:\\Arizona", L"D:\\Arizona", L"E:\\Arizona",
        L"C:\\Arizona Games", L"D:\\Arizona Games", L"E:\\Arizona Games"
    };
    for (const auto& p : arizonaPaths) {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            App::steamGames.push_back({L"Arizona RP (Launcher)", false, p, 0, L""});
            break;
        }
    }

    auto isPriorityGame = [](const App::Game& g) {
        return g.name.find(L"Counter-Strike") != std::wstring::npos ||
               g.name.find(L"Dota 2") != std::wstring::npos ||
               g.name.find(L"Arizona") != std::wstring::npos;
    };
    std::sort(App::steamGames.begin(), App::steamGames.end(),
        [&](const App::Game& a, const App::Game& b) {
            bool pa = isPriorityGame(a), pb = isPriorityGame(b);
            if (pa != pb) return pa > pb;
            if (a.name != b.name) return a.name < b.name;
            return a.appid < b.appid;
        });
    App::steamGames.erase(
        std::unique(App::steamGames.begin(), App::steamGames.end(),
            [](const App::Game& a, const App::Game& b) {
                return a.name == b.name && a.appid == b.appid;
            }),
        App::steamGames.end());

    for (auto& g : App::steamGames) {
        if (g.name.empty() || g.name.find(L"appid_") == 0) {
            if (g.appid == 570) g.name = L"Dota 2";
            else if (g.appid == 730) g.name = L"Counter-Strike 2";
            else if (g.appid == 2229870) g.name = L"Command & Conquer: Generals";
            else if (!g.installdir.empty()) g.name = g.installdir;
        }
    }

    App::steamGames.erase(
        std::remove_if(App::steamGames.begin(), App::steamGames.end(),
            [](const App::Game& g) {
                if (g.name.empty()) return true;
                if (g.name.find(L"appid_") == 0) return true;
                if (g.name.find(L"Steamworks") != std::wstring::npos) return true;
                if (g.name.find(L"Steam Linux Runtime") != std::wstring::npos) return true;
                return false;
            }),
        App::steamGames.end());

    App::installedGames = App::steamGames;
}

std::atomic<double> FpsMonitor::currentFps{0.0};
std::atomic<double> FpsMonitor::avgFps{0.0};
std::atomic<int>    FpsMonitor::samples{0};
std::atomic<bool>   FpsMonitor::running{false};
std::thread         FpsMonitor::worker;

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<BYTE> buf(size);
    auto* pImageCodecInfo = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return (int)j;
        }
    }
    return -1;
}

void DrawRoundRect(Graphics& g, RectF rect, float radius, Color fill, Color border) {
    GraphicsPath path; float d = radius * 2.0f;
    path.AddArc((REAL)rect.X, (REAL)rect.Y, (REAL)d, (REAL)d, 180.0f, 90.0f);
    path.AddArc((REAL)(rect.X + rect.Width - d), (REAL)rect.Y, (REAL)d, (REAL)d, 270.0f, 90.0f);
    path.AddArc((REAL)(rect.X + rect.Width - d), (REAL)(rect.Y + rect.Height - d), (REAL)d, (REAL)d, 0.0f, 90.0f);
    path.AddArc((REAL)rect.X, (REAL)(rect.Y + rect.Height - d), (REAL)d, (REAL)d, 90.0f, 90.0f);
    path.CloseFigure(); SolidBrush brush(fill); g.FillPath(&brush, &path);
    if (border.GetA() > 0) { Pen pen(border, 1.0f); g.DrawPath(&pen, &path); }
}

void DrawTextGdi(Graphics& g, const std::wstring& text, Font* font, RectF rect, Color color, StringAlignment align, StringAlignment lineAlign) {
    StringFormat format; format.SetAlignment(align); format.SetLineAlignment(lineAlign);
    SolidBrush brush(color); g.DrawString(text.c_str(), -1, font, rect, &format, &brush);
}

// Однострочный текст с многоточием: никогда не вылезает за правую границу карточки.
void DrawTextEllipsis(Graphics& g, const std::wstring& text, Font* font, RectF rect, Color color,
                      StringAlignment align,
                      StringAlignment lineAlign) {
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(lineAlign);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, font, rect, &format, &brush);
}

namespace Benchmark {


    static constexpr wchar_t SESSION_NAME[] = L"LEFTIK_Bench";
    // Microsoft-Windows-DXGI: {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
    static const GUID GUID_DXGI = { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };
    // Microsoft-Windows-Direct3D9: {783ACA0A-790E-4D7F-8451-AA850511C6B9}
    static const GUID GUID_D3D9 = { 0x783ACA0A, 0x790E, 0x4D7F, { 0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9 } };
    // Microsoft-Windows-DxgKrnl: {802EC45A-1E99-4B83-9920-87C98277BA9D}
    static const GUID GUID_DXGKRNL = { 0x802EC45A, 0x1E99, 0x4B83, { 0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D } };

    static std::atomic<bool> s_running{false};
    static TRACEHANDLE s_sessionHandle = 0;
    static TRACEHANDLE s_consumerHandle = INVALID_PROCESSTRACE_HANDLE;
    static std::thread s_consumerThread;
    static std::mutex s_dataMtx;
    static std::vector<FrameSample> s_samples;
    static Stats s_lastStats;
    static uint64_t s_qpcFreq = 10000000ULL;
    static std::atomic<uint64_t> s_lastPresentQpc{0};
    static std::atomic<DWORD> s_targetPid{0};
    static std::atomic<ULONGLONG> s_lastPidLookupMs{0};
    static std::wstring s_detectedExe;
    static std::atomic<ULONGLONG> s_startTickMs{0};
    static constexpr uint32_t MAX_DURATION_SEC = 300;

    // Stop() сериализуется отдельным мьютексом: UI-поток и фоновый авто-стоп
    // не должны одновременно закрывать сессию ETW.
    static std::mutex s_stopMtx;
    static std::mutex s_stopThreadMtx;
    static std::thread s_stopThread;
    static std::atomic<bool> s_stopPending{false};
    static std::atomic<bool> s_autoStopFlag{false};
    static std::atomic<bool> s_shutdown{false};

    // Приём сэмплов отделён от s_running: Stop() сбрасывает s_running сразу
    // (UI видит «остановлено»), но callback продолжает принимать события, пока
    // ProcessTrace не отдаст остаток буфера ETW после ControlTrace(STOP).
    // s_shutdown для этого использовать нельзя: Start() проверяет его и
    // после первого Stop() запуск замера стал бы невозможен.
    static std::atomic<bool> s_acceptSamples{false};
    static std::atomic<uint64_t> s_acceptFromQpc{0};

    bool IsRunning() {
        return s_running.load(std::memory_order_acquire);
    }

    static std::wstring BuildActiveTweaksNotes() {
        std::wstring notes;
        if (App::turboBoostActive) notes += L"TURBO;";
        if (App::magicBoostActive) notes += L"MAX_FPS;";
        if (App::isUltraActive)    notes += L"ULTRA;";
        if (App::isMaxActive)      notes += L"PRO;";
        int enabledCnt = 0;
        for (const auto& tw : App::tweaksList) {
            if (tw.isEnabled) enabledCnt++;
        }
        notes += L"tweaks=" + std::to_wstring(enabledCnt);
        return notes;
    }

    // MODULE B — Расчёт метрик (1% low, 0.1% low, frametime variance)
    static Stats ComputeStatsSnapshot(const std::vector<FrameSample>& rawSamples, uint32_t elapsedSec, const std::wstring& exeName) {
        Stats st{};
        st.durationSec = elapsedSec;
        st.gameExe = exeName.empty() ? L"(ожидание игры)" : exeName;
        st.notes = BuildActiveTweaksNotes();

        std::vector<uint32_t> ft;
        ft.reserve(rawSamples.size());
        for (const auto& s : rawSamples) {
            // Игнорируем сэмплы с frametimeUs < 500 (>2000 FPS — артефакт двойного Present)
            // и паузы свыше 2 секунд (свёрнутое окно / загрузка уровня).
            if (s.frametimeUs >= 500 && s.frametimeUs <= 2000000) {
                ft.push_back(s.frametimeUs);
            }
        }
        st.sampleCount = static_cast<uint32_t>(ft.size());
        if (ft.empty()) return st;

        double sumUs = 0.0;
        uint32_t minUs = ft[0], maxUs = ft[0];
        for (uint32_t v : ft) {
            sumUs += static_cast<double>(v);
            if (v < minUs) minUs = v;
            if (v > maxUs) maxUs = v;
        }
        double meanUs = sumUs / static_cast<double>(ft.size());
        if (meanUs > 0.0) st.meanFps = 1000000.0 / meanUs;
        if (maxUs > 0)    st.minFps  = 1000000.0 / static_cast<double>(maxUs);
        if (minUs >= 500) st.maxFps  = 1000000.0 / static_cast<double>(minUs);

        double varSum = 0.0;
        for (uint32_t v : ft) {
            double diff = static_cast<double>(v) - meanUs;
            varSum += diff * diff;
        }
        st.frametimeVarianceUs2 = varSum / static_cast<double>(ft.size());

        // 99th percentile frametime → 1% low FPS
        size_t idx99 = static_cast<size_t>(0.99 * static_cast<double>(ft.size()));
        if (idx99 >= ft.size()) idx99 = ft.size() - 1;
        std::nth_element(ft.begin(), ft.begin() + idx99, ft.end());
        uint32_t p99Us = ft[idx99];
        st.frametimeP99Us = static_cast<double>(p99Us);
        if (p99Us > 0) st.low1PctFps = 1000000.0 / static_cast<double>(p99Us);

        // 99.9th percentile frametime → 0.1% low FPS
        size_t idx999 = static_cast<size_t>(0.999 * static_cast<double>(ft.size()));
        if (idx999 >= ft.size()) idx999 = ft.size() - 1;
        std::nth_element(ft.begin(), ft.begin() + idx999, ft.end());
        uint32_t p999Us = ft[idx999];
        if (p999Us > 0) st.low01PctFps = 1000000.0 / static_cast<double>(p999Us);

        return st;
    }

    static void WINAPI EtwEventRecordCallback(PEVENT_RECORD pEvent) {
        if (!pEvent || !s_acceptSamples.load(std::memory_order_acquire)) return;

        const GUID& prov = pEvent->EventHeader.ProviderId;
        USHORT evId = pEvent->EventHeader.EventDescriptor.Id;
        UCHAR opcode = pEvent->EventHeader.EventDescriptor.Opcode;

        // Фильтруем только Present-start события:
        // DXGI: EventId 42 (Present_Start) или 179/180 (Present)
        // D3D9: EventId 1 (Present_Start)
        // DxgKrnl: EventId 184 (Present) / Opcode 1 (Start)
        bool isPresent = false;
        if (IsEqualGUID(prov, GUID_DXGI)) {
            if (evId == 42 || evId == 179 || evId == 180 || opcode == 1) isPresent = true;
        } else if (IsEqualGUID(prov, GUID_D3D9)) {
            if (evId == 1 || opcode == 1) isPresent = true;
        } else if (IsEqualGUID(prov, GUID_DXGKRNL)) {
            if (evId == 184) isPresent = true;
        }
        if (!isPresent) return;

        DWORD pid = pEvent->EventHeader.ProcessId;
        if (pid == 0 || pid == GetCurrentProcessId()) return;

        ULONGLONG nowMs = GetTickCount64();
        DWORD targetPid = s_targetPid.load(std::memory_order_relaxed);
        if (pid != targetPid) {
            ULONGLONG lastLookup = s_lastPidLookupMs.load(std::memory_order_relaxed);
            if (nowMs - lastLookup < 1000 && targetPid != 0) return;
            s_lastPidLookupMs.store(nowMs, std::memory_order_relaxed);
            std::wstring exeName;
            if (!Monitor::IsGameProcessPid(pid, &exeName)) return;
            // s_detectedExe читается UI-потоком под s_dataMtx — пишем под тем же мьютексом.
            {
                std::lock_guard<std::mutex> lk(s_dataMtx);
                s_targetPid.store(pid, std::memory_order_relaxed);
                s_detectedExe = exeName;
            }
            s_lastPresentQpc.store(0, std::memory_order_relaxed);
        }

        uint64_t qpc = static_cast<uint64_t>(pEvent->EventHeader.TimeStamp.QuadPart);
        if (qpc == 0) {
            LARGE_INTEGER li{};
            QueryPerformanceCounter(&li);
            qpc = static_cast<uint64_t>(li.QuadPart);
        }

        // ResetSamples() двигает cutoff вперёд: события, случившиеся до сброса,
        // но пришедшие из буфера ETW уже после него, не должны попасть в новый отрезок.
        const uint64_t acceptFrom = s_acceptFromQpc.load(std::memory_order_acquire);
        if (qpc < acceptFrom) return;

        // Защита от переполнения uint32_t: если дельта > 2 секунд (лаг ETW или свернутая игра), сбрасываем.
        uint64_t lastQpc = s_lastPresentQpc.load(std::memory_order_relaxed);
        if (lastQpc != 0 && qpc > lastQpc && s_qpcFreq > 0) {
            uint64_t deltaTicks = qpc - lastQpc;
            uint64_t us = (deltaTicks * 1000000ULL) / s_qpcFreq;
            if (us >= 500 && us <= 2000000) {
                std::lock_guard<std::mutex> lk(s_dataMtx);
                // Повторная проверка под мьютексом: ResetSamples мог обновить cutoff,
                // пока callback ждал освобождения s_dataMtx.
                if (qpc >= s_acceptFromQpc.load(std::memory_order_relaxed) &&
                    s_samples.size() < 360000) {
                    s_samples.push_back({ qpc, static_cast<uint32_t>(us), pid });
                }
            }
            s_lastPresentQpc.store(qpc, std::memory_order_relaxed); // Обновляем только при валидном QPC
        } else {
            s_lastPresentQpc.store(qpc, std::memory_order_relaxed); // Принудительный сброс при первом кадре или аномалии
        }
    }

    static void StopExistingEtwSession() {
        size_t propSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(SESSION_NAME) + 1) * sizeof(wchar_t) + 64;
        std::vector<BYTE> buf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
        props->Wnode.BufferSize = static_cast<ULONG>(propSize);
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        ControlTraceW(0, SESSION_NAME, props, EVENT_TRACE_CONTROL_STOP);
    }

    bool Start(HWND ownerForErrors) {
        if (s_shutdown.load(std::memory_order_acquire)) return false;
        if (s_stopPending.load(std::memory_order_acquire)) {
            if (ownerForErrors) {
                MessageBoxW(ownerForErrors, L"Предыдущий замер ещё останавливается.\nПовторите запуск через секунду.",
                            L"LEFTIK Benchmark", MB_OK | MB_ICONINFORMATION);
            }
            return false;
        }
        if (s_running.load(std::memory_order_acquire)) return true;
        if (s_consumerThread.joinable()) s_consumerThread.join();

        LARGE_INTEGER freq{};
        if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
            s_qpcFreq = static_cast<uint64_t>(freq.QuadPart);
        }

        {
            std::lock_guard<std::mutex> lk(s_dataMtx);
            s_samples.clear();
            s_samples.reserve(36000);
            s_lastStats = Stats{};
            s_lastPresentQpc.store(0, std::memory_order_relaxed);
            s_targetPid.store(0, std::memory_order_relaxed);
            s_lastPidLookupMs.store(0, std::memory_order_relaxed);
            s_detectedExe.clear();
            s_startTickMs.store(GetTickCount64(), std::memory_order_relaxed);
            s_autoStopFlag.store(false, std::memory_order_relaxed);
        }

        // Если предыдущая сессия осталась после краша — гасим её перед стартом
        StopExistingEtwSession();

        size_t propSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(SESSION_NAME) + 1) * sizeof(wchar_t) + 64;
        std::vector<BYTE> propBuf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(propBuf.data());
        props->Wnode.BufferSize = static_cast<ULONG>(propSize);
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1; // 1 = QPC clock resolution
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->BufferSize = 64;
        props->MinimumBuffers = 4;
        props->MaximumBuffers = 32;
        props->FlushTimer = 1;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        wcscpy_s(reinterpret_cast<wchar_t*>(propBuf.data() + props->LoggerNameOffset),
                 wcslen(SESSION_NAME) + 1, SESSION_NAME);

        TRACEHANDLE hSession = 0;
        ULONG status = StartTraceW(&hSession, SESSION_NAME, props);
        if (status == ERROR_ALREADY_EXISTS) {
            StopExistingEtwSession();
            memset(propBuf.data(), 0, propSize);
            props->Wnode.BufferSize = static_cast<ULONG>(propSize);
            props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            props->Wnode.ClientContext = 1;
            props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
            props->BufferSize = 64;
            props->MinimumBuffers = 4;
            props->MaximumBuffers = 32;
            props->FlushTimer = 1;
            props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
            wcscpy_s(reinterpret_cast<wchar_t*>(propBuf.data() + props->LoggerNameOffset),
                     wcslen(SESSION_NAME) + 1, SESSION_NAME);
            status = StartTraceW(&hSession, SESSION_NAME, props);
        }

        if (status != ERROR_SUCCESS) {
            if (ownerForErrors) {
                wchar_t errMsg[256];
                swprintf(errMsg, 256, L"Не удалось запустить ETW-сессию LEFTIK_Bench (код %lu).\nЗапустите программу от имени Администратора.", status);
                MessageBoxW(ownerForErrors, errMsg, L"LEFTIK Benchmark", MB_OK | MB_ICONERROR);
            }
            return false;
        }

        s_sessionHandle = hSession;
        EnableTraceEx2(hSession, &GUID_DXGI, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
        EnableTraceEx2(hSession, &GUID_D3D9, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
        EnableTraceEx2(hSession, &GUID_DXGKRNL, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);

        EVENT_TRACE_LOGFILEW logFile{};
        logFile.LoggerName = const_cast<LPWSTR>(SESSION_NAME);
        logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        logFile.EventRecordCallback = EtwEventRecordCallback;

        TRACEHANDLE hConsumer = OpenTraceW(&logFile);
        if (hConsumer == INVALID_PROCESSTRACE_HANDLE) {
            StopExistingEtwSession();
            s_sessionHandle = 0;
            if (ownerForErrors) {
                MessageBoxW(ownerForErrors, L"OpenTraceW вернул ошибку — реальный замер ETW недоступен.", L"LEFTIK Benchmark", MB_OK | MB_ICONERROR);
            }
            return false;
        }

        s_consumerHandle = hConsumer;
        s_running.store(true, std::memory_order_release);
        // Новый замер: принимаем всё с этого момента. Флаг снимается только
        // в Stop() после join, чтобы не потерять хвост буфера ETW.
        s_acceptFromQpc.store(0, std::memory_order_release);
        s_acceptSamples.store(true, std::memory_order_release);
        s_consumerThread = std::thread([hConsumer]() mutable {
            ProcessTrace(&hConsumer, 1, nullptr, nullptr);
        });
        return true;
    }

    Stats GetLastStats() {
        std::lock_guard<std::mutex> lk(s_dataMtx);
        if (s_running.load(std::memory_order_relaxed)) {
            uint32_t elapsed = static_cast<uint32_t>((GetTickCount64() - s_startTickMs.load(std::memory_order_relaxed)) / 1000ULL);
            Stats st = ComputeStatsSnapshot(s_samples, elapsed, s_detectedExe);
            st.waitingForGame = (s_targetPid.load(std::memory_order_relaxed) == 0);
            return st;
        }
        Stats done = s_lastStats;
        done.autoStopped = s_autoStopFlag.load(std::memory_order_relaxed);
        return done;
    }

    std::vector<FrameSample> GetLastSamples() {
        std::lock_guard<std::mutex> lk(s_dataMtx);
        if (s_samples.size() <= 600) return s_samples;
        return std::vector<FrameSample>(s_samples.end() - 600, s_samples.end());
    }

    // MODULE D — Экспорт графика в PNG (1200x600 GDI+)
    bool ExportGraphPng(const std::wstring& path) {
        Stats st = GetLastStats();
        std::vector<FrameSample> samples = GetLastSamples();

        const int W = 1200, H = 600;
        Bitmap bmp(W, H, PixelFormat32bppARGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        SolidBrush bgBrush(App::UI_BG);
        g.FillRectangle(&bgBrush, 0, 0, W, H);

        // Заголовок
        SYSTEMTIME sysTime{}; GetLocalTime(&sysTime);
        wchar_t headerBuf[256];
        swprintf(headerBuf, 256, L"LEFTIK Benchmark  ·  %ls  ·  %04d-%02d-%02d %02d:%02d",
                 st.gameExe.empty() ? L"No Game" : st.gameExe.c_str(),
                 sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute);
        DrawTextGdi(g, headerBuf, App::fH1, RectF(36, 22, W - 72, 34), App::UI_TEXT);

        wchar_t hwLine[512];
        swprintf(hwLine, 512, L"CPU: %ls   |   GPU: %ls   |   RAM: %ls   |   %ls",
                 App::cpuName.c_str(), App::gpuName.c_str(), App::totalRamText.c_str(), st.notes.c_str());
        DrawTextEllipsis(g, hwLine, App::fSmall, RectF(36, 58, W - 72, 20), App::UI_MUTED);

        // 4 карточки метрик
        int cardY = 92, cardH = 88, gap = 16;
        int cardW = (W - 72 - gap * 3) / 4;
        auto drawMetricTile = [&](int idx, const wchar_t* title, double val, const wchar_t* unit, Color col) {
            int cx = 36 + idx * (cardW + gap);
            DrawRoundRect(g, RectF((REAL)cx, (REAL)cardY, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);
            DrawTextGdi(g, title, App::fSmall, RectF((REAL)(cx + 16), (REAL)(cardY + 12), (REAL)(cardW - 32), 18), App::UI_MUTED);
            wchar_t vbuf[64];
            swprintf(vbuf, 64, L"%.1f %ls", val, unit);
            DrawTextGdi(g, vbuf, App::fMetric, RectF((REAL)(cx + 16), (REAL)(cardY + 34), (REAL)(cardW - 32), 42), col);
        };
        drawMetricTile(0, L"AVG FPS", st.meanFps, L"FPS", App::UI_GREEN);
        drawMetricTile(1, L"1% LOW FPS", st.low1PctFps, L"FPS", App::UI_ACCENT);
        drawMetricTile(2, L"0.1% LOW FPS", st.low01PctFps, L"FPS", App::YELLOW);
        drawMetricTile(3, L"P99 FRAMETIME", st.frametimeP99Us / 1000.0, L"ms", App::UI_TEXT);

        // Область графика frametime (0..50 мс)
        int gx = 68, gy = 200, gw = W - 104, gh = 330;
        DrawRoundRect(g, RectF((REAL)(gx - 32), (REAL)(gy - 12), (REAL)(gw + 48), (REAL)(gh + 36)), 12.0f, App::UI_CARD, App::UI_BORDER);

        Pen gridPen(Color(45, 255, 255, 255), 1.0f);
        const double maxMs = 50.0;
        const double guidesMs[] = { 6.9, 16.6, 33.3, 50.0 };
        const wchar_t* guideLabels[] = { L"6.9 ms (144Hz)", L"16.6 ms (60Hz)", L"33.3 ms (30Hz)", L"50.0 ms" };
        for (int i = 0; i < 4; ++i) {
            float ly = (float)(gy + gh - (guidesMs[i] / maxMs) * gh);
            g.DrawLine(&gridPen, (REAL)gx, ly, (REAL)(gx + gw), ly);
            DrawTextGdi(g, guideLabels[i], App::fSmall, RectF((REAL)(gx + 6), ly - 18.0f, 140.0f, 16.0f), App::UI_MUTED);
        }

        if (samples.size() >= 2) {
            GraphicsPath linePath;
            size_t worstIdx = 0;
            uint32_t worstUs = 0;
            float worstX = 0.0f, worstY = 0.0f;
            float prevX = 0.0f, prevY = 0.0f;

            for (size_t i = 0; i < samples.size(); ++i) {
                double ms = std::min(maxMs, samples[i].frametimeUs / 1000.0);
                float px = (float)gx + (float)i * ((float)gw / (float)(samples.size() - 1));
                float py = (float)(gy + gh - (ms / maxMs) * gh);
                if (i == 0) {
                    linePath.StartFigure();
                } else {
                    linePath.AddLine(prevX, prevY, px, py);
                }
                prevX = px; prevY = py;
                if (samples[i].frametimeUs > worstUs) {
                    worstUs = samples[i].frametimeUs;
                    worstIdx = i;
                    worstX = px; worstY = py;
                }
            }
            Pen graphPen(App::UI_GREEN, 2.0f);
            g.DrawPath(&graphPen, &linePath);

            // Красная точка худшего кадра (spike)
            if (worstUs > 0) {
                SolidBrush redBrush(App::RED);
                g.FillEllipse(&redBrush, worstX - 5.0f, worstY - 5.0f, 10.0f, 10.0f);
                wchar_t spikeBuf[64];
                swprintf(spikeBuf, 64, L"Spike: %.1f ms (#%zu)", worstUs / 1000.0, worstIdx);
                float labelX = std::min((float)(gx + gw - 150), std::max((float)gx, worstX - 60.0f));
                float labelY = std::max((float)(gy + 4), worstY - 24.0f);
                DrawTextGdi(g, spikeBuf, App::fSmall, RectF(labelX, labelY, 150.0f, 18.0f), App::RED);
            }
        } else {
            DrawTextGdi(g, L"Недостаточно данных Present для построения кривой", App::fBody,
                        RectF((REAL)gx, (REAL)gy, (REAL)gw, (REAL)gh), App::UI_MUTED, StringAlignmentCenter, StringAlignmentCenter);
        }

        // Водяной знак внизу справа
        DrawTextGdi(g, L"leftik.app · Free · No ads", App::fSmall,
                    RectF((REAL)(W - 260), (REAL)(H - 32), 224.0f, 20.0f), App::UI_MUTED, StringAlignmentFar, StringAlignmentCenter);

        CLSID pngClsid{};
        if (GetEncoderClsid(L"image/png", &pngClsid) < 0) return false;
        return bmp.Save(path.c_str(), &pngClsid, nullptr) == Ok;
    }
}

namespace Journal {
    // MODULE E — Интеграция результатов бенчмарка в Journal V3
    // Формат: V3|BENCHMARK|<b64_group>|<meanFps>|<low1PctFps>|<low01PctFps>|<sampleCount>|<b64_notes>
    void AppendBenchmarkLine(const Benchmark::Stats& st) {
        std::wstring grp = App::activeGroupId.empty() ? NewGroupId() : App::activeGroupId;
        char numBuf[128]{};
        snprintf(numBuf, sizeof(numBuf), "%.1f|%.1f|%.1f|%u",
                 st.meanFps, st.low1PctFps, st.low01PctFps, (unsigned int)st.sampleCount);
        std::wstring noteFull = st.gameExe + L" | " + st.notes;
        std::stringstream ss;
        ss << "V3|BENCHMARK|" << Util::Base64Encode(Util::ToUtf8(grp)) << '|'
           << numBuf << '|'
           << Util::Base64Encode(Util::ToUtf8(noteFull)) << "\n";
        AppendJournalLine(ss.str());
        Monitor::InvalidateGroupsCache();
    }
}

namespace Benchmark {
    void Stop() {
        // Сериализация: UI-поток, фоновый авто-стоп и Shutdown не должны
        // одновременно закрывать сессию ETW и джойнить consumer-поток.
        std::lock_guard<std::mutex> stopLock(s_stopMtx);

        bool wasRunning = s_running.exchange(false, std::memory_order_acq_rel);
        if (!wasRunning) {
            if (s_consumerThread.joinable()) s_consumerThread.join();
            s_acceptSamples.store(false, std::memory_order_release);
            return;
        }

        if (s_sessionHandle != 0) {
            EnableTraceEx2(s_sessionHandle, &GUID_DXGI, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, nullptr);
            EnableTraceEx2(s_sessionHandle, &GUID_D3D9, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, nullptr);
            EnableTraceEx2(s_sessionHandle, &GUID_DXGKRNL, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, nullptr);
        }
        StopExistingEtwSession();
        s_sessionHandle = 0;

        if (s_consumerHandle != INVALID_PROCESSTRACE_HANDLE) {
            CloseTrace(s_consumerHandle);
            s_consumerHandle = INVALID_PROCESSTRACE_HANDLE;
        }
        if (s_consumerThread.joinable()) {
            s_consumerThread.join();
        }

        // Только после join: ProcessTrace уже отдал последние события из буфера.
        // Снять флаг раньше — значит снова потерять хвост замера.
        s_acceptSamples.store(false, std::memory_order_release);

        Stats finalSt{};
        {
            std::lock_guard<std::mutex> lk(s_dataMtx);
            uint32_t elapsed = static_cast<uint32_t>((GetTickCount64() - s_startTickMs.load(std::memory_order_relaxed)) / 1000ULL);
            s_lastStats = ComputeStatsSnapshot(s_samples, elapsed, s_detectedExe);
            s_lastStats.waitingForGame = false;
            finalSt = s_lastStats;
        }
        if (finalSt.sampleCount > 0) {
            Journal::AppendBenchmarkLine(finalSt);
        }
    }

    uint32_t MaxDurationSec() { return MAX_DURATION_SEC; }

    bool AutoStopDue() {
        if (!s_running.load(std::memory_order_acquire)) return false;
        ULONGLONG start = s_startTickMs.load(std::memory_order_relaxed);
        if (start == 0) return false;
        return ((GetTickCount64() - start) / 1000ULL) >= MAX_DURATION_SEC;
    }

    // Асинхронная остановка: CloseTrace + join не должны блокировать UI-поток.
    // Повторный клик во время остановки игнорируется (s_stopPending).
    void RequestStop(HWND notify, bool autoStop) {
        bool expected = false;
        if (!s_stopPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;

        std::lock_guard<std::mutex> lk(s_stopThreadMtx);
        if (s_shutdown.load(std::memory_order_acquire) ||
            !s_running.load(std::memory_order_acquire)) {
            s_stopPending.store(false, std::memory_order_release);
            return;
        }
        if (s_stopThread.joinable()) s_stopThread.join(); // предыдущий воркер уже завершил Stop()

        s_stopThread = std::thread([notify, autoStop]() {
            Stop();
            s_autoStopFlag.store(autoStop, std::memory_order_release);
            s_stopPending.store(false, std::memory_order_release);
            if (notify) PostMessageW(notify, WM_LEFTIK_BENCH_STOPPED, autoStop ? 1 : 0, 0);
        });
    }

    // Штатное завершение приложения: дождаться фонового стопа, затем добить сессию синхронно.
    void Shutdown() {
        s_shutdown.store(true, std::memory_order_release);
        std::thread pending;
        {
            std::lock_guard<std::mutex> lk(s_stopThreadMtx);
            pending = std::move(s_stopThread);
        }
        if (pending.joinable()) pending.join();
        Stop();
    }

    // Сброс сэмплов на лету: ETW-сессия продолжает работать, начинается новый отрезок замера.
    void ResetSamples() {
        // Cutoff берём до очистки и до отпускания мьютекса: всё, что случилось
        // раньше этого QPC, — старый отрезок, даже если событие придёт из буфера позже.
        LARGE_INTEGER nowQpc{};
        QueryPerformanceCounter(&nowQpc);
        std::lock_guard<std::mutex> lk(s_dataMtx);
        if (nowQpc.QuadPart > 0) {
            s_acceptFromQpc.store(static_cast<uint64_t>(nowQpc.QuadPart), std::memory_order_release);
        }
        s_samples.clear();
        s_samples.reserve(36000);
        s_lastStats = Stats{};
        s_lastPresentQpc.store(0, std::memory_order_relaxed);
        s_targetPid.store(0, std::memory_order_relaxed);
        s_lastPidLookupMs.store(0, std::memory_order_relaxed);
        s_detectedExe.clear();
        s_startTickMs.store(GetTickCount64(), std::memory_order_relaxed);
        s_autoStopFlag.store(false, std::memory_order_relaxed);
    }

    // CSV для Excel: сводные метрики + покадровые fps/frametime.
    bool ExportCsv(const std::wstring& path) {
        Stats st{};
        std::vector<FrameSample> raw;
        {
            std::lock_guard<std::mutex> lk(s_dataMtx);
            if (s_running.load(std::memory_order_relaxed)) {
                uint32_t elapsed = static_cast<uint32_t>((GetTickCount64() - s_startTickMs.load(std::memory_order_relaxed)) / 1000ULL);
                st = ComputeStatsSnapshot(s_samples, elapsed, s_detectedExe);
                st.waitingForGame = (s_targetPid.load(std::memory_order_relaxed) == 0);
            } else {
                st = s_lastStats;
            }
            raw = s_samples;
        }
        if (st.sampleCount == 0 || raw.empty()) return false;

        std::ostringstream out;
        out << "\xEF\xBB\xBF"; // UTF-8 BOM — Excel корректно открывает кириллицу
        out << "LEFTIK Benchmark CSV\n";
        out << "Game;\"" << Util::ToUtf8(st.gameExe) << "\"\n";
        out << "DurationSec;" << st.durationSec << "\n";
        out << std::fixed << std::setprecision(3);
        out << "AvgFps;" << st.meanFps << "\n";
        out << "Low1PctFps;" << st.low1PctFps << "\n";
        out << "Low01PctFps;" << st.low01PctFps << "\n";
        out << "P99FrametimeMs;" << (st.frametimeP99Us / 1000.0) << "\n";
        out << "Frames;" << st.sampleCount << "\n";
        out << "Notes;\"" << Util::ToUtf8(st.notes) << "\"\n\n";
        out << "Frame;TimeSec;FrametimeMs;Fps\n";

        const uint64_t firstQpc = raw.front().timestampQpc;
        const double freq = static_cast<double>(s_qpcFreq ? s_qpcFreq : 10000000ULL);
        uint32_t row = 0;
        for (const auto& s : raw) {
            if (s.frametimeUs < 500 || s.frametimeUs > 2000000) continue; // те же фильтры, что и в статистике
            double tSec = (s.timestampQpc >= firstQpc)
                        ? static_cast<double>(s.timestampQpc - firstQpc) / freq : 0.0;
            double ms = s.frametimeUs / 1000.0;
            double fps = 1000000.0 / static_cast<double>(s.frametimeUs);
            out << (++row) << ';' << std::fixed << std::setprecision(6) << tSec
                << ';' << std::setprecision(3) << ms << ';' << fps << '\n';
        }
        return Util::WriteFileAtomic(path, out.str());
    }
}
