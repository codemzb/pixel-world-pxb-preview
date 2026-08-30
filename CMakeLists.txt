# CMakeLists.txt - pxb-preview
#
# Cross-platform build for the PXB preview program.
#
# Third-party sources are VENDORED under thirdparty/ (imgui, stb, miniz) so the
# build is fully offline and reproducible - no network access is required at
# configure time. The ONLY external dependency is SDL3, expected to be
# installed on the system.
#
# GL is resolved at runtime via SDL_GL_GetProcAddress (see src/app/gl_loader),
# so no glad / GL loader dependency is needed.
#
# If SDL3 is not on the default search path, point CMake at its install prefix
# via -DCMAKE_PREFIX_PATH=<path-to-sdl3>. On a typical MinGW setup that is
# enough; no further variables are required.
#
# Build:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
cmake_minimum_required(VERSION 3.16)
project(pxb_preview C CXX)   # C for vendored miniz.c, CXX for the app

# ---- App version (single source of truth: src/app/i18n.h kAppVersion) ------
# Parsed at configure time so every platform consumer tracks one field:
#   macOS .app bundle  -> MACOSX_BUNDLE_{BUNDLE,SHORT_VERSION_STRING} below
#   shell packagers    -> scripts/app_version.sh (same regex, POSIX side)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/app/i18n.h" _pxb_i18n)
string(REGEX MATCH "kAppVersion[ \t]*=[ \t]*\"([^\"]+)\"" _pxb_ver "${_pxb_i18n}")
set(PXB_APP_VERSION "${CMAKE_MATCH_1}")
if(NOT PXB_APP_VERSION)
  message(FATAL_ERROR "cannot parse kAppVersion from src/app/i18n.h")
endif()
message(STATUS "PXB Preview app version: ${PXB_APP_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)

# ---- SDL3 (system, optional FetchContent fallback) -----------------------
option(PXB_USE_SYSTEM_SDL3 "Use an installed SDL3 via find_package instead of FetchContent" ON)
if(PXB_USE_SYSTEM_SDL3)
  find_package(SDL3 REQUIRED)
  if(TARGET SDL3::SDL3-static)
    set(PXB_SDL_TARGET SDL3::SDL3-static)
  else()
    set(PXB_SDL_TARGET SDL3::SDL3)
  endif()
else()
  include(FetchContent)
  FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.0
  )
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST    OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(sdl3)
  set(PXB_SDL_TARGET SDL3::SDL3-static)
endif()

# ---- Dear ImGui (vendored) -----------------------------------------------
set(IMGUI_DIR ${TP}/imgui)
set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ---- Application + vendored core deps ------------------------------------
set(APP_SOURCES
  src/main.cpp
  src/core/gzip.cpp
  src/core/gzip_partial.cpp
  src/core/pxb_meta.cpp
  src/core/pxb_cache.cpp
  src/core/image.cpp
  src/core/fileutil.cpp
  src/core/pxb_reader.cpp
  src/app/renderer.cpp
  src/app/gl_loader.cpp
  src/app/app.cpp
  src/app/ui.cpp
  src/app/regutil.cpp
  src/app/i18n.cpp
  ${TP}/miniz/miniz.c
  ${TP}/miniz/miniz_tdef.c
  ${TP}/miniz/miniz_tinfl.c
)

add_executable(pxb-preview ${APP_SOURCES} ${IMGUI_SOURCES})

# Mark as a GUI (windows-subsystem) executable: no console flashes when the user
# double-clicks a .pxb file in Explorer. On MinGW this adds -mwindows to the
# linker; on MSVC it adds /SUBSYSTEM:WINDOWS.
if(WIN32)
  set_target_properties(pxb-preview PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(pxb-preview PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${TP}/stb
  ${TP}/miniz
)

target_link_libraries(pxb-preview PRIVATE ${PXB_SDL_TARGET})

# imgui_impl_sdl3 still references the SDL2-era boolean aliases
# SDL_TRUE/SDL_FALSE; SDL3 only provides them when SDL_ENABLE_OLD_NAMES is
# defined (otherwise SDL_oldnames.h turns them into intentional errors). Re-
# evaluate this define whenever the vendored imgui is upgraded.
target_compile_definitions(pxb-preview PRIVATE SDL_ENABLE_OLD_NAMES)

# GL: resolved at runtime; link the system GL only where the loader's
# GetProcAddress fallback needs it (Windows opengl32 provides GL 1.1 symbols).
if(WIN32)
  target_link_libraries(pxb-preview PRIVATE opengl32)
  # regutil.cpp: ShellExecuteW / SHChangeNotify (shell32), UserChoice query.
  target_link_libraries(pxb-preview PRIVATE shell32)
  # main.cpp: GetProcessMemoryInfo for the status-bar memory counters.
  target_link_libraries(pxb-preview PRIVATE psapi)
  # Embed the Pixel World icon into pxb-preview.exe so the title bar, taskbar
  # and Explorer use it. windres runs from the build directory and looks for
  # "icon.ico" relative to it, so copy the source icon there first.
  target_sources(pxb-preview PRIVATE assets/icon.rc)
  add_custom_command(TARGET pxb-preview PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/icon.ico"
            "${CMAKE_CURRENT_BINARY_DIR}/icon.ico"
    COMMENT "Copying assets/icon.ico for resource embedding")
endif()

# Statically link the C++ runtime so the distributed exe only needs SDL3.dll
# and the MinGW pthread runtime DLL next to it (no libstdc++-6.dll /
# libgcc_s_seh-1.dll). NOTE: despite -Bstatic, libwinpthread-1.dll still ends
# up a dynamic import (verified via objdump -p) — the packaging script ships
# it from the toolchain.
if(WIN32 AND MINGW)
  target_link_options(pxb-preview PRIVATE
    -static-libgcc -static-libstdc++ -Wl,-Bstatic,-lwinpthread,-Bdynamic)
endif()

if(UNIX AND NOT APPLE)
  find_package(OpenGL REQUIRED)
  target_link_libraries(pxb-preview PRIVATE dl pthread OpenGL::GL)
endif()

if(APPLE)
  set_target_properties(pxb-preview PROPERTIES MACOSX_BUNDLE TRUE)
  set_target_properties(pxb-preview PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/integrations/macos/Info.plist
    # Feed the ${MACOSX_BUNDLE_*} placeholders in that Info.plist template.
    MACOSX_BUNDLE_BUNDLE_VERSION "${PXB_APP_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PXB_APP_VERSION}")
  find_library(COCOA_LIB Cocoa)
  find_library(OPENGL_LIB OpenGL)
  target_link_libraries(pxb-preview PRIVATE ${COCOA_LIB} ${OPENGL_LIB})
endif()

# Install target for packaging scripts. BUNDLE is required by CMake for the
# macOS MACOSX_BUNDLE executable ("given no BUNDLE DESTINATION" configure
# error otherwise).
install(TARGETS pxb-preview
  RUNTIME DESTINATION bin
  BUNDLE  DESTINATION .)
