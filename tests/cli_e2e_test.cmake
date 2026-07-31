# End-to-end: install loom to a throwaway prefix, scaffold a project with it,
# then build and test that project with a real compiler.
#
# This is the only test that compiles generated output, and it is the direct
# regression test for two 0.1.0 defects: an application target named `Testing`
# collided with the build tree's ctest directory and failed to link, and
# `loom build` did not pass its own CMake package prefix, so the README's
# headline flow died on find_package(loom CONFIG REQUIRED). Neither could be
# caught by any test that stops at file generation.
#
# Deliberately invoked with no --prefix: prefix inference is what is under test.

foreach(required LOOM_SOURCE_DIR LOOM_CLI_BUILD_DIR WORK_DIR APP_NAME)
    if(NOT ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(prefix "${WORK_DIR}/prefix")
set(project_dir "${WORK_DIR}/${APP_NAME}")

# Run a command, failing the test with its full output. execute_process swallows
# output into variables, so a bare RESULT_VARIABLE check reports nothing useful.
function(loom_e2e_run label)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "COMMAND" ${ARGN})
    execute_process(
        COMMAND ${ARG_COMMAND}
        WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${label} failed (exit ${result})\n"
            "--- command ---\n${ARG_COMMAND}\n"
            "--- stdout ---\n${output}\n"
            "--- stderr ---\n${error}")
    endif()
    set(loom_e2e_output "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# --install never builds, so this cannot recurse into the ninja invocation that
# is running this test. If the build tree is stale the install simply fails.
set(ENV{LOOM_QUIET_INSTALL} 1)
loom_e2e_run("installing loom to ${prefix}"
    COMMAND "${CMAKE_COMMAND}" --install "${LOOM_CLI_BUILD_DIR}" --prefix "${prefix}"
)

set(loom_exe "${prefix}/bin/loom")
if(NOT EXISTS "${loom_exe}")
    message(FATAL_ERROR "loom was not installed to ${loom_exe}")
endif()

loom_e2e_run("loom new ${APP_NAME}"
    COMMAND "${loom_exe}" new "${APP_NAME}"
        --org com.example
        --directory "${project_dir}"
)

loom_e2e_run("loom build"
    COMMAND "${loom_exe}" build
    WORKING_DIRECTORY "${project_dir}"
)

# The bin/ layout is a contract, not an implementation detail: `loom dev`
# resolves the executable from exactly this path.
set(executable "${project_dir}/.loom/build/desktop-debug/bin/${APP_NAME}")
if(NOT EXISTS "${executable}")
    file(GLOB_RECURSE actually_built "${project_dir}/.loom/build/desktop-debug/*")
    message(FATAL_ERROR
        "loom build reported success but produced no executable at\n"
        "  ${executable}\n"
        "build tree contains:\n${actually_built}")
endif()

loom_e2e_run("loom test"
    COMMAND "${loom_exe}" test
    WORKING_DIRECTORY "${project_dir}"
)
if(NOT loom_e2e_output MATCHES "tests passed")
    message(FATAL_ERROR
        "loom test did not report a passing test run:\n${loom_e2e_output}")
endif()
