# Runs `loom style --check` over every ```qml block in the documentation, so a
# utility class the docs claim exists cannot quietly stop existing. The docs
# state exact class names constantly; without this they are the least-tested
# claims in the repository.
#
# docs/tooling/ is excluded: cli.md documents the checker itself, and its
# examples are deliberately *invalid* input chosen to show what gets reported.

if(NOT LOOM_EXE OR NOT DOCS_DIR OR NOT WORK_DIR)
    message(FATAL_ERROR "LOOM_EXE, DOCS_DIR and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

file(GLOB_RECURSE docs
    "${DOCS_DIR}/styling/*.md"
    "${DOCS_DIR}/reference/*.md"
    "${DOCS_DIR}/getting-started.md"
    "${DOCS_DIR}/README.md"
)

set(blocks 0)
foreach(doc IN LISTS docs)
    file(READ "${doc}" contents)
    get_filename_component(stem "${doc}" NAME_WE)
    # Non-greedy up to the closing fence; [^`] keeps a block from swallowing the
    # next one when several appear in a row.
    string(REGEX MATCHALL "```qml\n([^`]|`[^`]|``[^`])*```"
        matches "${contents}")
    set(index 0)
    foreach(match IN LISTS matches)
        if(match MATCHES "Lo\\.style")
            string(REGEX REPLACE "^```qml\n" "" body "${match}")
            string(REGEX REPLACE "```$" "" body "${body}")
            file(WRITE "${WORK_DIR}/${stem}_${index}.qml" "${body}")
            math(EXPR blocks "${blocks} + 1")
        endif()
        math(EXPR index "${index} + 1")
    endforeach()
endforeach()

if(blocks EQUAL 0)
    message(FATAL_ERROR
        "No QML blocks with Lo.style were extracted from ${DOCS_DIR}. "
        "The extraction is broken, not the documentation.")
endif()

execute_process(
    COMMAND "${LOOM_EXE}" style --check "${WORK_DIR}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "Documentation uses utility classes that do not exist:\n${output}")
endif()

message(STATUS "documentation style check: ${blocks} QML block(s) clean")
