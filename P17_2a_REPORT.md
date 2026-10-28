# Пакет #17 — часть 2a: отчёт

**Дата:** 27.10.2026  
**Ревизия:** после приёмки 2a, правки 1–2 (Intel APO без детекта, production без PGO) + release packaging: ресурсы, Inno Setup, beta-подпись, delivery.  
**Машина:** Windows 11 Pro `10.0.26300`, AMD Ryzen 5 5500U, Radeon Graphics, VBS/HVCI включены, HAGS `HwSchMode=2`, Intel APO отсутствует, приложение Xbox отсутствует.  
**Статус:** 2a.1, 2a.2, 2a.3, 2a.5 приняты; 2a.4 и 2a.6 приняты после правок. Production 2026.1 собран и упакован: MSVC Release с `RT_ICON`/`RT_GROUP_ICON`/`RT_VERSION`/`RT_MANIFEST`, подписан beta-сертификатом; `LEFTIK_2026.1_x64_Setup.exe` собран и подписан; uninstaller проверен во всех ветках; delivery готов. Пункт 2b.1 (MSI Mode Full) **не начинался**.

## 1. Что изменено

### 2a.1 — HVCI/1018: предупреждение вместо блокировки

- В `system.cpp`/`system.hpp` добавлен `IsTweakWarningOnly(int id)`.
- Для `1018` при `winBuild >= 26100 && vbsEnabled` `GetTweakBlockReason(1018)` возвращает текст-предупреждение, но `IsTweakAllowedForHardware(1018)` остаётся `true`.
- UI (`ui_main.cpp`) больше не рисует для 1018 «Заблокировано Guard»: при клике показывается `MB_YESNO`-предупреждение, применение — только после явного «Да».
- HVCI нигде не отключается автоматически: `1018` нет ни в ТУРБО, ни в Magic Boost.

### 2a.2 — HAGS/1074 только на Windows 10

- Состав ТУРБО вынесен в `BuildTurboTweakList()` (`tweaks.cpp`, объявление в `tweaks.hpp`) — его можно тестировать без реального применения.
- `1074` добавляется в ТУРБО только при `winBuild < 22000`; на Win11 тумблер остаётся ручным.
- `fullDesc` твика 1074 обновлён: предупреждение о 1 Гц микростаттере на Win11 (Ryzen + NVIDIA), запись в реестр — сразу, активация — после перезагрузки.
- Дополнительно (в рамках той же причины): `ApplyMagicBoost()` на Win11 пропускает `1074`; строка HAGS в отчёте Magic Boost показывается только на Win10.
- Турбо-пачка 1018 не менялась; Edge/Teams в ТУРБО не добавлены.

### 2a.3 — Xbox mode: детект, карточка, кнопка 3053

- В `core.hpp` добавлено `xboxModeAvailable`; детект — в `RefreshHardwareFlags()`.
- Условие: `winBuild >= 26100` и наличие пакета `Microsoft.GamingApp` в `HKCU\Software\Classes\Local Settings\...\Repository\Packages`.
- Намеренно **не** считаются Xbox mode: `Microsoft.Xbox.TCUI`, `XboxIdentityProvider`, Game Bar — они есть почти на каждом ПК и не дают полноэкранный режим.
- На дашборде после карточки ТУРБО добавлена карточка Xbox mode с кнопкой «Открыть настройки Xbox mode» (`3053`).
- `case 3053` вызывает официальный URI `ms-settings:gaming-fullscreen`; при `ShellExecuteW <= 32` — fallback `ms-settings:gaming`.
- В подтверждении ТУРБО добавлена строка: если Xbox mode доступен, LEFTIK его не дублирует; ТУРБО отключает службы Xbox Live (твик 1043), поэтому после Турбо их нужно включить обратно, если пользуетесь Xbox mode/приложением Xbox.
- LEFTIK в реестр для Xbox mode ничего не пишет.

### 2a.4 — Intel APO: только текстовая рекомендация

Правка по приёмке: детект службы убран, поле `intelApoInstalled` удалено.

- В `core.hpp` остался только `isIntelPlatform` (Intel GPU или Intel CPU). Поля `intelApoInstalled` больше нет.
- Из `RefreshHardwareFlags()` полностью удалён блок поиска служб `IntelApplicationOptimization`, `IntelDynamicTuning`, `IntelDTT`, `DTT`. Имя `IntelApplicationOptimization` было недостоверным: на реальной Intel-машине с APO детект мог вернуть `false` и показать ложный баннер.
- Условие баннера: `App::hwInfo.isIntelPlatform && (isCS2 || isDota)`.
- Текст баннера: «На Intel 12th gen+ Intel APO даёт до 27% в CS2. Проверьте в Intel DSA, установлен ли APO — LEFTIK его не дублирует.»
- Клика по баннеру нет. LEFTIK APO не устанавливает, не эмулирует и не проверяет.
- Если появится подтверждённое имя службы и Intel-машина с APO — детект можно вернуть отдельной правкой.

### 2a.5 — тексты NVIDIA/AMD

- NVIDIA: явно указано, что Reflex 2 / Frame Warp анонсирован в январе 2025, но на 2026 всё ещё не выпущен; в играх используется Reflex 1.
- AMD: Anti-Lag 2 назван тем, чем он стал в новых драйверах — FSR Latency Reduction 2.0; добавлено пояснение о переименовании.
- Суффикс про потоки (Hanami/affinity) сохранён.

### 2a.6 — LTCG/PGO: инфраструктура есть, бинарник не шипится

- MSVC: `/GL` и `/LTCG` включены всегда; `-DLEFTIK_PGO=ON` переключает `/GENPROFILE` ↔ `/USEPROFILE` и задаёт `/PGD:...`.
- MinGW: `-fprofile-generate` на первом проходе, `-fprofile-use -fprofile-correction -Wno-missing-profile -fprofile-dir=...` на втором.
- Между проходами обязательна повторная конфигурация CMake — `cmake --build` сам флаги не меняет.
- Production 2026.1 собран с `-DLEFTIK_PGO=OFF`; PGO-бинарник в delivery **не входит**.
- В `PGO.md` добавлен раздел «Правильный warmup для будущего релиза»: полный цикл ETW 5 минут на реальной игре, откат turbo-boost через Восстановление → Diff, ПЕРЕСКАНИРОВАТЬ, тумблеры 1002/1014/1017, все 8 страниц UI, штатное закрытие; сравнение latency через `QueryPerformanceCounter` вокруг `RestoreEntryLine`/`CalcCrc32` и собственный ETW-бенчмарк, а не размер exe.

### 2a.7 — release packaging: ресурсы, установщик, подпись

- Исходная MSVC-сборка содержала только `RT_MANIFEST`: иконки и `VERSIONINFO` в exe не было. Это был реальный дефект, а не предположение.
- `leftik.ico` сгенерирован заново: фиолетовый `#7B61FF`, белая шестерёнка Segoe MDL2 `\uE713`, 9 размеров от 16 до 256 px.
- `LEFTIK.rc` теперь всегда содержит `IDI_LEFTIK ICON "leftik.ico"` и числовой `1 VERSIONINFO`. Именованный `VS_VERSION_INFO` не годится: `GetFileVersionInfoSizeW` возвращал `0` при структурно валидном блобе. Числовой ID `1` обязателен.
- `RT_MANIFEST` разделён по тулчейнам: MSVC — `/MANIFEST:EMBED` + `/MANIFESTINPUT:LEFTIK.manifest` + `/MANIFESTUAC:NO`; MinGW — отдельный `LEFTIK_manifest.rc`. Дублирования манифеста и ошибок `LNK1327`/`c1010001` нет.
- В манифест добавлены `requireAdministrator` (приложение пишет `C:\ProgramData\LEFTIK`), `PerMonitorV2` и `supportedOS` Windows 10/11.
- Добавлен production-скрипт Inno Setup `installer\LEFTIK.iss`: фиксированный `AppId`, `DefaultDirName={autopf}\LEFTIK`, x64compatible, `PrivilegesRequired=admin`, `MinVersion=10.0.19041`, Start Menu shortcut ON, Desktop shortcut OFF по умолчанию, без Run-записи и без автообновления.
- Uninstaller по умолчанию сохраняет `HKCU\Software\LEFTIK\*` и `C:\ProgramData\LEFTIK\*`, предупреждает об активных твиках и требует явного подтверждения перед удалением данных.
- Функциональная логика 2a не менялась: изменения 2a.7 — ресурсы, манифест, установщик и упаковка.

## 2. Сборки и SHA256

| Артефакт | Размер | SHA256 | Примечание |
|---|---:|---|---|
| Baseline Hotfix 1.1 | 4 422 046 B | `8F98442DE316F600A6205F13E0ABBBB6ACE9B9F98D6301D3AF1C2DBDF82FEAF3` | принят ранее, история |
| Production 2a до правок приёмки | 4 429 660 B | `FE945ECF21B44A7ED2D22033E37F752A81FCD1C5F105BDB8C477C6DC47E7943A` | история, не шипится |
| **Production 2026.1 — MSVC Release, signed (ship)** | **1 151 872 B** | **`47AE408BBAF4FB470C5D3D5D738DE49CCBAC6596272FCCC0E867D8052FEC4819`** | **`bin\LEFTIK.exe`; SHA256 + DigiCert timestamp** |
| MSVC Release, unsigned (до подписи) | 1 144 320 B | `8E57986580EA2CA6E42F2451DE868D2B54040F4DCA6D18C17DDF5046A44B03AF` | доказательство чистой пересборки |
| MinGW Release — reference/fallback | 4 443 494 B | `89A0EF9FC66CA2203EC1F76D997B2E00D4FF8F7DC1A2A44B89D92A521092ED3A` | `bin\reference\LEFTIK_mingw.exe` |
| MinGW Release, stripped (diagnostic, ранее) | 2 019 840 B | `7E4DA685DDB45C23FCEC92A8F527A3607544D117418965951202CDBFACE5F4FD` | только для сравнения размера |
| PGO experiment (MinGW, Release, ранее) | 4 289 231 B | `59C6DE35D4BDDFF478C161F62CF1A5A03B5FCC76B58F321E318D6917BF2346D1` | не шипится |

- MSVC signed к MinGW FINAL: **−3 291 622 B** (−74,08%).
- MSVC signed к baseline: **−3 270 174 B** (−73,95%).
- MinGW FINAL к версии до packaging-правок: **+13 834 B** (+0,31%) — добавлены иконка и `VERSIONINFO`.
- PGO к MinGW FINAL: **−154 263 B** (−3,47%) — только размер, не latency; PGO-бинарник не шипится.

### MSVC Release — выполнен

Тулчейн: Visual Studio Build Tools `17.14.42`, MSVC `19.44.35229.0`, Windows SDK `10.0.26100.0`.

```powershell
cmake -B build-msvc -G "Visual Studio 17 2022" -A x64 -DLEFTIK_PGO=OFF
cmake --build build-msvc --config Release --clean-first
```

- В `CMakeLists.txt` для MSVC включён статический CRT: `CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded$<$<CONFIG:Debug>:Debug>` (`/MT`). `dumpbin /dependents` показывает только системные DLL (`USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `gdiplus`, `COMDLG32`, `UxTheme`, `dwmapi`, `ole32`, `OLEAUT32`, `dxgi`, `pdh`, `bcrypt`, `KERNEL32`) — без `MSVCP140`, `VCRUNTIME140` и UCRT-API-set. VC++ Redistributable не нужен.
- Предупреждение MSVC C4244 в `tweaks.cpp` (GUID `powercfg` конвертировался из `wchar_t` в `char` через итераторы) исправлено явным ASCII-преобразованием. Сборка проходит без warning'ов.
- `.text` MSVC: **813 568 B** (`0xC6A00` raw).
- Smoke-тест финального подписанного MSVC-бинарника: окно `1750×1062` открылось, дашборд отрисован, иконка видна в title bar (`screenshots/p17_msvc_smoke_2026.1.png`). При `BackgroundMode=1` `WM_CLOSE` штатно скрывает окно в трей, а не завершает процесс; после снимка процесс остановлен. Падений и записей в Application Error Log нет. Реальный `HKCU\Software\LEFTIK\States` не изменился.

**Ресурсы MSVC (проверено на финальном подписанном exe):**

| Тип | ID | Результат |
|---|---|---|
| `RT_ICON` (#3) | `#1`…`#9` | 9 изображений, 16–256 px |
| `RT_GROUP_ICON` (#14) | `#101` | присутствует |
| `RT_VERSION` (#16) | `#1` | присутствует |
| `RT_MANIFEST` (#24) | `#1` | присутствует, один |

- `GetFileVersionInfoSizeW = 1460`; `file_version=2026.1.0.0`; `CompanyName=LEFTIK`, `FileDescription=LEFTIK Optimizer with rollback`, `FileVersion=2026.1.0.0`, `ProductName=LEFTIK`, `ProductVersion=2026.1`.
- Извлечённый манифест: `level="requireAdministrator" uiAccess="false"`, `dpiAwareness=PerMonitorV2`, `supportedOS` Windows 10/11.
- Security flags: `High Entropy Virtual Addresses`, `Dynamic base`, `NX compatible`; subsystem `Windows GUI`, x64.
- Подписанный exe: **1 151 872 B / `47AE408BBAF4FB470C5D3D5D738DE49CCBAC6596272FCCC0E867D8052FEC4819`**.

**Разница размера > 5% — разобрана.** Причина не в выключенных исходниках, а в разных runtime-моделях:

- MinGW-файл содержит встроенную COFF-таблицу символов (~2.4 MB) и статические `libstdc++`/`libgcc`; после `strip` остаётся 2 019 840 B.
- `.text` MSVC — 813 568 B, MinGW — 1 516 544 B. Набор Windows-импортов тот же; добавление/потеря подсистем не обнаружены.
- MSVC собирает `/O2 /GL /LTCG` и статический CRT; MinGW — `-O3` + статические GCC-runtime. Разница ожидаема для этого кода.

MSVC-бинарник — production 2026.1. MinGW остаётся reference/fallback.

### MinGW reference

```powershell
cmake -B build-mingw -DLEFTIK_PGO=OFF
cmake --build build-mingw --config Release --clean-first
```

- Финальный MinGW: **4 443 494 B / `89A0EF9FC66CA2203EC1F76D997B2E00D4FF8F7DC1A2A44B89D92A521092ED3A`**.
- Ресурсы совпадают с MSVC: `RT_ICON` #3 (9 изображений), `RT_GROUP_ICON` #14 #101, `RT_VERSION` #16 #1, `RT_MANIFEST` #24 #1 — ровно один манифест.
- `GetFileVersionInfoSizeW = 1460`; версия и строки `VERSIONINFO` читаются.
- Извлечённый манифест MinGW: `requireAdministrator`, `PerMonitorV2`, Windows 10/11.
- Security flags: `High Entropy Virtual Addresses`, `Dynamic base`, `NX compatible`; `.text` — 1 516 544 B.
- `build-mingw\CMakeCache.txt` содержит `LEFTIK_PGO:BOOL=OFF`, в бинарнике нет `gcov`-символов. Строки `IntelApplicationOptimization` нет ни в ASCII, ни в UTF-16; строка баннера «Проверьте в Intel DSA» в UTF-16 присутствует, старый текст «Установите из Intel DSA» отсутствует.

### PGO-эксперимент (не релиз)

```powershell
cmake -B build-pgo-mingw -G "Ninja Multi-Config" -DLEFTIK_PGO=ON
cmake --build build-pgo-mingw --config Release
# короткий прогрев приложения
cmake -B build-pgo-mingw -G "Ninja Multi-Config" -DLEFTIK_PGO=ON
cmake --build build-pgo-mingw --config Release --clean-first
```

Профиль: 4 файла `.gcda`, суммарно 158 700 B; второй configure вывел `LEFTIK PGO: using ...`. Короткий warmup не покрывает hot paths, поэтому результат `−3,47%` по размеру не считается приростом производительности. **PGO-инфраструктура готова, бинарник не шипится до правильного warmup. В 2026.1.1.**

## 3. Регрессия 1.1 и новые сценарии

Harness: **48 PASS / 0 FAIL**, IAT-хук `MessageBoxW` — PASS, `tweaks=94`, `winBuild=26300`. Прогон выполнен на функциональной 2a-сборке до resource-правок; после 2a.7 логика не менялась, финальный MSVC-бинарник дополнительно проверен smoke-тестом.

Сумма случаев и assertions разделена, чтобы не смешивать разные сущности:

| Блок | Проверяемых случаев | Assertions |
|---|---:|---:|
| Регрессия Hotfix 1.1 (6 сценариев) | 6 | 23 |
| 2a.1 HVCI 1018 | 7 | 7 |
| 2a.2 HAGS/Turbo | 7 | 7 |
| 2a.3 Xbox mode | 5 | 5 |
| 2a.4 Intel APO | 4 | 4 |
| Teardown | 2 | 2 |
| **Итого** | **31** | **48** |

6 регрессионных сценариев (23 assertions):

1. Win11 Edge 1044: `ApplyTweakById` = false, state 0, тумблер off, предупреждение показано.
2. Win10 Edge 1044: true, state 1, тумблер on, critical-process вызван один раз, предупреждения нет.
3. Win10 Edge failure: false, state 0, тумблер off, critical-process attempted.
4. VBS 1017 + WSL, отмена: false, state 0, тумблер off, `bcdedit` не вызван.
5. UI Edge 1044 на Win11: state сброшен в 0, тумблер off.
6. HAGS 1074 на старом GPU: false, cache 0, тумблер off.

Новые проверки 2a (23 assertions + 2 teardown):

- 2a.1: 7 проверок — текст предупреждения, упоминание Memory Integrity, `warning-only`, отсутствие hardware-блока, отсутствие предупреждения при VBS off и на Win10.
- 2a.2: 7 проверок — HAGS отсутствует в ТУРБО на Win11, присутствует на Win10; Widgets 1121 и VBS 1017 не сломаны; `fullDesc` содержит «1 Гц» и «Windows 11».
- 2a.3: 5 проверок — фейковый `Microsoft.GamingApp` детектится на 24H2+, TCUI/IdentityProvider сами по себе не включают Xbox mode, build-gate на Win10 false.
- 2a.4: 4 проверки — источники читаемы; из `system.cpp` удалён `IntelApplicationOptimization` и `intelApoInstalled`; в `HardwareInfo` остался `isIntelPlatform` без поля APO; в `ui_main.cpp` условие `isIntelPlatform && (isCS2 || isDota)`, точный текст баннера и отсутствие `intelApoInstalled`. Это source-policy проверки, потому что Intel-хоста с APO нет; исполнять несуществующий детект на AMD было бы фикцией.
- Teardown: 2 проверки — ключ `HKCU\Software\LEFTIK_Test` удалён, фейковый пакет Xbox удалён.

Важно:

- `ApplyTurboBoost()` и `ApplyMagicBoost()` в harness **не исполнялись** (они меняют реальную систему). Проверен только `BuildTurboTweakList()`; отсутствие `1074` гарантирует, что HAGS не попадёт в failed на Win11.
- Реальный `HKCU\Software\LEFTIK\States` не изменён: экспорт 3 578 B, SHA256 `7F7356B7F5F839806BB83C8940C0E94CB5930285B0EBA034D05024ED4127365C` — совпадает до и после прогона.
- `BackgroundMode` после скриншотов и PGO-прогрева восстановлен в `1`; процессов LEFTIK не осталось.

## 4. Xbox mode: URI и скриншот

Официальная документация Microsoft (`Launch Windows Settings`) указывает:

> Xbox Mode — `ms-settings:gaming-fullscreen`

Проверка на `10.0.26300`: прямой запуск `ms-settings:gaming-fullscreen` открывает **«Игры → Режим Xbox»** (скриншот `screenshots/p17_uri_gaming_fullscreen.png`). Страница показывает «Включить режим Xbox» и требует последнюю версию приложения Xbox — на тестовой машине приложения нет, что совпадает с детектом LEFTIK.

Ранее в коде был `ms-settings:gaming-xboxmode`; на этой сборке он открывал главную страницу «Параметры». URI исправлен на официальный. Fallback `ms-settings:gaming` остаётся на случай, если `ShellExecuteW` вернёт ошибку (старая сборка/страница недоступна).

Скриншот карточки: `screenshots/p17_xbox_mode_card.png` (98 721 B). Карточка и кнопка «Открыть настройки Xbox mode» видны целиком. Предыдущие обрезанные варианты были артефактом DPI-unaware скриншотера (окно 1750×1062 при 125% DPI), а не ошибкой layout: после перевода скриншот-процесса в Per-Monitor V2 окно снято в натуральном размере.

## 5. Инсталлятор, подпись и uninstaller

### 5.1. Inno Setup

Скрипт: `installer\LEFTIK.iss`. Компилятор: Inno Setup `6.7.3`, `ISCC.exe` из per-user установки.

| Параметр | Значение |
|---|---|
| `AppId` | `{DDC77D79-60DF-463C-979F-8E0602B88C9B}` |
| `AppName` / `AppVersion` | `LEFTIK` / `2026.1` |
| `DisplayName` | `LEFTIK 2026.1` |
| `DefaultDirName` | `{autopf}\LEFTIK` |
| `ArchitecturesAllowed` | `x64compatible` |
| `ArchitecturesInstallIn64BitMode` | `x64compatible` |
| `PrivilegesRequired` | `admin` |
| `MinVersion` | `10.0.19041` |
| `OutputBaseFilename` | `LEFTIK_2026.1_x64_Setup` |
| `Compression` | `lzma2/ultra64` |
| `SolidCompression` | `yes` |
| Языки | русский + английский |

- Start Menu shortcut создаётся всегда; Desktop shortcut — только через task `desktopicon`, по умолчанию **unchecked**.
- Run-записи и автостарта нет; автообновления в 2026.1 нет.
- `UninstallDisplayIcon={app}\LEFTIK.exe`, `CloseApplications=yes`.
- В `[Files]` включён только подписанный `build-msvc\Release\LEFTIK.exe`.

### 5.2. Uninstaller: сохранение данных

- `InitializeUninstall()` сбрасывает `RemoveUserData := False`.
- `CurUninstallStepChanged(usUninstall)`:
  - при `UninstallSilent` данные сохраняются без вопросов;
  - иначе показывается `TaskDialogMsgBox` с предупреждением об активных твиках и кнопками «Продолжить» / «Отмена»;
  - при «Отмена» вызывается `Abort`, установка остаётся;
  - затем `MsgBox(... MB_YESNO or MB_DEFBUTTON2)`: по умолчанию **No**, данные сохраняются; «Да» — явное разрешение на удаление.
- `CurUninstallStepChanged(usPostUninstall)` удаляет `HKCU\Software\LEFTIK` (`RegDeleteKeyIncludingSubkeys`) и `C:\ProgramData\LEFTIK` (`DelTree`) только если `RemoveUserData = True`; иначе пишет в лог `LEFTIK: user data preserved by user choice`.
- Silent uninstall намеренно не удаляет `HKCU`/`ProgramData`.

### 5.3. Подпись beta

- Сертификат: `CN=LEFTIK Beta, O=LEFTIK, C=RU`; thumbprint `6ADAB512DC5F687BDA4B4DD866B0A36D77C25328`; serial `2E6789CAF7E2CD924FFD4CD2C354178F`; срок 27.10.2026–27.10.2028.
- `LEFTIK.exe` подписан **до** сборки установщика; `Setup.exe` — после. Оба: SHA256 + RFC3161 timestamp `http://timestamp.digicert.com`, digest SHA256.
- `Get-AuthenticodeSignature` показывает signer и timestamp, но `Status=UnknownError`: самоподписанный корень не является доверенным по умолчанию. SmartScreen будет показывать «Unknown publisher» до production EV-сертификата.
- В delivery входит только публичный `LEFTIK_Beta_CodeSigning.cer`; PFX и пароль **не входят**.

### 5.4. Сборка установщика

| Артефакт | Размер | SHA256 |
|---|---:|---|
| Setup unsigned | 2 428 470 B | `42E2205AD671CAC3B83D8FFE5C12EED85B0F070F3B3A70118E88EB6D296CE204` |
| **Setup signed (ship)** | **2 436 032 B** | **`250C786910FA176E3111A6064E1020B864807ADA0BE74AF3435B9B3DABC88439`** |

Version info установщика: `CompanyName=LEFTIK`, `FileDescription=LEFTIK Optimizer Setup`, `FileVersion=2026.1.0.0`, `ProductName=LEFTIK`, `ProductVersion=2026.1`.

### 5.5. Проверки установки и удаления

| Сценарий | Результат |
|---|---|
| Silent install `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART` | exit 0; установлен `C:\Program Files\LEFTIK\LEFTIK.exe`, hash `47AE...` совпадает с подписанным артефактом |
| Ярлыки после install | Start Menu `LEFTIK.lnk` и `Удалить LEFTIK.lnk` созданы; Desktop shortcut отсутствует; Run-записи нет |
| Uninstall-ключ | `HKLM\...\Uninstall\{DDC77D79-60DF-463C-979F-8E0602B88C9B}_is1`, `DisplayName=LEFTIK 2026.1` |
| Silent uninstall | exit 0; `C:\Program Files\LEFTIK` и uninstall-ключ удалены; `HKCU`/`ProgramData` сохранены; лог: `LEFTIK: user data preserved by user choice` |
| Interactive uninstaller → «Отмена» | предупреждение показано; `Abort`; приложение осталось установленным |
| Interactive → «Продолжить» → «Нет» | приложение удалено; `HKCU`/`ProgramData` сохранены |
| Diagnostic → «Продолжить» → «Да» | на клоне `LEFTIK_UninstallTest` тестовые ключ и каталог удалены; реальные `HKCU\Software\LEFTIK` и `C:\ProgramData\LEFTIK` не тронуты |

Доказательства: `installer-silent-install.log`, `installer-silent-uninstall.log`, `installer-uia-cancel-console.log`, `installer-uia-delete-test-console.log`, скриншоты `screenshots/p17_uninstall_warning.png` и `screenshots/p17_uninstall_data_prompt.png`.

Состояние данных на момент тестов (27.10.2026, вечер):

- `HKCU\Software\LEFTIK\States`: 3 578 B, SHA256 `7F7356B7F5F839806BB83C8940C0E94CB5930285B0EBA034D05024ED4127365C`; `BackgroundMode=1`.
- Весь `HKCU\Software\LEFTIK`: 3 838 B, SHA256 `20713C7BCE68F8490F5A39238154FA51CC20155990F89A5FC0FAC9E886C2FC4F` — совпадает до и после тестов.
- `C:\ProgramData\LEFTIK`: `leftik_debug.log` 2 385 B `D3EC9BFD...`, `leftik_journal.idx` 1 568 B `1F4451DE...`, `leftik_journal.log` 6 529 B `C4408F4C...` — не изменились ни одной тестовой веткой.
- Приложение сразу после тестов **не установлено** (то же состояние).

Отдельно зафиксировано: после этих тестов пользователь сам запускал приложение вечером 27.10 и применял твики (записи в журнале, `DispatchAction` в `leftik_debug.log`). Это обычная работа через UI, не результат тестов uninstaller:

- `HKCU\Software\LEFTIK` сейчас: 3 884 B, SHA256 `3B20376546F66D5E9FA7F7A36535082B861EC48D341D6B9FD43006202BFFEBE3`; `States` 3 624 B, SHA256 `BE2B9D1D97A71465746A983012CAD6116B53D2DB9278254291D1CC3E7C8E998C`, `BackgroundMode=1`.
- `C:\ProgramData\LEFTIK` сейчас: `leftik_debug.log` 3 242 B `9EE4C73B...`, `leftik_journal.idx` 1 568 B `1F4451DE...` (не менялся), `leftik_journal.log` 9 981 B `7FB3B2A5...`.
- Каталог проекта перенесён на `E:\MyCppApp`; подписанные артефакты совпадают по SHA256 с значениями выше.

## 6. Честные ограничения

- **Beta-подпись:** только самоподписанный сертификат. Chain не является доверенным по умолчанию, `Status=UnknownError`, SmartScreen покажет «Unknown publisher». Для production нужен EV code-signing certificate; подпись и timestamp уже отработаны.
- **Inno Setup:** установлен 6.7.3, компилятор печатает «Non-commercial use only». `license.txt` разрешает commercial use, но vendor просит приобрести коммерческую лицензию; решение/покупка — до production. Сейчас installer собран на beta-лицензии.
- **AppPublisher:** `LEFTIK` — placeholder. Юридическое лицо/издатель ещё не подтверждены; перед production заменить `AppPublisher`, `VersionInfoCompany`, `VersionInfoCopyright`.
- **Portable ZIP:** не создавался. Подписанный `bin\LEFTIK.exe` можно использовать как portable-копию, но манифест требует администратора, а приложение пишет `C:\ProgramData\LEFTIK`; это не «тихий» portable-режим.
- **Public beta:** 100–500 пользователей — следующий шаг после этого delivery. Финальный 2026.1 — только после beta без критических багов.
- **Intel APO:** Intel-машины с APO нет. Детект службы удалён как недостоверный; проверены только условие и текст баннера (source-policy) на AMD-хосте. Баннер визуально не снимался: на этой машине `isIntelPlatform == false`. Реальное поведение на Intel 12th gen+ с APO/DSA не подтверждено.
- **Xbox app:** приложения Xbox на машине нет. Для детекта и скриншота временно создавался пакет `Microsoft.GamingApp_LEFTIKTest`, затем удалён. Реальный запуск Xbox mode с установленным приложением не проверялся.
- **URI:** `ms-settings:gaming-fullscreen` проверен на build 26300. На более старых сборках страницы может не быть — для этого оставлен fallback. Кнопка вызывает тот же URI; отдельный end-to-end клик по кнопке не автоматизировался.
- **PGO:** PGO-бинарник не шипится. Короткий прогрев (без 5-минутного ETW, откатов, тумблеров и полного UI) не покрывает hot paths; `−3,47%` размера — не метрика. Процедура правильного warmup и сравнение latency описаны в `PGO.md`; PGO — в 2026.1.1.
- **MSVC:** сборка выполнена и проверена только на этой машине (Win11 `10.0.26300`, AMD). Smoke-тест подтверждает запуск, отрисовку и иконку, но не заменяет прогон на чистой VM и на Intel-конфигурации. Бинарник статически слинкован с CRT, поэтому VC++ Redistributable не требуется.
- **Smoke-тест и трей:** при `BackgroundMode=1` закрытие окна скрывает его в трей — это штатное поведение, а не зависание. Процесс остановлен после снимка; Application Error Log чист.
- **ТУРБО/Magic Boost — coverage gap:** `ApplyTurboBoost()`/`ApplyMagicBoost()` end-to-end не исполнялись. В частности, не проверялись `KillBackgroundApps`, `TimerMgr::Acquire`, `ClearStandbyList`, `ClearAllShaderCaches`. Проверены только состав списка и guard-логика. Это принято как ограничение текущего прогона, но должно быть закрыто перед public beta на реальной машине.
- **Uninstaller delete-data:** ветка «Да» проверена только на диагностическом клоне `LEFTIK_UninstallTest`, чтобы не удалить реальные данные. На реальных путях она не исполнялась; логика прямолинейная, но это остаётся ограничением доказательства.
- **2b.1:** не начат. MSI Mode Full требует тестов на 3+ конфигурациях; при отвале Realtek/USB сервис должен удаляться из `targets[]`, а не просто предупреждать.

## 7. Delivery

В среде **нет git** (`git` не установлен, каталог проекта не является репозиторием), поэтому вместо коммита/PR подготовлен архив:

```
LEFTIK_P17_2a_delivery.zip
```

Состав `LEFTIK_P17_2a_delivery`:

```
bin\LEFTIK.exe                         — подписанный MSVC production exe (ship)
bin\LEFTIK_2026.1_x64_Setup.exe        — подписанный Inno Setup installer
bin\reference\LEFTIK_mingw.exe         — MinGW reference/fallback
installer\LEFTIK.iss                   — production Inno Setup script
src\                                   — исходники + CMakeLists.txt, LEFTIK.rc,
                                         resource.h, leftik.ico, LEFTIK.manifest,
                                         LEFTIK_manifest.rc
signing\LEFTIK_Beta_CodeSigning.cer    — только публичный beta-сертификат
SIGNING.txt                            — параметры подписи и проверка
screenshots\                           — smoke, иконки, warning/data prompt, Xbox mode
logs\                                  — сборка, ресурсы, версия, installer, UIA-тесты
P17_2a_REPORT.md                       — этот отчёт
PGO.md                                 — процедура PGO warmup
SHA256SUMS.txt                         — хэши всех файлов поставки
```

Исключено из delivery:

- PGO-бинарник `4 289 231 B / 59C6DE35...`;
- PFX и пароль beta-сертификата;
- диагностический `LEFTIK_UninstallTest.iss`, `LEFTIK_UninstallTest_Setup.exe` и его install-логи;
- `LEFTIK_UninstallTest` ключи/каталоги (уже удалены с машины).

Public beta: 100–500 пользователей, затем финальный 2026.1 без критических багов. Отдельно остаётся решение по EV-сертификату, коммерческой лицензии Inno Setup и юридическому издателю.

## 8. Следующий шаг

1. Public beta 2026.1 для 100–500 пользователей на базе этого delivery.
2. Собрать beta-фидбек; финальный 2026.1 выпустить без критических багов.
3. До production: EV code-signing cert, проверка SmartScreen-репутации, коммерческая лицензия Inno Setup, подтверждение `AppPublisher`/юридического лица.
4. Закрыть coverage gap `KillBackgroundApps`/`TimerMgr::Acquire`/`ClearStandbyList`/`ClearAllShaderCaches` на реальной машине.
5. PGO — только в 2026.1.1 после правильного warmup из `PGO.md`.
6. 2b.1 MSI Mode Full не трогать до появления hardware-стенда (Intel/Realtek NIC + NVMe, Wi-Fi-only laptop, USB-хаб + мышь + клава).
