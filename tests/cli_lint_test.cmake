# The lint rules that read a whole class string rather than one class.
#
# `unknownUtility` and `arbitraryValue` ask "is this class real?", one class at
# a time. These two ask "do these classes make sense together?", which needs the
# literal. The interesting part is not what they catch but what they do not:
# `p-4 px-6` is the documented shorthand idiom, `hover:x x` is a variant doing
# its job, and the two branches of a ternary are supposed to write the same
# property. A rule that flagged those would be turned off within a day.

foreach(required LOOM_EXE TEST_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/qml")

file(WRITE "${TEST_DIR}/qml/Cases.qml"
"import QtQuick
import Loom

Item {
    property bool cond: false

    // Reported: every side p-4 writes, p-6 writes again.
    Rectangle { Lo.style: \"p-4 p-6 bg-red-500\" }

    // Not reported: px-6 covers two of p-4's four sides, which is the point.
    Rectangle { Lo.style: \"p-4 px-6\" }

    // Reported once, as a duplicate rather than also as a conflict.
    Rectangle { Lo.style: \"bg-red-500 bg-red-500\" }

    // Not reported: different conditions, so neither shadows the other.
    Rectangle { Lo.style: \"hover:bg-red-500 bg-blue-500\" }

    // Not reported: separate literals. Writing the same property in both
    // branches is what a ternary is for.
    Rectangle { Lo.style: cond ? \"bg-red-500\" : \"bg-blue-500\" }

    // loom-ignore conflictingClass
    Rectangle { Lo.style: \"bg-red-500 bg-blue-500\" }
}
")

execute_process(
    COMMAND "${LOOM_EXE}" style --check "${TEST_DIR}/qml"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
set(report "${output}${errors}")

# Warnings, not errors: a dead class is a mistake worth naming and not a reason
# to fail a build that would otherwise ship.
if(NOT status EQUAL 0)
    message(FATAL_ERROR "loom style --check should pass on warnings:\n${report}")
endif()

foreach(expected
    "'p-4' is overridden"
    "'bg-red-500' is already in this class string"
)
    if(NOT report MATCHES "${expected}")
        message(FATAL_ERROR "expected ${expected} in:\n${report}")
    endif()
endforeach()

# Exactly two findings of these two kinds. Anything more means one of the
# deliberately-legal lines above was flagged, and the count is what catches it
# without pinning line numbers this file will keep changing.
string(REGEX MATCHALL "conflictingClass" conflicts "${report}")
list(LENGTH conflicts conflict_count)
if(NOT conflict_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one conflictingClass -- p-4 px-6, a variant, a "
        "ternary or the suppressed line was flagged:\n${report}")
endif()

string(REGEX MATCHALL "duplicateClass" duplicates "${report}")
list(LENGTH duplicates duplicate_count)
if(NOT duplicate_count EQUAL 1)
    message(FATAL_ERROR "expected exactly one duplicateClass:\n${report}")
endif()

# `loom style` has had --json since it shipped; `loom lint` had not, so a CI job
# wanting structured output had to run the style half twice.
execute_process(
    COMMAND "${LOOM_EXE}" style --check --json "${TEST_DIR}/qml"
    RESULT_VARIABLE json_status
    OUTPUT_VARIABLE json_output
    ERROR_VARIABLE json_errors
)
if(NOT json_status EQUAL 0)
    message(FATAL_ERROR "loom style --json failed:\n${json_output}${json_errors}")
endif()
foreach(expected "conflictingClass" "duplicateClass")
    if(NOT json_output MATCHES "${expected}")
        message(FATAL_ERROR "JSON output is missing ${expected}:\n${json_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${LOOM_EXE}" lint --help
    OUTPUT_VARIABLE help_output
    ERROR_VARIABLE help_errors
)
if(NOT "${help_output}${help_errors}" MATCHES "--json")
    message(FATAL_ERROR "loom lint does not offer --json:\n${help_output}")
endif()

message(STATUS "lint rules: conflicting and duplicate classes reported, idioms not")
