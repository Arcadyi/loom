# Asserts the generated Loom.Controls qmldir declares the module the way
# consumers resolve it, and lists every shipped component.
#
# The failure this exists to catch is adding a file to src/controls/ and
# forgetting it in QML_FILES: the component then works in the source tree, is
# absent from the built module, and nothing else in the suite notices until an
# application fails at `import Loom.Controls` with a type it can see on disk.
if(NOT EXISTS "${QMLDIR}")
    message(FATAL_ERROR "Loom.Controls qmldir not found: ${QMLDIR}")
endif()

file(READ "${QMLDIR}" contents)

foreach(needle IN ITEMS
    "module Loom.Controls"
    # The components use `Lo` and the `Loom` singleton; without this line the
    # module resolves only where the application happens to import Loom first.
    "depends Loom"
    "depends QtQuick.Controls"
    "classname LoomControlsQmlPlugin"
    # QT_RESOURCE_ALIAS flattens the source subdirectory, so the qmldir entry
    # is the bare file name rather than src/controls/Box.qml.
    "Box 1.0 Box.qml"
)
    string(FIND "${contents}" "${needle}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Loom.Controls qmldir is missing: ${needle}")
    endif()
endforeach()

# Every .qml in src/controls/ must appear. Globbing here rather than repeating
# the list is the point: this catches the file that was never added to
# QML_FILES, which a hardcoded list would silently share the omission with.
file(GLOB control_sources "${CONTROLS_DIR}/*.qml")
foreach(control_source IN LISTS control_sources)
    cmake_path(GET control_source STEM control_type)
    string(FIND "${contents}" "${control_type} 1.0 ${control_type}.qml" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "src/controls/${control_type}.qml is not in the built module: "
            "add it to loom_controls_qml_files in CMakeLists.txt")
    endif()
endforeach()
