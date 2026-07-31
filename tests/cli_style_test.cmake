# `loom style` -- the Lo.style vocabulary, offline.
#
# This was the standalone `loomstyle` binary before the merge. Checking it
# through the CLI is what proves the fold actually happened rather than the
# subcommand merely existing.

foreach(required LOOM_EXE TEST_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/qml")

# --catalogue answers without a project at all: an editor asking for completion
# data should not have to be inside one.
execute_process(
    COMMAND "${LOOM_EXE}" style --catalogue
    RESULT_VARIABLE catalogue_result
    OUTPUT_VARIABLE catalogue_output
    ERROR_VARIABLE catalogue_error
)
if(NOT catalogue_result EQUAL 0)
    message(FATAL_ERROR
        "loom style --catalogue failed:\n${catalogue_output}\n${catalogue_error}")
endif()
foreach(expected "\"classes\"" "\"variants\"" "bg-blue-500" "rounded-lg" "hover")
    if(NOT catalogue_output MATCHES "${expected}")
        message(FATAL_ERROR
            "loom style --catalogue did not mention ${expected}")
    endif()
endforeach()

# A file whose classes all exist passes.
file(WRITE "${TEST_DIR}/qml/Good.qml"
"import QtQuick
import Loom

Item {
    Lo.style: \"p-4 bg-surface rounded-lg hover:bg-blue-600 md:p-6\"
}
")
execute_process(
    COMMAND "${LOOM_EXE}" style --check "${TEST_DIR}/qml"
    RESULT_VARIABLE good_result
    OUTPUT_VARIABLE good_output
    ERROR_VARIABLE good_error
)
if(NOT good_result EQUAL 0)
    message(FATAL_ERROR
        "loom style rejected a file of valid classes:\n${good_output}\n${good_error}")
endif()

# A typo is reported with file and line, and exits non-zero so CI catches it.
# This is the whole reason the checker exists: the runtime warning for an
# unknown class only fires if that code path runs, into a log nobody reads.
file(WRITE "${TEST_DIR}/qml/Bad.qml"
"import QtQuick
import Loom

Item {
    Lo.style: \"p-4 bg-blurple-500\"
}
")
execute_process(
    COMMAND "${LOOM_EXE}" style --check "${TEST_DIR}/qml"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_output
    ERROR_VARIABLE bad_error
)
if(bad_result EQUAL 0)
    message(FATAL_ERROR
        "loom style accepted an unknown class:\n${bad_output}\n${bad_error}")
endif()
if(NOT bad_error MATCHES "Bad\\.qml:5: unknown utility class 'bg-blurple-500'")
    message(FATAL_ERROR
        "loom style did not report the file and line of the unknown class:\n${bad_error}")
endif()

# The two modes are mutually exclusive rather than one silently winning.
execute_process(
    COMMAND "${LOOM_EXE}" style --check --catalogue
    RESULT_VARIABLE both_result
    OUTPUT_VARIABLE both_output
    ERROR_VARIABLE both_error
)
if(both_result EQUAL 0)
    message(FATAL_ERROR
        "loom style accepted --check and --catalogue together:\n${both_output}")
endif()
