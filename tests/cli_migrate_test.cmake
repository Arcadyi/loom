if(NOT LOOM_EXE OR NOT TEST_DIR)
    message(FATAL_ERROR "LOOM_EXE and TEST_DIR are required")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/design" "${TEST_DIR}/qml")
file(WRITE "${TEST_DIR}/loom.json" [=[
{
  "schemaVersion": 1,
  "project": { "name": "Migrated" },
  "qt": { "version": "6.11" },
  "design": "design/tokens.json",
  "applications": {
    "Migrated": {
      "name": "Migrated", "target": "Migrated",
      "id": "com.example.migrated", "uri": "com.example.Migrated",
      "entry": "Main", "qmlRoots": ["qml"], "assetRoots": [],
      "platforms": ["desktop", "android"]
    }
  }
}
]=])
file(WRITE "${TEST_DIR}/design/tokens.json" [=[
{
  "colors": { "brand": "#7c5cff" },
  "space": { "18": 72 },
  "breakpoints": { "md": 800 },
  "themes": {
    "oled": { "extends": "dark", "surface": "#000000" }
  },
  "defaultTheme": "oled"
}
]=])
file(WRITE "${TEST_DIR}/qml/Main.qml" [=[
import QtQuick
import Loom
Rectangle { Lo.style: "bg-brand p-18 md:rounded-lg" }
]=])

file(READ "${TEST_DIR}/loom.json" original_manifest)
file(READ "${TEST_DIR}/design/tokens.json" original_design)
execute_process(
    COMMAND "${LOOM_EXE}" migrate --to 2
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE preview_result
    OUTPUT_VARIABLE preview_output
    ERROR_VARIABLE preview_error
)
if(NOT preview_result EQUAL 0 OR NOT preview_output MATCHES "Would migrate")
    message(FATAL_ERROR
        "migration preview failed:\n${preview_output}\n${preview_error}")
endif()
file(READ "${TEST_DIR}/loom.json" preview_manifest)
file(READ "${TEST_DIR}/design/tokens.json" preview_design)
if(NOT preview_manifest STREQUAL original_manifest
    OR NOT preview_design STREQUAL original_design)
    message(FATAL_ERROR "migration preview modified project files")
endif()

execute_process(
    COMMAND "${LOOM_EXE}" migrate --to 2 --apply
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE apply_result
    OUTPUT_VARIABLE apply_output
    ERROR_VARIABLE apply_error
)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR
        "migration apply failed:\n${apply_output}\n${apply_error}")
endif()
file(READ "${TEST_DIR}/loom.json" migrated_manifest)
file(READ "${TEST_DIR}/design/tokens.json" migrated_design)
if(NOT migrated_manifest MATCHES "\"schemaVersion\"[ \t\r\n]*:[ \t\r\n]*2"
    OR NOT migrated_manifest MATCHES "\"platforms\"[ \t\r\n]*:[ \t\r\n]*\\{")
    message(FATAL_ERROR "project migration did not emit schema-v2 platforms")
endif()
if(NOT migrated_design MATCHES "\"tokens\""
    OR NOT migrated_design MATCHES "\"theme\""
    OR migrated_design MATCHES "\"defaultTheme\"")
    message(FATAL_ERROR "design migration did not emit the schema-v2 shape")
endif()

# Loading the project design through the real checker proves the migration is
# not merely JSON-shaped: the v2 runtime loader accepts it and exposes its
# custom vocabulary.
execute_process(
    COMMAND "${LOOM_EXE}" style --check
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR
        "migrated project was not usable:\n${check_output}\n${check_error}")
endif()

execute_process(
    COMMAND "${LOOM_EXE}" migrate --to 2 --apply
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0 OR NOT second_output MATCHES "already uses schema v2")
    message(FATAL_ERROR
        "migration was not idempotent:\n${second_output}\n${second_error}")
endif()
