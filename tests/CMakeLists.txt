set(DMOD_MODULE_NAME        test_carhmi)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    carhmi_test.c
    ../src/local_time.c
    ../src/volume.c
    ../src/covers.c
    ../src/playstate.c
    ../src/player.c
)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# dmutc: its calendar conversions (local_time.c). libdmaudio: the device volume.c sets (dmdrvi: its types)
# covers.c and player.c: the MP3 files (dmmp3, dmlist), their pictures made into images (dmimg, the decoders, libtodmvi; libdmview: the .dmvi format)
dmod_link_modules(${DMOD_MODULE_NAME}
    dmutc
    dmdrvi
    libdmaudio
    dmosi
    dmmp3
    dmlist
    dmimg
    libtodmvi
    libdmview
    dmimg_jpeg
    dmimg_png
)

# The MP3 files the covers are made of (tests/fixtures, made with ffmpeg)
target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE CARHMI_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Register the test module with ctest - this replays the same two steps CI
# runs (see .github/templates/ci.yml.template): install the test module's
# dependencies with dmf-get (and the decoders of the covers, loaded by name),
# then execute it through dmod_loader. The test's
# exit code equals its number of failed steps (see dmod_add_test).
find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}-local.dmd -y && ${DMF_GET_EXECUTABLE} install dmimg_jpeg -y && ${DMF_GET_EXECUTABLE} install dmimg_png -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf"
    )
else()
    message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${DMOD_MODULE_NAME}. Build the tools first with -DDMOD_BUILD_TOOLS=ON")
endif()
