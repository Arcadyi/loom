find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

# Every test links the static `loom` backing library plus the `loomplugin`
# static plugin: the plugin's initializer is what pulls the type registration
# and the qmldir resource into the binary, exactly as it does for consumers.
function(loom_add_test name)
    qt_add_executable("${name}" "${name}.cpp")
    target_link_libraries("${name}" PRIVATE loom loomplugin Qt6::Test)
    loom_set_target_defaults("${name}")
    add_test(NAME "${name}" COMMAND "${name}")
    # The LSAN_OPTIONS entry only matters under LOOM_SANITIZE; see lsan.supp
    # for why it exists at all.
    set_tests_properties("${name}" PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
    )
endfunction()

loom_add_test(tst_tokens)
target_compile_definitions(tst_tokens PRIVATE LOOM_VERSION_STR="${PROJECT_VERSION}")
# Tests exercise internals (registry, compiler) directly, not only the QML API.
target_include_directories(tst_tokens PRIVATE "${CMAKE_SOURCE_DIR}/src")

loom_add_test(tst_compiler)
target_include_directories(tst_compiler PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/tokens"
)

loom_add_test(tst_apply)
target_include_directories(tst_apply PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/tokens"
)

loom_add_test(tst_catalogue)
target_include_directories(tst_catalogue PRIVATE "${CMAKE_SOURCE_DIR}/src")

loom_add_test(tst_reactivity)
target_include_directories(tst_reactivity PRIVATE "${CMAKE_SOURCE_DIR}/src")

loom_add_test(tst_icon)
target_include_directories(tst_icon PRIVATE "${CMAKE_SOURCE_DIR}/src")
# A config living in the resource system, so the qrc: branch of iconRoot
# resolution is exercised the way loom::loadConfig(":/...") reaches it.
qt_add_resources(tst_icon "tst_icon_data" PREFIX "/" FILES data/tst_icon/loom.json)

loom_add_test(tst_breakpoints)
loom_add_test(tst_states)

loom_add_test(tst_transitions)
target_include_directories(tst_transitions PRIVATE "${CMAKE_SOURCE_DIR}/src")

loom_add_test(tst_config)
target_include_directories(tst_config PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/tokens"
)

# Scaffold-configure-build-run of a real consumer against the build tree: the
# only test that runs find_package(loom) the way applications do.
add_test(NAME loom_e2e_consume
    COMMAND "${CMAKE_COMMAND}"
        "-DLOOM_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e/consume"
        "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/e2e_consume_test.cmake"
)
set_tests_properties(loom_e2e_consume PROPERTIES
    LABELS e2e
    TIMEOUT 900
    RUN_SERIAL TRUE
)

# The generated qmllint target checks the gallery's QML against loom's
# generated qmltypes: the regression test that consumers stay lint-clean.
if(TARGET loomgallery)
    add_test(NAME loom_qmllint
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target loomgallery_qmllint
    )
    set_tests_properties(loom_qmllint PROPERTIES LABELS e2e RUN_SERIAL TRUE)
endif()

# The regression test that the typed API stays visible to tooling: qmltypes is
# what qmllint and Qt Creator read in consumer projects.
add_test(NAME loom_qmltypes_completeness
    COMMAND "${CMAKE_COMMAND}"
        "-DQMLTYPES=${CMAKE_BINARY_DIR}/qml/Loom/loom.qmltypes"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/qmltypes_test.cmake"
)
