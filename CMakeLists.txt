cmake_minimum_required(VERSION 3.22)

project(loom VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LOOM_BUILD_TESTS "Build loom tests" ON)
option(LOOM_BUILD_EXAMPLES "Build the loom gallery example" ON)
option(LOOM_BUILD_TOOLS "Build the loomstyle command-line tool" ON)
option(LOOM_WERROR "Treat compiler warnings as errors" OFF)
option(LOOM_SANITIZE "Build with the address and undefined-behavior sanitizers" OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick)

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

install(TARGETS loom loomplugin ${LOOM_GENERATED_HELPER_TARGETS}
    EXPORT loomTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/loom-objects"
)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
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
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/loomConfig.cmake"
"# Generated build-tree package for loom. Not installed.
include(CMakeFindDependencyMacro)
find_dependency(Qt6 6.11 COMPONENTS Core Gui Qml Quick)

include(\"\${CMAKE_CURRENT_LIST_DIR}/loomTargets.cmake\")
set(LOOM_QML_IMPORT_DIR \"${CMAKE_BINARY_DIR}/qml\")
")
configure_file(
    "${CMAKE_CURRENT_BINARY_DIR}/install/loomConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/loomConfigVersion.cmake"
    COPYONLY
)

if(LOOM_BUILD_TOOLS)
    add_subdirectory(tools/loomstyle)
endif()

# Examples before tests: the qmllint test only registers when the gallery
# target exists.
if(LOOM_BUILD_EXAMPLES)
    add_subdirectory(examples/gallery)
endif()

if(LOOM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
