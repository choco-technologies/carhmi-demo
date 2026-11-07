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
    ../src/sfx_synth.c
    ../src/sfx.c
    ../src/fuel.c
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

# Off-screen integration test of the compiled navigation view and real contacts.
set(DMOD_MODULE_NAME test_navigation_input)
set(DMOD_STACK_SIZE 4096)
dmod_add_executable(${DMOD_MODULE_NAME} ${PROJECT_VERSION} navigation_input.c ../src/fuel.c ../src/sfx_synth.c)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
dmod_link_modules(${DMOD_MODULE_NAME} libdmview@>=0.15.1 dmdrvi libdmaudio)

if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    set(NAVIGATION_MANIFEST_ARGS "")
    if(DMOD_DMM_URL)
        list(APPEND NAVIGATION_MANIFEST_ARGS -m "${DMOD_DMM_URL}")
    endif()
    add_test(NAME test_navigation_dependencies
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            ${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/test_navigation_input-local.dmd -y ${NAVIGATION_MANIFEST_ARGS})
    set_tests_properties(test_navigation_dependencies PROPERTIES FIXTURES_SETUP navigation)
    foreach(SCREEN IN ITEMS 480x272 320x240)
        add_test(NAME test_navigation_${SCREEN}
            COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
                ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/test_navigation_input.dmf
                --args ${CMAKE_BINARY_DIR}/views/${SCREEN}/carhmi.dmv)
        set_tests_properties(test_navigation_${SCREEN} PROPERTIES FIXTURES_REQUIRED navigation)
    endforeach()
endif()

# Real input/renderer/controller test; audio endpoints are observed stubs.
set(DMOD_MODULE_NAME test_literki_input)
set(DMOD_STACK_SIZE 4096)
dmod_add_executable(${DMOD_MODULE_NAME} ${PROJECT_VERSION}
    literki_input.c ../src/games.c ../src/letters.c)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
dmod_link_modules(${DMOD_MODULE_NAME} libdmview@>=0.15.1 dmdrvi)
if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    add_test(NAME test_literki_dependencies
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            ${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/test_literki_input-local.dmd -y ${NAVIGATION_MANIFEST_ARGS})
    set_tests_properties(test_literki_dependencies PROPERTIES FIXTURES_SETUP literki)
    foreach(SCREEN IN ITEMS 480x272 320x240)
        add_test(NAME test_literki_${SCREEN}
            COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
                ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/test_literki_input.dmf
                --args ${CMAKE_BINARY_DIR}/views/${SCREEN}/carhmi.dmv)
        set_tests_properties(test_literki_${SCREEN} PROPERTIES FIXTURES_REQUIRED literki)
    endforeach()
endif()

set(DMOD_MODULE_NAME test_literki_sound)
set(DMOD_STACK_SIZE 3072)
dmod_add_executable(${DMOD_MODULE_NAME} ${PROJECT_VERSION}
    literki_sound.c ../src/lesson_audio.c ../src/sfx_synth.c)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
dmod_link_modules(${DMOD_MODULE_NAME} dmosi dmdrvi dmmp3 libdmaudio)

# Exhaustive binary and pixel audit for the exact 480x272 deployment assets.
set(IMAGE_AUDIT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/literki_image_audit.inc)
add_custom_command(OUTPUT ${IMAGE_AUDIT_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/generate_literki_image_audit.py
        ${CMAKE_BINARY_DIR}/views/480x272 ${IMAGE_AUDIT_HEADER}
    DEPENDS carhmi_view ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/generate_literki_image_audit.py
    VERBATIM)
set(DMOD_MODULE_NAME test_literki_images)
set(DMOD_STACK_SIZE 3072)
dmod_add_executable(${DMOD_MODULE_NAME} ${PROJECT_VERSION} literki_images.c ${IMAGE_AUDIT_HEADER})
add_dependencies(${DMOD_MODULE_NAME} carhmi_view)
target_include_directories(${DMOD_MODULE_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
dmod_link_modules(${DMOD_MODULE_NAME} libdmview dmosi dmdrvi)
