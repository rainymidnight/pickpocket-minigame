# It's recommended to set a minimum CMake version.
# If you use CMake features from higher versions, update this to match.
cmake_minimum_required(VERSION 3.21)

# Single source for the plugin version: DLL metadata, SKSE metadata and startup version logging.
project(PickpocketMinigame VERSION 1.0.0 LANGUAGES CXX)

# Windows resources and SKSE store four numeric version components.
foreach(COMPONENT IN ITEMS PATCH TWEAK)
    if(NOT PROJECT_VERSION_${COMPONENT})
        set(PROJECT_VERSION_${COMPONENT} 0)
    endif()
endforeach()

set(AUTHOR_NAME "")

# Set this with -DOUTPUT_FOLDER=... if you want a specific Skyrim Data folder
# or mod-manager mod root for post-build deployment.
set(OUTPUT_FOLDER "" CACHE PATH "Optional Skyrim Data folder or mod-manager mod root for post-build deployment")

# If you're not using a mod manager, you probably want the SKSE plugin to go
# inside of your Skyrim "Data" folder.
#
# To do this automatically, set the `SKYRIM_FOLDER` environment variable
# to the path of your Skyrim Special Edition folder
if(NOT OUTPUT_FOLDER AND DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data" CACHE PATH "Optional Skyrim Data folder or mod-manager mod root for post-build deployment" FORCE)
endif()

# If you're using Mod Organizer 2 or Vortex, you might want this to go inside
# of your "mods" folder, inside of a subfolder named "<your mod>".
#
# To do this automatically, set the `SKYRIM_MODS_FOLDER` environment variable
# to the path of your "mods" folder
if(NOT OUTPUT_FOLDER AND DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}" CACHE PATH "Optional Skyrim Data folder or mod-manager mod root for post-build deployment" FORCE)
endif()

# Otherwise, you can set OUTPUT_FOLDER to any place you'd like :)
# set(OUTPUT_FOLDER "C:/path/to/any/folder")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibSSE-NG/CMakeLists.txt"
    OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibSSE-NG/extern/openvr/headers/openvr.h"
    OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/skse-menu-framework/SKSEMenuFramework.h")
    message(FATAL_ERROR "Initialize dependencies with: git submodule update --init --recursive")
endif()
add_subdirectory(external/CommonLibSSE-NG)

# Own the SKSE metadata so Address Library v5 is declared without patching CommonLib.
configure_file(cmake/PluginInfo.cpp.in PluginInfo.cpp @ONLY)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY
)

add_library(${PROJECT_NAME} SHARED
    "${CMAKE_CURRENT_BINARY_DIR}/PluginInfo.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    src/plugin.cpp
    src/Config.cpp
    src/Input.cpp
    src/UI.cpp
    src/UIPhase1.cpp
    src/UIPhase2.cpp
    src/UISettings.cpp
    src/SkyUIIcons.cpp
    src/PickpocketInventory.cpp
    src/PickpocketSession.cpp
    src/PickpocketEventHandler.cpp
    src/PickpocketPhase1.cpp
    src/PickpocketVanillaDeposit.cpp
    src/PickpocketHooks.cpp
    src/PickpocketOrdinatorCompat.cpp
    src/PickpocketRevealRules.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) # <--- use C++23 standard
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h) # <--- PCH.h is required!
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/skse-menu-framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/mesh-rendering-framework"
)

set(PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package")
set(PACKAGE_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/package")
set(PACKAGE_ARCHIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(PACKAGE_ARCHIVE "${PACKAGE_ARCHIVE_DIR}/${PROJECT_NAME}.zip")

add_custom_target(package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PACKAGE_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${PACKAGE_STAGE_DIR}/SKSE/Plugins"
        "${PACKAGE_ARCHIVE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PACKAGE_SOURCE_DIR}"
        "${PACKAGE_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${PACKAGE_STAGE_DIR}/SKSE/Plugins/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${PACKAGE_STAGE_DIR}"
        "${CMAKE_COMMAND}"
        "-DPACKAGE_STAGE_DIR=${PACKAGE_STAGE_DIR}"
        "-DPACKAGE_ARCHIVE=${PACKAGE_ARCHIVE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Package.cmake"
    DEPENDS "${PROJECT_NAME}"
    VERBATIM
)

# When your SKSE .dll is compiled, this will automatically copy the .dll into your mods folder.
# Only works if OUTPUT_FOLDER is configured directly or through the supported environment variables.
if(OUTPUT_FOLDER)
    # If you specify an <OUTPUT_FOLDER> (including via environment variables)
    # then we'll copy your mod files into Skyrim or a mod manager for you!

    # Copy the SKSE plugin .dll files into the SKSE/Plugins/ folder
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    set(PACKAGE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/package")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    # Copy matching symbols for configurations that generate them.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()

    if(EXISTS "${PACKAGE_FOLDER}")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PACKAGE_FOLDER}" "${OUTPUT_FOLDER}"
            VERBATIM
        )
    endif()
endif()
