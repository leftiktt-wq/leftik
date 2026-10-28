# LEFTIK — LTCG / PGO

Пакет #17, пункт 2a.6.

## Статус 2026.1

PGO-инфраструктура готова и проверена, но **PGO-бинарник в 2026.1 не
шипится**. Профиль, собранный коротким warmup, не покрывает горячие пути
(`EtwEventRecordCallback`, `CalcCrc32`, `GetJournalGroups`,
`RestoreEntryLine`), поэтому `-3.158%` размера — это не метрика
производительности, а возможное удаление достижимых cold-веток.

Production 2026.1 собирается без PGO. Основной бинарник — MSVC:

```powershell
cmake -B build-msvc -G "Visual Studio 17 2022" -A x64 -DLEFTIK_PGO=OFF
cmake --build build-msvc --config Release --clean-first
```

MinGW остаётся reference/fallback:

```powershell
cmake -B build-mingw -DLEFTIK_PGO=OFF
cmake --build build-mingw --config Release --clean-first
```

PGO вернём в 2026.1.1 после правильного warmup по процедуре ниже.

## Важно про CMake

`cmake --build` **не переконфигурирует** проект. Флаги PGO выбираются на этапе
конфигурации, поэтому между генерирующим и использующим проходом нужно снова
выполнить `cmake -B <build> ...`. Иначе второй проход останется с
`-fprofile-generate` (MinGW) или `/GENPROFILE` (MSVC).

## MinGW (проверяемый тулчейн)

```powershell
# Проход 1: инструментированная сборка
cmake -B build-pgo -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DLEFTIK_PGO=ON
cmake --build build-pgo --config Release

# Прогрев профиля (полный сценарий — см. раздел ниже)
.\build-pgo\Release\LEFTIK.exe

# Проход 2: повторная конфигурация — CMake увидит pgo/*.gcda и включит -fprofile-use
cmake -B build-pgo -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DLEFTIK_PGO=ON
cmake --build build-pgo --config Release --clean-first
```

Профиль лежит в `build-pgo/pgo/**.gcda`. Если файлов нет — второй проход
останется генерирующим; это честно видно в `CMakeCache`/выводе configure
(`LEFTIK PGO: generating...` вместо `using...`).

## MSVC

```powershell
# Проход 1
cmake -B build-msvc -G "Visual Studio 17 2022" -A x64 -DLEFTIK_PGO=ON
cmake --build build-msvc --config Release

# Прогрев
.\build-msvc\Release\LEFTIK.exe

# Слить .pgc в .pgd (pgomgr лежит рядом с link.exe)
pgomgr /merge build-msvc\pgo\*.pgc build-msvc\pgo\LEFTIK.pgd

# Проход 2: /USEPROFILE вместо /GENPROFILE
cmake -B build-msvc -G "Visual Studio 17 2022" -A x64 -DLEFTIK_PGO=ON
cmake --build build-msvc --config Release --clean-first
```

`/GL` и `/LTCG` включены и без `LEFTIK_PGO`: это штатный релизный режим MSVC.
PGO лишь добавляет `/GENPROFILE` или `/USEPROFILE`.

## Правильный warmup для будущего релиза

Короткий запуск «открыл-закрыл» профиль не прогревает. Для 2026.1.1 нужен
полный цикл на реальной машине и реальной игре.

1. **Собрать генерацию.** `cmake -B build-pgo -DLEFTIK_PGO=ON`, дождаться
   сообщения `LEFTIK PGO: generating profile`.
2. **Прогнать полный цикл, не убивая процесс.**
   - ETW-бенчмарк: 5 минут на реальной игре (не синтетика), с включённым
     мониторингом.
   - Откат группы `turbo-boost`: Восстановление → Diff → откат.
   - Кнопка **ПЕРЕСКАНИРОВАТЬ** в диагностике.
   - Тумблеры `1002`, `1014`, `1017` — включить и выключить каждый.
   - Открыть все 8 страниц UI.
   - Закрыть приложение штатно (профиль `.gcda`/`.pgc` сбрасывается на
     выходе; `Stop-Process` может оставить профиль неполным).
3. **Пересобрать с профилем.**
   - MinGW: повторная конфигурация `-DLEFTIK_PGO=ON` (CMake увидит `.gcda`)
     и сборка с `-fprofile-use -fprofile-correction`; проверить в выводе
     `LEFTIK PGO: using ...`.
   - MSVC: `pgomgr /merge` и повторная конфигурация с `/USEPROFILE`.
4. **Сравнивать latency, а не размер.** Замерять `QueryPerformanceCounter`
   вокруг `RestoreEntryLine` и `CalcCrc32` в debug-логе на одинаковом
   сценарии warmup, плюс собственный ETW-бенчмарк. Смотреть p50/p95/max и
   число вызовов; размер бинарника — только вторичная диагностика.
5. **Критерий приёмки.** Ни один горячий путь не деградировал; улучшение
   latency подтверждено на повторном прогоне. Если профиль дал только
   уменьшение размера — PGO не шипится.

## Что сравнивать

- latency горячих путей (`RestoreEntryLine`, `CalcCrc32`, ETW callback) —
  p50/p95/max на одинаковом сценарии;
- собственный ETW-бенчмарк до/после;
- отсутствие регрессии state-machine (6 сценариев 1.1 + сценарии 2a);
- размер и SHA256 — справочно, не как доказательство ускорения.

Обещанного «+5–10%» на GDI+ UI нет: эффект возможен только на горячих путях
`system.cpp` (ETW callback, CRC32, разбор журнала). Поэтому решение о PGO
принимается по latency, а не по размеру exe.
