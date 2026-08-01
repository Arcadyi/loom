cmake_minimum_required(VERSION 3.22)

project(loom VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LOOM_BUILD_TESTS "Build loom tests" ON)
option(LOOM_BUILD_E2E_TESTS "Build the scaffold-then-build end-to-end tests" ON)
option(LOOM_BUILD_CLI "Build the loom command-line tool" ON)
option(LOOM_BUILD_EXAMPLES "Build the loom gallery example" ON)
option(LOOM_WERROR "Treat compiler warnings as errors" OFF)
option(LOOM_SANITIZE "Build with the address and undefined-behavior sanitizers" OFF)
# The schema test needs a Python JSON Schema validator and skips without one.
# Turn this on where the validator is expected to be present -- CI -- so a
# missing one is a failure rather than a pass nobody notices.
option(LOOM_STRICT_SCHEMA_TEST "Fail rather than skip when jsonschema is missing" OFF)

# Network carries the development server and reload transport; QmlPrivate is for
# src/runtime/statecapture.cpp alone -- see the comment on the loom_runtime link
# below for why the private API is unavoidable there.
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Network Qml QmlPrivate Quick)

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
    set(LOOM_WARNING_OPTIONS /W4 /permissive-)
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
            templates/app/CMakeLists.txt.in
            templates/app/CMakePresets.json.in
            templates/app/design/tokens.json.in
            templates/app/qml/Main.qml.in
            templates/app/README.md.in
            templates/app/src/main.cpp.in
            templates/app/tests/tst_smoke.cpp.in
    )

    # The library target is already named `loom`, so the executable cannot be.
    # OUTPUT_NAME is what makes the installed binary `bin/loom` regardless.
    qt_add_executable(loom_cli src/cli/main.cpp)
    set_target_properties(loom_cli PROPERTIES OUTPUT_NAME loom)
    target_link_libraries(loom_cli PRIVATE loom_cli_core)
    loom_set_target_defaults(loom_cli)
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
)
    if(TARGET "${loom_helper}")
        list(APPEND LOOM_GENERATED_HELPER_TARGETS "${loom_helper}")
    endif()
endforeach()

install(TARGETS loom loomplugin loom_protocol loom_runtime ${LOOM_GENERATED_HELPER_TARGETS}
    EXPORT loomTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/loom-objects"
)
if(LOOM_BUILD_CLI)
    install(TARGETS loom_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
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
                    # Core and Network from its own lib/ and the rest from the
                    # host, which is exactly the mismatch this list prevents.
                    # No semicolons in this block: CONTENT reaches
                    # cmake_parse_arguments through an unquoted ${ARGN}, so a
                    # semicolon splits the script into stray arguments.
                    \"$<TARGET_FILE:Qt6::Gui>\"
                    \"$<TARGET_FILE:Qt6::Qml>\"
                    \"$<TARGET_FILE:Qt6::Quick>\"
            )
        "
    )
    install(SCRIPT "${loom_deploy_script}")
    install(CODE "set(loom_relative_bindir \"${CMAKE_INSTALL_BINDIR}\")")
    install(CODE [[
        if(NOT DEFINED ENV{DESTDIR} AND NOT DEFINED ENV{LOOM_QUIET_INSTALL})
            cmake_path(SET loom_bindir NORMALIZE "${CMAKE_INSTALL_PREFIX}/${loom_relative_bindir}")
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
find_dependency(Qt6 6.11 COMPONENTS Core Gui Network Qml QmlPrivate Quick)

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
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
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
