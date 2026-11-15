# ======================================================================
#               The page -> the view's sources
# ======================================================================
# assets/car_hmi.html is a page made in HTML, Tailwind CSS and JavaScript.
# todmvs (dmvs_html + dmvs_js) lays it out as a browser does and writes the
# view (carhmi.dmvs) with the fonts and images it draws, next to it; dmod's
# DMOD_ASSETS_PATHS turns them into carhmi.dmv, .dmvf and .dmvi at build time.
#
# The sources have to exist when dmod looks for the assets (configure time),
# so the page is converted when CMake configures; a change of the page or of
# its resources configures the project again. The conversion is dmod's
# (dmod_convert_page(): the converters, the staleness, the options); what is
# done to the view afterwards - the binders - is the project's.
#
#   carhmi_convert_page(<page> <width>x<height> <element> <name> <output dir>)
#
function(carhmi_convert_page page screen element name outDir)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(LESSON_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_literki_assets.py")
    set(FUEL_BINDER "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bind_fuel.py")
    set(NAVIGATION_BINDER "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bind_navigation.py")
    set(NAVIGATION_GESTURES "${CMAKE_CURRENT_SOURCE_DIR}/assets/navigation-gestures.dmvs")

    dmod_convert_page("${page}"
        OUTPUT_DIR "${outDir}" NAME ${name} ROOT ${element} SIZE ${screen} QUIET
        DEPENDS "${LESSON_ASSETS}" "${NAVIGATION_BINDER}" "${NAVIGATION_GESTURES}" "${FUEL_BINDER}"
        CONVERTED PAGE_WAS_CONVERTED)
    if(NOT PAGE_WAS_CONVERTED)
        return()
    endif()

    # The view as the page made it is treated: if that fails, the conversion is not done (it is made again at the next configure)
    set(STAMP "${outDir}/.${name}.stamp")
    execute_process(COMMAND ${Python3_EXECUTABLE} "${NAVIGATION_BINDER}"
        "${outDir}/${name}.dmvs" "${NAVIGATION_GESTURES}"
        RESULT_VARIABLE BIND_RESULT ERROR_VARIABLE BIND_ERROR)
    if(NOT BIND_RESULT EQUAL 0)
        file(REMOVE "${STAMP}")
        message(FATAL_ERROR "carhmi: cannot bind navigation gestures: ${BIND_ERROR}")
    endif()
    execute_process(COMMAND ${Python3_EXECUTABLE} "${FUEL_BINDER}"
        "${outDir}/${name}.dmvs"
        RESULT_VARIABLE FUEL_RESULT ERROR_VARIABLE FUEL_ERROR)
    if(NOT FUEL_RESULT EQUAL 0)
        file(REMOVE "${STAMP}")
        message(FATAL_ERROR "carhmi: cannot bind fuel level: ${FUEL_ERROR}")
    endif()
    execute_process(COMMAND ${Python3_EXECUTABLE} "${LESSON_ASSETS}" "${outDir}"
        RESULT_VARIABLE LESSON_RESULT ERROR_VARIABLE LESSON_ERROR)
    if(NOT LESSON_RESULT EQUAL 0)
        file(REMOVE "${STAMP}")
        message(FATAL_ERROR "carhmi: cannot prepare game pictures: ${LESSON_ERROR}")
    endif()
endfunction()
