# Asserts the gallery and the app template still use the components that exist
# to replace what they used to write by hand.
#
# The problem this guards against is not hypothetical: `Loom.Controls` shipped
# Box, Field, Button and Grid, and the gallery went on using none of them. It
# kept the Rectangle + Text + MouseArea triple that Button's own docstring
# names as its reason to exist, and reached for `spacing: Loom.space.sN` about
# three times as often as `gap-*` -- something a comment in tst_controls
# already remarked on, with nothing to stop it.
#
# A showcase that does not use the framework is worse than no showcase: it is
# the first thing anyone copies from.

foreach(required GALLERY_DIR TEMPLATE_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(problems "")

# Each entry is a regex, then the reason it should not appear.
set(banned
    "spacing: Loom\\.space\\."
    "spacing is what `gap-*` writes; use the class"
    "contentHeight: [A-Za-z_]+\\.(implicitHeight|height) \\+"
    "a hand-wired Flickable; use Scroll"
)

file(GLOB_RECURSE sources "${GALLERY_DIR}/*.qml" "${TEMPLATE_DIR}/*.qml.in")

list(LENGTH banned banned_length)
foreach(source IN LISTS sources)
    file(READ "${source}" contents)
    get_filename_component(shown "${source}" NAME)

    math(EXPR last "${banned_length} - 2")
    foreach(index RANGE 0 ${last} 2)
        list(GET banned ${index} pattern)
        math(EXPR reason_index "${index} + 1")
        list(GET banned ${reason_index} reason)
        if(contents MATCHES "${pattern}")
            string(APPEND problems "  ${shown}: ${reason}\n")
        endif()
    endforeach()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR
        "The examples have drifted back to constructions Loom.Controls "
        "replaces:\n${problems}"
        "Either use the component, or -- if the example is deliberately "
        "showing the long form -- relax the pattern in "
        "tests/dogfood_test.cmake and say why.")
endif()

# The other half: the components should actually appear. A file-count check
# rather than a per-type one, because which types a page needs is its own
# business; what matters is that the module is reached for at all.
set(users 0)
foreach(source IN LISTS sources)
    file(READ "${source}" contents)
    if(contents MATCHES "import Loom\\.Controls")
        math(EXPR users "${users} + 1")
    endif()
endforeach()

if(users LESS 4)
    message(FATAL_ERROR
        "only ${users} example file(s) import Loom.Controls -- the showcase "
        "has stopped using the component module again")
endif()

message(STATUS "dogfood: ${users} example file(s) use Loom.Controls, no banned constructions")
