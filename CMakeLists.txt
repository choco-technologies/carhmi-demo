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
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/carhmi_page.cmake)
set(CARHMI_SCREEN           480x272 CACHE STRING "Size of the display the page is laid out for")
carhmi_convert_page(
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/car_hmi.html
    ${CARHMI_SCREEN}
    hmi-screen                      # the element of the page that is the screen
    carhmi
    ${CMAKE_BINARY_DIR}/view-src
)

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
)

# libdmview: the display claimed, the channel of the claim (0.10).
# dmmp3: the MP3 files. libdmaudio: playing them. dmlist: the list of the files.
# dmutc: the time of the clock.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.7
    dmosi
    libdmview@>=0.10
    libsystemd
    dmmp3@>=0.1
    libdmaudio@>=0.1
    dmlist
    dmutc
)

enable_testing()
add_subdirectory(tests)
