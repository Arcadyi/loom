# Asserts every part-style property the shipped controls declare is known to
# the tooling.
#
# A part-style property is an ordinary QML string forwarded onto a sub-delegate's
# `Lo.style`, so nothing but its *name* distinguishes it from any other string
# property. src/cli/stylebindings.h carries that list, and it is what makes the
# library's part-styling surface visible to completion, to `loom lint`, and to
# the diagnostics that exist because a class string is not checked by the
# compiler.
#
# The failure this exists to catch is adding `property string handleStyle` to a
# control and forgetting the header: the property works at runtime, and every
# class string written into it is silently unchecked and uncompleted -- the
# exact gap the tooling was built to close, reopened one control at a time.
if(NOT EXISTS "${BINDINGS_HEADER}")
    message(FATAL_ERROR "style-binding list not found: ${BINDINGS_HEADER}")
endif()

file(READ "${BINDINGS_HEADER}" known)

file(GLOB control_sources "${CONTROLS_DIR}/*.qml")
foreach(control_source IN LISTS control_sources)
    file(READ "${control_source}" contents)
    cmake_path(GET control_source STEM control_type)

    # `property string <name>Style`, which is the whole convention. A part-style
    # that is aliased rather than declared (`property alias fooStyle: x.foo`)
    # is deliberately not matched: an alias forwards to a real property that is
    # itself either Lo.style or another declared part-style.
    string(REGEX MATCHALL "property[ \t]+string[ \t]+([A-Za-z0-9_]+)Style"
        declarations "${contents}")

    foreach(declaration IN LISTS declarations)
        string(REGEX REPLACE "property[ \t]+string[ \t]+" "" name "${declaration}")
        string(FIND "${known}" "\"${name}\"" found)
        if(found EQUAL -1)
            message(FATAL_ERROR
                "src/controls/${control_type}.qml declares '${name}', which "
                "src/cli/stylebindings.h does not list: every class string "
                "written into it would be invisible to loom lint and to the "
                "editor. Add it to loom::stylebindings::kNames.")
        endif()
    endforeach()
endforeach()
