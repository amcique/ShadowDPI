cmake_minimum_required(VERSION 3.25)

project(ShadowDPI VERSION 0.4.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "ShadowDPI targets Windows only.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR
        "ShadowDPI requires MSVC (Visual Studio 2022 or newer).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ShadowDPI only supports 64-bit builds.")
endif()

set(SHADOWDPI_VERSION_DOTTED "${PROJECT_VERSION}.0")
string(REPLACE "." "," SHADOWDPI_VERSION_COMMA "${SHADOWDPI_VERSION_DOTTED}")

# Keep the shipped executable self-contained: a static CRT means users only
# need WinDivert.dll at runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Let the compiler optimize across translation unit boundaries in Release
# builds. The packet loop is small enough that cross-TU inlining matters.
include(CheckIPOSupported)
check_ipo_supported(RESULT SHADOWDPI_IPO_SUPPORTED
    OUTPUT SHADOWDPI_IPO_ERROR
    LANGUAGES CXX)
if(SHADOWDPI_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "Interprocedural optimization enabled for Release builds")
else()
    message(STATUS "Interprocedural optimization unavailable: ${SHADOWDPI_IPO_ERROR}")
endif()

option(SHADOWDPI_BUILD_TESTS "Build the test suites and the parser benchmark" ON)
option(SHADOWDPI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SHADOWDPI_REQUIRE_WINDIVERT_RUNTIME
    "Fail configuration when WinDivert.dll or WinDivert64.sys is missing" OFF)

if(SHADOWDPI_BUILD_TESTS)
    enable_testing()
endif()

set(SHADOWDPI_WARNING_FLAGS /W4 /permissive- /Zc:__cplusplus /utf-8)
if(SHADOWDPI_WARNINGS_AS_ERRORS)
    list(APPEND SHADOWDPI_WARNING_FLAGS /WX)
endif()

set(WINDIVERT_ROOT "$ENV{WINDIVERT_ROOT}" CACHE PATH
    "Root directory of the WinDivert distribution")

# ---------------------------------------------------------------------------
# Core library: WinDivert-independent logic, no driver dependency.
# ---------------------------------------------------------------------------
add_library(shadowdpi_core STATIC
    src/core/util.cpp
    src/core/log.cpp
    src/core/config.cpp
    src/core/cli.cpp
    src/core/filter.cpp
    src/core/http.cpp
    src/core/tls.cpp
    src/core/blacklist.cpp
    src/core/ttl_tracker.cpp
    src/core/dns_tracker.cpp
    src/core/fake_payloads.cpp
)

target_include_directories(shadowdpi_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(shadowdpi_core PUBLIC cxx_std_20)
target_link_libraries(shadowdpi_core PUBLIC ws2_32)

target_compile_options(shadowdpi_core PRIVATE
    ${SHADOWDPI_WARNING_FLAGS}
    /guard:cf /sdl
)

# ---------------------------------------------------------------------------
# IPC: privileged service endpoint used by a future unprivileged GUI.
# ---------------------------------------------------------------------------
add_library(shadowdpi_ipc STATIC
    src/ipc/ipc.cpp
    src/ipc/ipc_pipe.cpp
    src/ipc/config_store.cpp
)

target_include_directories(shadowdpi_ipc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/windows"
)

target_compile_features(shadowdpi_ipc PUBLIC cxx_std_20)
target_link_libraries(shadowdpi_ipc PUBLIC advapi32)

target_compile_options(shadowdpi_ipc PRIVATE
    ${SHADOWDPI_WARNING_FLAGS}
    /guard:cf /sdl
)

# ---------------------------------------------------------------------------
# Windows executable: WinDivert integration.
# ---------------------------------------------------------------------------
find_path(WINDIVERT_INCLUDE_DIR
    NAMES windivert.h
    HINTS "${WINDIVERT_ROOT}/include" "${WINDIVERT_ROOT}"
)

set(WINDIVERT_ARCH "x64")
set(WINDIVERT_SYS_NAME "WinDivert64.sys")

if(SHADOWDPI_BUILD_TESTS)
    find_library(WINDIVERT_LIBRARY
        NAMES WinDivert
        HINTS "${WINDIVERT_ROOT}/${WINDIVERT_ARCH}" "${WINDIVERT_ROOT}"
    )
endif()

find_file(WINDIVERT_DLL
    NAMES WinDivert.dll
    HINTS "${WINDIVERT_ROOT}/${WINDIVERT_ARCH}" "${WINDIVERT_ROOT}"
)

find_file(WINDIVERT_SYS
    NAMES "${WINDIVERT_SYS_NAME}"
    HINTS "${WINDIVERT_ROOT}/${WINDIVERT_ARCH}" "${WINDIVERT_ROOT}"
)

if(NOT WINDIVERT_INCLUDE_DIR OR
   (SHADOWDPI_BUILD_TESTS AND NOT WINDIVERT_LIBRARY))
    message(FATAL_ERROR
        "WinDivert was not found. Set WINDIVERT_ROOT to the extracted "
        "WinDivert distribution (it must contain include/windivert.h and "
        "${WINDIVERT_ARCH}/WinDivert.lib).")
endif()

if(NOT WINDIVERT_DLL OR NOT WINDIVERT_SYS)
    if(SHADOWDPI_REQUIRE_WINDIVERT_RUNTIME)
        message(FATAL_ERROR
            "WinDivert runtime files are required but were not found. The "
            "build needs both WinDivert.dll and ${WINDIVERT_SYS_NAME} next to "
            "the executable. Set WINDIVERT_ROOT to a complete distribution.")
    endif()
    message(WARNING
        "WinDivert runtime files are missing from the distribution: "
        "the executable will build but cannot start without "
        "WinDivert.dll and ${WINDIVERT_SYS_NAME}.")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/shadowdpi.exe.manifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi.exe.manifest"
    @ONLY
)

# Resource paths are generated with absolute paths so the RC compiler does
# not depend on its working directory.
file(TO_NATIVE_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi.exe.manifest"
    SHADOWDPI_MANIFEST_RESOURCE)
string(REPLACE "\\" "\\\\" SHADOWDPI_MANIFEST_RESOURCE
    "${SHADOWDPI_MANIFEST_RESOURCE}")

# The icon is optional: drop your own resources/icon.ico in place and
# reconfigure to embed it. Without one the executable keeps the default
# Windows icon.
set(SHADOWDPI_ICON_ENTRY "")
set(SHADOWDPI_ICON_HASH "none")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico")
    file(TO_NATIVE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
        SHADOWDPI_ICON_RESOURCE)
    string(REPLACE "\\" "\\\\" SHADOWDPI_ICON_RESOURCE
        "${SHADOWDPI_ICON_RESOURCE}")
    set(SHADOWDPI_ICON_ENTRY
        "1 ICON \"${SHADOWDPI_ICON_RESOURCE}\"")
    # The hash is written into the generated .rc so that replacing the icon
    # changes the .rc text and forces the resource compiler to run again.
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
        SHADOWDPI_ICON_HASH)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico")
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/shadowdpi.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi.rc"
    @ONLY
)

add_executable(shadowdpi
    src/windows/main.cpp
    src/windows/application.cpp
    src/windows/lifecycle.cpp
    src/windows/service.cpp
    src/windows/packet.cpp
    src/windows/packet_engine.cpp
    src/windows/divert.cpp
    src/windows/fake_sender.cpp
    src/windows/windivert_api.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi.rc"
)

target_include_directories(shadowdpi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/windows"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    "${WINDIVERT_INCLUDE_DIR}"
)

# WinDivert is resolved at run time by windivert_api.cpp, so the executable has
# no static import of WinDivert.dll that the loader could resolve before main().
target_link_libraries(shadowdpi PRIVATE
    shadowdpi_core
    shadowdpi_ipc
)

target_compile_features(shadowdpi PRIVATE cxx_std_20)

target_compile_options(shadowdpi PRIVATE
    ${SHADOWDPI_WARNING_FLAGS}
    /guard:cf /sdl
)

target_link_options(shadowdpi PRIVATE
    /DYNAMICBASE /NXCOMPAT /MANIFEST:NO
)

# ---------------------------------------------------------------------------
# Tray GUI: unprivileged front end over the service IPC.
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/shadowdpi-gui.exe.manifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi-gui.exe.manifest"
    @ONLY
)

file(TO_NATIVE_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi-gui.exe.manifest"
    SHADOWDPI_GUI_MANIFEST_RESOURCE)
string(REPLACE "\\" "\\\\" SHADOWDPI_GUI_MANIFEST_RESOURCE
    "${SHADOWDPI_GUI_MANIFEST_RESOURCE}")
file(TO_NATIVE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
    SHADOWDPI_GUI_ICON_RESOURCE)
string(REPLACE "\\" "\\\\" SHADOWDPI_GUI_ICON_RESOURCE
    "${SHADOWDPI_GUI_ICON_RESOURCE}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/shadowdpi-gui.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi-gui.rc"
    @ONLY
)

add_executable(shadowdpi_gui WIN32
    src/gui/main.cpp
    src/gui/presets.cpp
    src/gui/strings.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/shadowdpi-gui.rc"
)

target_include_directories(shadowdpi_gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_link_libraries(shadowdpi_gui PRIVATE
    shadowdpi_ipc
    shell32
)

set_target_properties(shadowdpi_gui PROPERTIES OUTPUT_NAME "shadowdpi-gui")

target_compile_features(shadowdpi_gui PRIVATE cxx_std_20)
target_compile_options(shadowdpi_gui PRIVATE
    ${SHADOWDPI_WARNING_FLAGS}
    /guard:cf /sdl
)
target_link_options(shadowdpi_gui PRIVATE
    /DYNAMICBASE /NXCOMPAT /MANIFEST:NO
)

if(WINDIVERT_DLL)
    add_custom_command(TARGET shadowdpi POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${WINDIVERT_DLL}"
            "$<TARGET_FILE_DIR:shadowdpi>/WinDivert.dll"
        COMMENT "Copying WinDivert.dll next to shadowdpi.exe"
    )
endif()

if(WINDIVERT_SYS)
    add_custom_command(TARGET shadowdpi POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${WINDIVERT_SYS}"
            "$<TARGET_FILE_DIR:shadowdpi>/${WINDIVERT_SYS_NAME}"
        COMMENT "Copying ${WINDIVERT_SYS_NAME} next to shadowdpi.exe"
    )
endif()

install(TARGETS shadowdpi shadowdpi_gui RUNTIME DESTINATION .)
if(WINDIVERT_DLL)
    install(FILES "${WINDIVERT_DLL}" DESTINATION .)
endif()
if(WINDIVERT_SYS)
    install(FILES "${WINDIVERT_SYS}" DESTINATION .)
endif()

# Packet tooling (differential tests and the parser benchmark) links
# packet.cpp and needs WinDivert's user-mode helper DLL next to the
# executable. Neither needs the driver or administrator privileges.
if(SHADOWDPI_BUILD_TESTS)
    function(shadowdpi_add_packet_tool name)
        add_executable(${name}
            ${ARGN}
            src/windows/packet.cpp
            src/windows/windivert_api.cpp
        )
        target_include_directories(${name} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/windows"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${WINDIVERT_INCLUDE_DIR}"
        )
        target_link_libraries(${name} PRIVATE
            shadowdpi_core
            "${WINDIVERT_LIBRARY}"
        )
        target_compile_features(${name} PRIVATE cxx_std_20)

        target_compile_options(${name} PRIVATE
            ${SHADOWDPI_WARNING_FLAGS}
        )

        if(WINDIVERT_DLL)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${WINDIVERT_DLL}"
                    "$<TARGET_FILE_DIR:${name}>/WinDivert.dll"
                COMMENT "Copying WinDivert.dll next to ${name}"
            )
        endif()
    endfunction()

    shadowdpi_add_packet_tool(shadowdpi_windows_tests
        tests/windows_tests.cpp
        src/windows/packet_engine.cpp
        src/windows/fake_sender.cpp)
    add_test(NAME windows COMMAND shadowdpi_windows_tests)

    # The benchmark is a normal CTest entry: it prints timings and enforces
    # a speedup floor in optimized builds, and exits immediately without
    # measuring in unoptimized ones.
    shadowdpi_add_packet_tool(shadowdpi_parse_bench
        bench/parse_bench.cpp)
    add_test(NAME benchmark COMMAND shadowdpi_parse_bench --check)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SHADOWDPI_BUILD_TESTS)
    add_executable(shadowdpi_core_tests tests/core_tests.cpp)
    target_link_libraries(shadowdpi_core_tests PRIVATE shadowdpi_core)
    target_compile_features(shadowdpi_core_tests PRIVATE cxx_std_20)

    target_compile_options(shadowdpi_core_tests PRIVATE
        ${SHADOWDPI_WARNING_FLAGS}
    )

    add_test(NAME core COMMAND shadowdpi_core_tests)

    add_executable(shadowdpi_ipc_tests tests/ipc_tests.cpp)
    target_link_libraries(shadowdpi_ipc_tests PRIVATE
        shadowdpi_core
        shadowdpi_ipc
    )
    target_compile_features(shadowdpi_ipc_tests PRIVATE cxx_std_20)
    target_compile_options(shadowdpi_ipc_tests PRIVATE
        ${SHADOWDPI_WARNING_FLAGS}
    )
    add_test(NAME ipc COMMAND shadowdpi_ipc_tests)

    add_executable(shadowdpi_gui_tests tests/gui_tests.cpp
        src/gui/presets.cpp
        src/gui/strings.cpp
    )
    target_include_directories(shadowdpi_gui_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui"
    )
    target_compile_features(shadowdpi_gui_tests PRIVATE cxx_std_20)
    target_compile_options(shadowdpi_gui_tests PRIVATE
        ${SHADOWDPI_WARNING_FLAGS}
    )
    add_test(NAME gui COMMAND shadowdpi_gui_tests)
endif()
