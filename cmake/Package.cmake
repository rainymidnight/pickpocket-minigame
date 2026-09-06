if(NOT DEFINED PACKAGE_STAGE_DIR OR NOT DEFINED PACKAGE_ARCHIVE)
    message(FATAL_ERROR "PACKAGE_STAGE_DIR and PACKAGE_ARCHIVE are required")
endif()

set(REQUIRED_PACKAGE_FILES
    "SKSE/Plugins/PickpocketMinigame.dll"
    "SKSE/Plugins/PickpocketMinigame.ini"
    "SKSE/Plugins/PerkAdjuster/PickpocketMinigame.json"
)

foreach(REQUIRED_FILE IN LISTS REQUIRED_PACKAGE_FILES)
    if(NOT EXISTS "${PACKAGE_STAGE_DIR}/${REQUIRED_FILE}")
        message(FATAL_ERROR "Missing package file: ${REQUIRED_FILE}")
    endif()
endforeach()

file(GLOB_RECURSE PACKAGE_FILES
    LIST_DIRECTORIES false
    RELATIVE "${PACKAGE_STAGE_DIR}"
    "${PACKAGE_STAGE_DIR}/*"
)
list(SORT PACKAGE_FILES)

file(ARCHIVE_CREATE
    OUTPUT "${PACKAGE_ARCHIVE}"
    PATHS ${PACKAGE_FILES}
    FORMAT zip
    COMPRESSION Deflate
)

message(STATUS "Created ${PACKAGE_ARCHIVE}")
