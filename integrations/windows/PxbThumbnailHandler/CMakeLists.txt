# CMakeLists.txt for the Windows thumbnail shell extension (COM DLL).
# It reuses the SAME parsing core as the app (gzip + image + reader + json),
# so there is a single source of truth for the format and zero extra imaging
# deps beyond the vendored stb + miniz. No GL is used here (GDI only).
cmake_minimum_required(VERSION 3.16)
project(PxbThumbnailHandler C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/core)

add_library(pxb_core STATIC
  ${CORE_DIR}/gzip.cpp
  ${CORE_DIR}/gzip_partial.cpp
  ${CORE_DIR}/image.cpp
  ${CORE_DIR}/fileutil.cpp
  ${CORE_DIR}/pxb_meta.cpp
  ${CORE_DIR}/pxb_reader.cpp
  ${TP}/miniz/miniz.c
  ${TP}/miniz/miniz_tdef.c
  ${TP}/miniz/miniz_tinfl.c)
target_include_directories(pxb_core PUBLIC
  ${CORE_DIR}
  ${TP}/stb
  ${TP}/miniz)

add_library(PxbThumbnailHandler SHARED
  PxbThumbnailHandler.cpp)
target_link_libraries(PxbThumbnailHandler PRIVATE pxb_core)
target_include_directories(PxbThumbnailHandler PRIVATE ${CORE_DIR})
set_target_properties(PxbThumbnailHandler PROPERTIES
  OUTPUT_NAME "PxbThumbnailHandler"
  PREFIX "")
target_sources(PxbThumbnailHandler PRIVATE PxbThumbnailHandler.def)
if(MINGW)
  # uuid omitted: GUIDs are defined locally via INITGUID in the source.
  target_link_libraries(PxbThumbnailHandler PRIVATE ole32 shell32 gdi32 user32)
  # Statically link the C++ runtime so the DLL doesn't drag libstdc++-6.dll /
  # libgcc_s_seh-1.dll; only libwinpthread-1.dll stays dynamic.
  target_link_options(PxbThumbnailHandler PRIVATE
    -static-libgcc -static-libstdc++ -Wl,-Bstatic,-lwinpthread,-Bdynamic)
endif()
