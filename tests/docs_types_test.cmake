# Asserts every QML type the documentation instantiates actually exists.
#
# docs_style_test.cmake checks the *classes* inside `Lo.style`, which is the
# claim the docs make most often. It says nothing about the *types* around
# them, and that gap had a cost: `Icon { }` appeared in src/controls/Row.qml's
# docstring and in docs/styling/components.md, and `Card { }` in
# src/controls/Grid.qml's, for types that did not exist. Three examples in the
# repository, none of which any test could fail on.
#
# Loom types are resolved by globbing src/controls/. Everything else has to be
# named below -- an allowlist rather than an inference, because from a bare
# fragment there is no way to tell a QtQuick type from a Loom type that was
# never written. That is the whole failure being prevented, so guessing here
# would reintroduce it.

if(NOT DOCS_DIR OR NOT CONTROLS_DIR)
    message(FATAL_ERROR "DOCS_DIR and CONTROLS_DIR are required")
endif()

# Types from QtQuick, QtQuick.Controls, QtQuick.Layouts and QtQuick.Effects that
# the documentation legitimately reaches for, plus the two example-local
# components the docs define inline.
set(external_types
    Action ApplicationWindow ButtonGroup Column ColumnLayout GridLayout Item
    ListView Loader NumberAnimation Rectangle RectangularShadow Repeater
    RowLayout Text TextField
    # Defined by the examples that use them, not by loom.
    SectionTitle
)

file(GLOB control_sources "${CONTROLS_DIR}/*.qml")
set(known_types ${external_types})
foreach(control_source IN LISTS control_sources)
    cmake_path(GET control_source STEM control_type)
    list(APPEND known_types "${control_type}")
endforeach()

file(GLOB_RECURSE docs
    "${DOCS_DIR}/styling/*.md"
    "${DOCS_DIR}/reference/*.md"
    "${DOCS_DIR}/getting-started.md"
    "${DOCS_DIR}/README.md"
)

set(sources ${docs} ${control_sources})
set(checked 0)
set(problems "")

foreach(source IN LISTS sources)
    file(READ "${source}" contents)
    file(RELATIVE_PATH shown "${CMAKE_CURRENT_LIST_DIR}/.." "${source}")

    # ```qml fences in markdown, \qml ... \endqml in the control docstrings.
    # CMake's regex engine has no lazy quantifiers, so both patterns bound
    # themselves with a negated class instead: backticks for the markdown
    # fences (the idiom docs_style_test.cmake already uses), and backslashes
    # for the qdoc ones, which is safe because the only backslashes involved
    # are the \qml and \endqml delimiters themselves.
    string(REGEX MATCHALL "```qml\n([^`]|`[^`]|``[^`])*```" blocks "${contents}")
    string(REGEX MATCHALL "\\\\qml\n([^\\\\])*\\\\endqml" qdoc_blocks "${contents}")
    list(APPEND blocks ${qdoc_blocks})

    foreach(block IN LISTS blocks)
        # An object declaration: a capitalised name at the start of a line,
        # followed by an opening brace. Qualified names (`Lc.Row`) are skipped --
        # the prefix is an import alias this test cannot resolve.
        string(REGEX MATCHALL "[\r\n][ \t]*[A-Z][A-Za-z0-9_]*[ \t]*\\{"
            declarations "${block}")
        foreach(declaration IN LISTS declarations)
            string(REGEX REPLACE "[\r\n][ \t]*" "" type "${declaration}")
            string(REGEX REPLACE "[ \t]*\\{" "" type "${type}")
            math(EXPR checked "${checked} + 1")
            if(NOT type IN_LIST known_types)
                string(APPEND problems "  ${shown}: ${type}\n")
            endif()
        endforeach()
    endforeach()
endforeach()

if(checked EQUAL 0)
    message(FATAL_ERROR
        "No QML type declarations were extracted. The extraction is broken, "
        "not the documentation.")
endif()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR
        "Documentation instantiates types that do not exist in "
        "src/controls/, and are not in this test's allowlist:\n${problems}"
        "Either ship the type, fix the example, or -- if it is a QtQuick type "
        "-- add it to external_types in tests/docs_types_test.cmake.")
endif()

message(STATUS "documentation type check: ${checked} declaration(s) resolve")
