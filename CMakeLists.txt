cmake_minimum_required(VERSION 3.22)

project(loom VERSION 0.4.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LOOM_BUILD_TESTS "Build loom tests" ON)
option(LOOM_BUILD_E2E_TESTS "Build the scaffold-then-build end-to-end tests" ON)
option(LOOM_BUILD_CLI "Build the loom command-line tool" ON)
option(LOOM_BUILD_EXAMPLES "Build the loom gallery example" ON)
# The Loom.Controls component module. Off leaves the styling library exactly as
# it was -- Quick only, no Quick Controls dependency -- for consumers that want
# the utility engine and nothing else.
option(LOOM_BUILD_CONTROLS "Build the Loom.Controls QML component module" ON)
option(LOOM_WERROR "Treat compiler warnings as errors" OFF)
option(LOOM_SANITIZE "Build with the address and undefined-behavior sanitizers" OFF)
# The schema test needs a Python JSON Schema validator and skips without one.
# Turn this on where the validator is expected to be present -- CI -- so a
# missing one is a failure rather than a pass nobody notices.
option(LOOM_STRICT_SCHEMA_TEST "Fail rather than skip when jsonschema is missing" OFF)

# Network carries the development server and reload transport; QmlPrivate is for
# src/runtime/statecapture.cpp alone -- see the comment on the loom_runtime link
# below for why the private API is unavoidable there. QuickControls2 joins only
# when Loom.Controls is built, because Box derives from Control to inherit its
# padding and content-sizing rather than reimplementing them.
#
# Collected in a variable because the two generated package configs must
# find_dependency() exactly this set: a consumer resolving the exported targets
# needs every component the archives were linked against.
set(LOOM_QT_COMPONENTS Core Gui Network Qml QmlPrivate Quick)
if(LOOM_BUILD_CONTROLS)
    list(APPEND LOOM_QT_COMPONENTS QuickControls2)
endif()
string(JOIN " " LOOM_QT_COMPONENTS_STR ${LOOM_QT_COMPONENTS})
find_package(Qt6 6.11 REQUIRED COMPONENTS ${LOOM_QT_COMPONENTS})

qt_standard_project_setup(REQUIRES 6.11)

# One shared QML output root for every module in the tree, so generated
# qmllint targets resolve `import Loom` from the gallery and tests without
# extra -I plumbing.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

# Applied through a variable rather than an INTERFACE target on purpose: linking
# an INTERFACE target PRIVATE into a static library still records it as
# $<LINK_ONLY:...> in INTERFACE_LINK_LIBRARIES, and install(EXPORT loomTargets)
# then fails because the warnings target is not itself exported.
if(MSVC)
    # C4702 fires inside qnumeric.h, not loom's own sources. CMake already marks
    # the Qt include directories external and compiles with /external:W0, but
    # that governs the front end only: unreachable-code analysis runs during code
    # generation and is still attributed to the header, so /WX fails the build on
    # a warning no change here can fix.
    set(LOOM_WARNING_OPTIONS /W4 /permissive- /wd4702)
    set(LOOM_WERROR_OPTION /WX)
else()
    set(LOOM_WARNING_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
    )
    set(LOOM_WERROR_OPTION -Werror)
endif()
if(LOOM_WERROR)
    list(APPEND LOOM_WARNING_OPTIONS ${LOOM_WERROR_OPTION})
endif()

if(LOOM_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "LOOM_SANITIZE is not supported with MSVC")
    endif()
    # Both compile and link: the sanitizer runtime is chosen at link time.
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# Every first-party target gets the warning set; imported Qt targets never do.
function(loom_set_target_defaults target)
    target_compile_options("${target}" PRIVATE ${LOOM_WARNING_OPTIONS})
endfunction()

qt_add_library(loom STATIC)
qt_add_qml_module(loom
    URI Loom
    VERSION 1.0
    PLUGIN_TARGET loomplugin
    # Writes `linktarget loom::loomplugin` into qmldir. install(EXPORT) below
    # namespaces the target, so the bare name this defaults to does not exist
    # in a consumer's scope and qt6_import_qml_plugins warns that the plugin
    # will not be linked. The ALIAS below keeps the namespaced name resolvable
    # in this build tree too, where examples/gallery consumes the module.
    INSTALLED_PLUGIN_TARGET loom::loomplugin
    CLASS_NAME LoomQmlPlugin
    TYPEINFO loom.qmltypes
    DEPENDENCIES QtQuick
    SOURCES
        include/loom/loom.h
        include/loom/loomcatalogue.h
        src/loom.cpp
        src/loomconfigloader.h
        src/loomconfigloader.cpp
        src/tokens/loomtokendata.h
        src/tokens/loomtokengroups.h
        src/tokens/loomtokengroups.cpp
        src/tokens/loomtokenregistry.h
        src/tokens/loomtokenregistry.cpp
        src/tokens/loomtokens.h
        src/tokens/loomtokens.cpp
        src/tokens/loomvaluetypes.h
        src/style/loomstylecompiler.h
        src/style/loomstylecompiler.cpp
        src/style/loomtargetprofile.h
        src/style/loomtargetprofile.cpp
        src/style/loomstatewatcher.h
        src/style/loomstatewatcher.cpp
        src/style/loomstyleattached.h
        src/style/loomstyleattached.cpp
        src/style/loomcatalogue.cpp
        src/style/loomiconprovider.h
        src/style/loomiconprovider.cpp
)
target_include_directories(loom
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        # The generated loom_qmltyperegistrations.cpp includes registered
        # headers by basename, so every directory holding a QML-exposed header
        # must be an include directory of the backing target.
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tokens"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/style"
)
target_link_libraries(loom PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_features(loom PUBLIC cxx_std_20)
target_compile_definitions(loom PRIVATE LOOM_VERSION_STR="${PROJECT_VERSION}")
set_target_properties(loom PROPERTIES
    EXPORT_NAME loom
    POSITION_INDEPENDENT_CODE ON
)
set_target_properties(loomplugin PROPERTIES
    EXPORT_NAME loomplugin
    POSITION_INDEPENDENT_CODE ON
)
loom_set_target_defaults(loom)
loom_set_target_defaults(loomplugin)

# Match the names install(EXPORT ... NAMESPACE loom::) gives consumers, so the
# same target names work whether loom is found via find_package or pulled in
# with add_subdirectory.
add_library(loom::loom ALIAS loom)
add_library(loom::loomplugin ALIAS loomplugin)

# ---------------------------------------------------------------------------
# Loom.Controls: the component module. Structure, where everything above is
# appearance.
#
# A separate URI rather than QML_FILES on `Loom`, because QML resolves a type
# name to the *last* import that provides it. `Row`, `Grid` and `Button` are
# QtQuick and QtQuick.Controls names, so shipping them under `Loom` would
# silently re-point every existing `Row { }` in every project that writes
# `import QtQuick` followed by `import Loom` -- which is every project. A
# separate URI makes the shadowing opt-in, and the invariant below makes it
# harmless even then.
#
# INVARIANT: every type here whose name collides with a QtQuick or
# QtQuick.Controls type must *derive* from that type, so the shadowing is always
# a strict superset and can never take a property away. tst_controls asserts it.
# ---------------------------------------------------------------------------

if(LOOM_BUILD_CONTROLS)
    set(loom_controls_qml_files
        src/controls/Box.qml
        src/controls/Button.qml
        src/controls/Col.qml
        src/controls/Field.qml
        src/controls/Grid.qml
        src/controls/ListRow.qml
        src/controls/Row.qml
    )
    # Without an alias the module resource path keeps the source subdirectory --
    # `:/qt/qml/Loom/Controls/src/controls/Box.qml` -- and the generated qmldir
    # says so too. Flatten it, the way examples/gallery does, so the resource
    # path matches the type name.
    foreach(loom_controls_file IN LISTS loom_controls_qml_files)
        cmake_path(GET loom_controls_file FILENAME loom_controls_file_name)
        set_source_files_properties("${loom_controls_file}" PROPERTIES
            QT_RESOURCE_ALIAS "${loom_controls_file_name}"
        )
    endforeach()

    qt_add_library(loomcontrols STATIC)
    qt_add_qml_module(loomcontrols
        URI Loom.Controls
        VERSION 1.0
        PLUGIN_TARGET loomcontrolsplugin
        # Same reasoning as the `loom` module above: install(EXPORT) namespaces
        # the target, so the bare default name does not exist in a consumer's
        # scope and qt6_import_qml_plugins warns the plugin will not be linked.
        INSTALLED_PLUGIN_TARGET loom::loomcontrolsplugin
        CLASS_NAME LoomControlsQmlPlugin
        # Loom rather than a link dependency on purpose: the components need the
        # `Lo` attached type and the `Loom` singleton at QML resolution time, not
        # at link time. Linking loomcontrols against loom would be a cycle, since
        # loom PUBLIC-links loomcontrols below.
        DEPENDENCIES QtQuick QtQuick.Controls Loom
        QML_FILES ${loom_controls_qml_files}
    )
    # PUBLIC loom, and never the reverse. The components need `Lo` and the
    # `Loom` singleton registered to work at all, so anything linking
    # loomcontrols needs loom too -- and `DEPENDENCIES Loom` above already makes
    # CMake order loomcontrols after loom. Linking loom against loomcontrols to
    # get the module "for free" closes that into a cycle CMake rejects for
    # non-static targets. loom_add_application in cmake/loomFunctions.cmake is
    # what gets the module into applications instead; see the note there.
    target_link_libraries(loomcontrols PUBLIC loom loomplugin Qt6::QuickControls2)
    target_compile_features(loomcontrols PUBLIC cxx_std_20)
    set_target_properties(loomcontrols PROPERTIES
        EXPORT_NAME loomcontrols
        POSITION_INDEPENDENT_CODE ON
    )
    set_target_properties(loomcontrolsplugin PROPERTIES
        EXPORT_NAME loomcontrolsplugin
        POSITION_INDEPENDENT_CODE ON
    )
    loom_set_target_defaults(loomcontrols)
    loom_set_target_defaults(loomcontrolsplugin)
    add_library(loom::loomcontrols ALIAS loomcontrols)
    add_library(loom::loomcontrolsplugin ALIAS loomcontrolsplugin)
endif()

# ---------------------------------------------------------------------------
# Development tooling: the framing protocol, the in-application runtime, and the
# CLI. The styling library above is usable on its own; these add the build and
# hot-reload half.
# ---------------------------------------------------------------------------

add_library(loom_protocol STATIC
    src/protocol/protocol.cpp
)
add_library(loom::Protocol ALIAS loom_protocol)
target_include_directories(loom_protocol
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(loom_protocol PUBLIC Qt6::Core)
target_compile_features(loom_protocol PUBLIC cxx_std_20)
set_target_properties(loom_protocol PROPERTIES
    EXPORT_NAME Protocol
    POSITION_INDEPENDENT_CODE ON
)
loom_set_target_defaults(loom_protocol)

add_library(loom_runtime STATIC
    include/loom/reloadcontroller.h
    src/runtime/application.cpp
    src/runtime/reloadcontroller.cpp
    src/runtime/statecapture.h
    src/runtime/statecapture.cpp
)
add_library(loom::Runtime ALIAS loom_runtime)
target_include_directories(loom_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(loom_runtime
    PUBLIC
        loom_protocol
        # The structural expression of the merge: an application with hot reload
        # also has the styling layer, and the runtime can reload design tokens
        # in-process without recreating the scene.
        loom
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
    PRIVATE
        # statecapture.cpp only: QML-declared properties are not QProperty-backed,
        # so QQmlPropertyPrivate::binding() is the only way to tell a bound
        # property from a stored one before overwriting it.
        Qt6::QmlPrivate
)
target_compile_features(loom_runtime PUBLIC cxx_std_20)
set_target_properties(loom_runtime PROPERTIES
    EXPORT_NAME Runtime
    POSITION_INDEPENDENT_CODE ON
)
loom_set_target_defaults(loom_runtime)

if(LOOM_BUILD_CLI)
    # Shared by the installed shim and by `loom new`, which records the exact
    # proxy directory in local CLion settings. Define it before loom_cli_core's
    # compile definitions rather than duplicating the install layout in C++.
    set(LOOM_QMLLS_SHIM_INSTALL_DIR "${CMAKE_INSTALL_LIBEXECDIR}/loom")

    # QLibraryInfo::BinariesPath is not enough on distributions that install
    # public Qt commands to /usr/bin but host tools such as qmlls below
    # /usr/lib/qt6/bin. Preserve the exact host tool as a last-resort runtime
    # hint; discovery still prefers explicit and current-machine settings.
    set(loom_qt_host_prefix "${QT_HOST_PATH}")
    if(loom_qt_host_prefix STREQUAL "")
        set(loom_qt_host_prefix "${QT6_INSTALL_PREFIX}")
    endif()
    find_program(LOOM_QMLLS_FALLBACK_EXECUTABLE
        NAMES qmlls qmlls.exe
        HINTS
            "${loom_qt_host_prefix}/${QT6_HOST_INFO_BINDIR}"
            "${loom_qt_host_prefix}/bin"
        NO_CACHE
    )
    if(NOT LOOM_QMLLS_FALLBACK_EXECUTABLE)
        set(LOOM_QMLLS_FALLBACK_EXECUTABLE "")
    endif()

    # Everything the CLI does lives here rather than in the executable, so tests
    # can link it. Compiling these sources a second time into each test binary
    # was why commands/buildrunner/scaffolder/doctor had no unit tests at all.
    # Deliberately not installed: this is an internal build artifact.
    qt_add_library(loom_cli_core STATIC
        src/cli/buildrunner.cpp
        src/cli/commandline.cpp
        src/cli/commands.cpp
        src/cli/devserver.cpp
        src/cli/devsession.cpp
        src/cli/languageserverproxy.cpp
        src/cli/lspdocument.cpp
        src/cli/lspjsonrpc.cpp
        src/cli/projectmanifest.cpp
        src/cli/projectscaffolder.cpp
        src/cli/stylecheck.cpp
        src/cli/styleintelligence.cpp
        src/cli/styleworkspace.cpp
        src/cli/toolchaindoctor.cpp
    )
    target_include_directories(loom_cli_core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli"
    )
    target_include_directories(loom_cli_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/style"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tokens"
    )
    target_link_libraries(loom_cli_core PUBLIC
        loom_protocol
        # For the style catalogue behind `loom style` and `loom lint`. Pulls in
        # Qt Gui/Qml/Quick, but the CLI stays a QCoreApplication: the catalogue
        # never needs a GUI application object or a QML engine.
        loom
        Qt6::Core
        Qt6::Network
        Qt6::QmlPrivate
    )
    target_compile_features(loom_cli_core PUBLIC cxx_std_20)
    target_compile_definitions(loom_cli_core PUBLIC
        LOOM_VERSION_STR="${PROJECT_VERSION}"
        LOOM_TEMPLATE_DIR=":/loom/templates/app"
        # Where loomConfig.cmake sits relative to the install prefix, so the CLI
        # can find its own CMake package and pass it to generated projects.
        # Baked in because CMAKE_INSTALL_LIBDIR may be lib, lib64 or a
        # multiarch triplet.
        LOOM_RELATIVE_CMAKE_DIR="${CMAKE_INSTALL_LIBDIR}/cmake/loom"
        LOOM_RELATIVE_QMLLS_SHIM_DIR="${LOOM_QMLLS_SHIM_INSTALL_DIR}"
        LOOM_QMLLS_FALLBACK_PATH="${LOOM_QMLLS_FALLBACK_EXECUTABLE}"
    )
    set_target_properties(loom_cli_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    loom_set_target_defaults(loom_cli_core)
    set_source_files_properties(
        templates/app/tests/tst_smoke.cpp.in
        PROPERTIES SKIP_AUTOMOC ON
    )
    # On a static library Qt emits the resource as an object library and
    # propagates it through INTERFACE_SOURCES, so the initializer survives the
    # link. Verified by running `loom new`, not by compiling: a dropped
    # initializer reads as an empty template rather than a link error.
    qt_add_resources(loom_cli_core "loom_templates"
        PREFIX "/loom/templates/app"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/templates/app"
        FILES
            templates/app/.clang-format.in
            templates/app/.github/workflows/ci.yml.in
            templates/app/.gitignore.in
            templates/app/.idea/QtSettings.xml.in
            templates/app/.idea/qmlSettings.xml.in
            templates/app/CMakeLists.txt.in
            templates/app/CMakePresets.json.in
            templates/app/design/tokens.json.in
            templates/app/qml/components/FeatureCard.qml.in
            templates/app/qml/Main.qml.in
            templates/app/qml/pages/HomePage.qml.in
            templates/app/README.md.in
            templates/app/src/main.cpp.in
            templates/app/tests/tst_smoke.cpp.in
    )

    # The library target is already named `loom`, so the executable cannot be.
    # OUTPUT_NAME is what makes the installed binary `bin/loom` regardless.
    #
    # ARCHIVE_OUTPUT_NAME keeps that rename from colliding on Windows, where an
    # executable also produces an import library named after its output: the
    # linker is handed /implib:loom.lib beside the static library's own
    # loom.lib and stops with "LNK1149: output filename matches input
    # filename". Nothing installs this import library; it only needs a name of
    # its own. Inert on platforms where executables have no archive output.
    qt_add_executable(loom_cli src/cli/main.cpp)
    set_target_properties(loom_cli PROPERTIES
        OUTPUT_NAME loom
        ARCHIVE_OUTPUT_NAME loom_cli
    )
    target_link_libraries(loom_cli PRIVATE loom_cli_core)
    loom_set_target_defaults(loom_cli)

    # CLion discovers QML language servers by executable name and cannot launch
    # `loom lsp` as a command plus arguments. This companion binary presents the
    # expected `qmlls` name while running the same proxy as the CLI. It is
    # installed outside bin so it never overwrites or shadows Qt's real qmlls.
    qt_add_executable(loom_qmlls_shim src/cli/qmllsshim.cpp)
    set_target_properties(loom_qmlls_shim PROPERTIES OUTPUT_NAME qmlls)
    target_link_libraries(loom_qmlls_shim PRIVATE loom_cli_core)
    loom_set_target_defaults(loom_qmlls_shim)

    if(UNIX)
        # qt_deploy_runtime_dependencies installs the CLI's Qt libraries under
        # <prefix>/<libdir>. The shim lives two levels deeper, so give it an
        # install-relative lookup path as well. It intentionally has no qt.conf:
        # QLibraryInfo should keep describing the real Qt installation whose
        # qmlls the proxy launches.
        file(RELATIVE_PATH loom_qmlls_shim_to_lib
            "/${LOOM_QMLLS_SHIM_INSTALL_DIR}"
            "/${CMAKE_INSTALL_LIBDIR}"
        )
        if(APPLE)
            set(loom_qmlls_shim_origin "@loader_path")
        else()
            set(loom_qmlls_shim_origin "\$ORIGIN")
        endif()
        set_target_properties(loom_qmlls_shim PROPERTIES
            INSTALL_RPATH "${loom_qmlls_shim_origin}/${loom_qmlls_shim_to_lib}"
        )
    endif()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/loomFunctions.cmake")

# qt_add_qml_module generates helper object libraries that the exported targets
# link against: loomplugin_init (the static plugin's Q_IMPORT_QML_PLUGIN
# registration) and loom_resources_N (the qmldir/QML resources). All of them
# must be in the export set or install(EXPORT)/export(EXPORT) fail. Collected
# by probing rather than hardcoded, because the set grows when QML files or
# resources are added to the module.
set(LOOM_GENERATED_HELPER_TARGETS "")
foreach(loom_helper IN ITEMS
    loomplugin_init
    loom_resources_1
    loom_resources_2
    loom_resources_3
    loom_qmlcache
    # Loom.Controls generates the same family. It has QML_FILES where `loom`
    # has none, so its resource targets are the ones actually carrying the
    # component sources -- omitting them fails install(EXPORT) at configure
    # time with an unexported-dependency error, not at build time.
    loomcontrolsplugin_init
    loomcontrols_resources_1
    loomcontrols_resources_2
    loomcontrols_resources_3
    loomcontrols_qmlcache
)
    if(TARGET "${loom_helper}")
        list(APPEND LOOM_GENERATED_HELPER_TARGETS "${loom_helper}")
    endif()
endforeach()

if(LOOM_BUILD_CONTROLS)
    list(APPEND LOOM_EXPORTED_LIBRARIES loomcontrols loomcontrolsplugin)
endif()
install(TARGETS loom loomplugin loom_protocol loom_runtime
    ${LOOM_EXPORTED_LIBRARIES} ${LOOM_GENERATED_HELPER_TARGETS}
    EXPORT loomTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/loom-objects"
)
if(LOOM_BUILD_CLI)
    install(TARGETS loom_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(TARGETS loom_qmlls_shim
        RUNTIME DESTINATION "${LOOM_QMLLS_SHIM_INSTALL_DIR}"
        BUNDLE DESTINATION "${LOOM_QMLLS_SHIM_INSTALL_DIR}"
    )
    # macOS is the one desktop platform where Qt will not bundle itself around a
    # command-line binary: qt_deploy_runtime_dependencies delegates to
    # macdeployqt, which only understands .app bundles and fails the install
    # outright with "Could not find app bundle" on a plain executable. Do the
    # same job directly -- copy the frameworks the CLI actually loads in beside
    # it and resolve them through @executable_path -- so the installed tree, and
    # the disk image CPack builds from it, carry their own Qt instead of
    # pointing at whichever one happened to be on the machine that built them.
    #
    # Deliberately no absolute Qt path in the rpath as well: a fallback would
    # hide an incomplete copy on the build machine and produce a package that
    # only fails once it reaches someone else's.
    if(APPLE)
        set_property(TARGET loom_cli APPEND PROPERTY
            INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}"
        )
        # The shim already carries a @loader_path entry to the same directory.
        install(CODE "set(loom_built_cli \"$<TARGET_FILE:loom_cli>\")")
        install(CODE "set(loom_relative_libdir \"${CMAKE_INSTALL_LIBDIR}\")")
        install(CODE [[
            # Asked of the built binary rather than the installed one: the
            # install rpath points at frameworks this is about to put there, so
            # resolving against it would find nothing.
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES "${loom_built_cli}"
                RESOLVED_DEPENDENCIES_VAR loom_resolved
                UNRESOLVED_DEPENDENCIES_VAR loom_unresolved
                POST_EXCLUDE_REGEXES "^/usr/lib/" "^/System/"
            )
            set(loom_frameworks "")
            set(loom_libraries "")
            foreach(loom_dependency IN LISTS loom_resolved)
                # .../QtCore.framework/Versions/A/QtCore -> .../QtCore.framework
                if(loom_dependency MATCHES "^(.*\\.framework)/")
                    list(APPEND loom_frameworks "${CMAKE_MATCH_1}")
                else()
                    list(APPEND loom_libraries "${loom_dependency}")
                endif()
            endforeach()
            list(REMOVE_DUPLICATES loom_frameworks)
            list(REMOVE_DUPLICATES loom_libraries)

            set(loom_libdir "${CMAKE_INSTALL_PREFIX}/${loom_relative_libdir}")
            if(loom_frameworks)
                # Headers are a large part of a Qt framework and nothing loads
                # them; the binary and its Resources are what dyld needs.
                file(INSTALL DESTINATION "${loom_libdir}" TYPE DIRECTORY
                    FILES ${loom_frameworks}
                    REGEX "/Headers(/|$)" EXCLUDE
                )
            endif()
            if(loom_libraries)
                file(INSTALL DESTINATION "${loom_libdir}" TYPE SHARED_LIBRARY
                    FOLLOW_SYMLINK_CHAIN FILES ${loom_libraries}
                )
            endif()
            # Dropping the headers rewrites the bundle a signature covers, so
            # re-sign ad hoc the way macdeployqt does. Best effort: a machine
            # without codesign is not a reason to fail an install.
            foreach(loom_framework IN LISTS loom_frameworks)
                get_filename_component(loom_framework_name "${loom_framework}" NAME)
                execute_process(
                    COMMAND codesign --force --sign - "${loom_libdir}/${loom_framework_name}"
                    OUTPUT_QUIET ERROR_QUIET
                )
            endforeach()
            if(loom_unresolved)
                message(WARNING
                    "loom: these runtime dependencies were not found, and the "
                    "installed CLI will look for them on the host: ${loom_unresolved}")
            endif()
        ]])
    else()
        qt_generate_deploy_script(
            TARGET loom_cli
            OUTPUT_SCRIPT loom_deploy_script
            CONTENT "
                qt_deploy_runtime_dependencies(
                    EXECUTABLE \"$<TARGET_FILE:loom_cli>\"
                    GENERATE_QT_CONF
                    NO_PLUGINS
                    NO_TRANSLATIONS
                    NO_COMPILER_RUNTIME
                    POST_EXCLUDE_REGEXES
                        [[^/lib/]]
                        [[^/lib64/]]
                        [[^/usr/lib/]]
                        [[^/usr/lib64/]]
                    POST_INCLUDE_FILES
                        \"$<TARGET_FILE:Qt6::Core>\"
                        \"$<TARGET_FILE:Qt6::Network>\"
                        # Gui/Qml/Quick came in with the style catalogue behind
                        # `loom style`. Without them the deployed binary resolves
                        # Core and Network from its own lib/ and the rest from
                        # the host, which is exactly the mismatch this list
                        # prevents. No semicolons in this block: CONTENT reaches
                        # cmake_parse_arguments through an unquoted ${ARGN}, so a
                        # semicolon splits the script into stray arguments.
                        \"$<TARGET_FILE:Qt6::Gui>\"
                        \"$<TARGET_FILE:Qt6::Qml>\"
                        \"$<TARGET_FILE:Qt6::Quick>\"
                )
            "
        )
        install(SCRIPT "${loom_deploy_script}")
    endif()
    install(CODE "set(loom_relative_bindir \"${CMAKE_INSTALL_BINDIR}\")")
    install(CODE
        "set(loom_relative_qmlls_shim \"${LOOM_QMLLS_SHIM_INSTALL_DIR}/qmlls${CMAKE_EXECUTABLE_SUFFIX}\")"
    )
    install(CODE [[
        if(NOT DEFINED ENV{DESTDIR} AND NOT DEFINED ENV{LOOM_QUIET_INSTALL})
            cmake_path(SET loom_bindir NORMALIZE "${CMAKE_INSTALL_PREFIX}/${loom_relative_bindir}")
            cmake_path(SET loom_qmlls_shim NORMALIZE "${CMAKE_INSTALL_PREFIX}/${loom_relative_qmlls_shim}")
            message(STATUS "CLion qmlls proxy: ${loom_qmlls_shim}")
            cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST loom_path_entries)
            if(NOT loom_bindir IN_LIST loom_path_entries)
                message(STATUS "loom is installed to ${loom_bindir}, which is not on PATH.")
                message(STATUS "  fish: fish_add_path -U \"${loom_bindir}\"")
                message(STATUS "  bash/zsh: echo 'export PATH=\"${loom_bindir}:$PATH\"' >> ~/.profile")
                message(STATUS "  or run it directly: ${loom_bindir}/loom --prefix \"${CMAKE_INSTALL_PREFIX}\"")
            endif()
        endif()
    ]])
endif()
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY schemas/ DESTINATION "${CMAKE_INSTALL_DATADIR}/loom/schemas")
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
foreach(qt_license_directory IN ITEMS
    "${QT6_INSTALL_PREFIX}/LICENSES"
    "/usr/share/licenses/qt6-base"
)
    if(EXISTS "${qt_license_directory}")
        install(
            DIRECTORY "${qt_license_directory}/"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}/third-party/qt"
        )
        break()
    endif()
endforeach()
install(FILES cmake/loomFunctions.cmake DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/loom")
# qmldir + loom.qmltypes, so consumer qmllint and Qt Creator resolve `import
# Loom` without the consumer compiling anything. The static plugin archive is
# not in here; linking happens through the exported CMake targets.
install(
    DIRECTORY "${CMAKE_BINARY_DIR}/qml/Loom"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/qml"
    # The plugin archive consumers link against is the exported target in
    # ${CMAKE_INSTALL_LIBDIR}; the copy Qt drops next to the qmldir is dead
    # weight.
    PATTERN "*.a" EXCLUDE
)
install(EXPORT loomTargets
    FILE loomTargets.cmake
    NAMESPACE loom::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/loom"
)

# The install-tree config lives in a subdirectory so it cannot be picked up by
# -DCMAKE_PREFIX_PATH=<builddir>. It hardcodes install-relative paths, so being
# found from the build tree would produce a confusing failure rather than a miss.
configure_package_config_file(
    cmake/loomConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/install/loomConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/loom"
    PATH_VARS CMAKE_INSTALL_LIBDIR
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/install/loomConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/install/loomConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/install/loomConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/loom"
)

# A usable build-tree package, so -DCMAKE_PREFIX_PATH=<builddir> works without
# installing.
export(EXPORT loomTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/loomTargets.cmake"
    NAMESPACE loom::
)
configure_file(
    cmake/loomFunctions.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/loomFunctions.cmake"
    COPYONLY
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/loomConfig.cmake"
"# Generated build-tree package for loom. Not installed.
include(CMakeFindDependencyMacro)
find_dependency(Qt6 6.11 COMPONENTS ${LOOM_QT_COMPONENTS_STR})

include(\"\${CMAKE_CURRENT_LIST_DIR}/loomTargets.cmake\")
include(\"\${CMAKE_CURRENT_LIST_DIR}/loomFunctions.cmake\")
set(LOOM_QML_IMPORT_DIR \"${CMAKE_BINARY_DIR}/qml\")
")
configure_file(
    "${CMAKE_CURRENT_BINARY_DIR}/install/loomConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/loomConfigVersion.cmake"
    COPYONLY
)

# Examples before tests: the qmllint test only registers when the gallery
# target exists.
if(LOOM_BUILD_EXAMPLES)
    add_subdirectory(examples/gallery)
endif()

if(LOOM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "loom")
set(CPACK_PACKAGE_VENDOR "loom contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Utility-first styling and live reload for Qt/QML")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Arcadyi/loom")
set(CPACK_PACKAGE_CONTACT "loom contributors")
# Copied rather than named where it lies, because WiX decides how to render the
# licence from the file extension and refuses one it does not recognise: "
# unsupported WiX License file extension ''" is the whole of an MSI build
# failing over a file called LICENSE. The copy costs nothing on the generators
# that do not care.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt"
    COPYONLY
)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
if(WIN32)
    set(CPACK_GENERATOR "WIX")
    # Pinned to the value CPack has been deriving on its own. An installer
    # identifies the product it upgrades by this, so letting it be implicit
    # means it changes the day something it is derived from changes, and the
    # next MSI installs alongside the old one instead of replacing it.
    set(CPACK_WIX_UPGRADE_GUID "6F928CC1-81DC-442A-92E5-BEA5F2D9AF44")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "loom contributors")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # CPack otherwise shells out to dpkg to discover this value and silently
    # falls back to i386 on non-Debian build hosts. Release builders are not
    # required to have dpkg, so map CMake's processor name explicitly.
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" loom_deb_processor)
    if(loom_deb_processor MATCHES "^(x86_64|amd64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    elseif(loom_deb_processor MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    elseif(loom_deb_processor MATCHES "^(armv7|armv7l)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
    elseif(loom_deb_processor MATCHES "^(i[3-6]86|x86)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
    elseif(loom_deb_processor STREQUAL "ppc64le")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "ppc64el")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${loom_deb_processor}")
    endif()
endif()
set(CPACK_SOURCE_GENERATOR "TGZ")
# Anchored at the tree root so a source package never ships a build tree, an
# install tree, or editor state. Without this, `cpack --config
# CPackSourceConfig.cmake` packages whatever happens to be lying around.
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/\\\\.github/"
    "/\\\\.idea/"
    "/\\\\.cache/"
    "/\\\\.claude/"
    "/build/"
    "/build-.*/"
    "/dist/"
    "/CMakeFiles/"
    "CMakeCache\\\\.txt$"
    "CMakeUserPresets\\\\.json$"
    "compile_commands\\\\.json$"
    "\\\\.user$"
    "\\\\.swp$"
)
include(CPack)
