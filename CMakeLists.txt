cmake_minimum_required(VERSION 4.2)

project(chuck_tester)

# Copy Qt's runtime dependencies (DLLs, QML modules, plugins) next to an example
# executable as a POST_BUILD step so it runs from the build dir without needing
# Qt's bin/ on PATH. Mirrors what `cmake --install` would do for shipping, but
# eagerly during the dev/CI build.
#
# Linux is a no-op: Qt's shared libs typically resolve via the loader's standard
# search paths or the binary's rpath. Add explicit deployment there only if you
# hit "cannot open shared object" at runtime.
function(tester_deploy_example target)
    if(WIN32)
        find_program(_TESTER_WINDEPLOYQT windeployqt
            HINTS "${Qt6_DIR}/../../../bin"
            REQUIRED)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${_TESTER_WINDEPLOYQT}"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-compiler-runtime
                "$<TARGET_FILE:${target}>"
            COMMENT "windeployqt → $<TARGET_FILE_NAME:${target}>"
            VERBATIM)
    elseif(APPLE)
        find_program(_TESTER_MACDEPLOYQT macdeployqt
            HINTS "${Qt6_DIR}/../../../bin"
            REQUIRED)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${_TESTER_MACDEPLOYQT}"
                "$<TARGET_BUNDLE_DIR:${target}>"
                -qmldir=${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "macdeployqt → ${target}"
            VERBATIM)
    endif()
endfunction()

add_subdirectory(tester)
add_subdirectory(examples/minimal)
add_subdirectory(examples/popup)
add_subdirectory(examples/singleton)
add_subdirectory(examples/todoapp)
add_subdirectory(examples/text_editor)