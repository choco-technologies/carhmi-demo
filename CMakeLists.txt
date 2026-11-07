cmake_minimum_required(VERSION 3.18)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED DMOD_MODULE_VERSION)
    set(DMOD_MODULE_VERSION "0.1" CACHE STRING "DMOD module version")
endif()

# If a local dmod checkout is provided, use it instead of fetching from git
if(DEFINED DMOD_DIR AND NOT "${DMOD_DIR}" STREQUAL "")
    set(FETCHCONTENT_SOURCE_DIR_DMOD "${DMOD_DIR}" CACHE PATH "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    dmod
    GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
    GIT_TAG        develop
)

set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")
set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")

FetchContent_MakeAvailable(dmod)

project(carhmi VERSION ${DMOD_MODULE_VERSION}
    DESCRIPTION "carhmi DMOD application module"
    LANGUAGES C CXX)

set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory")
set(DMOD_SCRIPTS_DIR ${DMOD_DIR}/scripts CACHE PATH "DMOD scripts directory")
include(${DMOD_DIR}/paths.cmake)
dmod_setup_external_module()

# ======================================================================
#               The view: assets/car_hmi.html
# ======================================================================
# The page (HTML, Tailwind CSS, JavaScript) is converted by todmvs to a .dmvs
# with the fonts and images it draws (see cmake/carhmi_page.cmake); dmod
# turns them into the views (.dmv, .dmvf, .dmvi) at build time. The views are
# too big for a microcontroller's flash, so they are a package of their own
# (carhmi_view) that goes where the view should be - the SD card.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_literki.py
    RESULT_VARIABLE LESSON_DATA_RESULT)
if(NOT LESSON_DATA_RESULT EQUAL 0)
    message(FATAL_ERROR "Cannot generate Literki vocabulary")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/games/literki/words.json
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_literki.py)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/carhmi_page.cmake)
set(DMOD_ASSETS_SCREENS 480x272 320x240)
foreach(SCREEN IN LISTS DMOD_ASSETS_SCREENS)
    carhmi_convert_page(
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/car_hmi.html
        ${SCREEN}
        hmi-screen
        carhmi
        ${CMAKE_BINARY_DIR}/view-src/${SCREEN}
    )
endforeach()

# ======================================================================
#               carhmi_view - the view (data)
# ======================================================================
set(DMOD_MODULE_NAME        carhmi_view)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/carhmi_view.dmr)
set(DMOD_ASSETS_PATHS       ${CMAKE_BINARY_DIR}/view-src)
set(DMOD_MAL_IMPLS          )

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/carhmi_view.c
)

# Audio recordings stay next to the installed view, separate from music.
file(GLOB LESSON_MP3_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/games/literki/audio/*.mp3")
set(LESSON_AUDIO_COMMANDS)
foreach(SCREEN IN LISTS DMOD_ASSETS_SCREENS)
    list(APPEND LESSON_AUDIO_COMMANDS COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_literki_assets.py
        ${CMAKE_BINARY_DIR}/views/${SCREEN} --audio)
endforeach()
add_custom_target(literki_audio_assets ${LESSON_AUDIO_COMMANDS} VERBATIM)
add_dependencies(carhmi_view literki_audio_assets)
set_property(TARGET carhmi_view APPEND PROPERTY LINK_DEPENDS ${LESSON_MP3_FILES})

# Preserve the historical host-build path for tools that open the default view.
# The package still contains only the separate, self-contained variant bundles.
add_custom_target(carhmi_default_view ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/views/480x272 ${CMAKE_BINARY_DIR}/views
    DEPENDS carhmi_view
    VERBATIM)

# Every normal/release build verifies the contents of the generated ZIP.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(check_carhmi_views ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_view_package.py ${CMAKE_BINARY_DIR}
    DEPENDS carhmi_view
    VERBATIM)

# ======================================================================
#               carhmi - the application (the service)
# ======================================================================
set(DMOD_MODULE_NAME        carhmi)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         4096)
set(DMOD_PRIORITY           1)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/carhmi.dmr)
set(DMOD_MAL_IMPLS          )

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/carhmi.c
    src/player.c
    src/local_time.c
    src/volume.c
    src/covers.c
    src/playstate.c
    src/sfx.c
    src/fuel.c
    src/letters.c
    src/games.c
    src/lesson_audio.c
    src/sfx_synth.c
)

# libdmview: the display claimed, the channel of the claim (0.10).
# dmmp3: the MP3 files. libdmaudio: playing them. dmlist: the list of the files.
# dmutc: the time of the clock. dmimg, libtodmvi: the covers - read by the
# decoders dmimg_jpeg, dmimg_png (loaded by name), made into images.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.7
    dmosi
    libdmview@>=0.15.1
    libsystemd
    dmmp3@>=0.1
    libdmaudio@>=0.3
    dmlist
    dmutc
    dmimg
    libtodmvi
    dmimg_jpeg
    dmimg_png
)

enable_testing()
add_subdirectory(tests)
