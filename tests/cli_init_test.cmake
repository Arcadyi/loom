if(NOT LOOM_EXE OR NOT TEST_DIR)
    message(FATAL_ERROR "LOOM_EXE and TEST_DIR are required")
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/qml")
file(WRITE "${TEST_DIR}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(Existing LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Quick)
qt_add_executable(existing src/main.cpp)
qt_add_qml_module(existing
    URI com.example.Existing
    QML_FILES qml/Main.qml
)
]=])
file(WRITE "${TEST_DIR}/qml/Main.qml" "import QtQuick\nItem {}\n")

execute_process(
    COMMAND "${LOOM_EXE}" init --apply
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "loom init failed:\n${output}\n${error}")
endif()

file(READ "${TEST_DIR}/CMakeLists.txt" cmake_contents)
if(NOT cmake_contents MATCHES "# loom: begin")
    message(FATAL_ERROR "loom init did not add its marked CMake block")
endif()
file(READ "${TEST_DIR}/loom.json" manifest)
if(NOT manifest MATCHES "com[.]example[.]Existing")
    message(FATAL_ERROR "loom init did not infer the QML URI")
endif()

execute_process(
    COMMAND "${LOOM_EXE}" init --apply
    WORKING_DIRECTORY "${TEST_DIR}"
    RESULT_VARIABLE second_result
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "A second loom init should be idempotent")
endif()

file(READ "${TEST_DIR}/CMakeLists.txt" second_cmake_contents)
string(REGEX MATCHALL "# loom: begin" markers "${second_cmake_contents}")
list(LENGTH markers marker_count)
if(NOT marker_count EQUAL 1)
    message(FATAL_ERROR "loom init duplicated its CMake integration")
endif()

# An existing loom.json is the source of truth. Re-inferring from the
# CMakeLists regex could emit a block naming a different target than the one
# `loom dev` reads out of the manifest, leaving the two permanently at odds.
set(DIVERGED_DIR "${TEST_DIR}-diverged")
file(REMOVE_RECURSE "${DIVERGED_DIR}")
file(MAKE_DIRECTORY "${DIVERGED_DIR}/qml")
file(WRITE "${DIVERGED_DIR}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(Existing LANGUAGES CXX)
qt_add_qml_module(inferredTarget
    URI com.example.Inferred
    QML_FILES qml/Main.qml
)
]=])
file(WRITE "${DIVERGED_DIR}/qml/Main.qml" "import QtQuick\nItem {}\n")
file(WRITE "${DIVERGED_DIR}/loom.json" [=[
{
    "$schema": "https://raw.githubusercontent.com/Arcadyi/loom/master/schemas/project-v2.schema.json",
    "schemaVersion": 2,
    "project": { "name": "Existing" },
    "qt": { "version": "6.11" },
    "applications": {
        "manifestTarget": {
            "name": "Existing",
            "target": "manifestTarget",
            "id": "com.example.manifest",
            "uri": "com.example.Manifest",
            "entry": "Main",
            "qmlRoots": ["qml"],
            "assetRoots": [],
            "platforms": {"desktop": {}}
        }
    }
}
]=])

execute_process(
    COMMAND "${LOOM_EXE}" init --apply
    WORKING_DIRECTORY "${DIVERGED_DIR}"
    RESULT_VARIABLE diverged_result
    OUTPUT_VARIABLE diverged_output
    ERROR_VARIABLE diverged_error
)
if(NOT diverged_result EQUAL 0)
    message(FATAL_ERROR "loom init failed:\n${diverged_output}\n${diverged_error}")
endif()

file(READ "${DIVERGED_DIR}/CMakeLists.txt" diverged_cmake)
if(NOT diverged_cmake MATCHES "TARGET manifestTarget")
    message(FATAL_ERROR
        "loom init ignored the existing manifest and re-inferred from CMakeLists:\n"
        "${diverged_cmake}")
endif()
if(diverged_cmake MATCHES "TARGET inferredTarget")
    message(FATAL_ERROR "loom init wrote a target that contradicts loom.json")
endif()
