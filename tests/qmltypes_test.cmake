# Asserts that the generated loom.qmltypes actually exposes the typed token
# API: one representative property per scale, plus the singleton itself. If
# moc ever stops expanding the X-macro property tables, this is the test that
# goes red.
if(NOT EXISTS "${QMLTYPES}")
    message(FATAL_ERROR "qmltypes file not found: ${QMLTYPES}")
endif()

file(READ "${QMLTYPES}" contents)

foreach(needle IN ITEMS
    "Loom"
    "blue500"
    "surface"
    "s4"
    "x2l"
    "trackingWide"
    "o50"
    "d150"
    "easeInOut"
    "lineHeight"
    "isSingleton: true"
)
    string(FIND "${contents}" "${needle}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "qmltypes is missing expected entry: ${needle}")
    endif()
endforeach()
