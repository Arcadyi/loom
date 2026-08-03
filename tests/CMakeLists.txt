find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

# LINK names what the test links besides Qt6::Test, defaulting to the styling
# library: the static `loom` backing library plus the `loomplugin` static
# plugin, whose initializer is what pulls the type registration and the qmldir
# resource into the binary, exactly as it does for consumers. Tooling tests pass
# loom::Protocol, loom::Runtime or loom_cli_core instead. INCLUDE and
# ENVIRONMENT extend the defaults rather than replacing them.
function(loom_add_test name)
    cmake_parse_arguments(LOOM_ARG "" "" "LINK;INCLUDE;ENVIRONMENT" ${ARGN})
    if(LOOM_ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "loom_add_test received unknown arguments: "
            "${LOOM_ARG_UNPARSED_ARGUMENTS}")
    endif()
    set(loom_test_link ${LOOM_ARG_LINK})
    if(NOT loom_test_link)
        set(loom_test_link loom loomplugin)
    endif()

    qt_add_executable("${name}" "${name}.cpp")
    target_link_libraries("${name}" PRIVATE ${loom_test_link} Qt6::Test)
    if(LOOM_ARG_INCLUDE)
        target_include_directories("${name}" PRIVATE ${LOOM_ARG_INCLUDE})
    endif()
    loom_set_target_defaults("${name}")
    add_test(NAME "${name}" COMMAND "${name}")

    # Offscreen and software rendering so the suite runs headless. The
    # LSAN_OPTIONS entry only matters under LOOM_SANITIZE; see lsan.supp for why
    # it exists at all.
    set(loom_test_environment
        QT_QPA_PLATFORM=offscreen
        QSG_RHI_BACKEND=software
        "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
    )
    list(APPEND loom_test_environment ${LOOM_ARG_ENVIRONMENT})
    set_tests_properties("${name}" PROPERTIES
        ENVIRONMENT "${loom_test_environment}"
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

# ---------------------------------------------------------------------------
# The build and hot-reload half.
# ---------------------------------------------------------------------------

# loomcontrols rather than the default loom/loomplugin: the component module is
# its own QML module with its own static plugin, and it PUBLIC-links loom, so
# this pulls in both. `loom` deliberately does not link back -- see the cycle
# note in the top-level CMakeLists.
if(TARGET loomcontrols)
    loom_add_test(tst_controls LINK loomcontrols loomcontrolsplugin)
endif()

loom_add_test(tst_protocol LINK loom::Protocol)

loom_add_test(tst_runtime
    LINK loom::Runtime
    # The design reload tests assert on the token registry, which is internal.
    INCLUDE "${CMAKE_SOURCE_DIR}/src"
    # A private cache root, so a reload test never sweeps or reuses bundle
    # directories belonging to a real development session on this machine.
    ENVIRONMENT "XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime-test-cache"
)
qt_add_resources(tst_runtime "runtime_test_qml"
    PREFIX "/qt/qml/com/example/Test"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    FILES data/Main.qml
)
# A Window-rooted scene, so the reload path is exercised against a real
# QQuickWindow rather than a bare Item. Destroying the last window is what
# can terminate the application mid-reload.
qt_add_resources(tst_runtime "runtime_test_window_qml"
    PREFIX "/qt/qml/com/example/WindowTest"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    FILES data/WindowMain.qml
)

# The automatic capture is unit-tested directly rather than through a reload:
# what matters is which properties it selects, and a bundle round-trip would
# only obscure that.
loom_add_test(tst_statecapture
    LINK loom::Runtime Qt6::Qml
    INCLUDE "${CMAKE_SOURCE_DIR}/src/runtime"
)

# Everything below links loom_cli_core instead of re-listing src/cli sources, so
# adding a test for any CLI unit costs one loom_add_test line.
if(TARGET loom_cli_core)
    loom_add_test(tst_manifest LINK loom_cli_core)
    loom_add_test(tst_devserver LINK loom_cli_core)
    loom_add_test(tst_commandline LINK loom_cli_core)

    qt_add_executable(fake_qmlls fake_qmlls.cpp)
    target_link_libraries(fake_qmlls PRIVATE loom_cli_core Qt6::Core)
    loom_set_target_defaults(fake_qmlls)

    loom_add_test(tst_lsp LINK loom_cli_core)
    target_compile_definitions(tst_lsp PRIVATE
        LOOM_TEST_EXE="$<TARGET_FILE:loom_cli>"
        LOOM_FAKE_QMLLS="$<TARGET_FILE:fake_qmlls>"
        LOOM_QMLLS_SHIM="$<TARGET_FILE:loom_qmlls_shim>"
    )
    add_dependencies(tst_lsp loom_cli loom_qmlls_shim fake_qmlls)

    loom_add_test(tst_devsession LINK loom_cli_core)
    set_tests_properties(tst_devsession PROPERTIES TIMEOUT 60)
endif()

if(TARGET loom_cli)
    add_test(NAME loom_cli_help COMMAND loom_cli --help)
    add_test(NAME loom_cli_version COMMAND loom_cli --version)
    add_test(
        NAME loom_cli_new
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated-project"
            "-DLOOM_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_new_test.cmake"
    )
    add_test(
        NAME loom_schema
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/schema-project"
            "-DSCHEMA=${CMAKE_SOURCE_DIR}/schemas/project-v2.schema.json"
            "-DDESIGN_SCHEMA=${CMAKE_SOURCE_DIR}/schemas/design-v2.schema.json"
            "-DSTRICT=${LOOM_STRICT_SCHEMA_TEST}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/schema_test.cmake"
    )
    add_test(
        NAME loom_cli_init
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/existing-project"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_init_test.cmake"
    )
    # `loom style` needs no project and no build, so it is checked directly
    # rather than through the e2e tier.
    add_test(
        NAME loom_cli_style
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/style-check"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_style_test.cmake"
    )
    add_test(
        NAME loom_cli_migrate
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/migration"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_migrate_test.cmake"
    )
    # The documentation names exact utility classes on nearly every page. This
    # runs the real checker over every QML block in it, so a class the docs
    # promise cannot quietly stop existing.
    add_test(
        NAME loom_docs_style
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DDOCS_DIR=${CMAKE_SOURCE_DIR}/docs"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/docs-style"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/docs_style_test.cmake"
    )
    # The gallery is the framework's own showcase; it ships 61 Lo.style
    # literals that nothing checked until now.
    add_test(
        NAME loom_gallery_style
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_EXE=$<TARGET_FILE:loom_cli>"
            "-DTARGET_DIR=${CMAKE_SOURCE_DIR}/examples/gallery/qml"
            "-DDESIGN=${CMAKE_SOURCE_DIR}/examples/gallery/design/tokens.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/paths_style_test.cmake"
    )
endif()

# The end-to-end tests install loom to a throwaway prefix and drive the real
# scaffold -> configure -> build -> ctest loop. They are the only tests that run
# a compiler over generated output, and the direct regression test for the
# Testing/ link collision that shipped in 0.1.0. Off-by-default-slow: the rest
# of the suite stays under a second.
if(LOOM_BUILD_E2E_TESTS AND TARGET loom_cli)
    foreach(loom_e2e_app IN ITEMS E2EApp Testing)
        add_test(
            NAME "loom_e2e_${loom_e2e_app}"
            COMMAND "${CMAKE_COMMAND}"
                "-DLOOM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DLOOM_CLI_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DQT6_DIR=${Qt6_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e/${loom_e2e_app}"
                "-DAPP_NAME=${loom_e2e_app}"
                "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_e2e_test.cmake"
        )
        set_tests_properties("loom_e2e_${loom_e2e_app}" PROPERTIES
            LABELS e2e
            TIMEOUT 900
            # Two concurrent installs into separate prefixes are fine, but two
            # concurrent ninja builds of Qt-heavy projects thrash a laptop.
            RUN_SERIAL TRUE
        )
    endforeach()

    # Deployment gets its own end-to-end test rather than another APP_NAME in
    # the loop above: it drives a different command and asserts a different
    # thing -- that the installed tree runs.
    add_test(
        NAME loom_e2e_deploy
        COMMAND "${CMAKE_COMMAND}"
            "-DLOOM_CLI_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DLOOM_QMLLS_SHIM_RELATIVE=${LOOM_QMLLS_SHIM_INSTALL_DIR}/qmlls${CMAKE_EXECUTABLE_SUFFIX}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e/deploy"
            "-DCMAKE_GENERATOR=${CMAKE_GENERATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_deploy_test.cmake"
    )
    set_tests_properties(loom_e2e_deploy PROPERTIES
        LABELS e2e
        TIMEOUT 900
        RUN_SERIAL TRUE
    )
endif()

# Scaffold-configure-build-run of a real consumer against the build tree: the
# only test that runs find_package(loom) the way applications do. Inside the
# same guard as the rest of the compiling tier -- it was registered outside it,
# so LOOM_BUILD_E2E_TESTS=OFF still compiled a whole consumer project.
if(LOOM_BUILD_E2E_TESTS)
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
endif()

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

# The same guarantee one level up, for the component module: that every file in
# src/controls/ actually reached the built module.
if(TARGET loomcontrols)
    add_test(NAME loom_controls_qmldir
        COMMAND "${CMAKE_COMMAND}"
            "-DQMLDIR=${CMAKE_BINARY_DIR}/qml/Loom/Controls/qmldir"
            "-DCONTROLS_DIR=${CMAKE_SOURCE_DIR}/src/controls"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/controls_qmldir_test.cmake"
    )
endif()
