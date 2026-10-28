cmake_minimum_required(VERSION 3.20)
project(LEFTIK LANGUAGES CXX RC)

# MSVC: статический CRT (/MT), чтобы LEFTIK.exe запускался без отдельного
# VC++ Redistributable. Для Release это осознанный выбор: один exe + системные DLL.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Для одно-конфигурационных генераторов (Ninja / MinGW Makefiles) по умолчанию Release.
# Visual Studio выбирает конфигурацию через --config Release.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Исходники. LEFTIK.rc содержит ICON + VERSIONINFO и включается всегда.
# RT_MANIFEST разделён: MSVC внедряет его линкером (/MANIFEST:EMBED),
# MinGW — отдельным LEFTIK_manifest.rc, иначе получится дубль (LNK1327).
set(LEFTIK_SOURCES
    core.cpp
    system.cpp
    tweaks.cpp
    ui_main.cpp
    LEFTIK.rc
)
if(MINGW)
    list(APPEND LEFTIK_SOURCES LEFTIK_manifest.rc)
endif()

add_executable(LEFTIK WIN32 ${LEFTIK_SOURCES})

target_include_directories(LEFTIK PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(LEFTIK PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
)

# Полный набор библиотек. Дублирование с #pragma comment(lib, ...) безопасно:
# MSVC просто не добавит библиотеку дважды.
target_link_libraries(LEFTIK PRIVATE
    user32 gdi32 advapi32 shell32 psapi gdiplus comdlg32
    uxtheme dwmapi ole32 oleaut32 uuid dxgi pdh wbemuuid
    bcrypt tdh winmm
)

if(MSVC)
    # /utf-8 критичен: исходник в UTF-8 (русские строки и emoji).
    target_compile_definitions(LEFTIK PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(LEFTIK PRIVATE /utf-8)

    # WIN32 в add_executable уже задаёт подсистему, /ENTRY страхует wWinMain.
    # Манифест (UAC requireAdministrator + PerMonitorV2) внедряет линкер.
    # LEFTIK.rc содержит только ICON/VERSIONINFO, поэтому RT_MANIFEST не дублируется.
    target_link_options(LEFTIK PRIVATE
        /SUBSYSTEM:WINDOWS
        /ENTRY:wWinMainCRTStartup
        "/MANIFEST:EMBED"
        # UAC уже описан в LEFTIK.manifest; иначе mt.exe получает два
        # requestedExecutionLevel (asInvoker от линкера + requireAdministrator)
        # и падает с LNK1327/c1010001.
        "/MANIFESTUAC:NO"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/LEFTIK.manifest"
    )

    # --- 2a.6: LTCG + PGO (MSVC) ---
    # /GL + /LTCG включены всегда: это штатная оптимизация релиза.
    # PGO двухпроходный и управляется -DLEFTIK_PGO=ON:
    #   проход 1 — /GENPROFILE, запуск exe, pgomgr /merge *.pgc LEFTIK.pgd;
    #   проход 2 — повторная конфигурация (уже с .pgd) и /USEPROFILE.
    # ВАЖНО: cmake --build сам по себе НЕ переконфигурирует проект, поэтому
    # между проходами нужно снова выполнить cmake -B <build> -DLEFTIK_PGO=ON.
    # Подробная процедура — в PGO.md.
    target_compile_options(LEFTIK PRIVATE /GL)
    target_link_options(LEFTIK PRIVATE /LTCG)

    option(LEFTIK_PGO "Enable PGO instrumentation" OFF)
    if(LEFTIK_PGO)
        set(LEFTIK_PGO_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgo")
        file(MAKE_DIRECTORY "${LEFTIK_PGO_DIR}")
        set(LEFTIK_PGD "${LEFTIK_PGO_DIR}/LEFTIK.pgd")
        target_link_options(LEFTIK PRIVATE "/PGD:${LEFTIK_PGD}")
        if(EXISTS "${LEFTIK_PGD}")
            target_link_options(LEFTIK PRIVATE /USEPROFILE)
            message(STATUS "LEFTIK PGO: using profile ${LEFTIK_PGD}")
        else()
            target_link_options(LEFTIK PRIVATE /GENPROFILE)
            message(STATUS "LEFTIK PGO: generating profile in ${LEFTIK_PGO_DIR}")
        endif()
    endif()
elseif(MINGW)
    # wWinMain в MinGW требует -municode; -mwindows добавляет WIN32_EXECUTABLE.
    target_link_options(LEFTIK PRIVATE -municode)

    # --- 2a.6: PGO (MinGW/GCC) ---
    # Генерирующий проход пишет .gcda в pgo/. Использующий проход включается
    # только когда .gcda реально появились: одного существования каталога мало
    # (его создаёт сама конфигурация). Между проходами обязательна повторная
    # конфигурация — иначе CMake оставит старые флаги.
    option(LEFTIK_PGO "Enable PGO instrumentation" OFF)
    if(LEFTIK_PGO)
        set(LEFTIK_PGO_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgo")
        file(MAKE_DIRECTORY "${LEFTIK_PGO_DIR}")
        file(GLOB_RECURSE LEFTIK_PGO_DATA "${LEFTIK_PGO_DIR}/*.gcda")
        if(LEFTIK_PGO_DATA)
            target_compile_options(LEFTIK PRIVATE
                -fprofile-use -fprofile-correction -Wno-missing-profile
                -fprofile-dir=${LEFTIK_PGO_DIR})
            message(STATUS "LEFTIK PGO: using ${LEFTIK_PGO_DIR} (${LEFTIK_PGO_DATA})")
        else()
            target_compile_options(LEFTIK PRIVATE
                -fprofile-generate -fprofile-dir=${LEFTIK_PGO_DIR})
            target_link_options(LEFTIK PRIVATE
                -fprofile-generate -fprofile-dir=${LEFTIK_PGO_DIR})
            message(STATUS "LEFTIK PGO: generating profile in ${LEFTIK_PGO_DIR}")
        endif()
    endif()
endif()

# ============================================================================
# P17 восстановление после утери исходников (пакеты #17/#18):
# регрессионный harness — отдельная цель, собирается ТОЛЬКО по -DBUILD_TESTS=ON.
# Исходники тестов живут в tests\ (в репозитории), не во временных каталогах.
# ============================================================================
option(BUILD_TESTS "Build LEFTIK_harness regression target" OFF)
if(BUILD_TESTS)
    set(_leftik_src_dir ${CMAKE_CURRENT_SOURCE_DIR})
    string(REPLACE "\\" "/" _leftik_src_dir ${_leftik_src_dir})
    add_executable(LEFTIK_harness
        tests/harness_unit.cpp
        tests/harness_main.cpp
    )
    target_include_directories(LEFTIK_harness PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(LEFTIK_harness PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
        LEFTIK_SOURCE_DIR="${_leftik_src_dir}"
    )
    target_link_libraries(LEFTIK_harness PRIVATE
        user32 gdi32 advapi32 shell32 psapi gdiplus comdlg32
        uxtheme dwmapi ole32 oleaut32 uuid dxgi pdh wbemuuid
        bcrypt tdh winmm
    )
    if(MSVC)
        target_compile_definitions(LEFTIK_harness PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(LEFTIK_harness PRIVATE /utf-8 /W3)
    endif()
endif()
