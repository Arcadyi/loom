include_guard(GLOBAL)

function(loom_enable_hot_reload)
    set(options)
    set(oneValueArgs TARGET URI ENTRY QML_ROOT ASSET_ROOT)
    cmake_parse_arguments(LOOM_ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    # NOT DEFINED / STREQUAL "" rather than a truthiness test: if(NOT LOOM_ARG_X)
    # is also true for a legitimate value of OFF, NO, 0, or anything ending in
    # -NOTFOUND, so a valid argument could be rejected as missing.
    foreach(required TARGET URI ENTRY QML_ROOT)
        if(NOT DEFINED LOOM_ARG_${required} OR LOOM_ARG_${required} STREQUAL "")
            message(FATAL_ERROR "loom_enable_hot_reload requires ${required}")
        endif()
    endforeach()
    # Without these, loom_enable_hot_reload(TARGET App URl com.example.App)
    # silently ignores the typo and configures with no URI.
    if(LOOM_ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "loom_enable_hot_reload received unknown arguments: "
            "${LOOM_ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(LOOM_ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "loom_enable_hot_reload is missing values for: "
            "${LOOM_ARG_KEYWORDS_MISSING_VALUES}")
    endif()

    # Build-tree directories CMake, Qt and CTest create for themselves. A target
    # with one of these names cannot be linked: the linker is handed an output
    # path that is already a directory. loom_add_application redirects its own
    # target into bin/, but a project integrated with loom init keeps whatever
    # layout it had, and loom cannot fix that project's CMake for it.
    set(loom_reserved_names
        Testing CMakeFiles CMakeCache bin lib include share
        meta_types qmltypes .qt .rcc
    )
    if("${LOOM_ARG_TARGET}" IN_LIST loom_reserved_names)
        message(WARNING
            "Target '${LOOM_ARG_TARGET}' has the same name as a directory the build "
            "tree creates, which usually fails at link time with \"cannot open "
            "output file\". Set RUNTIME_OUTPUT_DIRECTORY on the target, or rename "
            "it.")
    endif()

    if(NOT TARGET "${LOOM_ARG_TARGET}")
        message(FATAL_ERROR "loom target '${LOOM_ARG_TARGET}' does not exist")
    endif()

    target_link_libraries("${LOOM_ARG_TARGET}" PRIVATE loom::Runtime)
    # LOOM_APP_*, not LOOM_ARG_APP_*: these are the names the generated main.cpp
    # and include/loom/application.h read. LOOM_ARG_ is only the prefix for
    # cmake_parse_arguments output, so the definitions are spelled out here
    # rather than derived from the parsed variable names.
    target_compile_definitions("${LOOM_ARG_TARGET}" PRIVATE
        LOOM_APP_URI="${LOOM_ARG_URI}"
        LOOM_APP_ENTRY="${LOOM_ARG_ENTRY}"
    )

    set_property(TARGET "${LOOM_ARG_TARGET}" PROPERTY LOOM_URI "${LOOM_ARG_URI}")
    set_property(TARGET "${LOOM_ARG_TARGET}" PROPERTY LOOM_ENTRY "${LOOM_ARG_ENTRY}")
    set_property(TARGET "${LOOM_ARG_TARGET}" PROPERTY LOOM_QML_ROOT "${LOOM_ARG_QML_ROOT}")
    if(LOOM_ARG_ASSET_ROOT)
        set_property(TARGET "${LOOM_ARG_TARGET}" PROPERTY LOOM_ASSET_ROOT "${LOOM_ARG_ASSET_ROOT}")
    endif()
endfunction()

function(loom_add_application target)
    set(options)
    set(oneValueArgs URI ENTRY QML_ROOT ASSET_ROOT DESIGN)
    set(multiValueArgs SOURCES SINGLETONS IMPORT_PATHS)
    cmake_parse_arguments(LOOM_ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach(required URI ENTRY QML_ROOT)
        if(NOT DEFINED LOOM_ARG_${required} OR LOOM_ARG_${required} STREQUAL "")
            message(FATAL_ERROR "loom_add_application requires ${required}")
        endif()
    endforeach()
    # The motivating typo: loom_add_application(App SOURCE src/main.cpp) --
    # SOURCE rather than SOURCES -- used to configure happily with no sources.
    if(LOOM_ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "loom_add_application received unknown arguments: "
            "${LOOM_ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(LOOM_ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "loom_add_application is missing values for: "
            "${LOOM_ARG_KEYWORDS_MISSING_VALUES}")
    endif()

    file(GLOB_RECURSE loom_qml_files CONFIGURE_DEPENDS
        "${LOOM_ARG_QML_ROOT}/*.qml"
        "${LOOM_ARG_QML_ROOT}/*.js"
        "${LOOM_ARG_QML_ROOT}/*.mjs"
    )
    set(loom_asset_files)
    if(LOOM_ARG_ASSET_ROOT AND EXISTS "${LOOM_ARG_ASSET_ROOT}")
        file(GLOB_RECURSE loom_asset_files CONFIGURE_DEPENDS
            "${LOOM_ARG_ASSET_ROOT}/*"
        )
    endif()

    foreach(source IN LISTS loom_qml_files)
        file(RELATIVE_PATH alias "${LOOM_ARG_QML_ROOT}" "${source}")
        set_source_files_properties("${source}" PROPERTIES QT_RESOURCE_ALIAS "${alias}")
    endforeach()

    # SINGLETONS names files relative to QML_ROOT. Qt records the singleton flag
    # in the module's generated qmldir, which loom now bundles for development
    # too, so a singleton is a singleton in both builds rather than only in the
    # compiled one.
    foreach(singleton IN LISTS LOOM_ARG_SINGLETONS)
        set(loom_singleton_path "${LOOM_ARG_QML_ROOT}/${singleton}")
        if(NOT EXISTS "${loom_singleton_path}")
            message(FATAL_ERROR
                "loom_add_application SINGLETONS lists '${singleton}', which does "
                "not exist under QML_ROOT (${LOOM_ARG_QML_ROOT})")
        endif()
        if(NOT "${loom_singleton_path}" IN_LIST loom_qml_files)
            message(FATAL_ERROR
                "loom_add_application SINGLETONS lists '${singleton}', which is not "
                "one of the module's QML files")
        endif()
        set_source_files_properties("${loom_singleton_path}" PROPERTIES
            QT_QML_SINGLETON_TYPE TRUE
        )
    endforeach()
    foreach(source IN LISTS loom_asset_files)
        file(RELATIVE_PATH alias "${LOOM_ARG_ASSET_ROOT}" "${source}")
        set_source_files_properties("${source}" PROPERTIES QT_RESOURCE_ALIAS "assets/${alias}")
    endforeach()

    # The design tokens ride into the module's resources under a fixed alias, so
    # loom::Application can load them with no path configuration. In development
    # `loom dev` pushes the on-disk file instead, which is what makes an edit
    # repaint the running window; this copy is what a release build reads.
    if(LOOM_ARG_DESIGN)
        if(NOT EXISTS "${LOOM_ARG_DESIGN}")
            message(FATAL_ERROR
                "loom_add_application DESIGN points at '${LOOM_ARG_DESIGN}', which "
                "does not exist")
        endif()
        set_source_files_properties("${LOOM_ARG_DESIGN}" PROPERTIES
            QT_RESOURCE_ALIAS "loom-design.json"
        )
        list(APPEND loom_asset_files "${LOOM_ARG_DESIGN}")
    endif()

    qt_add_executable("${target}" ${LOOM_ARG_SOURCES})

    # Keep application binaries in bin/ so that an application name can never
    # collide with a build-tree directory. CTest reserves a directory named
    # Testing/, which otherwise breaks linking for an application called
    # Testing. loom's dev command resolves the executable from this location.
    set_target_properties("${target}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    foreach(loom_config IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${loom_config}" loom_config_suffix)
        set_target_properties("${target}" PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${loom_config_suffix}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()

    # LOOM_QML_IMPORT_DIR comes from loomConfig.cmake and holds Loom/qmldir and
    # Loom/loom.qmltypes. Defaulting to it means `import Loom` resolves for
    # qmllint and qmlls in every generated application without the caller
    # wiring it up; linking loom::loomplugin is what registers the module at
    # runtime. An explicit IMPORT_PATHS still wins, and extra paths can be
    # appended to it.
    set(loom_import_paths ${LOOM_ARG_IMPORT_PATHS})
    if(NOT loom_import_paths AND DEFINED LOOM_QML_IMPORT_DIR)
        set(loom_import_paths "${LOOM_QML_IMPORT_DIR}")
    endif()

    # CMake-aware IDEs inspect this conventional cache variable to discover
    # custom QML modules. IMPORT_PATH below is still the source of truth for Qt
    # build tools; publishing the same roots here makes `import Loom` resolve in
    # CLion/Qt Creator as soon as configure succeeds. Preserve paths supplied by
    # the application and by earlier loom targets in the same project.
    set(loom_editor_import_paths ${QML_IMPORT_PATH})
    list(APPEND loom_editor_import_paths ${loom_import_paths})
    list(REMOVE_DUPLICATES loom_editor_import_paths)
    set(QML_IMPORT_PATH "${loom_editor_import_paths}" CACHE STRING
        "Additional QML import roots exposed to IDE code models" FORCE
    )

    # IMPORT_PATHS reaches qt_add_qml_module rather than being set afterwards
    # as a target property: Qt reads QT_QML_IMPORT_PATH eagerly while building
    # the generated qmllint/qmlcachegen command lines here, so a later
    # set_property never appears in them.
    qt_add_qml_module("${target}"
        URI "${LOOM_ARG_URI}"
        QML_FILES ${loom_qml_files}
        RESOURCES ${loom_asset_files}
        IMPORT_PATH ${loom_import_paths}
    )
    target_link_libraries("${target}" PRIVATE Qt6::Quick)
    loom_enable_hot_reload(
        TARGET "${target}"
        URI "${LOOM_ARG_URI}"
        ENTRY "${LOOM_ARG_ENTRY}"
        QML_ROOT "${LOOM_ARG_QML_ROOT}"
        ASSET_ROOT "${LOOM_ARG_ASSET_ROOT}"
    )

    # Where the compiled-in copy landed, resolved against the module URI rather
    # than hardcoded: qt_add_qml_module roots module resources at
    # :/qt/qml/<uri-as-path>/. loom::Application loads this before the scene, so
    # a themed application starts themed instead of repainting on first frame.
    #
    # A ":/" resource path rather than a "qrc:" URL: loom::loadConfig opens it
    # with QFile, which understands the former and not the latter.
    if(LOOM_ARG_DESIGN)
        string(REPLACE "." "/" loom_uri_path "${LOOM_ARG_URI}")
        target_compile_definitions("${target}" PRIVATE
            LOOM_APP_DESIGN=":/qt/qml/${loom_uri_path}/loom-design.json"
        )
    endif()
endfunction()

# Installs an application built with loom_add_application into a prefix that
# can be packaged or shipped.
#
# Qt is not bundled, and there is currently no option to bundle it. Both routes
# were measured on Linux with Qt 6.11 against a distribution Qt rooted at /usr:
#
#   * qt_generate_deploy_qml_app_script produces a 247 MB tree that includes
#     ld-linux-x86-64.so.2 itself, and the resulting binary core-dumps.
#   * qt_deploy_runtime_dependencies with the system directories excluded and
#     the Qt libraries named back in -- the approach that successfully ships
#     loom's own CLI -- fails for a GUI QML application inside
#     file(GET_RUNTIME_DEPENDENCIES) with "file unknown error", with and
#     without ADDITIONAL_MODULES for the QML plugins.
#
# The difference appears to be console application versus GUI application with
# plugins, not the distribution Qt: loom's own bin/loom does bundle Qt this
# way and resolves it from its own lib/. Until that is understood, use a
# dedicated packaging tool (linuxdeploy, appimage-builder) over the installed
# prefix. What this function produces is a correct, ordinary install that runs
# against the Qt the host already has.
function(loom_install_application)
    set(oneValueArgs TARGET)
    cmake_parse_arguments(LOOM_ARG "" "${oneValueArgs}" "" ${ARGN})

    if(NOT DEFINED LOOM_ARG_TARGET OR LOOM_ARG_TARGET STREQUAL "")
        message(FATAL_ERROR "loom_install_application requires TARGET")
    endif()
    if(LOOM_ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "loom_install_application received unknown arguments: "
            "${LOOM_ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(LOOM_ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "loom_install_application is missing values for: "
            "${LOOM_ARG_KEYWORDS_MISSING_VALUES}")
    endif()
    if(NOT TARGET "${LOOM_ARG_TARGET}")
        message(FATAL_ERROR
            "loom target '${LOOM_ARG_TARGET}' does not exist")
    endif()

    include(GNUInstallDirs)

    # Application QML is compiled into the executable's resources by
    # qt_add_qml_module, so the binary is self-contained apart from Qt itself.
    install(TARGETS "${LOOM_ARG_TARGET}"
        BUNDLE DESTINATION .
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

endfunction()
