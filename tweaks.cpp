#include "tweaks.hpp"


int TweakIndexById(int id) {
    for (size_t i = 0; i < App::tweaksList.size(); ++i)
        if (App::tweaksList[i].id == id) return (int)i;
    return -1;
}

namespace NetworkTweaker {
    void OptimizeAdapters() {
        auto adapters = GetPhysicalAdapters();
        for (const auto& key : adapters) {
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"PnPCapabilities", 24, L"Сетевые адаптеры");
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"*RSS", 1, L"Сетевые адаптеры");
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"*WakeOnMagicPacket", 0, L"Сетевые адаптеры");
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"*WakeOnPattern", 0, L"Сетевые адаптеры");
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"EnableWakeOnLan", 0, L"Сетевые адаптеры");
        }
    }
}

// v58.0 Feature A: перед выходом проверяем, не оставили ли мы кастомный DNS.
// Возвращает: true — можно продолжать выход, false — пользователь нажал Отмена.
bool PromptRestoreDnsBeforeExit() {
    if (App::dnsPromptedThisSession) return true;

    HKEY hDns{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\PrevDNS", 0, KEY_READ, &hDns) != ERROR_SUCCESS)
        return true; // PrevDNS нет — флаг НЕ взводим, вдруг пользователь отменит выход и включит DNS позже
    RegCloseKey(hDns);

    App::dnsPromptedThisSession = true; // Взводим только когда реально показываем диалог

    int r = MessageBoxW(App::hwnd,
        L"Установлен кастомный DNS (Cloudflare 1.1.1.1, твик 3010).\n\n"
        L"Восстановить прежний DNS перед выходом?\n\n"
        L"Да — вернуть старый DNS\n"
        L"Нет — оставить Cloudflare\n"
        L"Отмена — не выходить из LEFTIK",
        L"LEFTIK DNS", MB_YESNOCANCEL | MB_ICONQUESTION);

    if (r == IDCANCEL) return false;
    if (r == IDYES) {
        RevertTweak(3010);
        Journal::LogAction(L"DNS восстановлен при выходе из программы");
    }
    return true;
}

void T_AdvancedNetwork() {
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"MaxUserPort", 65534, L"Сетевой буст");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"TcpTimedWaitDelay", 30, L"Сетевой буст");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"Tcp1323Opts", 1, L"Сетевой буст");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"DefaultTTL", 64, L"Сетевой буст");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"TcpMaxDupAcks", 2, L"Сетевой буст");
}

void ShowGameConfig(const std::wstring& gameName) {
    std::wstring text = L"";
    if (gameName.find(L"Counter-Strike") != std::wstring::npos) {
        text = L"Киберспортивный конфиг CS2 (Source 2):\n"
               L"fps_max 0\nfps_max_ui 120\nengine_no_focus_sleep 0\n"
               L"engine_low_latency_sleep_after_client_tick 1\n"
               L"rate 786432\ncl_net_buffer_ticks 0\ncl_interp_ratio 1\n"
               L"r_fullscreen_gamma 2.2\nr_drawtracers_firstperson 1\n"
               L"r_csgo_water_effects 0\nr_csgo_water_refraction 0\n"
               L"cl_showfps 0\ncq_netgraph 0\nsnd_mixahead 0.001\n\n"
               L"video.txt: cpu_level=0, gpu_level=0, shader_detail=0, effect_detail=0, "
               L"particle_lighting_enable=0, r_low_latency=2, mat_vsync=0";
    } else if (gameName.find(L"Dota 2") != std::wstring::npos) {
        text = L"Киберспортивный конфиг Dota 2 (Source 2):\n"
               L"fps_max 0\nfps_max_ui 60\nengine_no_focus_sleep 0\n"
               L"engine_low_latency_sleep_after_client_tick 1\n"
               L"rate 786432\ncl_interp_ratio 1\ncl_predict 1\n"
               L"dota_cheap_water 1\ndota_ambient_creatures 0\n"
               L"dota_ambient_cloth 0\ndota_screen_shake 0\n"
               L"r_deferred_specular 0\nr_deferred_specular_bloom 0\n"
               L"r_deferred_additive_pass 0\nr_deferred_height_fog 0\n"
               L"r_grass_quality 0\ncl_particle_fallback_base 4\n"
               L"cl_particle_fallback_multiplier 0\n@panorama_show_fps 0";
    }
    MessageBoxW(App::hwnd, text.c_str(), (L"Конфиг " + gameName).c_str(), MB_OK);
}

// Рекурсивно ищет exe в папке игры (например, gta_sa.exe может лежать в подпапке bin, а не в корне)
std::wstring FindGameExe(const std::wstring& rootDir, const std::wstring& exeName) {
    if (!std::filesystem::exists(rootDir)) return L"";
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(rootDir, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        bool isFile = it->is_regular_file(ec);
        if (!ec && isFile && _wcsicmp(it->path().filename().c_str(), exeName.c_str()) == 0) {
            return it->path().wstring();
        }
        ec.clear();
        it.increment(ec);
        if (ec) break; // недоступная подпапка и т.п. — тихо прекращаем поиск, не падая
    }
    return L"";
}

void CreateOptimizedShortcut(const std::wstring& targetExe, const std::wstring& gameName) {
    std::wstring desktopPath = Util::ReadRegString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders", L"Desktop");
    if (desktopPath.empty()) return;

    // В реестре путь обычно записан как "%USERPROFILE%\Desktop" — раскрываем переменные окружения,
    // иначе IPersistFile::Save молча не сработает
    wchar_t expanded[MAX_PATH];
    if (ExpandEnvironmentStringsW(desktopPath.c_str(), expanded, MAX_PATH) == 0) return;
    std::wstring lnkPath = Util::Join(expanded, gameName + L" (LEFTIK Boost).lnk");
    
    IShellLinkW* psl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl)) && psl) {
        psl->SetPath(targetExe.c_str());
        psl->SetArguments(L"");
        psl->SetDescription(L"Запуск игры с максимальным приоритетом от LEFTIK Optimizer");
        
        bool saved = false;
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf)) && ppf) {
            saved = SUCCEEDED(ppf->Save(lnkPath.c_str(), TRUE));
            ppf->Release();
        }
        psl->Release();

        if (saved) {
            MessageBoxW(App::hwnd, (L"Ярлык для " + gameName + L" создан на рабочем столе!").c_str(), L"LEFTIK", MB_OK);
        } else {
            MessageBoxW(App::hwnd, (L"Не удалось сохранить ярлык:\n" + lnkPath + L"\n\nЦелевой файл: " + targetExe).c_str(), L"LEFTIK", MB_OK);
        }
    }
}

void SetGameHighPerformanceGPU(const std::wstring& exeName) {
    Journal::SetString(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
                       exeName, L"SwapEffectUpgradeEnable=1;GpuPreference=2;", L"GPU & Flip Model");
}

void ApplyGameExeTweaks(const std::wstring& exeName) {
    if (exeName != L"dota2.exe") {
        std::wstring fsePath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
        std::wstring existing = Util::ReadRegString(HKEY_CURRENT_USER, fsePath.c_str(), exeName.c_str());
        if (existing.find(L"DISABLEDXMAXIMIZEDWINDOWEDMODE") == std::wstring::npos) {
            std::wstring merged = existing.empty() ? L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE"
                                                   : (existing + L" DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE");
            Journal::SetString(HKEY_CURRENT_USER, fsePath, exeName, merged, L"Отключение FSE + HighDPI");
        }
    }
    SetGameHighPerformanceGPU(exeName);

    std::wstring shortExe = exeName;
    size_t slash = shortExe.find_last_of(L"\\/");
    if (slash != std::wstring::npos) shortExe = shortExe.substr(slash + 1);
    if (!shortExe.empty()) {
        std::wstring ifeoPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + shortExe + L"\\PerfOptions";
        Journal::SetDword(HKEY_LOCAL_MACHINE, ifeoPath, L"CpuPriorityClass", 3, L"IFEO High CPU: " + shortExe);
        Journal::SetDword(HKEY_LOCAL_MACHINE, ifeoPath, L"IoPriority", 3, L"IFEO High IO: " + shortExe);
        Journal::SetDword(HKEY_LOCAL_MACHINE, ifeoPath, L"PagePriority", 5, L"IFEO PagePriority: " + shortExe);
    }
}

// Удаляет ранее записанный блок LEFTIK из текста конфига, не трогая остальное содержимое (бинды, Sensitivity и т.д.)
static void StripLeftikBlock(std::string& content) {
    const std::string START = "// LEFTIK START";
    const std::string END = "// LEFTIK END";
    size_t start = content.find(START);
    if (start == std::string::npos) return;
    size_t end = content.find(END, start);
    if (end == std::string::npos) content.erase(start); // незакрытый блок — режем до конца файла
    else content.erase(start, end - start + END.length());
    // подчищаем пустые строки на месте блока, чтобы файл не обрастал переносами при повторных применениях
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) content.pop_back();
}

// Одноразовая очистка: старый заголовок LEFTIK в samp.cfg больше не пишем.
// Ищем именно служебную строку (а не любой пользовательский комментарий).
static void RemoveLegacySampHeaderLine(std::string& content) {
    const std::string marker = "; LEFTIK Optimizer";
    size_t search = 0;
    while ((search = content.find(marker, search)) != std::string::npos) {
        size_t lineStart = content.rfind('\n', search);
        lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
        size_t lineEnd = content.find('\n', search);
        size_t lineLen = (lineEnd == std::string::npos) ? content.size() - lineStart : lineEnd - lineStart;
        std::string line = content.substr(lineStart, lineLen);
        if (line.find("SAMP") != std::string::npos) {
            if (lineEnd == std::string::npos) content.erase(lineStart);
            else content.erase(lineStart, lineEnd - lineStart + 1);
            search = lineStart;
        } else {
            search = (lineEnd == std::string::npos) ? content.size() : lineEnd + 1;
        }
    }
}

static void SetSource2VideoKey(std::string& content, const std::string& key, const std::string& val) {
    std::string quotedKey = "\"" + key + "\"";
    size_t pos = content.find(quotedKey);
    if (pos != std::string::npos) {
        size_t valStart = content.find('"', pos + quotedKey.size());
        if (valStart != std::string::npos) {
            size_t valEnd = content.find('"', valStart + 1);
            if (valEnd != std::string::npos) {
                content.replace(valStart + 1, valEnd - valStart - 1, val);
                return;
            }
        }
    }
    size_t closeBrace = content.rfind('}');
    if (closeBrace != std::string::npos) {
        content.insert(closeBrace, "\t\"" + key + "\"\t\t\"" + val + "\"\n");
    }
}

void ApplyCS2Config() {
    if (IsProcessRunning(L"cs2.exe")) {
        MessageBoxW(App::hwnd,
            L"CS2 запущен. Закройте игру перед изменением конфига —\n"
            L"при выходе игра перезапишет autoexec.cfg из внутреннего кеша.",
            L"LEFTIK", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring gameRoot = L"";
    for (const auto& game : App::steamGames) {
        if (game.name.find(L"Counter-Strike") != std::wstring::npos) {
            std::wstring dir = game.installdir.empty() ? L"Counter-Strike Global Offensive" : game.installdir;
            gameRoot = Util::Join(Util::Join(game.path, L"steamapps\\common"), dir);
            break;
        }
    }
    if (gameRoot.empty()) gameRoot = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Global Offensive";
    std::wstring cfgPath = gameRoot + L"\\game\\csgo\\cfg\\autoexec.cfg";
    std::wstring videoPath = gameRoot + L"\\game\\csgo\\cfg\\video.txt";

    std::filesystem::path p(cfgPath);
    std::string existingContent = "";
    if (std::filesystem::exists(p)) {
        std::ifstream in(p);
        std::stringstream buffer;
        buffer << in.rdbuf();
        existingContent = buffer.str();
    }

    // Стираем только старый блок LEFTIK — личные бинды и настройки пользователя остаются нетронутыми
    StripLeftikBlock(existingContent);

    std::string newContent = existingContent;
    if (!newContent.empty() && newContent.back() != '\n') newContent += '\n';
    newContent +=
        "\n// LEFTIK START\n"
               "// --- Core FPS (Source 2) ---\n"
               "fps_max 0\n"
               "fps_max_ui 120\n"
               "engine_no_focus_sleep 0\n"
               "engine_low_latency_sleep_after_client_tick 1\n"
               "// --- Network (Source 2) ---\n"
               "rate 786432\n"
               "cl_interp_ratio 1\n"
               "cl_predict 1\n"
               "// --- Input latency ---\n"
               "r_low_latency 2\n"
               "// --- Video (безопасно) ---\n"
               "mat_vsync 0\n"
               "r_fullscreen_gamma 2.2\n"
               "r_drawtracers_firstperson 1\n"
               "cl_showfps 0\n"
               "cq_netgraph 0\n"
               "// --- Устаревшие CSGO-команды УДАЛЕНЫ ---\n"
               "// r_csgo_water_effects / r_csgo_water_refraction — удалены в CS2\n"
               "// cl_net_buffer_ticks 0 — заменено архитектурой Source 2\n"
               "// snd_mixahead 0.001 — вызывает треск на слабых CPU\n"
               "// LEFTIK END\n";
    if (!BackupIfExists(cfgPath)) {
        MessageBoxW(App::hwnd, L"Не удалось сделать бэкап конфига — запись отменена.", L"LEFTIK", MB_OK | MB_ICONERROR);
        return;
    }
    if (!Util::WriteFileAtomic(cfgPath, newContent)) {
        MessageBoxW(App::hwnd, L"Не удалось записать конфиг CS2 — файл не тронут.", L"Ошибка", MB_OK);
        return;
    }

    ApplyGameExeTweaks(L"cs2.exe");

    // Точечное обновление video.txt без сброса разрешения и герцовки игрока.
    // 1.7: повреждённый существующий файл не перезаписываем пустым шаблоном.
    bool videoExists = Util::FileExists(videoPath);
    std::string videoContent;
    if (!videoExists) {
        videoContent = "\"VideoConfig\"\n{\n}\n";
    } else {
        videoContent = Util::ReadFileToString(videoPath);
        size_t openBr = videoContent.find('{');
        size_t closeBr = videoContent.rfind('}');
        bool videoValid = !videoContent.empty() &&
            openBr != std::string::npos &&
            closeBr != std::string::npos &&
            closeBr > openBr;
        if (!videoValid) {
            MessageBoxW(App::hwnd,
                L"autoexec.cfg обновлён, но файл video.txt повреждён или недоступен.\n"
                L"Настройки графики (разрешение, герцовка) в video.txt НЕ изменены.\n"
                L"Сбросьте видео-настройки в самой игре и примените конфиг снова.",
                L"LEFTIK CS2", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    SetSource2VideoKey(videoContent, "setting.cpu_level", "0");
    SetSource2VideoKey(videoContent, "setting.gpu_level", "0");
    // mem_level: 2 (High) — загружает все текстуры в VRAM сразу, мгновенное
    // переключение оружия/карт. Но на HDD даёт фризы при подгрузке — ставим 1 (Medium).
    SetSource2VideoKey(videoContent, "setting.mem_level", App::hwInfo.diskIsSSD ? "2" : "1");
    SetSource2VideoKey(videoContent, "setting.gpu_mem_level", App::hwInfo.diskIsSSD ? "2" : "1");
    SetSource2VideoKey(videoContent, "setting.mat_antialias", "0");
    SetSource2VideoKey(videoContent, "setting.mat_vsync", "0");
    SetSource2VideoKey(videoContent, "setting.r_low_latency", "2");
    SetSource2VideoKey(videoContent, "setting.shader_detail", "0");
    SetSource2VideoKey(videoContent, "setting.effect_detail", "0");
    // 0 = lowest (про-стандарт CS2; динамические тени остаются от global_shadow_quality)
    SetSource2VideoKey(videoContent, "setting.csm_quality_level", "0");
    SetSource2VideoKey(videoContent, "setting.particle_lighting_enable", "0");
    SetSource2VideoKey(videoContent, "setting.r_csgo_cmaa_enable", "0");
    SetSource2VideoKey(videoContent, "setting.msaa_samples", "0");
    SetSource2VideoKey(videoContent, "setting.r_texture_filtering_mode", "0");
    SetSource2VideoKey(videoContent, "setting.fullscreen_mode", "2"); // exclusive fullscreen
    SetSource2VideoKey(videoContent, "setting.dx11_can_use_dx12", "0"); // DX11 быстрее

    if (!BackupIfExists(videoPath)) {
        MessageBoxW(App::hwnd,
            L"autoexec.cfg обновлён, но бэкап video.txt сделать не удалось —\n"
            L"video.txt НЕ изменён. Закройте игру/проверьте права и повторите.",
            L"LEFTIK CS2", MB_OK | MB_ICONERROR);
        return;
    }
    if (!Util::WriteFileAtomic(videoPath, videoContent)) {
        MessageBoxW(App::hwnd,
            L"autoexec.cfg обновлён, но video.txt записать не удалось.\n"
            L"Проверьте, что папка cfg существует и доступна.",
            L"LEFTIK CS2", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring msg = L"Киберспортивный конфиг CS2 безопасно обновлен (бинды и разрешение сохранены)!\n\n"
        L"autoexec.cfg:\n" + cfgPath + L"\n\nvideo.txt:\n" + videoPath;
    MessageBoxW(App::hwnd, msg.c_str(), L"LEFTIK CS2", MB_OK);
}

void ApplyDota2Config() {
    if (IsProcessRunning(L"dota2.exe")) {
        MessageBoxW(App::hwnd,
            L"Dota 2 запущена. Закройте игру перед изменением конфига —\n"
            L"при выходе игра перезапишет autoexec.cfg из внутреннего кеша.",
            L"LEFTIK", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring gameRoot = L"";
    for (const auto& game : App::steamGames) {
        if (game.name.find(L"Dota 2") != std::wstring::npos) {
            std::wstring dir = game.installdir.empty() ? L"dota 2 beta" : game.installdir;
            gameRoot = Util::Join(Util::Join(game.path, L"steamapps\\common"), dir);
            break;
        }
    }
    if (gameRoot.empty()) gameRoot = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\dota 2 beta";
    std::wstring cfgPath = gameRoot + L"\\game\\dota\\cfg\\autoexec.cfg";
    std::wstring videoPath = gameRoot + L"\\game\\dota\\cfg\\video.txt";

    std::filesystem::path p(cfgPath);
    std::string existingContent = "";
    if (std::filesystem::exists(p)) {
        std::ifstream in(p);
        std::stringstream buffer;
        buffer << in.rdbuf();
        existingContent = buffer.str();
    }

    // Стираем только старый блок LEFTIK — личные бинды остаются нетронутыми
    StripLeftikBlock(existingContent);

    std::string newContent = existingContent;
    if (!newContent.empty() && newContent.back() != '\n') newContent += '\n';
    newContent +=
        "\n// LEFTIK START\n"
               "// --- Core FPS (Source 2) ---\n"
               "fps_max 0\n"
               "fps_max_ui 60\n"
               "engine_no_focus_sleep 0\n"
               "engine_low_latency_sleep_after_client_tick 1\n"
               "// --- Network ---\n"
               "rate 786432\n"
               "cl_interp_ratio 1\n"
               "cl_predict 1\n"
               "// --- Input latency ---\n"
               "r_low_latency 2\n"
               "mat_vsync 0\n"
               "// --- UI ---\n"
               "cl_showfps 0\n"
               "@panorama_show_fps 0\n"
               "// LEFTIK END\n";
    if (!BackupIfExists(cfgPath)) {
        MessageBoxW(App::hwnd, L"Не удалось сделать бэкап конфига — запись отменена.", L"LEFTIK", MB_OK | MB_ICONERROR);
        return;
    }
    if (!Util::WriteFileAtomic(cfgPath, newContent)) {
        MessageBoxW(App::hwnd, L"Не удалось записать конфиг Dota 2 — файл не тронут.", L"Ошибка", MB_OK);
        return;
    }

    ApplyGameExeTweaks(L"dota2.exe");

    // Точечное обновление video.txt без сброса разрешения и герцовки игрока.
    // 1.7: повреждённый существующий файл не перезаписываем пустым шаблоном.
    bool videoExists = Util::FileExists(videoPath);
    std::string videoContent;
    if (!videoExists) {
        videoContent = "\"VideoConfig\"\n{\n}\n";
    } else {
        videoContent = Util::ReadFileToString(videoPath);
        size_t openBr = videoContent.find('{');
        size_t closeBr = videoContent.rfind('}');
        bool videoValid = !videoContent.empty() &&
            openBr != std::string::npos &&
            closeBr != std::string::npos &&
            closeBr > openBr;
        if (!videoValid) {
            MessageBoxW(App::hwnd,
                L"autoexec.cfg обновлён, но файл video.txt повреждён или недоступен.\n"
                L"Настройки графики (разрешение, герцовка) в video.txt НЕ изменены.\n"
                L"Сбросьте видео-настройки в самой игре и примените конфиг снова.",
                L"LEFTIK Dota 2", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    SetSource2VideoKey(videoContent, "setting.mat_vsync", "0");
    SetSource2VideoKey(videoContent, "setting.r_low_latency", "2");
    SetSource2VideoKey(videoContent, "setting.cpu_level", "0");
    SetSource2VideoKey(videoContent, "setting.gpu_level", "0");
    // 1 = Medium: 0 (Lowest) даёт <3% FPS, но делает текстуры героев и иконок нечитаемыми
    SetSource2VideoKey(videoContent, "setting.r_texture_quality", "1");
    SetSource2VideoKey(videoContent, "setting.r_dashboard_render_quality", "1");
    SetSource2VideoKey(videoContent, "setting.r_grass_quality", "0");
    SetSource2VideoKey(videoContent, "setting.r_ssao", "0");
    SetSource2VideoKey(videoContent, "setting.dota_cheap_water", "1");
    SetSource2VideoKey(videoContent, "setting.r_deferred_simple_light", "0");
    SetSource2VideoKey(videoContent, "setting.r_deferred_height_fog", "0");
    SetSource2VideoKey(videoContent, "setting.r_deferred_additive_pass", "0");
    SetSource2VideoKey(videoContent, "setting.r_deferred_specular", "0");
    SetSource2VideoKey(videoContent, "setting.r_deferred_specular_bloom", "0");
    SetSource2VideoKey(videoContent, "setting.cl_particle_fallback_base", "4");
    SetSource2VideoKey(videoContent, "setting.cl_particle_fallback_multiplier", "0");
    SetSource2VideoKey(videoContent, "setting.dota_ambient_creatures", "0");
    SetSource2VideoKey(videoContent, "setting.dota_ambient_cloth", "0");
    SetSource2VideoKey(videoContent, "setting.fullscreen_mode", "2"); // exclusive fullscreen

    if (!BackupIfExists(videoPath)) {
        MessageBoxW(App::hwnd,
            L"autoexec.cfg обновлён, но бэкап video.txt сделать не удалось —\n"
            L"video.txt НЕ изменён. Закройте игру/проверьте права и повторите.",
            L"LEFTIK Dota 2", MB_OK | MB_ICONERROR);
        return;
    }
    if (!Util::WriteFileAtomic(videoPath, videoContent)) {
        MessageBoxW(App::hwnd,
            L"autoexec.cfg обновлён, но video.txt записать не удалось.\n"
            L"Проверьте, что папка cfg существует и доступна.",
            L"LEFTIK Dota 2", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring msg = L"Киберспортивный конфиг Dota 2 безопасно обновлен (бинды и разрешение сохранены)!\n\n"
        L"autoexec.cfg:\n" + cfgPath + L"\n\nvideo.txt:\n" + videoPath;
    MessageBoxW(App::hwnd, msg.c_str(), L"LEFTIK Dota 2", MB_OK);
}

// Склеивает существующие launch options с новыми флагами, не дублируя их.
// Так мы НЕ затираем флаги, которые пользователь уже вписал себе сам (например -w 1280 -tickrate 128).
static std::wstring MergeLaunchOptions(const std::wstring& existing, const std::vector<std::wstring>& addFlags) {
    std::wstring result = existing;
    for (const auto& f : addFlags) {
        if (result.find(f) == std::wstring::npos) {
            if (!result.empty()) result += L" ";
            result += f;
        }
    }
    return result;
}

void ApplyCS2LaunchOptions() {
    std::wstring key = L"Software\\Valve\\Steam\\Apps\\730";
    std::wstring existing = Util::ReadRegString(HKEY_CURRENT_USER, key.c_str(), L"LaunchOptions");
    // -high: поднимает приоритет — но рискует с анти-читом. Заменяем на IFEO.
    // -console: удобно, но не даёт FPS.
    // Итог: только -novid (убирает 5 сек заставки) и -allow_third_party_software
    // (нужен для MSI Afterburner, но безопасен для VAC).
    std::vector<std::wstring> addFlags = { L"-novid", L"-allow_third_party_software", L"-fullscreen", L"-nojoy" };
    // Не добавляем -threads и +fps_max 400, чтобы не конфликтовать с autoexec.cfg и планировщиком Source 2
    std::wstring merged = MergeLaunchOptions(existing, addFlags);
    Journal::SetString(HKEY_CURRENT_USER, key, L"LaunchOptions", merged, L"CS2 Launch Options");
    Journal::LogAction(L"Launch Options CS2 обновлены");
    MessageBoxW(App::hwnd, (L"Параметры запуска CS2 обновлены (твои флаги сохранены):\n" + merged).c_str(), L"LEFTIK CS2", MB_OK);
}

void ApplyDota2LaunchOptions() {
    std::wstring key = L"Software\\Valve\\Steam\\Apps\\570";
    std::wstring existing = Util::ReadRegString(HKEY_CURRENT_USER, key.c_str(), L"LaunchOptions");
    int physicalCores = _wtoi(App::hwInfo.cpuCores.c_str());
    // Симметрично CS2: без известного числа физических ядер чужой -threads не трогаем.
    if (physicalCores > 0) {
        size_t threadsPos = existing.find(L"-threads ");
        if (threadsPos != std::wstring::npos) {
            size_t endPos = existing.find(L' ', threadsPos + 9);
            if (endPos == std::wstring::npos) endPos = existing.length();
            existing.erase(threadsPos, endPos - threadsPos + 1);
        }
    }
    // Убрано: -high (конфликт с VAC/IFEO), -windowed -noborder (сбрасывает fullscreen и даёт +1 кадр DWM-латентности)
    std::vector<std::wstring> addFlags = {
        L"-novid", L"-nojoy", L"+fps_max 0", L"-dx11", L"+exec autoexec.cfg", L"-prewarm"
    };
    if (physicalCores > 0) addFlags.push_back(L"-threads " + std::to_wstring(physicalCores));
    std::wstring merged = MergeLaunchOptions(existing, addFlags);
    Journal::SetString(HKEY_CURRENT_USER, key, L"LaunchOptions", merged, L"Dota 2 Launch Options");
    Journal::LogAction(L"Launch Options Dota 2 обновлены");
    MessageBoxW(App::hwnd, (L"Параметры запуска Dota 2 обновлены (твои флаги сохранены):\n" + merged).c_str(), L"LEFTIK Dota 2", MB_OK);
}

void ApplyArizonaConfig() {
    if (IsProcessRunning(L"gta_sa.exe") || IsProcessRunning(L"samp.exe") || IsProcessRunning(L"Arizona Games Launcher.exe")) {
        MessageBoxW(App::hwnd,
            L"Arizona RP / GTA San Andreas запущена. Закройте игру и лаунчер\n"
            L"перед изменением конфигов — при выходе клиент может их перезаписать.",
            L"LEFTIK", MB_OK | MB_ICONWARNING);
        return;
    }

    int mode = MessageBoxW(App::hwnd,
        L"Выберите режим буста Arizona RP:\n\n"
        L"ДА — Комфортный (stream 300м — дефолт, все машины/игроки видны)\n"
        L"НЕТ — Агрессивный (stream 200м — машины пропадают дальше 200м, +10-15% FPS)\n"
        L"ОТМЕНА — Не применять",
        L"LEFTIK Arizona", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (mode == IDCANCEL) return;
    std::string streamDistStr = (mode == IDYES) ? "300.0" : "200.0";

    PWSTR userDocs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &userDocs))) return;
    std::wstring sampDir = std::wstring(userDocs) + L"\\GTA San Andreas User Files\\SAMP";
    CoTaskMemFree(userDocs);

    if (GetFileAttributesW(sampDir.c_str()) == INVALID_FILE_ATTRIBUTES) CreateDirectoryW(sampDir.c_str(), nullptr);

    std::filesystem::path cfgPath = std::filesystem::path(sampDir) / L"sa-mp.cfg";
    std::string existingContent = "";
    if (std::filesystem::exists(cfgPath)) {
        std::ifstream in(cfgPath);
        std::stringstream buffer;
        buffer << in.rdbuf();
        existingContent = buffer.str();
    }

    // Стираем только старый блок LEFTIK — прочие ключи sa-mp.cfg сохраняются
    StripLeftikBlock(existingContent);

    std::string newContent = existingContent;
    if (!newContent.empty() && newContent.back() != '\n') newContent += '\n';
    newContent +=
        "\n// LEFTIK START\n"
               "fpslimit=90\n"
               "pagesize=4096\n"
               "visualquality=0\n"
               "multisampling=0\n"
               "stream_distance=" + streamDistStr + "\n"
               "stream_rate=1000\n"
               "no_audio=0\n"
               "disable_crashes=1\n"
               "disable_headmove=1\n"
               "fontsize=1\n"
               "autologin=1\n"
               "timestamp=1\n"
               "chat_scroll=1\n"
               "voice_enable=0\n"
               "board_enable=1\n"
               "// LEFTIK END\n";
    if (!BackupIfExists(cfgPath.wstring())) {
        MessageBoxW(App::hwnd, L"Не удалось сделать бэкап конфига — запись отменена.", L"LEFTIK", MB_OK | MB_ICONERROR);
        return;
    }
    if (!Util::WriteFileAtomic(cfgPath.wstring(), newContent)) {
        MessageBoxW(App::hwnd,
            L"Не удалось записать sa-mp.cfg — файл не тронут.\n"
            L"Проверьте права на папку SAMP и повторите.",
            L"LEFTIK Arizona", MB_OK | MB_ICONERROR);
        return;
    }

    std::filesystem::path sampCfgPath = std::filesystem::path(sampDir) / L"samp.cfg";
    std::string existingSamp = Util::ReadFileToString(sampCfgPath.wstring());
    // Одноразовая очистка: удаляем старый заголовок LEFTIK, который писали предыдущие версии.
    RemoveLegacySampHeaderLine(existingSamp);
    StripLeftikBlock(existingSamp);
    std::string sampContent = existingSamp;
    if (!sampContent.empty() && sampContent.back() != '\n') sampContent += '\n';
    sampContent += "\n// LEFTIK START\n"
                   "fpslimit=90\n"
                   "fontsize=2\n"
                   "pagesize=4096\n"
                   "no_audio=0\n"
                   "disable_headmove=1\n"
                   "multisampling=0\n"
                   "voice_enable=0\n"
                   "chat_scroll=1\n"
                   "timestamp=1\n"
                   "// LEFTIK END\n";
    if (!BackupIfExists(sampCfgPath.wstring())) {
        MessageBoxW(App::hwnd,
            L"sa-mp.cfg уже обновлён, но бэкап samp.cfg сделать не удалось —\n"
            L"samp.cfg НЕ изменён. Проверьте права и повторите.",
            L"LEFTIK Arizona", MB_OK | MB_ICONERROR);
        return;
    }
    if (!Util::WriteFileAtomic(sampCfgPath.wstring(), sampContent)) {
        MessageBoxW(App::hwnd,
            L"sa-mp.cfg обновлён, но samp.cfg записать не удалось.\n"
            L"Проверьте права на папку SAMP и повторите.",
            L"LEFTIK Arizona", MB_OK | MB_ICONWARNING);
        return;
    }

    bool hasStreamerPlugins = false;
    for (auto& ext : { L"streamer.asi", L"salimits.asi", L"moonloader.asi", L"sampfuncs.asi" }) {
        if (Util::FileExists(Util::Join(sampDir, ext))) hasStreamerPlugins = true;
    }

    if (hasStreamerPlugins) {
        std::wstring streamPath = Util::Join(sampDir, L"stream.ini");
        std::wstring salPath = Util::Join(sampDir, L"salimits.ini");
        if (!BackupIfExists(streamPath)) {
            MessageBoxW(App::hwnd,
                L"Конфиги SAMP обновлены, но бэкап stream.ini сделать не удалось —\n"
                L"stream.ini НЕ изменён.",
                L"LEFTIK Arizona", MB_OK | MB_ICONERROR);
            return;
        }
        if (!Util::WriteFileAtomic(streamPath, "stream_distance=" + streamDistStr + "\r\nstream_rate=1000\r\n")) {
            MessageBoxW(App::hwnd,
                L"Не удалось записать stream.ini — файл не тронут.",
                L"LEFTIK Arizona", MB_OK | MB_ICONERROR);
            return;
        }
        if (!BackupIfExists(salPath)) {
            MessageBoxW(App::hwnd,
                L"stream.ini обновлён, но бэкап salimits.ini сделать не удалось —\n"
                L"salimits.ini НЕ изменён.",
                L"LEFTIK Arizona", MB_OK | MB_ICONERROR);
            return;
        }
        if (!Util::WriteFileAtomic(salPath, "[MAIN]\r\nStreamingMemory=512\r\nMaxStreamingObjects=2000\r\n")) {
            MessageBoxW(App::hwnd,
                L"Не удалось записать salimits.ini — файл не тронут.",
                L"LEFTIK Arizona", MB_OK | MB_ICONERROR);
            return;
        }
    }

    ApplyGameExeTweaks(L"gta_sa.exe");
    ApplyGameExeTweaks(L"samp.exe");

    std::wstring msg = L"Буст для Arizona RP применён.\n\n"
        L"Файлы:\n"
        L"  " + sampDir + L"\\sa-mp.cfg\n"
        L"  " + sampDir + L"\\samp.cfg\n\n"
        + (hasStreamerPlugins
            ? std::wstring(L"Обнаружены плагины стриминга — stream.ini/salimits.ini записаны.\n")
            : std::wstring(L"Плагины стриминга НЕ обнаружены — stream.ini/salimits.ini не нужны.\n"))
        + L"\nБэкап существующих конфигов: *.bak в той же папке.\n\n"
        L"Обязательно зайдите в Настройки GTA SA -> Display Setup и убедитесь, что Frame Limiter ВЫКЛЮЧЕН. Иначе fpslimit=90 в конфиге не сработает и FPS будет залочен на 60.";
    MessageBoxW(App::hwnd, msg.c_str(), L"LEFTIK Arizona", MB_OK);
}

void ApplyArizonaLauncherHints() {
    MessageBoxW(App::hwnd,
    L"Для максимального FPS в Arizona RP:\n\n"
    L"1. Откройте лаунчер Arizona Games.\n"
    L"2. Перейдите в раздел «Моды» и установите MoonLoader и Game Fixer.\n"
    L"3. В игре введите /gmenu и настройте параметры в разделе Boost FPS.\n"
    L"4. В файле sa-mp.cfg установите fpslimit=90 (дефолт Arizona; не выше 100 — выше будут десинки).\n"
    L"5. Обязательно отключите Frame Limiter в настройках GTA SA.\n\n"
    L"Обязательно зайдите в Настройки GTA SA -> Display Setup и убедитесь, что Frame Limiter ВЫКЛЮЧЕН. Иначе fpslimit в конфиге не сработает и FPS будет залочен на 60.",
    L"LEFTIK Arizona Tips", MB_OK | MB_ICONINFORMATION);
}

// ===== v58.0 / 1.2: снимок скрытых параметров питания для точного отката 1102 =====
// /qh печатает секцию на каждый параметр: GUID, min/max/step (или индексы), затем
// Current AC и Current DC. Текущие значения — последние два 0x... в секции параметра.
struct PowerAdvValues {
    DWORD cpmAc = 0, cpmDc = 0;   // CPMINCORES
    DWORD ptmAc = 0, ptmDc = 0;   // PROCTHROTTLEMIN
    DWORD pbmAc = 0, pbmDc = 0;   // PERFBOOSTMODE
    DWORD eppAc = 0, eppDc = 0;   // PERFEPP
};
struct PowerAdvSnapshot {
    std::wstring scheme;
    PowerAdvValues v;
};

static const wchar_t* const kPowerAdvCpm = L"0cc5b647-c1df-4637-891a-dec35c318583";
static const wchar_t* const kPowerAdvPtm = L"893dee8e-2bef-41e0-89c6-b55d0929964c";
static const wchar_t* const kPowerAdvPbm = L"be337238-0d82-4146-a960-4f3749d470c7";
static const wchar_t* const kPowerAdvEpp = L"36687f9e-e3a5-4dbf-b1dc-15eb381c6863";

// Позиция первого GUID (8-4-4-4-12 hex) в тексте, начиная с from.
static size_t PowerAdvFindGuid(const std::string& text, size_t from) {
    static const int groups[5] = { 8, 4, 4, 4, 12 };
    auto isHex = [](char c) { return isxdigit((unsigned char)c) != 0; };
    for (size_t i = from; i + 36 <= text.size(); ++i) {
        size_t pos = i; bool ok = true;
        for (int g = 0; g < 5 && ok; ++g) {
            for (int k = 0; k < groups[g]; ++k, ++pos)
                if (!isHex(text[pos])) { ok = false; break; }
            if (ok && g < 4) { if (text[pos] != '-') ok = false; else ++pos; }
        }
        if (ok) return i;
    }
    return std::string::npos;
}

// ASCII-only поиск без учета регистра: локализованные байты powercfg не трогаем.
static size_t PowerAdvFindNoCase(const std::string& hay, const std::string& needle, size_t from = 0) {
    if (needle.empty() || from >= hay.size() || hay.size() - from < needle.size()) return std::string::npos;
    auto lower = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    };
    for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && lower(hay[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return i;
    }
    return std::string::npos;
}

static std::wstring PowerAdvFirstGuid(const std::string& text) {
    size_t pos = PowerAdvFindGuid(text, 0);
    if (pos == std::string::npos) return L"";
    std::string s = text.substr(pos, 36);
    return std::wstring(s.begin(), s.end());
}

static bool PowerAdvParseAcDc(const std::string& out, DWORD& ac, DWORD& dc) {
    std::vector<DWORD> vals;
    for (size_t i = 0; i + 2 <= out.size(); ++i) {
        if (out[i] != '0' || (out[i + 1] != 'x' && out[i + 1] != 'X')) continue;
        size_t p = i + 2; DWORD v = 0; bool any = false;
        while (p < out.size() && p - (i + 2) < 8 && isxdigit((unsigned char)out[p])) {
            char c = (char)tolower((unsigned char)out[p]);
            v = (v << 4) | (DWORD)(c <= '9' ? c - '0' : c - 'a' + 10);
            any = true; ++p;
        }
        if (any) { vals.push_back(v); i = p - 1; }
    }
    if (vals.size() < 2) return false;
    ac = vals[vals.size() - 2];
    dc = vals.back();
    return true;
}

// Один вызов powercfg /qh на всю подгруппу: секция параметра ограничена следующим GUID.
static bool PowerAdvCapture(const std::wstring& scheme, PowerAdvValues& v) {
    v = PowerAdvValues{};
    DWORD ec = 1; std::string out;
    ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\powercfg.exe",
        L"/qh " + scheme + L" SUB_PROCESSOR", 8000, ec, &out);
    if (st != PWS_OK || ec != 0) return false;

    auto readSetting = [&](const wchar_t* guid, DWORD& ac, DWORD& dc) -> bool {
        // GUID в powercfg /qh — ASCII; преобразуем явно, без narrowing-предупреждения.
        std::wstring w(guid);
        std::string needle;
        needle.reserve(w.size());
        for (wchar_t c : w) needle.push_back(static_cast<char>(c));
        size_t start = PowerAdvFindNoCase(out, needle, 0);
        if (start == std::string::npos) return false;
        size_t end = PowerAdvFindGuid(out, start + needle.size());
        if (end == std::string::npos) end = out.size();
        return PowerAdvParseAcDc(out.substr(start, end - start), ac, dc);
    };

    return readSetting(kPowerAdvCpm, v.cpmAc, v.cpmDc) &&
           readSetting(kPowerAdvPtm, v.ptmAc, v.ptmDc) &&
           readSetting(kPowerAdvPbm, v.pbmAc, v.pbmDc) &&
           readSetting(kPowerAdvEpp, v.eppAc, v.eppDc);
}

static bool PowerAdvIsGuid(const std::wstring& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != L'-') return false;
        } else if (!iswxdigit(c)) {
            return false;
        }
    }
    return true;
}

static std::wstring PowerAdvEncode(const PowerAdvSnapshot& s) {
    auto pair = [](DWORD ac, DWORD dc) {
        return std::to_wstring(ac) + L"," + std::to_wstring(dc);
    };
    return L"V1|" + s.scheme + L"|" +
           pair(s.v.cpmAc, s.v.cpmDc) + L"|" +
           pair(s.v.ptmAc, s.v.ptmDc) + L"|" +
           pair(s.v.pbmAc, s.v.pbmDc) + L"|" +
           pair(s.v.eppAc, s.v.eppDc);
}

static bool PowerAdvParseNumber(const std::wstring& s, DWORD& out) {
    if (s.empty() || s.size() > 3) return false;
    DWORD v = 0;
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9') return false;
        v = v * 10 + (DWORD)(c - L'0');
    }
    out = v;
    return true;
}

static bool PowerAdvParsePair(const std::wstring& s, DWORD& ac, DWORD& dc) {
    size_t comma = s.find(L',');
    if (comma == std::wstring::npos) return false;
    return PowerAdvParseNumber(s.substr(0, comma), ac) &&
           PowerAdvParseNumber(s.substr(comma + 1), dc);
}

static bool PowerAdvDecode(const std::wstring& raw, PowerAdvSnapshot& s) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;) {
        size_t bar = raw.find(L'|', start);
        parts.push_back(raw.substr(start, bar == std::wstring::npos ? std::wstring::npos : bar - start));
        if (bar == std::wstring::npos) break;
        start = bar + 1;
    }
    if (parts.size() != 6 || parts[0] != L"V1" || !PowerAdvIsGuid(parts[1])) return false;
    PowerAdvSnapshot tmp;
    tmp.scheme = parts[1];
    if (!PowerAdvParsePair(parts[2], tmp.v.cpmAc, tmp.v.cpmDc) ||
        !PowerAdvParsePair(parts[3], tmp.v.ptmAc, tmp.v.ptmDc) ||
        !PowerAdvParsePair(parts[4], tmp.v.pbmAc, tmp.v.pbmDc) ||
        !PowerAdvParsePair(parts[5], tmp.v.eppAc, tmp.v.eppDc)) return false;
    s = tmp;
    return true;
}

static bool PowerAdvApplyAc(const std::wstring& scheme, const wchar_t* setting, DWORD value) {
    DWORD ec = 1;
    ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\powercfg.exe",
        L"/setacvalueindex " + scheme + L" SUB_PROCESSOR " + setting + L" " + std::to_wstring(value),
        6000, ec);
    return st == PWS_OK && ec == 0;
}

static bool PowerAdvVerifyAc(const std::wstring& scheme, const PowerAdvValues& want) {
    PowerAdvValues now;
    if (!PowerAdvCapture(scheme, now)) return false;
    return now.cpmAc == want.cpmAc && now.ptmAc == want.ptmAc &&
           now.pbmAc == want.pbmAc && now.eppAc == want.eppAc;
}

// /setactive нужен только для уже выбранной схемы; чужой выбор пользователя не меняем.
static bool PowerAdvActivateIfCurrent(const std::wstring& scheme) {
    std::wstring active = PowerAdvFirstGuid(RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /getactivescheme", 4000));
    if (active.empty() || _wcsicmp(active.c_str(), scheme.c_str()) != 0) return true;
    DWORD ec = 1;
    ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\powercfg.exe", L"/setactive " + scheme, 6000, ec);
    return st == PWS_OK && ec == 0;
}

static std::wstring ExtractFirstGuid(const std::string& text);

bool ReadTweakState(int id) {
    // Кэш HKCU\Software\LEFTIK\States нужен только для твиков-команд, которые нельзя прочитать из реестра
    // (таймер, кэш DX, EAC/Vanguard, DNS, Ultimate, Edge/Teams, очистки). Для остальных — читаем реестр.

    auto ReadCache = [&]() -> int {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\States", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD val = 0, sz = sizeof(val), type = 0;
            wchar_t name[32]; swprintf(name, 32, L"%d", id);
            LONG rc = RegQueryValueExW(hKey, name, nullptr, &type, (BYTE*)&val, &sz);
            RegCloseKey(hKey);
            if (rc == ERROR_SUCCESS && type == REG_DWORD) return val;
        }
        return -1;
    };

    HKEY hKey;
    auto CkD = [&](HKEY root, const wchar_t* path, const wchar_t* name, DWORD expect) -> bool {
        if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return false;
        DWORD val = 0, sz = sizeof(val), tp = 0;
        LONG rc = RegQueryValueExW(hKey, name, nullptr, &tp, (BYTE*)&val, &sz);
        RegCloseKey(hKey);
        return rc == ERROR_SUCCESS && tp == REG_DWORD && val == expect;
    };
    auto CkStr = [&](HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* expect) -> bool {
        if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) return false;
        wchar_t buf[512]{}; DWORD sz = sizeof(buf), tp = 0;
        LONG rc = RegQueryValueExW(hKey, name, nullptr, &tp, (BYTE*)buf, &sz);
        RegCloseKey(hKey);
        return rc == ERROR_SUCCESS && wcscmp(buf, expect) == 0;
    };
    auto CkSvcOff = [&](const wchar_t* svc) -> bool {
        std::wstring p = L"SYSTEM\\CurrentControlSet\\Services\\"; p += svc;
        return CkD(HKEY_LOCAL_MACHINE, p.c_str(), L"Start", 4);
    };
    auto CkSvcOn = [&](const wchar_t* svc, DWORD expect) -> bool {
        std::wstring p = L"SYSTEM\\CurrentControlSet\\Services\\"; p += svc;
        return CkD(HKEY_LOCAL_MACHINE, p.c_str(), L"Start", expect);
    };

    switch (id) {
        // --- BASIC ---
        case 1000: return CkD(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", 0);
        case 1001: return CkStr(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"0");
        case 1002: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation", 0x26);
        case 1003: return CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\CrossDevice", L"CrossDeviceResumeEnabled", 0);
        case 1004: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SvcHostSplitThresholdInKB", 0xFFFFFFFF);
        case 1005: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisable8dot3NameCreation", 1);
        case 1006: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisableLastAccessUpdate", 1);
        case 1007: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\Maintenance", L"MaintenanceDisabled", 1);
        case 1008: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\mouclass\\Parameters", L"MouseDataQueueSize", 20);
        case 1070: {
            HKEY hk;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
                LONG rc = RegQueryValueExW(hk, L"LEFTIK_Optimizer", nullptr, nullptr, nullptr, nullptr);
                RegCloseKey(hk);
                return rc == ERROR_SUCCESS;
            }
            return false;
        }
        case 1071: return CkStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", L"0");
        case 1072: return CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 0);
        case 1074: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 2);
        case 1075: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management\\PrefetchParameters", L"EnablePrefetcher", 0);
        case 1076: return CkD(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 2);

        // --- SECURITY ---
        case 1010: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", L"DisableAntiSpyware", 1);
        case 1011: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", 0);
        case 1012: return CkSvcOff(L"wscsvc") || CkSvcOff(L"SecurityHealthService");
        case 1013: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableSmartScreen", 0);
        case 1014: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\StandardProfile", L"EnableFirewall", 0);
        case 1015: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Script\\Settings", L"AmsiEnable", 0);
        case 1016: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI", L"VulnerableDriverBlocklistEnable", 0);

        // --- POWER ---
        case 1021: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", L"PowerThrottlingOff", 1);
        case 1023: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power", L"HibernateEnabled", 0);
        case 1024: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\USB", L"DisableSelectiveSuspend", 1);
        case 1073: return ReadTweakState(1021); // legacy-алиас к 1021

        // --- NETWORK ---
        case 1030: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Psched", L"NonBestEffortLimit", 0);
        case 1031: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters", L"DisabledComponents", 0x20) ||
                          CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters", L"DisabledComponents", 0xFF);
        case 1032: {
            // Активен, если хотя бы в одном интерфейсе включен Nagle-off
            const wchar_t* baseI = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
            HKEY hIf;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseI, 0, KEY_READ, &hIf) != ERROR_SUCCESS) return false;
            bool any = false;
            wchar_t name[128]; DWORD len = 128, idx = 0;
            for (;;) {
                len = 128;
                if (RegEnumKeyExW(hIf, idx, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                std::wstring p = std::wstring(baseI) + L"\\" + name;
                if (CkD(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpAckFrequency", 1)) { any = true; break; }
                ++idx;
            }
            RegCloseKey(hIf);
            return any;
        }
        // 1033/1034/1035 — дефолт Windows совпадает с "оптимальным", проверка по
        // реестру даёт false positive. Читаем только кэш LEFTIK.
        case 1033: case 1034: case 1035: { int c = ReadCache(); return c == 1; }
        case 1082: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"MaxUserPort", 65534);
        case 1094: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"NetworkThrottlingIndex", 0xFFFFFFFF);

        // --- DEBLOAT ---
        case 1040: return CkSvcOff(L"SysMain");
        case 1041: return CkSvcOff(L"WSearch");
        // 1042: если OneDrive изначально не установлен (LTSC/корпоратив),
        // проверка по файлу даёт false positive. Читаем кэш.
        case 1042: { int c = ReadCache(); return c == 1; }
        case 1043: return CkSvcOff(L"XblAuthManager") && CkSvcOff(L"XblGameSave");

        // --- PRIVACY ---
        case 1050: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 0);
        case 1051: return CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0);
        case 1052: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors", L"DisableLocation", 1);

        // --- SERVICES ---
        case 1060: return CkSvcOff(L"Spooler");
        case 1061: return CkSvcOff(L"RemoteRegistry");
        case 1062: return CkSvcOff(L"Fax");
        case 1063: return CkSvcOff(L"WerSvc");
        case 1064: return CkSvcOff(L"bthserv");
        case 1065: return CkSvcOff(L"TabletInputService");

        // --- 1095–1099, 1102–1104 ---
        case 1095: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"SystemResponsiveness", 0);
        case 1096: return CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0);
        case 1097: { int c = ReadCache(); return c == 1; }
        case 1098: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"GPU Priority", 8) &&
                          CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"Priority", 6);
        case 1099: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\csrss.exe\\PerfOptions", L"CpuPriorityClass", 4);
        case 1102: {
            // v58.0: проверяем не только наличие снимка, но и то, что схема,
            // к которой применялся твик, всё ещё активна. Если пользователь сменил
            // план вручную — твик фактически не действует, тумблер должен быть off.
            std::wstring raw = Journal::LoadPrevPowerAdv();
            if (raw.empty()) return false;
            PowerAdvSnapshot snap;
            if (!PowerAdvDecode(raw, snap)) return false;
            std::wstring active = ExtractFirstGuid(
                RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /getactivescheme", 4000));
            if (active.empty()) return false;
            return _wcsicmp(active.c_str(), snap.scheme.c_str()) == 0;
        }
        case 1103: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"OverlayTestMode", 5);
        case 1104: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", L"DisablePagingExecutive", 1);

        // --- NVIDIA / AMD ---
        case 1100: return CkD(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak", L"PowerMizerEnable", 1);
        case 1101: return CkSvcOff(L"NvTelemetryContainer");
        case 1110: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000", L"EnableUlps", 0);
        case 1111: return CkSvcOff(L"AMD Crash Defender Service");

        // --- FIXES: 2007-2010 можно прочитать из реестра ---
        case 2007: return CkSvcOn(L"EasyAntiCheat", 2);
        case 2008: return CkSvcOn(L"vgc", 2) && CkSvcOn(L"vgk", 1);
        case 2009: return CkSvcOn(L"BlueStacksDrv", 2);
        case 2010: return CkD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters", L"EnableHandsFree", 0);
        case 2013: return CkD(HKEY_CURRENT_USER, L"Software\\Discord", L"HardwareAcceleration", 0);

        // --- v57.12: новые фишки ---
        case 1121: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0);

        // --- Блоки L/M/N ---
        case 1130: return CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 0) &&
                          CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", 0);
        case 1131: return CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"DeferFeatureUpdates", 1) &&
                          CkD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"DeferFeatureUpdatesPeriodInDays", 365);
        case 1132: return CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Clipboard", L"EnableCloudClipboard", 0) &&
                          CkD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Clipboard", L"EnableClipboardHistory", 0);

        case 1140: return CkD(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"EnableGameOverlay", 0);

        // --- Твики-команды: читаем только из кэша ---
        // 1080 (таймер), 1081 (кэш DX), 2001-2006 (одноразовые фиксы), 3010 (DNS),
        // 1044 (Edge), 1045 (Teams) — после применения команды состояние в реестре не отражается.
        // 1017 (VBS) и 1020 (Ultimate) тоже здесь: T_VBS пишет bcdedit (DeviceGuard-ключ не трогает),
        // а powercfg -duplicatescheme активирует КОПИЮ с новым GUID — шаблонный GUID в ActivePowerScheme не попадает.
        case 1080: case 1081: case 2001: case 2002: case 2003: case 2004: case 2005: case 2006:
        case 3010: case 1044: case 1045:
        case 1017: case 1018: case 1020: case 1133:
        case 1120: case 1122: // v57.12: компрессия/autotuning — по кэшу
        case 2011: case 2012: case 2014: // одноразовые фиксы: Store/CompactOS/Audio
        case 2015: case 2016: case 2017: case 2018: // Print/Xbox/WebView2/GTA5
        case 2019: case 2020: case 2021: // DWM/FiveM/Discord Share
        {
            int c = ReadCache();
            return c == 1;
        }

        default: return false;
    }
}

void LoadTweakStates() {
    for (auto& tw : App::tweaksList) tw.isEnabled = ReadTweakState(tw.id);
}

bool VerifyDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD expect) {
    HKEY k; DWORD v = 0, sz = sizeof(v), tp = 0;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    LONG rc = RegQueryValueExW(k, name, nullptr, &tp, (BYTE*)&v, &sz);
    RegCloseKey(k);
    return rc == ERROR_SUCCESS && tp == REG_DWORD && v == expect;
}

static bool SetDwordAutoGroup(HKEY root, const std::wstring& path, const std::wstring& name, DWORD value, const std::wstring& tweakName) {
    if (!App::activeGroupId.empty())
        return Journal::SetDwordGrouped(root, path, name, value, tweakName, App::activeGroupId);
    return Journal::SetDword(root, path, name, value, tweakName);
}
static bool SetStringAutoGroup(HKEY root, const std::wstring& path, const std::wstring& name, const std::wstring& value, const std::wstring& tweakName) {
    if (!App::activeGroupId.empty())
        return Journal::SetStringGrouped(root, path, name, value, tweakName, App::activeGroupId);
    return Journal::SetString(root, path, name, value, tweakName);
}

void T_GameDVR() {
    // Полное отключение Game DVR / Game Bar background recording
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", 0, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 2, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_HonorUserFSEBehaviorMode", 1, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_DXGIHonorFSEWindowsCompatible", 1, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"HistoricalCaptureEnabled", 0, L"GameDVR Off");
    // Отключить Game Bar как процесс
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"UseNexusForGameBarEnabled", 0, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"ShowStartupPanel", 0, L"GameDVR Off");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 0, L"GameDVR Off");

    if (!VerifyDword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", 0)) {
        HANDLE hf = CreateFileW(App::debugLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            const char* msg = "FAIL: T_GameDVR registry write did not take effect\r\n";
            DWORD w; WriteFile(hf, msg, (DWORD)strlen(msg), &w, NULL); CloseHandle(hf);
        }
    }
}
void T_MouseAccel() { 
    Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"0", L"Мышь 1:1");
    Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", L"0", L"Мышь 1:1");
    Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", L"0", L"Мышь 1:1");
}
void T_MouseBuffer() {
    // По умолчанию Windows держит 100. Снижаем до 20 (0x14) для снижения задержки ввода.
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\mouclass\\Parameters", L"MouseDataQueueSize", 20, L"Буфер мыши");
}
void T_Priority() { 
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation", 0x26, L"Win32Priority"); 
    if (!VerifyDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation", 0x26)) {
        HANDLE hf = CreateFileW(App::debugLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            const char* msg = "FAIL: T_Priority registry write did not take effect\r\n";
            DWORD w; WriteFile(hf, msg, (DWORD)strlen(msg), &w, NULL); CloseHandle(hf);
        }
    }
}
void T_CrossDevice() { Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\CrossDevice", L"CrossDeviceResumeEnabled", 0, L"Cross-Device"); }
void T_SvcHost() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SvcHostSplitThresholdInKB", 0xFFFFFFFF, L"Группировка svchost"); }
void T_NTFS8_3() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisable8dot3NameCreation", 1, L"NTFS 8.3 Имена"); }
void T_NTFSLast() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisableLastAccessUpdate", 1, L"NTFS Last Access"); }
void T_AutoMaint() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\Maintenance", L"MaintenanceDisabled", 1, L"Автообслуживание"); }

void T_Defender() {
    DWORD tpm = 0, sz = sizeof(tpm);
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Features", 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"TamperProtection", nullptr, nullptr, (BYTE*)&tpm, &sz);
        RegCloseKey(k);
    }
    bool active = (tpm == 2 || tpm == 4 || tpm == 5);
    if (active) {
        MessageBoxW(App::hwnd,
            L"Windows Defender НЕ отключён.\n\n"
            L"Причина: Tamper Protection (Защита от подделки) активна.\n"
            L"Отключите её вручную: Параметры → Безопасность Windows →\n"
            L"Защита от вирусов → Управление настройками → Защита от подделки.\n"
            L"После отключения TP вернитесь и повторите твик.",
            L"LEFTIK Defender", MB_OK | MB_ICONWARNING);
        SetTweakStateById(1010, false);
        return;
    }
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", L"DisableAntiSpyware", 1, L"Windows Defender");
    // Проверка на успех записи
    DWORD v = 0; sz = sizeof(v);
    bool applied = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, L"DisableAntiSpyware", nullptr, nullptr, (BYTE*)&v, &sz) == ERROR_SUCCESS && v == 1) applied = true;
        RegCloseKey(k);
    }
    if (!applied) {
        MessageBoxW(App::hwnd, L"Не удалось записать ключ. Возможно, нет прав.", L"LEFTIK", MB_OK | MB_ICONWARNING);
        SetTweakStateById(1010, false);
    }
}
void T_UAC() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", 0, L"Отключить UAC"); }
void T_SecCenter() { DisableSvc(L"wscsvc"); DisableSvc(L"SecurityHealthService"); }
void T_SmartScreen() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableSmartScreen", 0, L"SmartScreen"); }

// Критичные твики выполняем только через RunProcessWait: ShellExecuteW не
// сообщает код возврата, поэтому «команда отправлена» ≠ «команда сработала».
// altOkExit позволяет считать успехом идемпотентный исход (например, 1062
// «служба уже остановлена» для sc stop).
static bool RunCriticalProcess(const wchar_t* exePath, const std::wstring& args,
                               DWORD timeoutMs, const wchar_t* failMessage, int tweakId,
                               DWORD altOkExit = 0xFFFFFFFFu) {
    DWORD ec = 0;
    ProcWaitStatus st = RunProcessWait(exePath, args, timeoutMs, ec);
    const bool ok = (st == PWS_OK && ec == 0) ||
                    (altOkExit != 0xFFFFFFFFu && st == PWS_EXIT_CODE && ec == altOkExit);
    if (ok) return true;

    std::wstring msg = failMessage;
    msg += L"\n\nСтатус: " + std::to_wstring((int)st) +
           L", код: " + std::to_wstring(ec) + L".";
    MessageBoxW(App::hwnd, msg.c_str(), L"LEFTIK", MB_OK | MB_ICONERROR);
    SetTweakStateById(tweakId, false);
    return false;
}

void T_Firewall() {
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\netsh.exe",
            L"advfirewall set allprofiles state off", 20000,
            L"netsh не отключил брандмауэр. Настройка не применена.", 1014)) return;
    Journal::LogAction(L"Брандмауэр выключен", L"netsh advfirewall set allprofiles state on");
}
void T_AMSI() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Script\\Settings", L"AmsiEnable", 0, L"AMSI Interface"); }
void T_CodeInteg() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI", L"VulnerableDriverBlocklistEnable", 0, L"Code Integrity"); }
void T_HVCI() {
    // Отключает только Hypervisor-Enforced Code Integrity (Memory Integrity),
    // не затрагивая общий ключ EnableVirtualizationBasedSecurity (твик 1017).
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", L"Enabled", 0, L"HVCI Off");
    MessageBoxW(App::hwnd, L"Целостность памяти (HVCI) отключена.\n\nОБЯЗАТЕЛЬНО ПЕРЕЗАГРУЗИТЕ ПК.\nЭто может дать +2-5% FPS на слабых CPU.", L"LEFTIK HVCI", MB_OK | MB_ICONWARNING);
}
void T_VBS() {
    HKEY hWsl{};
    bool hasWslOrDocker = (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss", 0, KEY_READ, &hWsl) == ERROR_SUCCESS);
    if (hWsl) RegCloseKey(hWsl);
    if (!hasWslOrDocker) {
        HKEY hDocker{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\com.docker.service", 0, KEY_READ, &hDocker) == ERROR_SUCCESS) {
            hasWslOrDocker = true;
            RegCloseKey(hDocker);
        }
    }
    if (hasWslOrDocker) {
        if (MessageBoxW(App::hwnd,
            L"Обнаружен WSL2 или Docker Desktop.\n\n"
            L"Отключение гипервизора (bcdedit hypervisorlaunchtype off) сделает их\n"
            L"неработоспособными до обратного включения твика.\n\n"
            L"Продолжить?",
            L"LEFTIK — VBS / Hyper-V", MB_YESNO | MB_ICONWARNING) != IDYES) {
            SetTweakStateById(1017, false);
            return;
        }
    }
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\bcdedit.exe",
            L"/set hypervisorlaunchtype off", 20000,
            L"bcdedit вернул ошибку. VBS не отключён.", 1017)) return;
    Journal::LogAction(L"VBS отключен", L"bcdedit /set hypervisorlaunchtype auto");
}

void T_SteamLite() {
    bool steamRunning = IsProcessRunning(L"steam.exe");
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"EnableGameOverlay", 0, L"Steam Lite");
    if (steamRunning) {
        RunCmd(L"/c taskkill /f /im steamwebhelper.exe");
        RunCmd(L"/c taskkill /f /im steam.exe");
    }

    // Очистка кэша Steam (htmlcache, appcache, logs)
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData))) {
        std::wstring lad = localAppData;
        CoTaskMemFree(localAppData);
        DeleteFolderRecursive(lad + L"\\Steam\\htmlcache");
        DeleteFolderRecursive(lad + L"\\Steam\\appcache");
        DeleteFolderRecursive(lad + L"\\Steam\\logs");
        Journal::LogAction(L"Steam Lite: очистка кэша htmlcache, appcache и logs");
    }

    if (steamRunning) {
        MessageBoxW(App::hwnd,
            L"ВНИМАНИЕ: Steam был принудительно закрыт. Ключ оверлея применен, кэш и логи (htmlcache, appcache, logs) очищены.",
            L"LEFTIK Steam", MB_OK | MB_ICONWARNING);
    } else {
        MessageBoxW(App::hwnd,
            L"Оверлей Steam отключён, кэш и логи (htmlcache, appcache, logs) очищены.\n\n"
            L"Steam не был запущен — настройка применится при следующем запуске.",
            L"LEFTIK Steam", MB_OK);
    }
}

void T_DisableDynamicTick() {
    if (!IsUserAnAdmin()) {
        MessageBoxW(App::hwnd, L"Требуются права администратора.", L"LEFTIK", MB_ICONERROR);
        SetTweakStateById(1133, false);
        return;
    }
    std::string before = RunCaptureStdout(L"C:\\Windows\\System32\\bcdedit.exe /enum {current}", 4000);
    std::wstring rollback = L"bcdedit /deletevalue disabledynamictick";
    {
        size_t pos = before.find("disabledynamictick");
        if (pos != std::string::npos) {
            size_t eol = before.find('\n', pos);
            std::string line = before.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            if (line.find("Yes") != std::string::npos || line.find("Да") != std::string::npos)
                rollback = L"bcdedit /set disabledynamictick yes";
            else if (line.find("No") != std::string::npos || line.find("Нет") != std::string::npos)
                rollback = L"bcdedit /set disabledynamictick no";
        }
    }
    Journal::LogAction(L"Отключение динамического тика", rollback);

    DWORD ec = 0;
    ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\bcdedit.exe",
        L"/set disabledynamictick yes", 20000, ec);
    if (st != PWS_OK || ec != 0) {
        MessageBoxW(App::hwnd, L"bcdedit вернул ошибку — изменения не применены.",
                    L"LEFTIK", MB_ICONERROR);
        SetTweakStateById(1133, false);
        return;
    }
    MessageBoxW(App::hwnd,
        L"Динамический тик отключён.\n\n"
        L"Изменения вступят в силу после перезагрузки.\n"
        L"Снижает задержку ввода, но повышает энергопотребление.",
        L"LEFTIK", MB_OK | MB_ICONWARNING);
}

// RunCaptureStdout перенесён к RunProcessWait (см. блок runner'ов).

// Первый GUID (8-4-4-4-12 hex) в тексте — кодировка локали powercfg не важна
static std::wstring ExtractFirstGuid(const std::string& text) {
    auto isHex = [](char c) { return isxdigit((unsigned char)c) != 0; };
    static const int groups[5] = { 8, 4, 4, 4, 12 };
    for (size_t i = 0; i + 36 <= text.size(); ++i) {
        size_t pos = i; bool ok = true;
        for (int g = 0; g < 5 && ok; ++g) {
            for (int k = 0; k < groups[g]; ++k, ++pos)
                if (!isHex(text[pos])) { ok = false; break; }
            if (ok && g < 4) { if (text[pos] != '-') ok = false; else ++pos; }
        }
        if (ok) { std::string s = text.substr(i, 36); return std::wstring(s.begin(), s.end()); }
    }
    return L"";
}

static std::wstring s_prevPowerSchemeGuid; // схема, активная до Ultimate — для честного отката

void T_Ultimate() {
    // 1.5: сначала ищем уже созданную схему, чтобы не плодить дубликаты при каждом включении.
    std::wstring curActive = ExtractFirstGuid(RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /getactivescheme", 4000));
    std::wstring ultGuid;
    std::string listOut = RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /list", 4000);
    {
        std::stringstream ls(listOut);
        std::string listLine;
        while (std::getline(ls, listLine)) {
            if (PowerAdvFindNoCase(listLine, "LEFTIK Ultimate") != std::string::npos ||
                PowerAdvFindNoCase(listLine, "e9a42b02-d5df-448d-aa00-03f14749eb61") != std::string::npos) {
                ultGuid = ExtractFirstGuid(listLine);
                if (!ultGuid.empty()) break;
            }
        }
    }

    if (ultGuid.empty()) {
        // Копия Ultimate Performance; duplicatescheme печатает GUID копии в stdout.
        std::string dupOut = RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61", 6000);
        ultGuid = ExtractFirstGuid(dupOut);
        if (!ultGuid.empty()) {
            DWORD ec = 1;
            RunProcessWait(L"C:\\Windows\\System32\\powercfg.exe",
                L"/changename " + ultGuid + L" \"LEFTIK Ultimate\"", 6000, ec);
        }
    }

    if (ultGuid.empty()) {
        for (auto& tw : App::tweaksList) if (tw.id == 1020) tw.isEnabled = false;
        SaveTweakState(1020, false);
        Journal::LogAction(L"Ultimate Performance: создать схему не удалось");
        return;
    }

    // Предыдущую схему запоминаем только если это не сама Ultimate:
    // повторное включение не должно затирать сохранённый откат.
    if (!curActive.empty() && _wcsicmp(curActive.c_str(), ultGuid.c_str()) != 0) {
        s_prevPowerSchemeGuid = curActive;
        Journal::SavePrevPowerScheme(curActive);
    }

    // Отключаем паркинг ядер: минимум ядер в простое = 100% (сетевой и батарейный профили)
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setacvalueindex " + ultGuid + L" SUB_PROCESSOR CPMINCORES 100", 4000);
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setdcvalueindex " + ultGuid + L" SUB_PROCESSOR CPMINCORES 100", 4000);
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setactive " + ultGuid, 4000);
    Journal::LogAction(L"Ultimate Performance: схема активирована (" + ultGuid + L")");
}
void T_PowerThrot() { SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", L"PowerThrottlingOff", 1, L"Power Throttling"); }
void T_Hibernate() { Journal::LogAction(L"Гибернация отключена", L"powercfg -h on"); RunCmd(L"/c C:\\Windows\\System32\\powercfg.exe -h off"); }
void T_USBSuspend() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\USB", L"DisableSelectiveSuspend", 1, L"USB Selective Suspend"); }

void T_QoS() { SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Psched", L"NonBestEffortLimit", 0, L"QoS Limit"); }
void T_IPv6() {
    // v58.0: 0x20 отключает только туннельные переходы (6to4, Teredo, ISATAP),
    // но оставляет IPv6 loopback ::1 — не ломает UWP, Xbox Live, VPN.
    // 0xFF (как было раньше) отключает ВСЁ, включая localhost.
    Journal::SetDword(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",
        L"DisabledComponents", 0x20, L"IPv6 (только туннели)");
}
void T_Nagle() {
    // Base path (Tcpip\Parameters) Windows для этих ключей игнорирует — пишем в КАЖДЫЙ интерфейс.
    // TcpAckFrequency=1 — ACK на каждый пакет, TCPNoDelay=1 — запрет Нейгла, TcpDelAckTicks=0 — без отложки.
    const wchar_t* base = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
    HKEY hIf;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base, 0, KEY_READ, &hIf) == ERROR_SUCCESS) {
        wchar_t name[128]; DWORD len = 128, idx = 0;
        for (;;) {
            len = 128;
            if (RegEnumKeyExW(hIf, idx, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring p = std::wstring(base) + L"\\" + name;
            Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"TcpAckFrequency", 1, L"Отключить Nagle");
            Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"TCPNoDelay", 1, L"Отключить Nagle");
            Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"TcpDelAckTicks", 0, L"Отключить Nagle");
            ++idx;
        }
        RegCloseKey(hIf);
    }
}
void T_RSS() { NetworkTweaker::OptimizeAdapters(); Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"EnableRSS", 1, L"RSS"); }
void T_Checksum() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"DisableTaskOffload", 0, L"Checksum Offload"); }
void T_LSO() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"DisableLargeSendOffload", 0, L"Large Send Offload"); }

void T_MenuShowDelay() { SetStringAutoGroup(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", L"0", L"MenuShowDelay"); }
void T_StartupDelay() { SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 0, L"StartupDelay"); }
void T_PowerThrottlingGlobal() { T_PowerThrot(); }
bool T_HwSchMode(bool silent) {
    // Проверка GPU: HAGS работает только на RTX 20xx+ и RX 5000+
    std::wstring g = App::gpuName;
    std::transform(g.begin(), g.end(), g.begin(), ::towlower);
    bool isOldNvidia = (g.find(L"gtx 10") != std::wstring::npos ||
                        g.find(L"gtx 9")  != std::wstring::npos ||
                        g.find(L"gtx 7")  != std::wstring::npos ||
                        g.find(L"gtx 6")  != std::wstring::npos);
    bool isOldAmd = (g.find(L"rx 5") != std::wstring::npos && g.find(L"rx 5000") == std::wstring::npos &&
                     g.find(L"rx 5500") == std::wstring::npos && g.find(L"rx 5600") == std::wstring::npos &&
                     g.find(L"rx 5700") == std::wstring::npos) ||
                    (g.find(L"rx 4") != std::wstring::npos);
    if (isOldNvidia || isOldAmd) {
        if (!silent) {
            MessageBoxW(App::hwnd,
                isOldAmd
                    ? L"⚠️ HAGS не поддерживается на архитектуре AMD GCN (RX 400/500).\n\nТвик НЕ применён."
                    : L"⚠️ HAGS НЕ РЕКОМЕНДУЕТСЯ для вашей видеокарты (GTX 10xx или старше).\n\n"
                      L"Аппаратный планировщик GPU на старых картах даёт -5-10% FPS.\n"
                      L"Твик НЕ применён.",
                L"LEFTIK HAGS", MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    if (!SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 2, L"HAGS"))
        return false;
    if (!silent) {
        MessageBoxW(App::hwnd,
            L"HAGS включён. ПЕРЕЗАГРУЗИТЕ ПК для активации.\n\n"
            L"На RTX 20xx+ и RX 5000+ даёт +0-5% FPS и стабильный фреймтайм.\n"
            L"До перезагрузки может быть лок FPS 60.",
            L"LEFTIK HAGS", MB_OK | MB_ICONWARNING);
    }
    return true;
}
bool T_HwSchModeSilent() { return T_HwSchMode(true); }

// Сохраняет текущие DNS каждого активного (Up) адаптера в HKCU\Software\LEFTIK\PrevDNS
// через Journal::SetString перед установкой Cloudflare DNS (1.1.1.1, 1.0.0.1).
void T_DNS() {
    std::string out; DWORD ec = 0;
    RunPowerShellWait(
        L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"Get-NetAdapter | Where-Object Status -eq 'Up' | ForEach-Object { "
        L"$a = $_.Name; "
        L"$d = (Get-DnsClientServerAddress -InterfaceIndex $_.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue).ServerAddresses -join ','; "
        L"Write-Output ($a + '|' + $d) }",
        15000, ec, &out);
    std::stringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        std::wstring alias = Util::FromUtf8(line.substr(0, bar));
        std::wstring dns = Util::FromUtf8(line.substr(bar + 1));
        if (!alias.empty()) {
            Journal::SetString(HKEY_CURRENT_USER, L"Software\\LEFTIK\\PrevDNS", alias,
                               dns.empty() ? L"DHCP" : dns, L"DNS Backup");
        }
    }
    RunPowerShell(L"Get-NetAdapter | Where-Object Status -eq 'Up' | Set-DnsClientServerAddress -ServerAddresses ('1.1.1.1','1.0.0.1')");
}
void T_Prefetcher() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management\\PrefetchParameters", L"EnablePrefetcher", 0, L"Disable Prefetch"); }
void T_FSEBehavior() { Journal::SetDword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 2, L"Disable FSE"); }
// T_PlatformClock (bcdedit useplatformclock) удален: на большинстве систем только добавляет задержки планировщика.

void T_SysMain() { DisableSvc(L"SysMain"); }
void T_WSearch() { DisableSvc(L"WSearch"); }
void T_OneDrive() {
    const wchar_t* setup = L"C:\\Windows\\System32\\OneDriveSetup.exe";
    if (!Util::FileExists(setup)) {
        MessageBoxW(App::hwnd,
            L"OneDriveSetup.exe не найден (OneDrive установлен через Microsoft Store).\n\n"
            L"Удалить вручную: Параметры → Приложения → Microsoft OneDrive → Удалить.\n\n"
            L"Твик НЕ применён, тумблер возвращён в выключенное состояние.",
            L"LEFTIK", MB_OK | MB_ICONWARNING);
        SetTweakStateById(1042, false);
        return;
    }
    Journal::LogAction(L"Удаление OneDrive");
    RunCmd(L"/c taskkill /f /im OneDrive.exe & C:\\Windows\\System32\\OneDriveSetup.exe /uninstall");
}
void T_Xbox() { DisableSvc(L"XblAuthManager"); DisableSvc(L"XblGameSave"); }
void T_Edge() {
    if (GetWindowsBuild() >= 22000) {
        MessageBoxW(App::hwnd,
            L"Удаление Microsoft Edge на Windows 11 заблокировано Microsoft.\n"
            L"Твик не будет применён (тумблер останется выключенным).\n\n"
            L"Windows 10 позволяет удалить Edge через Remove-AppxPackage.",
            L"LEFTIK", MB_OK | MB_ICONINFORMATION);
        SetTweakStateById(1044, false);
        return;
    }
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
            L"-NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; Get-AppxPackage *MicrosoftEdge* | Remove-AppxPackage\"",
            120000, L"PowerShell не удалил Microsoft Edge. Твик не применён.", 1044)) return;
    Journal::LogAction(L"Удаление Edge");
}
void T_Teams() {
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
            L"-NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; Get-AppxPackage *Teams* | Remove-AppxPackage\"",
            120000, L"PowerShell не удалил Microsoft Teams. Твик не применён.", 1045)) return;
    Journal::LogAction(L"Удаление Teams");
}

void T_Telemetry() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 0, L"Телеметрия"); DisableSvc(L"DiagTrack"); }
void T_AdID() { Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0, L"Рекламный ID"); }
void T_Location() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors", L"DisableLocation", 1, L"Геолокация"); }

void T_Spooler() { DisableSvc(L"Spooler"); }
void T_RemoteReg() { DisableSvc(L"RemoteRegistry"); }
void T_Fax() { DisableSvc(L"Fax"); }
void T_WerSvc() { DisableSvc(L"WerSvc"); }
void T_Bluetooth() { DisableSvc(L"bthserv"); }

void F_EAC() { Journal::LogAction(L"Fix EAC"); RunCmd(L"/c sc config EasyAntiCheat start= auto & sc start EasyAntiCheat"); }
void F_Vanguard() { Journal::LogAction(L"Fix Vanguard"); RunCmd(L"/c sc config vgc start= auto & sc config vgk start= system"); }
void F_BlueStacks() { Journal::LogAction(L"BlueStacks Fix"); RunCmd(L"/c sc config BlueStacksDrv start= auto & sc config BstHdAndroidSvc start= auto & sc config BstHdLogRotatorSvc start= auto"); }
void F_BluetoothAudio() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters", L"EnableHandsFree", 0, L"Bluetooth Audio"); }

void F_WU() {
    // Составная команда оставлена одной строкой cmd: порядок stop → rename → start
    // важен, а exit code cmd возвращает по последней операции (net start bits).
    const std::wstring args = L"/d /s /c \"net stop wuauserv & net stop bits & ren %windir%\\SoftwareDistribution SoftwareDistribution.old & net start wuauserv & net start bits\"";
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\cmd.exe", args, 180000,
            L"Сброс Windows Update завершился ошибкой. Службы wuauserv/bits не перезапущены.", 2001)) return;
    Journal::LogAction(L"Сброс Windows Update");
}
void F_Store() { Journal::LogAction(L"Переустановка Store"); RunCmd(L"/c powershell -Command \"Get-AppxPackage -AllUsers *WindowsStore* | Foreach {Add-AppxPackage -DisableDevelopmentMode -Register $($_.InstallLocation)\\AppxManifest.xml}\""); }
bool F_Net() {
    if (MessageBoxW(App::hwnd,
        L"Сброс сети (Winsock + TCP/IP).\n\n"
        L"ВНИМАНИЕ: после выполнения потребуется ПЕРЕЗАГРУЗКА,\n"
        L"иначе сеть может не работать до перезапуска системы.\n\n"
        L"Продолжить?",
        L"LEFTIK", MB_YESNO | MB_ICONWARNING) != IDYES) return false;
    Journal::LogAction(L"Сброс сети");
    RunCmd(L"/c C:\\Windows\\System32\\netsh.exe winhttp reset proxy & C:\\Windows\\System32\\ipconfig.exe /flushdns & C:\\Windows\\System32\\netsh.exe winsock reset & C:\\Windows\\System32\\netsh.exe int ip reset");
    MessageBoxW(App::hwnd, L"Команды сброса сети отправлены на выполнение.\nПерезагрузите ПК, чтобы применить изменения.", L"LEFTIK", MB_OK);
    return true;
}
void F_Icons() { Journal::LogAction(L"Сброс иконок"); RunCmd(L"/c ie4uinit.exe -show"); }
bool F_Dism() {
    if (MessageBoxW(App::hwnd,
        L"DISM + SFC — длительная операция (10-30 минут).\n"
        L"Прогресс отображается в отдельном окне консоли, которое\n"
        L"закроется по нажатию клавиши после завершения.\n\n"
        L"НЕ закрывайте приложение во время проверки.\n\n"
        L"Продолжить?",
        L"LEFTIK", MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
    Journal::LogAction(L"DISM + SFC");
    ShellExecuteW(nullptr, L"runas", L"cmd.exe",
        L"/c DISM /Online /Cleanup-Image /RestoreHealth && sfc /scannow && echo. && echo === ГОТОВО === && pause",
        nullptr, SW_SHOWNORMAL);
    return true;
}

// --- Системные твики задержек ---
void T_NetworkThrottling() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"NetworkThrottlingIndex", 0xFFFFFFFF, L"Network Throttling"); }
void T_SystemResponsiveness() { Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"SystemResponsiveness", 0, L"System Responsiveness"); }
void T_DisableTransparency() { Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0, L"Отключить прозрачность"); }

void T_EnableGpuMsiMode() { // 1097
    const wchar_t* pciRoot = L"SYSTEM\\CurrentControlSet\\Enum\\PCI";
    HKEY hPci{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, pciRoot, 0, KEY_READ, &hPci) != ERROR_SUCCESS) return;
    wchar_t devId[256]; DWORD devLen = 256, idx = 0;
    while (RegEnumKeyExW(hPci, idx++, devId, &(devLen = 256), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        std::wstring devPath = std::wstring(pciRoot) + L"\\" + devId;
        HKEY hDev{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, devPath.c_str(), 0, KEY_READ, &hDev) != ERROR_SUCCESS) continue;
        wchar_t instId[256]; DWORD instLen = 256, idx2 = 0;
        while (RegEnumKeyExW(hDev, idx2++, instId, &(instLen = 256), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            std::wstring instPath = devPath + L"\\" + instId;
            std::wstring svc = Util::ReadRegString(HKEY_LOCAL_MACHINE, instPath.c_str(), L"Service");
            if (_wcsicmp(svc.c_str(), L"nvlddmkm") == 0 || _wcsicmp(svc.c_str(), L"amdkmdag") == 0 || _wcsicmp(svc.c_str(), L"amdwddmg") == 0) {
                std::wstring msiPath = instPath + L"\\Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties";
                SetDwordAutoGroup(HKEY_LOCAL_MACHINE, msiPath, L"MSISupported", 1, L"GPU MSI Mode");
            }
        }
        RegCloseKey(hDev);
    }
    RegCloseKey(hPci);
}

void T_MmcssGamesProfile() { // 1098
    const std::wstring key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, key, L"GPU Priority", 8, L"MMCSS Games");
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, key, L"Priority", 6, L"MMCSS Games");
    SetStringAutoGroup(HKEY_LOCAL_MACHINE, key, L"Scheduling Category", L"High", L"MMCSS Games");
    SetStringAutoGroup(HKEY_LOCAL_MACHINE, key, L"SFIO Priority", L"High", L"MMCSS Games");
}

void T_RawInputBoost() { // 1099 (Отдельный тумблер, не входит в авто-пачку MAX FPS)
    const std::wstring ifeo = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\csrss.exe\\PerfOptions";
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, ifeo, L"CpuPriorityClass", 4, L"CSRSS High Priority");
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, ifeo, L"IoPriority", 3, L"CSRSS High IO");
}

void T_CpuUnparkAndBoost() { // 1102
    auto failAndRevertUi = [&](const wchar_t* msg) {
        for (auto& tw : App::tweaksList) if (tw.id == 1102) tw.isEnabled = false;
        SaveTweakState(1102, false);
        if (msg) MessageBoxW(App::hwnd, msg, L"LEFTIK Power", MB_OK | MB_ICONWARNING);
    };

    std::wstring curGuid = ExtractFirstGuid(RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /getactivescheme", 4000));
    bool realGuid = !curGuid.empty();
    if (!realGuid) {
        failAndRevertUi(L"Не удалось определить GUID активной схемы питания.");
        return;
    }

    PowerAdvSnapshot snap;
    std::wstring savedRaw = Journal::LoadPrevPowerAdv();
    bool haveSaved = !savedRaw.empty() && PowerAdvDecode(savedRaw, snap);

    if (haveSaved && _wcsicmp(snap.scheme.c_str(), curGuid.c_str()) != 0) {
        // Сначала возвращаем исходные значения схеме из старого снимка.
        bool oldRestored =
            PowerAdvApplyAc(snap.scheme, L"CPMINCORES", snap.v.cpmAc) &&
            PowerAdvApplyAc(snap.scheme, L"PROCTHROTTLEMIN", snap.v.ptmAc) &&
            PowerAdvApplyAc(snap.scheme, L"PERFBOOSTMODE", snap.v.pbmAc) &&
            PowerAdvApplyAc(snap.scheme, L"PERFEPP", snap.v.eppAc) &&
            PowerAdvActivateIfCurrent(snap.scheme) &&
            PowerAdvVerifyAc(snap.scheme, snap.v);
        if (!oldRestored) {
            failAndRevertUi(L"Не удалось откатить параметры предыдущей схемы питания. Снимок сохранён.");
            return;
        }
        Journal::ClearPrevPowerAdv();
        haveSaved = false;
        Journal::LogAction(L"CPU Unpark: параметры предыдущей схемы восстановлены, выполняется захват текущей");
    }

    if (!haveSaved) {
        if (!PowerAdvCapture(curGuid, snap.v)) {
            failAndRevertUi(L"Не удалось сохранить снимок параметров питания — изменения отменены.");
            return;
        }
        snap.scheme = curGuid;
        if (!Journal::SavePrevPowerAdv(PowerAdvEncode(snap))) {
            failAndRevertUi(L"Не удалось сохранить снимок параметров питания — изменения отменены.");
            return;
        }
    }

    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setacvalueindex " + curGuid + L" SUB_PROCESSOR CPMINCORES 100", 4000);
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setacvalueindex " + curGuid + L" SUB_PROCESSOR PROCTHROTTLEMIN 100", 4000);
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setacvalueindex " + curGuid + L" SUB_PROCESSOR PERFBOOSTMODE 2", 4000);
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setacvalueindex " + curGuid + L" SUB_PROCESSOR PERFEPP 0", 4000);
    RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /setactive " + curGuid, 4000);
    Journal::LogAction(L"CPU Unpark 100% + Aggressive Turbo Boost");
}

void T_DwmMpoFix() { // 1103
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"OverlayTestMode", 5, L"Disable MPO");
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"DisableOverlays", 1, L"Disable MPO");
}

void T_KernelMemoryLock() { // 1104
    // v58.4: IoPageLockLimit удалён — ключ не работает на 64-bit Windows (убран в XP+).
    // Оставляем только DisablePagingExecutive (работает, но не даёт +FPS — только стабильность).
    const std::wstring mm = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
    Journal::SetDword(HKEY_LOCAL_MACHINE, mm, L"DisablePagingExecutive", 1, L"DisablePagingExecutive");
}

// --- Твики NVIDIA (1100-1109) и AMD (1110-1119), применяются только на своей карте ---
void T_NvidiaPowerMax() {
    // Легаси-ключи NVTweak: на старых драйверах форсируют макс. производительность,
    // на новых могут игнорироваться (это безопасно — ничего не ломают)
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak", L"PowerMizerEnable", 1, L"NVIDIA PowerMizer");
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak", L"PerfLevelSrc", 0x2222, L"NVIDIA PowerMizer");
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak", L"PowerMizerLevel", 1, L"NVIDIA PowerMizer");
    Journal::LogAction(L"NVIDIA PowerMizer Max");
    MessageBoxW(App::hwnd, L"NVIDIA переведена в режим максимальной производительности.\n(Может увеличить нагрев в простое). На новых драйверах ключ может игнорироваться.", L"LEFTIK NVIDIA", MB_OK);
}
void T_NvidiaTelemetryOff() {
    // 1062 = ERROR_SERVICE_NOT_ACTIVE: служба уже остановлена — это успех.
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\sc.exe",
            L"stop NvTelemetryContainer", 20000,
            L"Не удалось остановить службу NvTelemetryContainer.", 1101, 1062)) return;
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\sc.exe",
            L"config NvTelemetryContainer start= disabled", 20000,
            L"Не удалось отключить автозапуск NvTelemetryContainer.", 1101)) return;
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\sc.exe",
            L"stop NvContainerLocalSystem", 20000,
            L"Не удалось остановить службу NvContainerLocalSystem.", 1101, 1062)) return;
    if (!RunCriticalProcess(L"C:\\Windows\\System32\\sc.exe",
            L"config NvContainerLocalSystem start= demand", 20000,
            L"Не удалось перевести NvContainerLocalSystem в ручной запуск.", 1101)) return;
    Journal::LogAction(L"Отключение NVIDIA Telemetry");
}
void T_AmdUlpsOff() {
    Journal::LogAction(L"AMD ULPS Off");
    for (int i = 0; i < 10; ++i) {
        std::wstring p = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\000" + std::to_wstring(i);
        HKEY k{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, p.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegCloseKey(k);
            Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"EnableUlps", 0, L"AMD ULPS");
            Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"EnableUlps_NA", 0, L"AMD ULPS");
        }
    }
    MessageBoxW(App::hwnd, L"AMD ULPS отключен (все видеоадаптеры).\nПерезагрузите ПК, чтобы микрофризы исчезли.", L"LEFTIK AMD", MB_OK);
}
void T_AmdTelemetryOff() {
    Journal::LogAction(L"Отключение AMD Telemetry");
    // Имена служб с пробелами обязательно в кавычках, иначе sc config молча падает
    RunCmd(L"/c sc stop \"AMD Crash Defender Service\" & sc config \"AMD Crash Defender Service\" start= disabled");
    RunCmd(L"/c sc stop \"AMD External Events Utility\" & sc config \"AMD External Events Utility\" start= demand");
}

// --- v57.12 Security+ ---

// Управление компрессией памяти Windows 10/11. На 16+ ГБ RAM отключаем
// (Windows не тратит такты на сжатие впустую). Ниже 16 ГБ Guard это блокирует.
// ВАЖНО: Disable-MMAgent — командлет PowerShell, через cmd он не выполняется.
void T_MemoryCompressionToggle() {
    auto revertState1120 = []() {
        for (auto& tw : App::tweaksList) if (tw.id == 1120) tw.isEnabled = false;
        SaveTweakState(1120, false);
        InvalidateRect(App::hwnd, nullptr, FALSE);
    };
    const wchar_t* blockReason = GetTweakBlockReason(1120);
    if (blockReason) { revertState1120(); MessageBoxW(App::hwnd, blockReason, L"\U0001F6E1\uFE0F Hardware Guard", MB_OK | MB_ICONINFORMATION); return; }
    if (App::busyJob.load(std::memory_order_relaxed)) { revertState1120(); MessageBoxW(App::hwnd, L"Дождитесь завершения предыдущей операции.", L"LEFTIK", MB_OK); return; }
    App::busyJob.store(true);
    Journal::LogAction(L"Отключение компрессии памяти", L"powershell -command Enable-MMAgent -MemoryCompression");
    std::thread([]() {
        AsyncJobResult* r = new AsyncJobResult(); r->jobId = 1;
        DWORD ec = 0;
        ProcWaitStatus st = RunPowerShellWait(L"Disable-MMAgent -MemoryCompression", 30000, ec);
        r->status = (int)st;
        if (st == PWS_OK && ec == 0) {
            // Фактическая проверка результата без привязки к локали ("False" / "Ложь")
            std::string out; DWORD ec2 = 0;
            ProcWaitStatus vs = RunPowerShellWait(L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; (Get-MMAgent).MemoryCompression.ToString()", 20000, ec2, &out);
            std::wstring wout = Util::FromUtf8(out);
            std::transform(wout.begin(), wout.end(), wout.begin(), ::towlower);
            std::string lowOut = out;
            for (char& c : lowOut) c = (char)tolower((unsigned char)c);
            if (vs == PWS_OK && (lowOut.find("false") != std::string::npos || wout.find(L"false") != std::wstring::npos || wout.find(L"ложь") != std::wstring::npos))
                r->verified = true;
            else
                r->detail = L"Команда выполнена, но фактическое состояние подтвердить не удалось (Get-MMAgent).";
        } else if (st == PWS_TIMEOUT) {
            r->detail = L"Команда не завершилась за отведённое время. Процесс НЕ принуждался; результат не подтверждён — проверьте Get-MMAgent вручную.";
        } else {
            r->detail = L"Ошибка выполнения (статус " + std::to_wstring((int)st) + L", exit=" + std::to_wstring(ec) + L").";
        }
        // Async lifetime: если окно умерло или PostMessage не прошёл — сбрасываем busyJob и чистим сами
        bool posted = false;
        if (!App::appClosing.load() && App::hwnd) {
            posted = (PostMessageW(App::hwnd, WM_APP_JOBRESULT, (WPARAM)r, 0) != FALSE);
        }
        if (!posted) {
            App::busyJob.store(false);
            delete r;
        }
    }).detach();
}

// Отключение Windows Widgets (Win11). Widgets.exe + WidgetService.exe
// потребляют 200-400 МБ RAM в фоне. Ниже 16 ГБ RAM это критично.
void T_DisableWidgets() {
    SetDwordAutoGroup(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0, L"Отключение Widgets");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"TaskbarDa", 0, L"Отключение Widgets");
    RunCmd(L"/c taskkill /f /im Widgets.exe & taskkill /f /im WidgetService.exe");
    MessageBoxW(App::hwnd,
        L"Windows Widgets отключены.\n"
        L"Фоновые процессы Widgets.exe и WidgetService.exe остановлены.\n"
        L"Иконка виджетов убрана с панели задач.", L"LEFTIK Widgets", MB_OK);
}

// TCP Autotuning = restricted. На Wi-Fi/ADSL снижает джиттер пинга без потери скорости.
void T_TcpAutotuning() {
    if (App::busyJob.load(std::memory_order_relaxed)) {
        for (auto& tw : App::tweaksList) if (tw.id == 1122) tw.isEnabled = false;
        SaveTweakState(1122, false);
        InvalidateRect(App::hwnd, nullptr, FALSE);
        MessageBoxW(App::hwnd, L"Дождитесь завершения предыдущей операции.", L"LEFTIK", MB_OK);
        return;
    }
    App::busyJob.store(true);
    Journal::LogAction(L"TCP Autotuning → restricted", L"netsh int tcp set global autotuninglevel=normal");
    std::thread([]() {
        AsyncJobResult* r = new AsyncJobResult(); r->jobId = 2;
        // Предыдущее значение — попытка (подписи зависят от локализации); попадает в отчёт
        std::string prev = RunCaptureStdout(L"C:\\Windows\\System32\\netsh.exe int tcp show global", 10000);
        {
            std::string low; for (char c : prev) low += (char)tolower((unsigned char)c);
            size_t p = low.find("autotun");
            if (p == std::string::npos) p = low.find("\xd0\xb0\xd0\xb2\xd1\x82\xd0\xbe"); // "авто" в utf8 — русская сборка
            if (p == std::string::npos) p = low.find("автонастро");
            if (p != std::string::npos) {
                size_t colon = prev.find(':', p);
                if (colon != std::string::npos) {
                    std::string val = prev.substr(colon + 1, 24);
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\r' || val.front() == '\n' || val.front() == '\t')) val.erase(val.begin());
                    size_t e = val.find_first_of("\r\n\t ");
                    if (e != std::string::npos) val.erase(e);
                    if (!val.empty()) r->detail = L"Было: " + std::wstring(val.begin(), val.end()) + L". ";
                }
            }
        }
        DWORD ec = 0;
        ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\netsh.exe", L"int tcp set global autotuninglevel=restricted", 20000, ec);
        r->status = (int)st;
        if (st == PWS_OK && ec == 0) {
            std::string now = RunCaptureStdout(L"C:\\Windows\\System32\\netsh.exe int tcp show global", 10000);
            std::string low; for (char c : now) low += (char)tolower((unsigned char)c);
            std::wstring wnow = Util::FromUtf8(now);
            std::transform(wnow.begin(), wnow.end(), wnow.begin(), ::towlower);
            // Также проверяем OEM CP866 и CP1251 для русской локали ("огранич")
            auto hasSubstrInCodepage = [&](UINT cp, const wchar_t* needle) -> bool {
                int len = MultiByteToWideChar(cp, 0, now.c_str(), (int)now.size(), nullptr, 0);
                if (len <= 0) return false;
                std::wstring ws(len, 0);
                MultiByteToWideChar(cp, 0, now.c_str(), (int)now.size(), &ws[0], len);
                std::transform(ws.begin(), ws.end(), ws.begin(), ::towlower);
                return ws.find(needle) != std::wstring::npos;
            };
            if (low.find("restricted") != std::string::npos ||
                wnow.find(L"огранич") != std::wstring::npos ||
                hasSubstrInCodepage(CP_OEMCP, L"огранич") ||
                hasSubstrInCodepage(1251, L"огранич")) {
                r->verified = true;
            } else if (r->detail.empty()) {
                r->detail = L"Команда выполнена, подтвердить значение не удалось.";
            }
        } else if (st == PWS_TIMEOUT) {
            r->detail += L"Команда не завершилась за отведённое время; процесс НЕ принуждался. Проверьте: netsh int tcp show global";
        } else {
            r->detail += L"Ошибка выполнения (статус " + std::to_wstring((int)st) + L", exit=" + std::to_wstring(ec) + L"). Проверьте: netsh int tcp show global";
        }
        // Async lifetime: если окно умерло или PostMessage не прошёл — сбрасываем busyJob и чистим сами
        bool posted = false;
        if (!App::appClosing.load() && App::hwnd) {
            posted = (PostMessageW(App::hwnd, WM_APP_JOBRESULT, (WPARAM)r, 0) != FALSE);
        }
        if (!posted) {
            App::busyJob.store(false);
            delete r;
        }
    }).detach();
}

// --- Блок L/M/N: игровые, privacy и сервисные фишки ---
void T_DisableGameMode() {
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 0, L"Отключение Game Mode");
    SetDwordAutoGroup(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", 0, L"Отключение Game Mode");
}

void T_DeferUpdates() {
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"DeferFeatureUpdates", 1, L"Отложить обновления");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"DeferFeatureUpdatesPeriodInDays", 365, L"Отложить обновления");
}

void T_TabletInputService() { DisableSvc(L"TabletInputService"); }

void T_CloudClipboard() {
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Clipboard", L"EnableCloudClipboard", 0, L"Облачный буфер обмена");
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Clipboard", L"EnableClipboardHistory", 0, L"История буфера");
}

bool F_StoreFix() {
    Journal::LogAction(L"Фикс Microsoft Store (Platinum)");
    // Кавычки внутри PowerShell-команды: InstallLocation может содержать пробелы.
    if (!RunCmdVisible(L"/c wsreset -i & powershell -NoProfile -Command \"Get-AppxPackage -AllUsers *WindowsStore* | Foreach {Add-AppxPackage -DisableDevelopmentMode -Register \\\"$($_.InstallLocation)\\AppxManifest.xml\\\"}\" & echo. & echo ГОТОВО & pause")) {
        MessageBoxW(App::hwnd,
            L"Не удалось запустить фикс Store (cmd.exe не стартовал).\n"
            L"Подробности — в журнале LEFTIK.",
            L"LEFTIK Store Fix", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void F_DiscordFix() {
    Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Discord", L"HardwareAcceleration", 0, L"Discord Fix");
    MessageBoxW(App::hwnd,
        L"Аппаратное ускорение Discord отключено.\nПерезапустите Discord, чтобы починить демонстрацию экрана.",
        L"LEFTIK Discord Fix", MB_OK);
}

bool F_AudioFix() {
    Journal::LogAction(L"Фикс звука xaudio2_7 (GTA/FiveM)");
    if (!RunCmdVisible(L"/c regsvr32 /s xaudio2_7.dll & echo. & sfc /scannow & echo. & echo ГОТОВО & pause")) {
        MessageBoxW(App::hwnd,
            L"Не удалось запустить консоль для Audio Fix.",
            L"LEFTIK Audio Fix", MB_OK | MB_ICONERROR);
        return false;
    }
    MessageBoxW(App::hwnd,
        L"Перерегистрация xaudio2_7 и sfc /scannow запущены в отдельном окне.\n"
        L"SFC может выполняться 10-30 минут — дождитесь надписи «ГОТОВО».",
        L"LEFTIK Audio Fix", MB_OK);
    return true;
}

bool F_CompactOS() {
    if (MessageBoxW(App::hwnd,
        L"Сжать системные файлы Windows для экономии 2-4 ГБ на диске C?\nНа SSD это почти не скажется на скорости.",
        L"LEFTIK CompactOS", MB_YESNO | MB_ICONQUESTION) != IDYES) return false;

    Journal::LogAction(L"CompactOS: сжатие системных файлов");
    if (!RunCmdVisible(L"/c compact.exe /compactos:always & echo. & echo ГОТОВО & pause")) {
        MessageBoxW(App::hwnd,
            L"Не удалось запустить compact.exe (cmd.exe не стартовал).",
            L"LEFTIK CompactOS", MB_OK | MB_ICONERROR);
        return false;
    }
    MessageBoxW(App::hwnd,
        L"Команда CompactOS запущена в отдельном окне.\n"
        L"Сжатие может занять несколько минут; прогресс — в консоли.",
        L"LEFTIK CompactOS", MB_OK | MB_ICONINFORMATION);
    return true;
}

// --- Блок O: мега-фиксы Platinum+ ---
void F_FixDiscordShare() {
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"OverlaySupported", 1, L"Discord Fix");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"ForceDirectFlip", 0, L"Discord Fix");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"EnableDirtyRectangles", 1, L"Discord Fix");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\Scheduler", L"EnablePreemption", 1, L"Discord Fix");
    MessageBoxW(App::hwnd, L"Фикс демонстрации экрана Discord применён.\nПерезапустите Discord.", L"LEFTIK", MB_OK);
}

void F_FixFiveMAudio() {
    // Удаление settings.xml GTA V с бэкапом (не перезаписывая существующий .bak)
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &docs))) {
        std::wstring gtaSettings = std::wstring(docs) + L"\\Rockstar Games\\GTA V\\settings.xml";
        if (Util::FileExists(gtaSettings)) {
            BOOL copied = CopyFileW(gtaSettings.c_str(), (gtaSettings + L".bak").c_str(), TRUE);
            if (!copied) {
                DWORD ce = GetLastError();
                // Старый .bak уже существует — он и есть резервная копия.
                if (ce == ERROR_FILE_EXISTS || ce == ERROR_ALREADY_EXISTS) copied = TRUE;
            }
            // Удаляем настройки только при наличии резервной копии.
            if (copied) DeleteFileW(gtaSettings.c_str());
        }
        CoTaskMemFree(docs);
    }
    // Очистка кэша FiveM
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData))) {
        DeleteFolderRecursive(std::wstring(localAppData) + L"\\FiveM\\FiveM.app\\data\\cache");
        CoTaskMemFree(localAppData);
    }
    // Проверка DirectX
    if (!Util::FileExists(L"C:\\Windows\\System32\\xaudio2_7.dll")) {
        RunCmd(L"/c powershell -NoProfile -Command \"Invoke-WebRequest -Uri 'https://download.microsoft.com/download/1/7/1/1718CCC4-6316-448C-9E6A-110F0E163452/dxwebsetup.exe' -OutFile '%TEMP%\\dxwebsetup.exe'\" & \"%TEMP%\\dxwebsetup.exe\"");
    }
    MessageBoxW(App::hwnd, L"Кэш FiveM очищен, настройки GTA V сброшены.", L"LEFTIK", MB_OK);
}

void F_FixGta5Crash() {
    std::wstring mm = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, mm, L"AllocationPreference");
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, mm, L"AvoidLargePageCollisions");
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, mm, L"LargePageAlways");
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, mm, L"DisableMemoryCompression");
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, mm, L"DisablePageCombining");
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, mm, L"EnablePageCombining");
    MessageBoxW(App::hwnd, L"Агрессивные настройки памяти сброшены. Вылеты в GTA 5 должны прекратиться.", L"LEFTIK", MB_OK);
}

bool F_FixPrintSpooler() {
    if (MessageBoxW(App::hwnd, L"Очередь печати будет удалена. Продолжить?", L"LEFTIK Print Spooler", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return false;
    // v58.0: PowerShell с -ErrorAction, а не del /S. Удаляет только файлы очереди,
    // не сносит подпапки и не падает на залоченных файлах.
    RunCmd(L"/c net stop spooler & sc config spooler start= auto & powershell -NoProfile -ExecutionPolicy Bypass -Command \"Remove-Item -Path 'C:\\Windows\\System32\\Spool\\Printers\\*.*' -Force -ErrorAction SilentlyContinue\" & net start spooler");
    MessageBoxW(App::hwnd, L"Служба печати восстановлена, очередь очищена.", L"LEFTIK", MB_OK);
    return true;
}

void F_FixXboxServices() {
    const wchar_t* svcs[] = {L"XblAuthManager", L"XblGameSave", L"XboxNetApiSvc", L"XboxGipSvc"};
    for (auto svc : svcs) {
        std::wstring path = L"SYSTEM\\CurrentControlSet\\Services\\"; path += svc;
        // Не создаём ключи для служб, которых нет в этой редакции Windows.
        HKEY probe{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE, &probe) != ERROR_SUCCESS) continue;
        RegCloseKey(probe);
        Journal::SetDword(HKEY_LOCAL_MACHINE, path, L"Start", 3, L"Фикс Xbox");
    }
    MessageBoxW(App::hwnd, L"Службы Xbox переведены в ручной режим. Game Pass должен заработать.", L"LEFTIK", MB_OK);
}

void F_FixWebView2() {
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\EdgeUpdate");
    RunCmd(L"/c winget install -e --id Microsoft.EdgeWebView2Runtime --accept-source-agreements --accept-package-agreements");
    MessageBoxW(App::hwnd, L"Команда переустановки WebView2 отправлена.", L"LEFTIK", MB_OK);
}

void F_RevertDwmTweaks() {
    Journal::DeleteValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"OverlayTestMode");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"ForceDirectFlip", 0, L"Откат DWM");
    Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"DisableDirectFlip", 0, L"Откат DWM");
    MessageBoxW(App::hwnd, L"Настройки DWM сброшены к стандартным.", L"LEFTIK", MB_OK);
}

// Проверка активного анти-чита. Если что-то найдено — твики, меняющие
// приоритеты/ядро, могут быть расценены как подозрительная активность.
bool IsAntiCheatActive(std::wstring* detectedName) {
    struct AC { const wchar_t* exe; const wchar_t* name; };
    static const AC list[] = {
        {L"vgc.exe",               L"Vanguard (Valorant)"},
        {L"vgtray.exe",            L"Vanguard Tray"},
        {L"EasyAntiCheat.exe",     L"EasyAntiCheat"},
        {L"EasyAntiCheat_EOS.exe", L"EAC (Epic)"},
        {L"BEService.exe",         L"BattlEye"},
        {L"BEClient_x64.exe",      L"BattlEye Client"},
        {L"FaceitClient.exe",      L"FACEIT"},
        {L"ESEA.exe",              L"ESEA"},
        {L"GameGuard.des",         L"nProtect GameGuard"},
        {L"xigncode.exe",          L"XignCode3"},
    };
    for (const auto& ac : list) {
        if (IsProcessRunning(ac.exe)) {
            if (detectedName) *detectedName = ac.name;
            return true;
        }
    }
    return false;
}

// Хелпер: применить одиночный твик по ID (для Турбо).
static bool ApplyTweakById(int id) {
    const int idx = TweakIndexById(id);
    if (idx < 0) return false;

    // 1. Ставим «включаем» ДО вызова T_*: внутренний SetTweakStateById(false)
    //    от T_Edge/T_VBS/F_WU/T_DisableDynamicTick/RunCriticalProcess честно перетрёт.
    App::tweaksList[idx].isEnabled = true;
    SaveTweakState(id, true);

    switch (id) {
        case 1000: T_GameDVR(); break;
        case 1002: T_Priority(); break;
        case 1008: T_MouseBuffer(); break;
        case 1017: T_VBS(); break;
        case 1030: T_QoS(); break;
        case 1040: T_SysMain(); break;
        case 1041: T_WSearch(); break;
        case 1043: T_Xbox(); break;
        case 1050: T_Telemetry(); break;
        case 1051: T_AdID(); break;
        case 1052: T_Location(); break;
        case 1071: T_MenuShowDelay(); break;
        case 1072: T_StartupDelay(); break;
        case 1074:
            if (!T_HwSchModeSilent()) {
                App::tweaksList[idx].isEnabled = false;
                SaveTweakState(id, false);
                return false;
            }
            break;
        case 1094: T_NetworkThrottling(); break;
        case 1095: T_SystemResponsiveness(); break;
        case 1096: T_DisableTransparency(); break;
        case 1097: T_EnableGpuMsiMode(); break;
        case 1098: T_MmcssGamesProfile(); break;
        case 1099: T_RawInputBoost(); break;
        case 1102: T_CpuUnparkAndBoost(); break;
        case 1103: T_DwmMpoFix(); break;
        case 1121: T_DisableWidgets(); break;
        case 1130: T_DisableGameMode(); break;
        case 1133: T_DisableDynamicTick(); break;
        default:
            App::tweaksList[idx].isEnabled = false;
            SaveTweakState(id, false);
            return false;
    }

    // 2. Если T_* уже сбросил isEnabled через SetTweakStateById(id,false) — уважаем.
    if (!App::tweaksList[idx].isEnabled) return false;

    // 3. Реестровые твики сверяем с живым реестром. Cache-only вернёт только что
    //    записанный true — это и есть успех.
    if (!ReadTweakState(id)) {
        App::tweaksList[idx].isEnabled = false;
        SaveTweakState(id, false);
        return false;
    }
    return true;
}

// 2a.2: состав ТУРБО вынесен в чистую функцию, чтобы его можно было проверить
// в state-machine harness без реального применения твиков.
std::vector<int> BuildTurboTweakList() {
    std::vector<int> tweaks = { 1000, 1002, 1008, 1030, 1071, 1072, 1094, 1095, 1096, 1130, 1040, 1041, 1043, 1050, 1051, 1052 };
    if (App::g_hardwareTier != App::HardwareTier::Low) {
        tweaks.insert(tweaks.end(), { 1017, 1097, 1098, 1099, 1103 });
    }
    if (App::g_hardwareTier == App::HardwareTier::High && !App::isLaptop) {
        tweaks.insert(tweaks.end(), { 1102, 1133 });
    }
    // HAGS: на Win11 подтверждён 1 Гц stutter (Ryzen + NVIDIA).
    // Турбо не включает твик с известной регрессией. Ручной тумблер остаётся.
    if (App::hwInfo.winBuild < 22000) tweaks.push_back(1074); // HAGS
    if (App::hwInfo.winBuild >= 22000) tweaks.push_back(1121); // Widgets
    return tweaks;
}

// ТУРБО-РЕЖИМ: выжимает все соки.
void ApplyTurboBoost() {
    if (App::busyJob.exchange(true, std::memory_order_acq_rel)) {
        MessageBoxW(App::hwnd, L"Дождитесь завершения предыдущей фоновой операции.", L"LEFTIK", MB_OK | MB_ICONINFORMATION);
        return;
    }
    struct BusyGuard { ~BusyGuard() { App::busyJob.store(false, std::memory_order_release); } } busyGuard;

    std::wstring acName;
    if (IsAntiCheatActive(&acName)) {
        MessageBoxW(App::hwnd, (L"🚫 ТУРБО-РЕЖИМ НЕДОСТУПЕН\n\nОбнаружен активный анти-чит: " + acName + L"\n\nТурбо применяет VBS off и MSI Mode — риск БАНА.\nЗакройте анти-чит и повторите.").c_str(), L"LEFTIK — Защита", MB_OK | MB_ICONERROR);
        return;
    }
    if (App::isMaxActive || App::isUltraActive || App::turboBoostActive) {
        MessageBoxW(App::hwnd, L"Активен другой профиль. Сначала выключите его.", L"LEFTIK ТУРБО", MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<int> tweaks = BuildTurboTweakList();

    std::vector<int> allowed, skipped;
    for (int id : tweaks) {
        if (IsTweakAllowedForHardware(id)) allowed.push_back(id);
        else skipped.push_back(id);
    }

    int needReboot = 0;
    for (int id : allowed) {
        int idx = TweakIndexById(id);
        if (idx >= 0 && App::tweaksList[idx].requiresReboot) needReboot++;
    }

    const wchar_t* xboxNote = App::hwInfo.xboxModeAvailable
        ? L"\n\nПРИМЕЧАНИЕ: Xbox mode Windows доступен (Win11 24H2+ и приложение Xbox). "
          L"LEFTIK его не дублирует и не включает. ТУРБО отключает службы Xbox Live "
          L"(твик 1043) — если пользуетесь Xbox mode или приложением Xbox, после Турбо "
          L"включите их обратно."
        : L"";
    const wchar_t* hagsPart = (App::hwInfo.winBuild < 22000) ? L"HAGS, " : L"";

    wchar_t msg[1800];
    swprintf(msg, 1800,
        L"⚡ ТУРБО-РЕЖИМ\n\nПрименяет %d твиков:\n  • Деблоат (SysMain, Xbox, Телеметрия)\n  • Приоритеты (MSI Mode, MMCSS, CSRSS)\n  • VBS off, %lsБуфер мыши\n  • Закрытие фоновых приложений\n  • Очистка standby и таймер 0.5ms\n\n⚠️ Из них %d требуют ПЕРЕЗАГРУЗКУ ПК.\n\nПропущено (Hardware Guard): %d\n\nПродолжить?%ls",
        (int)allowed.size(), hagsPart, needReboot, (int)skipped.size(), xboxNote);

    if (MessageBoxW(App::hwnd, msg, L"LEFTIK ТУРБО-РЕЖИМ", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    if (MessageBoxW(App::hwnd, L"Создать точку восстановления перед применением?", L"LEFTIK ТУРБО", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        RunPowerShell(L"Enable-ComputerRestore -Drive 'C:\\'");
        Sleep(300);
        RunPowerShell(L"Checkpoint-Computer -Description 'LEFTIK Turbo' -RestorePointType 'MODIFY_SETTINGS'");
        Sleep(2000);
    }

    HCURSOR hOld = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    KillBackgroundApps();
    bool timerOk = TimerMgr::Acquire(L"turbo-boost");
    MemoryOptimizer::ClearStandbyList();
    ClearAllShaderCaches();

    int applied = 0, failed = 0;
    {
        App::activeGroupId = L"turbo-boost";
        struct GroupGuard { ~GroupGuard() { App::activeGroupId.clear(); } } grpGuard;
        for (int id : allowed) {
            if (ApplyTweakById(id)) applied++;
            else failed++;
        }
    }

    for (auto& t : App::tweaksList) t.isEnabled = ReadTweakState(t.id);
    App::turboBoostActive = true;
    Monitor::InvalidateGroupsCache();
    InvalidateRect(App::hwnd, nullptr, FALSE);
    SetCursor(hOld);

    wchar_t report[1024];
    swprintf(report, 1024,
        L"⚡ ТУРБО-РЕЖИМ АКТИВИРОВАН\n\nПрименено: %d из %d\nПровалено: %d\nПропущено: %d\n\nТаймер 0.5ms: %ls\n\n⚠️ ПЕРЕЗАГРУЗИТЕ ПК для активации VBS off, MSI Mode и CPU Unpark.",
        applied, (int)allowed.size(), failed, (int)skipped.size(), timerOk ? L"включён" : L"НЕ включён");
    MessageBoxW(App::hwnd, report, L"LEFTIK ТУРБО", MB_OK | MB_ICONINFORMATION);

    if (MessageBoxW(App::hwnd, L"Перезагрузить ПК сейчас?", L"LEFTIK ТУРБО", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        RunCmd(L"/c shutdown /r /t 10 /c \"LEFTIK Turbo: перезагрузка\"");
    }
}

// --- Блок G: максимальный игровой профиль одним нажатием ---
void ApplyMagicBoost() {
    if (App::busyJob.exchange(true, std::memory_order_acq_rel)) {
        MessageBoxW(App::hwnd, L"Дождитесь завершения предыдущей фоновой операции.", L"LEFTIK", MB_OK | MB_ICONINFORMATION);
        return;
    }
    struct BusyGuard { ~BusyGuard() { App::busyJob.store(false, std::memory_order_release); } } busyGuard;

    // Защита от бана: проверяем запущенные анти-читы перед модификацией приоритетов/ядра
    std::wstring acName;
    if (IsAntiCheatActive(&acName)) {
        MessageBoxW(App::hwnd,
            (L"🚫 ОБНАРУЖЕН АНТИ-ЧИТ: " + acName + L"\n\n"
             L"MAX FPS включает твики (CSRSS priority, MSI Mode, VBS Off),\n"
             L"которые могут быть расценены как подозрительная активность.\n\n"
             L"РИСК БАНА АККАУНТА.\n\n"
             L"Закройте анти-чит, перезагрузите ПК и повторите.\n"
             L"Профиль НЕ применён.").c_str(),
            L"LEFTIK — Защита от бана", MB_OK | MB_ICONERROR);
        return;
    }

    // v58.0: предотвращаем конфликт профилей. Раньше MAX FPS применялся поверх ULTRA/PRO
    // и ломал откат.
    if (App::isMaxActive || App::isUltraActive || App::turboBoostActive) {
        MessageBoxW(App::hwnd,
            L"Активен другой профиль (PRO Высокий, ULTRA или ТУРБО).\n"
            L"Сначала выключите его — одновременная работа нескольких профилей запрещена.",
            L"LEFTIK MAX FPS", MB_OK | MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(App::hwnd,
        L"MAX FPS (1-КЛИК) — максимальный киберспортивный профиль.\n\n"
        L"Будут применены твики: GameDVR Off, Win32Priority, HAGS (RTX20+),\n"
        L"PowerThrottling Off, Game Mode Off, GPU MSI Mode, MMCSS Games, MPO Fix и таймер 0.5ms.\n\n"
        L"ВНИМАНИЕ: фоновые приложения (браузер, лаунчеры) будут закрыты —\n"
        L"несохранённые данные могут быть потеряны.\n\n"
        L"Изменения ядра/GPU вступят в силу в полную мощь после перезагрузки.\n\n"
        L"Продолжить?",
        L"LEFTIK MAX FPS", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    MemoryOptimizer::ClearStandbyList();
    KillBackgroundApps();
    bool timerOk = TimerMgr::Acquire(L"magic-boost");
    ClearDXCache();

    int applied = 0, failed = 0, skipped = 0;
    // Убрано: 1094 (NetworkThrottling — не FPS), 1017 (VBS — риск, эффект только на корпоративных),
    // 1104 (Kernel Lock — плацебо на 64-bit), 1102 (CPU Unpark — риск на ноутах)
    // Оставлено то, что реально даёт FPS/латентность:
    const int magicTweaks[] = {
        1000,   // GameDVR off — +2-5% если было включено
        1002,   // Win32Priority — +1-3%
        1074,   // HAGS — +0-5% на RTX20+, блокируется на GTX10-
        1021,   // PowerThrottling off — +1-3%
        1130,   // Game Mode off — стабильнее фреймтайм
        1097,   // MSI Mode — ниже DPC latency (не FPS, но input)
        1098,   // MMCSS Games — приоритет GPU/CPU для игр
        1103,   // MPO off — убирает фризы в оконном режиме
    };
    {
        App::activeGroupId = L"magic-boost";
        struct GroupGuard { ~GroupGuard() { App::activeGroupId.clear(); } } grpGuard;
        for (int id : magicTweaks) {
            // 2a.2: та же причина, что и в ТУРБО — на Win11 HAGS даёт 1 Гц stutter
            // на части Ryzen+NVIDIA. Профиль не включает его молча; ручной тумблер есть.
            if (id == 1074 && App::hwInfo.winBuild >= 22000) continue;
            if (!IsTweakAllowedForHardware(id)) { skipped++; continue; }
            bool ok = true;
            switch (id) {
                case 1000: T_GameDVR(); break;
                case 1002: T_Priority(); break;
                case 1074: ok = T_HwSchModeSilent(); break;
                case 1021: T_PowerThrot(); break;
                case 1130: T_DisableGameMode(); break;
                case 1097: T_EnableGpuMsiMode(); break;
                case 1098: T_MmcssGamesProfile(); break;
                case 1103: T_DwmMpoFix(); break;
            }
            // v58.0 / v59.2: честный подсчёт — проверяем, что твик реально применился
            if (ok && ReadTweakState(id)) {
                SaveTweakState(id, true);
                applied++;
            } else {
                SaveTweakState(id, false);
                failed++;
            }
        }
    }

    App::magicBoostActive = true;
    Monitor::InvalidateGroupsCache();
    for (auto& t : App::tweaksList) t.isEnabled = ReadTweakState(t.id);
    InvalidateRect(App::hwnd, nullptr, FALSE);

    const wchar_t* hagsReportLine = (App::hwInfo.winBuild < 22000)
        ? L"  • HAGS (RTX20+): +0-5%\n"
        : L"";
    wchar_t report[768];
    swprintf(report, 768,
        L"Игровой профиль применён: %d из %d (провалено: %d, пропущено: %d).\n"
        L"%ls\n"
        L"Что реально даёт FPS:\n"
        L"  • GameDVR off: +2-5%%\n"
        L"  • Win32Priority: +1-3%%\n"
        L"%ls"
        L"  • PowerThrottling off: +1-3%%\n"
        L"  • Game Mode off: стабильнее\n"
        L"  • MSI Mode: -0.5ms DPC (не FPS)\n"
        L"  • MPO off: убирает фризы в borderless\n\n"
        L"Максимальный суммарный эффект: +10-15%% в лучшем случае.\n"
        L"Основной FPS в CS2/Dota даёт САМА ИГРА (настройки видео, разрешение).\n\n"
        L"⚠️ Для максимума FPS: используйте игровые профили CS2/Dota/Arizona,\n"
        L"а не только MAX FPS.",
        applied, applied + failed + skipped, failed, skipped,
        timerOk ? L"ОЗУ очищена, кэш шейдеров сброшен, таймер 0.5ms включен."
                : L"ОЗУ очищена, но таймер 0.5ms включить НЕ удалось.",
        hagsReportLine);
    MessageBoxW(App::hwnd, report, L"LEFTIK MAX FPS", MB_OK);
    InvalidateRect(App::hwnd, nullptr, FALSE);
}

// Полный откат твика к дефолту. Симметричен T_* / F_* — то, что включает тумблер, выключатель возвращает назад.
void RevertTweak(int id) {
    switch (id) {
        // --- BASIC ---
        case 1000:
            Journal::SetDword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_Enabled", 1, L"GameDVR (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 0, L"GameDVR (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 1, L"GameDVR (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"HistoricalCaptureEnabled", 1, L"GameDVR (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"UseNexusForGameBarEnabled", 1, L"GameDVR (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"ShowStartupPanel", 1, L"GameDVR (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1, L"GameDVR (revert)");
            break;
        case 1001:
            Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseSpeed", L"1", L"Мышь (revert)");
            Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold1", L"6", L"Мышь (revert)");
            Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Mouse", L"MouseThreshold2", L"10", L"Мышь (revert)");
            break;
        case 1002: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation", 2, L"Win32Priority (revert)"); break;
        case 1003: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\CrossDevice", L"CrossDeviceResumeEnabled", 1, L"Cross-Device (revert)"); break;
        case 1004: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SvcHostSplitThresholdInKB", 0x400000, L"svchost (revert)"); break;
        case 1005: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisable8dot3NameCreation", 2, L"NTFS 8.3 (revert)"); break;
        case 1006: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"NtfsDisableLastAccessUpdate", 0x80000003, L"NTFS LastAccess (revert)"); break;
        case 1007: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\Maintenance", L"MaintenanceDisabled", 0, L"Автообслуживание (revert)"); break;
        case 1008: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\mouclass\\Parameters", L"MouseDataQueueSize", 100, L"Буфер мыши (revert)"); break;
        case 1070: Journal::DeleteValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"LEFTIK_Optimizer", L"Автозагрузка (revert)"); break;
        case 1071: Journal::SetString(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay", L"400", L"MenuShowDelay (revert)"); break;
        case 1072: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 1, L"StartupDelay (revert)"); break;
        case 1074: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", 1, L"HAGS (revert)"); break;
        case 1075: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management\\PrefetchParameters", L"EnablePrefetcher", 3, L"Prefetcher (revert)"); break;
        case 1076: Journal::SetDword(HKEY_CURRENT_USER, L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 0, L"FSE (revert)"); break;

        // --- SECURITY ---
        case 1010: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", L"DisableAntiSpyware", 0, L"Defender (revert)"); break;
        case 1011: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", 1, L"UAC (revert)"); break;
        case 1012:
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\wscsvc", L"Start", 2, L"Центр безопасности (revert)");
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\SecurityHealthService", L"Start", 3, L"Центр безопасности (revert)");
            break;
        case 1013: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableSmartScreen", 1, L"SmartScreen (revert)"); break;
        case 1014: {
            DWORD ec = 0;
            ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\netsh.exe",
                                               L"advfirewall set allprofiles state on", 20000, ec);
            if (st != PWS_OK || ec != 0) {
                MessageBoxW(App::hwnd,
                    L"Не удалось вернуть брандмауэр во включённое состояние.\n"
                    L"Проверьте Windows Security вручную.", L"LEFTIK", MB_OK | MB_ICONERROR);
                SetTweakStateById(1014, true);
            }
            break;
        }
        case 1015: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Script\\Settings", L"AmsiEnable", 1, L"AMSI (revert)"); break;
        case 1016: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI", L"VulnerableDriverBlocklistEnable", 1, L"CodeInteg (revert)"); break;
        case 1017: {
            DWORD ec = 0;
            ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\bcdedit.exe",
                                               L"/set hypervisorlaunchtype auto", 20000, ec);
            if (st != PWS_OK || ec != 0) {
                MessageBoxW(App::hwnd,
                    L"Не удалось вернуть hypervisorlaunchtype auto.\n"
                    L"VBS остаётся отключённым — исправьте вручную через bcdedit.",
                    L"LEFTIK", MB_OK | MB_ICONERROR);
                SetTweakStateById(1017, true);
            }
            break;
        }
        case 1018:
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", L"Enabled", 1, L"HVCI (revert)");
            break;

        // --- POWER ---
        case 1020: {
            // Приоритет: registry-persisted GUID, fallback на in-memory,
            // затем стандартная сбалансированная схема Windows.
            std::wstring prev = Journal::LoadPrevPowerScheme();
            if (prev.empty()) prev = s_prevPowerSchemeGuid;
            if (prev.empty()) prev = L"381b4222-f694-41f0-9685-ff5bb260df2e";
            DWORD ec = 0;
            ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\powercfg.exe", L"/setactive " + prev, 4000, ec);
            if (st == PWS_OK && ec == 0) {
                s_prevPowerSchemeGuid.clear();
                Journal::ClearPrevPowerScheme();
            } else {
                MessageBoxW(App::hwnd,
                    L"Не удалось активировать предыдущую схему питания.\n"
                    L"Сохранённый GUID в PrevPowerScheme не удалён.",
                    L"LEFTIK Power", MB_OK | MB_ICONWARNING);
            }
            break;
        }
        case 1021: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling", L"PowerThrottlingOff", 0, L"PowerThrottling (revert)"); break;
        case 1023: Journal::LogAction(L"Гибернация (revert)", L"powercfg -h on"); RunCmd(L"/c C:\\Windows\\System32\\powercfg.exe -h on"); break;
        case 1024: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\USB", L"DisableSelectiveSuspend", 0, L"USB Suspend (revert)"); break;
        case 1073: RevertTweak(1021); break; // legacy-алиас к 1021

        // --- NETWORK ---
        case 1030: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Psched", L"NonBestEffortLimit", 20, L"QoS (revert)"); break;
        case 1031: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters", L"DisabledComponents", 0, L"IPv6 (revert)"); break;
        case 1032: {
            const wchar_t* baseI = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
            HKEY hIf;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseI, 0, KEY_READ, &hIf) == ERROR_SUCCESS) {
                wchar_t name[128]; DWORD len = 128, idx = 0;
                for (;;) {
                    len = 128;
                    if (RegEnumKeyExW(hIf, idx, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                    std::wstring p = std::wstring(baseI) + L"\\" + name;
                    Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"TcpAckFrequency", 2, L"Nagle (revert)");
                    Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"TCPNoDelay", 0, L"Nagle (revert)");
                    Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"TcpDelAckTicks", 2, L"Nagle (revert)");
                    ++idx;
                }
                RegCloseKey(hIf);
            }
            break;
        }
        case 1033: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"EnableRSS", 1, L"RSS (revert)"); break;
        case 1034: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"DisableTaskOffload", 0, L"Checksum (revert)"); break;
        case 1035: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"DisableLargeSendOffload", 0, L"LSO (revert)"); break;
        case 1082: {
            // 1.3: этих значений нет в чистой Windows 10/11 — возвращаем систему
            // к состоянию без переопределений, а не к лимиту Windows XP (5000).
            const std::wstring tcpParams = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, tcpParams, L"MaxUserPort", L"MaxUserPort (revert)");
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, tcpParams, L"TcpTimedWaitDelay", L"TcpTimedWaitDelay (revert)");
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, tcpParams, L"Tcp1323Opts", L"Tcp1323Opts (revert)");
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, tcpParams, L"DefaultTTL", L"DefaultTTL (revert)");
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, tcpParams, L"TcpMaxDupAcks", L"TcpMaxDupAcks (revert)");
            break;
        }
        case 1094: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"NetworkThrottlingIndex", 10, L"NetworkThrottling (revert)"); break;
        case 1095: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"SystemResponsiveness", 20, L"SystemResponsiveness (revert)"); break;
        case 1096: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 1, L"Прозрачность (revert)"); break;
        case 1097: {
            const wchar_t* pciRoot = L"SYSTEM\\CurrentControlSet\\Enum\\PCI";
            HKEY hPci{};
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, pciRoot, 0, KEY_READ, &hPci) == ERROR_SUCCESS) {
                wchar_t devId[256]; DWORD devLen = 256, idx = 0;
                while (RegEnumKeyExW(hPci, idx++, devId, &(devLen = 256), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                    std::wstring devPath = std::wstring(pciRoot) + L"\\" + devId;
                    HKEY hDev{};
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, devPath.c_str(), 0, KEY_READ, &hDev) != ERROR_SUCCESS) continue;
                    wchar_t instId[256]; DWORD instLen = 256, idx2 = 0;
                    while (RegEnumKeyExW(hDev, idx2++, instId, &(instLen = 256), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                        std::wstring instPath = devPath + L"\\" + instId;
                        std::wstring svc = Util::ReadRegString(HKEY_LOCAL_MACHINE, instPath.c_str(), L"Service");
                        if (_wcsicmp(svc.c_str(), L"nvlddmkm") == 0 || _wcsicmp(svc.c_str(), L"amdkmdag") == 0 || _wcsicmp(svc.c_str(), L"amdwddmg") == 0) {
                            std::wstring msiPath = instPath + L"\\Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties";
                            Journal::SetDword(HKEY_LOCAL_MACHINE, msiPath, L"MSISupported", 0, L"GPU MSI Mode (revert)");
                        }
                    }
                    RegCloseKey(hDev);
                }
                RegCloseKey(hPci);
            }
            break;
        }
        case 1098: {
            const std::wstring key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"GPU Priority", 2, L"MMCSS Games (revert)");
            Journal::SetDword(HKEY_LOCAL_MACHINE, key, L"Priority", 2, L"MMCSS Games (revert)");
            Journal::SetString(HKEY_LOCAL_MACHINE, key, L"Scheduling Category", L"Medium", L"MMCSS Games (revert)");
            Journal::SetString(HKEY_LOCAL_MACHINE, key, L"SFIO Priority", L"Normal", L"MMCSS Games (revert)");
            break;
        }
        case 1099: {
            const std::wstring ifeo = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\csrss.exe\\PerfOptions";
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, ifeo, L"CpuPriorityClass", L"CSRSS Priority (revert)");
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, ifeo, L"IoPriority", L"CSRSS IO (revert)");
            break;
        }
        case 1102: {
            // 1.2: точный откат по снимку. Восстанавливаем AC-значения на той схеме,
            // которую изменил твик; DC твик не менял, поэтому /setdcvalueindex не нужен.
            PowerAdvSnapshot snap;
            std::wstring savedRaw = Journal::LoadPrevPowerAdv();
            bool haveSaved = !savedRaw.empty() && PowerAdvDecode(savedRaw, snap);
            bool restored = false;
            bool inexact = false;
            std::wstring target;

            if (haveSaved) {
                target = snap.scheme;
                restored =
                    PowerAdvApplyAc(target, L"CPMINCORES", snap.v.cpmAc) &&
                    PowerAdvApplyAc(target, L"PROCTHROTTLEMIN", snap.v.ptmAc) &&
                    PowerAdvApplyAc(target, L"PERFBOOSTMODE", snap.v.pbmAc) &&
                    PowerAdvApplyAc(target, L"PERFEPP", snap.v.eppAc) &&
                    PowerAdvActivateIfCurrent(target) &&
                    PowerAdvVerifyAc(target, snap.v);
            } else {
                // Снимка нет (старая версия, потерян ключ, ошибка чтения) — безопасный
                // дефолт Windows, но честно помечаем откат как неточный.
                target = ExtractFirstGuid(RunCaptureStdout(L"C:\\Windows\\System32\\powercfg.exe /getactivescheme", 4000));
                if (target.empty()) target = L"SCHEME_CURRENT";
                PowerAdvValues def;
                def.cpmAc = 5; def.ptmAc = 5; def.pbmAc = 0; def.eppAc = 50;
                restored =
                    PowerAdvApplyAc(target, L"CPMINCORES", def.cpmAc) &&
                    PowerAdvApplyAc(target, L"PROCTHROTTLEMIN", def.ptmAc) &&
                    PowerAdvApplyAc(target, L"PERFBOOSTMODE", def.pbmAc) &&
                    PowerAdvApplyAc(target, L"PERFEPP", def.eppAc) &&
                    PowerAdvActivateIfCurrent(target) &&
                    PowerAdvVerifyAc(target, def);
                inexact = true;
            }

            if (restored) {
                Journal::ClearPrevPowerAdv();
                Journal::LogAction(inexact
                    ? L"CPU Unpark (revert): снимка не было, применены дефолты Windows (неточный откат)"
                    : L"CPU Unpark (revert): исходные параметры восстановлены");
                if (inexact && !savedRaw.empty()) {
                    // Повреждённый снимок: предупреждаем, что откат неточный.
                    MessageBoxW(App::hwnd,
                        L"Сохранённые значения параметров питания повреждены.\n"
                        L"Применены стандартные значения Windows: CPMINCORES 5, PROCTHROTTLEMIN 5, PERFBOOSTMODE 0, PERFEPP 50.\n\n"
                        L"Откат неточный: если вы меняли эти параметры вручную, проверьте схему питания.",
                        L"LEFTIK Power", MB_OK | MB_ICONWARNING);
                }
            } else {
                if (haveSaved) {
                    Journal::LogAction(L"CPU Unpark (revert): восстановить параметры не удалось, снимок сохранён");
                    MessageBoxW(App::hwnd,
                        L"Не удалось полностью восстановить параметры питания.\n"
                        L"Снимок в HKCU\\Software\\LEFTIK\\PrevPowerAdv сохранён — откат можно повторить.",
                        L"LEFTIK Power", MB_OK | MB_ICONWARNING);
                } else if (!savedRaw.empty()) {
                    Journal::LogAction(L"CPU Unpark (revert): снимок повреждён, параметры не восстановлены");
                    MessageBoxW(App::hwnd,
                        L"Сохранённый снимок параметров питания повреждён.\n"
                        L"Проверьте схему питания вручную: автоматический откат невозможен.",
                        L"LEFTIK Power", MB_OK | MB_ICONWARNING);
                } else {
                    // Снимка нет (случайный вызов): без MessageBox, чтобы не спамить окнами.
                    Journal::LogAction(L"CPU Unpark (revert): снимка нет, стандартные значения применить не удалось");
                }
            }
            break;
        }
        case 1103:
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\DWM", L"OverlayTestMode", L"MPO (revert)");
            Journal::DeleteValue(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"DisableOverlays", L"MPO (revert)");
            break;
        case 1104: {
            const std::wstring mm = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
            Journal::SetDword(HKEY_LOCAL_MACHINE, mm, L"DisablePagingExecutive", 0, L"DisablePagingExecutive (revert)");
            break;
        }

        // --- DEBLOAT ---
        case 1040: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\SysMain", L"Start", 2, L"SysMain (revert)"); break;
        case 1041: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\WSearch", L"Start", 2, L"WSearch (revert)"); break;
        case 1043:
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\XblAuthManager", L"Start", 3, L"Xbox (revert)");
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\XblGameSave", L"Start", 3, L"Xbox (revert)");
            break;
        case 1042: /* OneDrive удалён — восстановление только вручную, лог отработает */ break;

        // --- PRIVACY ---
        case 1050: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 1, L"Телеметрия (revert)"); break;
        case 1051: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 1, L"РекламныйID (revert)"); break;
        case 1052: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\LocationAndSensors", L"DisableLocation", 0, L"Геолокация (revert)"); break;

        // --- SERVICES ---
        case 1060: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Spooler", L"Start", 2, L"Spooler (revert)"); break;
        case 1061: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\RemoteRegistry", L"Start", 3, L"RemoteRegistry (revert)"); break;
        case 1062: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Fax", L"Start", 3, L"Fax (revert)"); break;
        case 1063: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\WerSvc", L"Start", 3, L"WerSvc (revert)"); break;
        case 1064: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\bthserv", L"Start", 3, L"bthserv (revert)"); break;

        // --- NVIDIA / AMD ---
        case 1100: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\NVIDIA Corporation\\Global\\NVTweak", L"PowerMizerEnable", 0, L"NVIDIA PowerMizer (revert)"); break;
        case 1101: {
            DWORD ec = 0;
            ProcWaitStatus st = RunProcessWait(L"C:\\Windows\\System32\\sc.exe",
                                               L"config NvTelemetryContainer start= demand", 20000, ec);
            if (st != PWS_OK || ec != 0) {
                MessageBoxW(App::hwnd,
                    L"Не удалось вернуть NvTelemetryContainer в ручной запуск.\n"
                    L"Исправьте вручную: sc config NvTelemetryContainer start= demand",
                    L"LEFTIK", MB_OK | MB_ICONERROR);
                SetTweakStateById(1101, true);
            }
            break;
        }
        case 1110:
            for (int i = 0; i < 10; ++i) {
                std::wstring p = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\000" + std::to_wstring(i);
                HKEY k{};
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, p.c_str(), 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                    RegCloseKey(k);
                    Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"EnableUlps", 1, L"AMD ULPS (revert)");
                    Journal::SetDword(HKEY_LOCAL_MACHINE, p, L"EnableUlps_NA", 1, L"AMD ULPS (revert)");
                }
            }
            break;
        case 1111: RunCmd(L"/c sc config \"AMD Crash Defender Service\" start= demand"); break;

        // --- v57.12: новые фишки ---
        case 1120:
            if (App::busyJob.load(std::memory_order_relaxed)) {
                MessageBoxW(App::hwnd, L"Дождитесь завершения операции.", L"LEFTIK", MB_OK);
                return;
            }
            RunPowerShell(L"Enable-MMAgent -MemoryCompression");
            break;
        case 1121:
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 1, L"Вернуть Widgets");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"TaskbarDa", 1, L"Вернуть Widgets");
            break;
        case 1122:
            if (App::busyJob.load(std::memory_order_relaxed)) {
                MessageBoxW(App::hwnd, L"Дождитесь завершения операции.", L"LEFTIK", MB_OK);
                return;
            }
            RunCmd(L"/c C:\\Windows\\System32\\netsh.exe int tcp set global autotuninglevel=normal");
            break;

        // --- Блоки L/M/N ---
        case 1130:
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 1, L"Game Mode (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", 1, L"Game Mode (revert)");
            break;
        case 1131:
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"DeferFeatureUpdates", 0, L"Обновления (revert)");
            Journal::SetDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate", L"DeferFeatureUpdatesPeriodInDays", 0, L"Обновления (revert)");
            break;
        case 1065: Journal::SetDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\TabletInputService", L"Start", 3, L"TabletInputService (revert)"); break;
        case 1132:
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Clipboard", L"EnableCloudClipboard", 1, L"Буфер обмена (revert)");
            Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Clipboard", L"EnableClipboardHistory", 1, L"Буфер обмена (revert)");
            break;
        case 1133: RunCmd(L"/c C:\\Windows\\System32\\bcdedit.exe /deletevalue disabledynamictick"); break;
        case 1140: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"EnableGameOverlay", 1, L"Steam Lite (revert)"); break;
        case 2011: break; // Store Fix — одноразовая команда, отката нет
        case 2012: {
            if (!RunCmdVisible(L"/c compact.exe /compactos:never & echo. & echo ГОТОВО & pause")) {
                MessageBoxW(App::hwnd,
                    L"Не удалось запустить распаковку CompactOS (cmd.exe не стартовал).",
                    L"LEFTIK CompactOS", MB_OK | MB_ICONERROR);
                break;
            }
            MessageBoxW(App::hwnd,
                L"Распаковка CompactOS запущена в отдельном окне.\n"
                L"Команда может выполняться несколько минут.",
                L"LEFTIK CompactOS", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case 2013: Journal::SetDword(HKEY_CURRENT_USER, L"Software\\Discord", L"HardwareAcceleration", 1, L"Discord Fix (revert)"); break;
        case 2014: break; // Audio Fix — одноразовая команда, отката нет
        // Блок O: это ремонтные операции. Откат для них не имеет смысла/безопасного
        // обратного действия, поэтому выключение тумблера только снимает отметку.
        case 2015: case 2016: case 2017: case 2018:
        case 2019: case 2020: case 2021: break;

        // --- Твики-команды: у части есть обратная команда, у одноразовых — только лог в журнале ---
        case 1080: TimerMgr::Release(L"tweak1080"); break;
        case 3010: {
            std::vector<std::pair<std::wstring, std::wstring>> savedDns;
            HKEY hk{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\PrevDNS", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
                wchar_t vName[256]; wchar_t vData[1024];
                DWORD idx = 0;
                for (;;) {
                    DWORD nLen = 256, dLen = sizeof(vData), tp = 0;
                    if (RegEnumValueW(hk, idx, vName, &nLen, nullptr, &tp, (BYTE*)vData, &dLen) != ERROR_SUCCESS) break;
                    if (tp == REG_SZ) savedDns.push_back({ vName, vData });
                    ++idx;
                }
                RegCloseKey(hk);
            }
            if (!savedDns.empty()) {
                std::wstring psCmd;
                for (const auto& kv : savedDns) {
                    std::wstring escapedAlias;
                    for (wchar_t c : kv.first) {
                        if (c == L'\'') escapedAlias += L"''";
                        else escapedAlias += c;
                    }
                    if (kv.second.empty() || kv.second == L"DHCP") {
                        psCmd += L"Set-DnsClientServerAddress -InterfaceAlias '" + escapedAlias + L"' -ResetServerAddresses -ErrorAction SilentlyContinue; ";
                    } else {
                        std::wstring addrList;
                        std::wstringstream wss(kv.second);
                        std::wstring item;
                        while (std::getline(wss, item, L',')) {
                            if (item.empty()) continue;
                            if (!addrList.empty()) addrList += L",";
                            addrList += L"'" + item + L"'";
                        }
                        if (!addrList.empty())
                            psCmd += L"Set-DnsClientServerAddress -InterfaceAlias '" + escapedAlias + L"' -ServerAddresses (" + addrList + L") -ErrorAction SilentlyContinue; ";
                        else
                            psCmd += L"Set-DnsClientServerAddress -InterfaceAlias '" + escapedAlias + L"' -ResetServerAddresses -ErrorAction SilentlyContinue; ";
                    }
                    Journal::DeleteValue(HKEY_CURRENT_USER, L"Software\\LEFTIK\\PrevDNS", kv.first, L"DNS (revert)");
                }
                RunPowerShell(psCmd);
                RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\LEFTIK\\PrevDNS");
            } else {
                MessageBoxW(App::hwnd,
                    L"ВНИМАНИЕ: сохранённый бэкап DNS в PrevDNS не найден.\n"
                    L"Выполняется сброс DNS на получение автоматически (DHCP).",
                    L"LEFTIK DNS", MB_OK | MB_ICONWARNING);
                RunPowerShell(L"Get-NetAdapter | Where-Object Status -eq 'Up' | Set-DnsClientServerAddress -ResetServerAddresses");
            }
            break;
        }
        case 1081: case 2001: case 2002: case 2003: case 2004: case 2005: case 2006: case 1044: case 1045:
            // Одноразовые команды — откат невозможен; состояние сбрасывается в кэше, в реестре ничего не оставляем
            break;

        default: break;
    }
}

// Блок H: Auto Power Guard. Срабатывает только на ноутбуках при смене питания.
void HandlePowerStateChange(bool onAC) {
    if (!App::isLaptop) return;
    App::hwInfo.onACPower = onAC;

    if (!onAC) {
        bool revertedAny = false;
        if (ReadTweakState(1021)) { RevertTweak(1021); SaveTweakState(1021, false); revertedAny = true; }
        if (ReadTweakState(1073)) { RevertTweak(1073); SaveTweakState(1073, false); revertedAny = true; }
        if (revertedAny) {
            for (auto& tw : App::tweaksList)
                if (tw.id == 1021 || tw.id == 1073) tw.isEnabled = ReadTweakState(tw.id);
            bool profileHint = (App::magicBoostActive || App::isMaxActive || App::isUltraActive);
            MessageBoxW(App::hwnd,
                profileHint
                    ? L"Отключена зарядка (активен игровой профиль). Сняты энергозатратные твики (Power Throttling) для экономии батареи."
                    : L"Отключена зарядка. Сняты энергозатратные твики (Power Throttling) для экономии батареи.",
                L"LEFTIK Auto-Guard", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        if (App::magicBoostActive) {
            // На AC возвращаем только Power Throttling.
            if (IsTweakAllowedForHardware(1021)) T_PowerThrot();
            for (auto& tw : App::tweaksList)
                if (tw.id == 1021) tw.isEnabled = ReadTweakState(tw.id);
            MessageBoxW(App::hwnd,
                L"Питание от сети. Буст снова активен.",
                L"LEFTIK Auto-Guard", MB_OK | MB_ICONINFORMATION);
        }
    }
    InvalidateRect(App::hwnd, nullptr, FALSE);
}

void InitTweaks() {
    // system не знает tweaks: связываем откат журнала с RevertTweak/ReadTweakState.
    Journal::SetRollbackHooks(&RevertTweak, &ReadTweakState);
    App::tweaksList.push_back({L"GameDVR Off", L"Освобождение FPS", L"Отключает встроенную запись Xbox Game Bar, которая съедает до 5-10% FPS в играх.", 1000, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Мышь 1:1", L"Отключение акселерации", L"Убирает ускорение мыши Windows. Важно для точного аима в шутерах (CS2).", 1001, App::CAT_BASIC, true, true, false, false});
    App::tweaksList.push_back({L"Win32Priority", L"Кванты CPU (Hex 26)", L"Процессор отдает больше квантов времени активному окну (игре).", 1002, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Cross-Device", L"Синхронизация с телефоном", L"Отключает фоновую синхронизацию уведомлений с Android/iOS.", 1003, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Группировка svchost", L"Экономия ОЗУ (риск стабильности)", L"Собирает службы в один процесс. Экономит ОЗУ, но может снизить стабильность.", 1004, App::CAT_BASIC, true, true, false, false, true});
    App::tweaksList.push_back({L"NTFS 8.3 Имена", L"Ускорение файловой системы", L"Отключает создание коротких имен файлов (DOS). Ускоряет работу SSD.", 1005, App::CAT_BASIC, false, true, false, false, true});
    App::tweaksList.push_back({L"NTFS Last Access", L"Отключение меток доступа", L"Уменьшает износ SSD, запрещая запись времени открытия файлов.", 1006, App::CAT_BASIC, false, true, false, false, true});
    App::tweaksList.push_back({L"Автообслуживание", L"Отключение фоновых задач", L"Выключает автоматическую дефрагментацию Windows.", 1007, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Буфер мыши (20 пакетов)", L"Снижение Input Lag", L"Уменьшает буфер опроса мыши со 100 до 20 пакетов. Снижает задержку ввода на 1-2 мс в шутерах (CS2, Valorant). Безопасно для USB-мышей.", 1008, App::CAT_BASIC, false, true, false, true});

    App::tweaksList.push_back({L"Windows Defender", L"Отключение антивируса", L"ОПАСНО! Только если у вас есть сторонний антивирус.", 1010, App::CAT_SECURITY, true, true, false, false});
    App::tweaksList.push_back({L"Отключить UAC", L"Уведомления контроля", L"Отключает запросы 'Разрешить приложению внести изменения?'.", 1011, App::CAT_SECURITY, true, true, false, true});
    App::tweaksList.push_back({L"Центр безопасности", L"Скрыть иконку и службу", L"Убирает значок щита в трее.", 1012, App::CAT_SECURITY, false, true, false, false});
    App::tweaksList.push_back({L"SmartScreen", L"Отключение проверки", L"Отключает облачную проверку скачанных файлов.", 1013, App::CAT_SECURITY, false, true, false, false});
    App::tweaksList.push_back({L"Брандмауэр", L"Отключение сетевого экрана", L"ОПАСНО! Ваш ПК становится уязвим для атак из сети.", 1014, App::CAT_SECURITY, true, true, false, false});
    App::tweaksList.push_back({L"AMSI Interface", L"Отключение сканирования", L"Отключает сканирование PowerShell скриптов.", 1015, App::CAT_SECURITY, true, true, false, false});
    App::tweaksList.push_back({L"Code Integrity", L"Кастомные драйверы", L"Отключает проверку цифровой подписи драйверов.", 1016, App::CAT_SECURITY, true, true, false, false});
    App::tweaksList.push_back({L"VBS / Hyper-V Off", L"bcdedit hypervisor off", L"Отключает гипервизор Windows (VBS, изоляцию ядра). Дает +5-10% FPS, но перестанут работать WSL2, Docker и Hyper-V.", 1017, App::CAT_SECURITY, true, true, false, true});
    App::tweaksList.push_back({L"Целостность памяти (HVCI)", L"Отключение Memory Integrity", L"Отключает проверку драйверов в изолированной среде. Снижает нагрузку на CPU в играх. Требует перезагрузки. ВНИМАНИЕ: снижает общую защиту Windows.", 1018, App::CAT_SECURITY, true, true, false, true});

    App::tweaksList.push_back({L"Ultimate Performance", L"Скрытая схема питания", L"Создает и активирует схему LEFTIK Ultimate (без дубликатов). При выключении возвращает прежнюю схему.", 1020, App::CAT_POWER, false, true, false, false});
    App::tweaksList.push_back({L"Power Throttling", L"Снятие лимитов ЦП", L"Запрещает Windows замедлять фоновые процессы. ОПАСНО ДЛЯ НОУТБУКОВ!", 1021, App::CAT_POWER, false, false, false, false});
    App::tweaksList.push_back({L"Гибернация", L"Отключение hiberfil.sys", L"Удаляет файл гибернации. Освобождает 5-10 ГБ на диске C.", 1023, App::CAT_POWER, false, true, false, false});
    App::tweaksList.push_back({L"USB Selective Suspend", L"Отключение сна USB", L"Запрещает отключать USB-порты для экономии энергии.", 1024, App::CAT_POWER, false, true, false, false});

    App::tweaksList.push_back({L"QoS Limit", L"Снятие лимита 20%", L"Отключает резервирование 20% пропускной способности канала.", 1030, App::CAT_NETWORK, false, true, false, false});
    App::tweaksList.push_back({L"IPv6", L"Отключение IPv6", L"Иногда решает проблемы с высоким пингом в играх.", 1031, App::CAT_NETWORK, false, true, false, false});
    App::tweaksList.push_back({L"Отключить Nagle", L"TCP Ack Frequency", L"Снижает пинг в онлайн-играх за счет отказа от пакетирования. Применяется ко всем сетевым адаптерам.", 1032, App::CAT_NETWORK, false, true, false, false});
    App::tweaksList.push_back({L"RSS (Receive Side Scaling)", L"Сетевые пакеты по ядрам", L"Распределяет обработку сетевых пакетов по ядрам ЦП.", 1033, App::CAT_NETWORK, false, true, false, false, true});
    App::tweaksList.push_back({L"Checksum Offload", L"Разгрузка ЦП (сеть)", L"Разгружает ЦП при работе с сетью.", 1034, App::CAT_NETWORK, false, true, false, false, true});
    App::tweaksList.push_back({L"Large Send Offload", L"Разгрузка ЦП (LSO)", L"Разгружает ЦП при загрузках.", 1035, App::CAT_NETWORK, false, true, false, false, true});

    App::tweaksList.push_back({L"Служба SysMain", L"Superfetch", L"Отключает кэширование программ в ОЗУ. На SSD не нужно.", 1040, App::CAT_DEBLOAT, false, true, false, false});
    App::tweaksList.push_back({L"Служба WSearch", L"Поиск Windows", L"Отключает индексацию файлов. Поиск станет медленнее.", 1041, App::CAT_DEBLOAT, false, true, false, false});
    App::tweaksList.push_back({L"Отключить OneDrive", L"Удаление OneDrive", L"Выпиливает встроенное облако. ОПАСНО: Файлы удалятся! Необратимо: после удаления тумблер при перезапуске останется включённым.", 1042, App::CAT_DEBLOAT, true, true, false, false});
    App::tweaksList.push_back({L"Отключить Xbox", L"Службы Xbox Live", L"Отключает фоновые службы Xbox.", 1043, App::CAT_DEBLOAT, false, true, false, false});
    App::tweaksList.push_back({L"Удалить Edge", L"Выпиливание браузера", L"Полностью удаляет Microsoft Edge.", 1044, App::CAT_DEBLOAT, true, true, false, false});
    App::tweaksList.push_back({L"Удалить Teams", L"Удаление мессенджера", L"Удаляет Microsoft Teams.", 1045, App::CAT_DEBLOAT, true, true, false, false});

    App::tweaksList.push_back({L"Телеметрия", L"AllowTelemetry = 0", L"Отключает отправку данных в Microsoft.", 1050, App::CAT_PRIVACY, false, true, false, false});
    App::tweaksList.push_back({L"Рекламный ID", L"Отключение слежки", L"Запрещает собирать данные для рекламы.", 1051, App::CAT_PRIVACY, false, true, false, false});
    App::tweaksList.push_back({L"Геолокация", L"Отключение служб", L"Выключает определение местоположения.", 1052, App::CAT_PRIVACY, false, true, false, false});

    App::tweaksList.push_back({L"Диспетчер печати", L"Print Spooler", L"Вы не сможете распечатать документы!", 1060, App::CAT_SERVICES, false, true, false, false});
    App::tweaksList.push_back({L"Удаленный реестр", L"RemoteRegistry", L"Запрещает удаленное изменение реестра.", 1061, App::CAT_SERVICES, false, true, false, false});
    App::tweaksList.push_back({L"Факс", L"Служба факса", L"Отключает службу факсов.", 1062, App::CAT_SERVICES, false, true, false, false});
    App::tweaksList.push_back({L"Отчеты об ошибках", L"WerSvc", L"Отключает отправку отчетов о крашах.", 1063, App::CAT_SERVICES, false, true, false, false});
    App::tweaksList.push_back({L"Bluetooth", L"bthserv", L"Вы не сможете подключить BT-наушники!", 1064, App::CAT_SERVICES, false, true, false, false});
    App::tweaksList.push_back({L"Сенсорная клавиатура", L"TabletInputService", L"Отключает службу планшета/сенсорной клавиатуры (CTF Loader). Не нужна на ПК без тач-экрана.", 1065, App::CAT_SERVICES, false, true, false, false});

    App::tweaksList.push_back({L"Сброс Windows Update", L"Очистка кэша WU", L"Удаляет кэш обновлений. Решает ошибки WU.", 2001, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"Переустановка Store", L"Фикс Microsoft Store", L"Перерегистрирует Microsoft Store.", 2002, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"Сброс сети (Winsock/IP)", L"Полный сброс TCP/IP", L"Решает проблемы с отсутствием интернета.", 2003, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"Восстановление иконок", L"Сброс кэша иконок", L"Исправляет белые ярлыки.", 2004, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"DISM + SFC", L"Восстановление системы", L"Восстанавливает системные файлы Windows.", 2006, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"EasyAntiCheat (EAC)", L"Запуск службы EAC", L"Фикс ошибок в играх с EAC.", 2007, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"Vanguard (Valorant)", L"Восстановление VGC/VGK", L"Фикс ошибки Vanguard.", 2008, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"BlueStacks Fix", L"Запуск служб эмулятора", L"Включает службы для BlueStacks.", 2009, App::CAT_FIXES, true, true, false, false});
    App::tweaksList.push_back({L"Bluetooth Audio", L"Фикс наушников HFP", L"Улучшает качество звука в BT-наушниках.", 2010, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Store Fix (Platinum)", L"Перерегистрация Store", L"Запускает wsreset и перерегистрирует WindowsStore. Может занять несколько минут.", 2011, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"CompactOS", L"Сжатие системных файлов", L"Сжимает системные файлы Windows и освобождает 2-4 ГБ на диске C. На SSD влияет на скорость минимально.", 2012, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Discord Fix", L"Демонстрация экрана", L"Отключает аппаратное ускорение оверлея Discord. Помогает при чёрном экране в демонстрации на NVIDIA; нужен перезапуск Discord.", 2013, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Audio Fix", L"xaudio2_7 для GTA/FiveM", L"Перерегистрирует xaudio2_7.dll и запускает sfc /scannow. SFC может выполняться 10-30 минут.", 2014, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Print Spooler Fix", L"Восстановление печати", L"Запускает службу печати и очищает очередь заданий. Помогает, если печать перестала работать.", 2015, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Xbox / Game Pass Fix", L"Службы Xbox", L"Переводит службы Xbox в ручной режим. Помогает, если Game Pass не запускается.", 2016, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"WebView2 Fix", L"Переустановка WebView2", L"Удаляет политику EdgeUpdate и запускает установку WebView2 через winget.", 2017, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"GTA 5 Crash Fix", L"Сброс настроек памяти", L"Удаляет агрессивные твики памяти, которые могут вызывать вылеты GTA 5.", 2018, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"DWM Revert", L"Сброс настроек DWM", L"Возвращает стандартные настройки DWM после экспериментов с оверлеями.", 2019, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"FiveM Audio Fix", L"Сброс GTA V и кэша FiveM", L"Сбрасывает settings.xml GTA V и очищает кэш FiveM. Помогает при вылетах и пропаже звука.", 2020, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Discord Screen Share Fix", L"Фикс демонстрации экрана", L"Включает совместимость DWM-оверлея для демонстрации экрана Discord.", 2021, App::CAT_FIXES, false, true, false, false});
    
    App::tweaksList.push_back({L"MenuShowDelay = 0", L"Ускорение меню", L"Убирает задержку при открытии выпадающих меню в Windows.", 1071, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"StartupDelay", L"Быстрый старт программ", L"Отключает задержку автозагрузки приложений при входе в систему.", 1072, App::CAT_BASIC, false, true, false, false});
    // ID 1073 (Power Throttling Off) — legacy-дубликат 1021 (тот же ключ PowerThrottlingOff); скрыт из UI, перенаправляется на 1021.
    App::tweaksList.push_back({L"HAGS (Планировщик GPU)", L"Аппаратный ускоренный GPU", L"HAGS включается в реестр сразу, активация — после перезагрузки. ВАЖНО: на Windows 11 возможен 1 Гц микростаттер на части конфигураций (Ryzen + NVIDIA). Если после включения появились равномерные подёргивания (ровно раз в секунду) — выключите твик: HAGS не виноват в FPS, но портит frame pacing на Win11.", 1074, App::CAT_BASIC, false, true, false, true});
    App::tweaksList.push_back({L"Disable Prefetch", L"Отключение префетча", L"Отключает Prefetch, снижая износ SSD дисков.", 1075, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Disable FSE", L"Оптимизация полноэкранного режима", L"Улучшает поведение старых игр в эксклюзивном полноэкранном режиме.", 1076, App::CAT_BASIC, false, true, false, false});
    // Твик 1077 (UsePlatformClock / HPET) удален: принудительный platform clock добавляет DPC-задержки на большинстве систем.

    App::tweaksList.push_back({L"Таймер 0.5ms (HPET)", L"Максимальная плавность", L"Убирает микрофризы и делает картинку невероятно плавной. ОПАСНО: повышает энергопотребление CPU.", 1080, App::CAT_BASIC, true, true, false, false});
    App::tweaksList.push_back({L"Сброс кэша DirectX", L"Убирает статтеры", L"Очищает D3DSCache. Помогает если в играх проседает FPS при появлении новых эффектов.", 1081, App::CAT_FIXES, false, true, false, false});
    App::tweaksList.push_back({L"Сетевой буст (Порты)", L"Снижение пинга", L"Расширяет порты Windows (MaxUserPort) и ускоряет TCP-соединения. Помогает на плохом интернете.", 1082, App::CAT_NETWORK, false, true, false, false, true});

    App::tweaksList.push_back({L"DNS Cloudflare (1.1.1.1)", L"Снижение пинга", L"Устанавливает быстрый и безопасный DNS от Cloudflare. Ускоряет открытие сайтов и снижает пинг в играх. Легко отключается.", 3010, App::CAT_NETWORK, false, true, false, false});
    App::tweaksList.push_back({L"Автозагрузка Windows", L"Запуск с системой", L"Добавляет LEFTIK в автозагрузку Windows. Программа будет запускаться сама при включении ПК.", 1070, App::CAT_BASIC, false, true, false, false});

    // Новые системные твики задержек
    App::tweaksList.push_back({L"Network Throttling", L"Снятие лимита сети", L"Отключает искусственное ограничение обработки сетевых пакетов (NetworkThrottlingIndex). Снижает джиттер и потери.", 1094, App::CAT_NETWORK, false, true, false, false});
    App::tweaksList.push_back({L"System Responsiveness", L"Приоритет фоновых задач", L"Windows отдает 100% ресурсов играм, а не фоновым службам (SystemResponsiveness = 0).", 1095, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Прозрачность Windows", L"Разгрузка DWM", L"Отключает эффекты прозрачности (Acrylic/Mica). Снижает нагрузку на видеокарту в оконном режиме.", 1096, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"[GPU] MSI Mode", L"Аппаратные прерывания", L"Переводит видеокарту (NVIDIA/AMD) в режим Message Signaled Interrupts (MSISupported=1). Снижает DPC-латентность. Требует перезагрузки.", 1097, App::CAT_BASIC, false, true, false, true});
    App::tweaksList.push_back({L"[MMCSS] Профиль Games Max", L"GPU Priority 8 + SFIO High", L"Настраивает планировщик мультимедиа Windows (MMCSS) на максимальный приоритет для игр: GPU Priority=8, Priority=6, Scheduling=High.", 1098, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"[Input] CSRSS High Priority", L"Минимальный инпут-лаг", L"Поднимает приоритет подсистемы ввода csrss.exe до High и IO Priority до High через IFEO PerfOptions (без урезания очереди мыши). Требует перезагрузки.", 1099, App::CAT_BASIC, true, true, false, true});
    App::tweaksList.push_back({L"[CPU] Unpark 100% + Boost", L"Отключение парковки ядер", L"Разблокирует 100% ядер процессора, убирает троттлинг частоты и включает агрессивный Turbo Boost в активной схеме питания.", 1102, App::CAT_POWER, false, false, false, false});
    App::tweaksList.push_back({L"[DWM] Отключить MPO", L"Фикс статтеров и фликов", L"Отключает Multiplane Overlay (OverlayTestMode=5). Убирает микрофризы, мерцание и просадки FPS в оконном/borderless режиме на NVIDIA и AMD.", 1103, App::CAT_BASIC, false, true, false, true});
    App::tweaksList.push_back({L"[RAM] Kernel Memory Lock", L"Запрет свопа ядра + 64MB IO", L"Запрещает выгрузку ядра и драйверов в файл подкачки (DisablePagingExecutive=1) и увеличивает лимит блокировки страниц ввода-вывода.", 1104, App::CAT_BASIC, false, true, false, true});

    // Вендорские твики: NVIDIA (1100-1109) и AMD (1110-1119). Hardware Guard не даст применить чужие.
    App::tweaksList.push_back({L"[NVIDIA] PowerMizer Max", L"Макс. производительность", L"Легаси-ключи NVTweak для постоянных частот. Дает стабильный фреймтайм на старых драйверах, на новых может игнорироваться.", 1100, App::CAT_POWER, false, false, false, false});
    App::tweaksList.push_back({L"[NVIDIA] Telemetry Off", L"Отключение телеметрии", L"Выключает фоновые службы NVIDIA Telemetry/NvContainer, которые грузят ЦП и диск. ShadowPlay может перестать работать.", 1101, App::CAT_PRIVACY, false, true, false, false});
    App::tweaksList.push_back({L"[AMD] ULPS Off", L"Отключение глубокого сна", L"Убирает микрофризы при выходе из простоя (Ultra Low Power State) на CrossFire/двух чипах. Требует перезагрузку.", 1110, App::CAT_POWER, false, false, false, false});
    App::tweaksList.push_back({L"[AMD] Telemetry Off", L"Отключение сбора логов", L"Отключает службу AMD Crash Defender (сбор логов) и переводит External Events в ручной режим.", 1111, App::CAT_PRIVACY, false, true, false, false});

    // --- v57.12 Security+ ---
    App::tweaksList.push_back({L"[RAM] Управление компрессией", L"Разгрузка CPU на 16+ ГБ", L"Отключает сжатие страниц памяти. На 16+ ГБ RAM разгружает CPU. Ниже 16 ГБ заблокировано (сжатие необходимо для стабильности).", 1120, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"[Win11] Отключить Widgets", L"Фоновая разгрузка", L"Останавливает Widgets.exe и WidgetService.exe, убирает иконку с панели задач. Освобождает 200-400 МБ RAM. Только Windows 11.", 1121, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"[TCP] Autotuning restricted", L"Стабильный пинг", L"Заменяет агрессивное масштабирование TCP-окна (normal) на restricted. Снижает джиттер пинга на Wi-Fi и ADSL. Помогает в CS2/Dota/SAMP.", 1122, App::CAT_NETWORK, false, true, false, false});

    // --- Финальный блок G–N ---
    App::tweaksList.push_back({L"Game Mode Off", L"Отключение Game Mode", L"Отключает автоматический режим игры Windows. Иногда он мешает стабильному фреймтайму; применяется после перезапуска игры.", 1130, App::CAT_BASIC, false, true, false, false});
    App::tweaksList.push_back({L"Отложить обновления", L"Функциональные обновления 365 дней", L"Политика Windows Update: откладывает функциональные обновления Windows на 365 дней. Не отключает обновления безопасности.", 1131, App::CAT_PRIVACY, false, true, false, false});
    App::tweaksList.push_back({L"Облачный буфер", L"Cloud Clipboard + история", L"Отключает облачную синхронизацию буфера обмена и локальную историю. Ваши скопированные данные не уходят в облако.", 1132, App::CAT_PRIVACY, false, true, false, false});
    App::tweaksList.push_back({L"Динамический тик", L"Снижение задержки CPU",
        L"Отключает сон процессора между тиками (bcdedit disabledynamictick). Снижает задержку ввода на десктопах. ОПАСНО ДЛЯ НОУТБУКОВ: перегрев и разряд батареи. Требует перезагрузки.",
        1133, App::CAT_BASIC, true, false, false, true});
    App::tweaksList.push_back({L"[Steam] Отключить оверлей и браузер", L"Steam Lite: меньше ОЗУ", L"Отключает внутриигровой оверлей Steam и убивает фоновый steamwebhelper.exe. Снижает потребление ОЗУ на 200-400 МБ. Оверлей и магазин перестанут открываться поверх игры.", 1140, App::CAT_DEBLOAT, false, true, false, false});
}
