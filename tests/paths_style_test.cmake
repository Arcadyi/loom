# Runs `loom style --check` over a directory of real QML. Used for the gallery:
# the framework ships a checker for exactly this class of bug and did not point
# it at its own example.

if(NOT LOOM_EXE OR NOT TARGET_DIR)
    message(FATAL_ERROR "LOOM_EXE and TARGET_DIR are required")
endif()

execute_process(
    COMMAND "${LOOM_EXE}" style --check "${TARGET_DIR}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "${TARGET_DIR} uses unknown utility classes:\n${output}")
endif()

message(STATUS "${output}")
