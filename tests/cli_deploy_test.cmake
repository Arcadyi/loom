# Scaffold, deploy, and check the result is something you could actually ship:
# the binary exists in the install prefix and runs from there.
#
# The "and runs" half is the point. Qt's own qt_generate_deploy_qml_app_script
# was measured on this platform producing a 247 MB tree containing
# ld-linux-x86-64.so.2, whose binary core-dumps -- a check that stopped at
# "files were installed" would have called that a success.

foreach(required RESPIN_BUILD_DIR WORK_DIR)
    if(NOT ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(prefix "${WORK_DIR}/prefix")
set(project_dir "${WORK_DIR}/Deployable")
set(output "${WORK_DIR}/out")

function(deploy_run label)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "COMMAND" ${ARGN})
    execute_process(
        COMMAND ${ARG_COMMAND}
        WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${label} failed (exit ${result})\n--- stdout ---\n${out}\n"
            "--- stderr ---\n${err}")
    endif()
    set(deploy_output "${out}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(ENV{RESPIN_QUIET_INSTALL} 1)
deploy_run("installing respin"
    COMMAND "${CMAKE_COMMAND}" --install "${RESPIN_BUILD_DIR}" --prefix "${prefix}"
)
set(respin_exe "${prefix}/bin/respin")

deploy_run("respin new"
    COMMAND "${respin_exe}" new Deployable --org com.example --directory "${project_dir}"
)

deploy_run("respin deploy"
    COMMAND "${respin_exe}" deploy --config Release --output "${output}" --package
    WORKING_DIRECTORY "${project_dir}"
)

set(installed "${output}/bin/Deployable")
if(NOT EXISTS "${installed}")
    file(GLOB_RECURSE produced "${output}/*")
    message(FATAL_ERROR
        "respin deploy reported success but installed no binary at\n  ${installed}\n"
        "prefix contains:\n${produced}")
endif()

# Deploying must not quietly turn into a Qt bundle again: that is the mode that
# produced a broken 247 MB tree, and it is measurable as size.
file(SIZE "${installed}" installed_size)
if(installed_size LESS 100000)
    message(FATAL_ERROR "The installed binary is implausibly small (${installed_size} bytes)")
endif()

file(GLOB archives "${output}/*.tar.gz")
if(NOT archives)
    message(FATAL_ERROR "respin deploy --package produced no archive in ${output}")
endif()

# The whole point: it has to run. offscreen so no display is needed; the app
# never exits on its own, so a timeout is the success signal and any other
# result means it failed to start.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen "${installed}"
    TIMEOUT 10
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
# execute_process reports a timeout as a non-numeric string on some CMake
# versions, so accept anything that is not a clean or crashing exit.
if(run_result MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "The deployed application exited immediately with ${run_result} instead of "
        "staying up:\n${run_output}\n${run_error}")
endif()
