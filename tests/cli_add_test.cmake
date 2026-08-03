# `loom add` writes into a project someone is working in, so what it refuses
# matters as much as what it writes. Scaffolds a project, adds to it, and checks
# both directions.
#
# It also guards the template resources: the sources are compiled into the CLI
# from an explicitly enumerated FILES list, so a template added to templates/add
# but not to that list produces a binary that fails at "Could not read the
# template" -- which nothing else would notice.

if(NOT LOOM_EXE OR NOT TEST_DIR)
    message(FATAL_ERROR "LOOM_EXE and TEST_DIR are required")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
execute_process(
    COMMAND "${LOOM_EXE}" new AddedTo --org com.example --directory "${TEST_DIR}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "loom new failed:\n${error}")
endif()

function(loom_add kind name expected_result out_output)
    execute_process(
        COMMAND "${LOOM_EXE}" add "${kind}" "${name}"
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE add_result
        OUTPUT_VARIABLE add_output
        ERROR_VARIABLE add_error
    )
    if(NOT add_result EQUAL expected_result)
        message(FATAL_ERROR
            "loom add ${kind} ${name} returned ${add_result}, expected "
            "${expected_result}:\n${add_output}${add_error}")
    endif()
    set(${out_output} "${add_output}${add_error}" PARENT_SCOPE)
endfunction()

loom_add(page Settings 0 created_page)
loom_add(component Badge 0 created_component)

foreach(required qml/pages/Settings.qml qml/components/Badge.qml)
    if(NOT EXISTS "${TEST_DIR}/${required}")
        message(FATAL_ERROR "loom add did not create ${required}")
    endif()
endforeach()

# The substitution actually ran. A template shipped with its placeholder intact
# would still be a valid QML file, so nothing else would catch it.
file(READ "${TEST_DIR}/qml/pages/Settings.qml" page)
if(page MATCHES "@LOOM_TYPE_NAME@")
    message(FATAL_ERROR "loom add left a placeholder in the generated page")
endif()
if(NOT page MATCHES "Settings")
    message(FATAL_ERROR "loom add did not substitute the type name")
endif()

# No CMake edit: the whole point is that the QML root is globbed, and saying so
# in the output is what stops someone hunting for a registration step.
file(READ "${TEST_DIR}/CMakeLists.txt" project_cmake)
if(project_cmake MATCHES "Settings|Badge")
    message(FATAL_ERROR "loom add edited CMakeLists.txt; the QML root is globbed")
endif()
if(NOT created_page MATCHES "no CMake change is needed")
    message(FATAL_ERROR "loom add did not say that no CMake change is needed")
endif()

# Refusals. Overwriting a file in a project someone is working in is the one
# outcome that loses work.
loom_add(page Settings 1 overwrite_output)
if(NOT overwrite_output MATCHES "already exists")
    message(FATAL_ERROR "loom add overwrote, or refused for the wrong reason")
endif()

# QML resolves a component from its file name, so a lower-case name produces a
# file no other QML can refer to.
loom_add(component badge 2 lowercase_output)
if(NOT lowercase_output MATCHES "QML type name")
    message(FATAL_ERROR "loom add accepted a name QML cannot address as a type")
endif()

loom_add(model Thing 2 kind_output)
if(NOT kind_output MATCHES "unknown kind")
    message(FATAL_ERROR "loom add accepted an unknown kind")
endif()

# Outside a project there is no qml root to add to, and saying which is missing
# is more useful than a path that does not exist.
execute_process(
    COMMAND "${LOOM_EXE}" add page Orphan
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE orphan_result
    OUTPUT_VARIABLE orphan_output
    ERROR_VARIABLE orphan_error
)
if(orphan_result EQUAL 0)
    message(FATAL_ERROR "loom add succeeded outside a project")
endif()
if(NOT "${orphan_output}${orphan_error}" MATCHES "loom.json")
    message(FATAL_ERROR
        "loom add outside a project did not mention the missing manifest:\n"
        "${orphan_output}${orphan_error}")
endif()

message(STATUS "loom add: created, substituted, and refused as expected")
