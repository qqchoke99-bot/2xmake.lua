cmake_minimum_required(VERSION 3.22)
project(SoundPhysicsLite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MOD_ID "sound_physics_lite" CACHE STRING "Mod id")
set(MOD_NAME "Sound Physics Lite" CACHE STRING "Display name")
set(MOD_AUTHOR "Ported for Levi" CACHE STRING "Author")
set(MOD_VERSION "0.8.0-mvp" CACHE STRING "Version")
set(MOD_LIBRARY_NAME "SoundPhysicsLite" CACHE STRING "Library name without lib/.so")

include(FetchContent)

# Official path used by QYCottage levilauncher-android-mod-template
FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG        0.2.2
    SOURCE_SUBDIR  cmake/preloader-sdk
)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.2.0
)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(preloader_android nlohmann_json fmt)
FetchContent_GetProperties(preloader_android)

if(NOT preloader_android_SOURCE_DIR)
    message(FATAL_ERROR
        "FetchContent failed for LiteLDev/preloader-android@0.2.2. "
        "Check network access to https://github.com/LiteLDev/preloader-android")
endif()

set(LEVI_PRELOADER_INCLUDE_DIR "${preloader_android_SOURCE_DIR}/include")
if(NOT EXISTS "${LEVI_PRELOADER_INCLUDE_DIR}/pl/Mod.hpp")
    message(FATAL_ERROR
        "preloader-android include missing pl/Mod.hpp at: ${LEVI_PRELOADER_INCLUDE_DIR}. "
        "Need public SDK headers from LiteLDev/preloader-android.")
endif()

if(NOT ANDROID)
    message(FATAL_ERROR "This mod must be configured with Android NDK toolchain (ANDROID=1).")
endif()

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported (got ${CMAKE_ANDROID_ARCH_ABI})")
endif()

option(LEVI_LINK_PRELOADER "Link preloader target" ON)
if(LEVI_LINK_PRELOADER AND NOT TARGET preloader)
    if(EXISTS "${preloader_android_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${preloader_android_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_preloader_android")
    else()
        message(WARNING "preloader CMakeLists not found; linking with unresolved symbols for runtime")
    endif()
endif()

add_library(${MOD_LIBRARY_NAME} SHARED
    src/SoundPhysicsLite.cpp
)

set_target_properties(${MOD_LIBRARY_NAME} PROPERTIES
    OUTPUT_NAME "${MOD_LIBRARY_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(${MOD_LIBRARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LEVI_PRELOADER_INCLUDE_DIR}
)

target_compile_options(${MOD_LIBRARY_NAME} PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fvisibility=hidden -fvisibility-inlines-hidden>
)

target_link_options(${MOD_LIBRARY_NAME} PRIVATE
    "-Wl,-z,max-page-size=16384"
)

find_library(log-lib log REQUIRED)
find_library(android-lib android REQUIRED)
find_library(dl-lib dl REQUIRED)

target_link_libraries(${MOD_LIBRARY_NAME} PRIVATE
    ${log-lib}
    ${android-lib}
    ${dl-lib}
    nlohmann_json::nlohmann_json
    fmt::fmt
)

if(TARGET preloader)
    target_link_libraries(${MOD_LIBRARY_NAME} PRIVATE preloader)
else()
    # Runtime provides preloader symbols when loaded by LeviLauncher
    target_link_options(${MOD_LIBRARY_NAME} PRIVATE "-Wl,--unresolved-symbols=ignore-all")
    message(STATUS "preloader target not linked; using unresolved-symbols=ignore-all")
endif()

if(CMAKE_STRIP)
    add_custom_command(TARGET ${MOD_LIBRARY_NAME} POST_BUILD
        COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${MOD_LIBRARY_NAME}>"
        COMMENT "Strip ${MOD_LIBRARY_NAME}"
        VERBATIM
    )
endif()

# Package flat levipack (CameraOverhaul-style: no nested mod id folder)
set(LEVI_PACK_DIR "${CMAKE_BINARY_DIR}/levipack_root")
set(LEVI_PACK_FILE "${CMAKE_BINARY_DIR}/SPL.levipack")

add_custom_target(levi_package ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LEVI_PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LEVI_PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${MOD_LIBRARY_NAME}>" "${LEVI_PACK_DIR}/lib${MOD_LIBRARY_NAME}.so"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${LEVI_PACK_DIR}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" "${LEVI_PACK_DIR}/icon.png"
    COMMAND ${CMAKE_COMMAND} -E chdir "${LEVI_PACK_DIR}"
            ${CMAKE_COMMAND} -E tar "cf" "${LEVI_PACK_FILE}" --format=zip
            "manifest.json" "lib${MOD_LIBRARY_NAME}.so" "icon.png"
    DEPENDS ${MOD_LIBRARY_NAME}
    COMMENT "Pack SPL.levipack (manifest + so + icon)"
    VERBATIM
)
