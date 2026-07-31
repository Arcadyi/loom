include_guard(GLOBAL)

function(respin_enable_hot_reload)
    set(options)
    set(oneValueArgs TARGET URI ENTRY QML_ROOT ASSET_ROOT)
    cmake_parse_arguments(RESPIN "${options}" "${oneValueArgs}" "" ${ARGN})

    # NOT DEFINED / STREQUAL "" rather than a truthiness test: if(NOT RESPIN_X)
    # is also true for a legitimate value of OFF, NO, 0, or anything ending in
    # -NOTFOUND, so a valid argument could be rejected as missing.
    foreach(required TARGET URI ENTRY QML_ROOT)
        if(NOT DEFINED RESPIN_${required} OR RESPIN_${required} STREQUAL "")
            message(FATAL_ERROR "respin_enable_hot_reload requires ${required}")
        endif()
    endforeach()
    # Without these, respin_enable_hot_reload(TARGET App URl com.example.App)
    # silently ignores the typo and configures with no URI.
    if(RESPIN_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "respin_enable_hot_reload received unknown arguments: "
            "${RESPIN_UNPARSED_ARGUMENTS}")
    endif()
    if(RESPIN_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "respin_enable_hot_reload is missing values for: "
            "${RESPIN_KEYWORDS_MISSING_VALUES}")
    endif()

    # Build-tree directories CMake, Qt and CTest create for themselves. A target
    # with one of these names cannot be linked: the linker is handed an output
    # path that is already a directory. respin_add_application redirects its own
    # target into bin/, but a project integrated with respin init keeps whatever
    # layout it had, and respin cannot fix that project's CMake for it.
    set(respin_reserved_names
        Testing CMakeFiles CMakeCache bin lib include share
        meta_types qmltypes .qt .rcc
    )
    if("${RESPIN_TARGET}" IN_LIST respin_reserved_names)
        message(WARNING
            "Target '${RESPIN_TARGET}' has the same name as a directory the build "
            "tree creates, which usually fails at link time with \"cannot open "
            "output file\". Set RUNTIME_OUTPUT_DIRECTORY on the target, or rename "
            "it.")
    endif()

    if(NOT TARGET "${RESPIN_TARGET}")
        message(FATAL_ERROR "respin target '${RESPIN_TARGET}' does not exist")
    endif()

    target_link_libraries("${RESPIN_TARGET}" PRIVATE respin::Runtime)
    target_compile_definitions("${RESPIN_TARGET}" PRIVATE
        RESPIN_APP_URI="${RESPIN_URI}"
        RESPIN_APP_ENTRY="${RESPIN_ENTRY}"
    )

    set_property(TARGET "${RESPIN_TARGET}" PROPERTY RESPIN_URI "${RESPIN_URI}")
    set_property(TARGET "${RESPIN_TARGET}" PROPERTY RESPIN_ENTRY "${RESPIN_ENTRY}")
    set_property(TARGET "${RESPIN_TARGET}" PROPERTY RESPIN_QML_ROOT "${RESPIN_QML_ROOT}")
    if(RESPIN_ASSET_ROOT)
        set_property(TARGET "${RESPIN_TARGET}" PROPERTY RESPIN_ASSET_ROOT "${RESPIN_ASSET_ROOT}")
    endif()
endfunction()

function(respin_add_application target)
    set(options)
    set(oneValueArgs URI ENTRY QML_ROOT ASSET_ROOT)
    set(multiValueArgs SOURCES SINGLETONS IMPORT_PATHS)
    cmake_parse_arguments(RESPIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach(required URI ENTRY QML_ROOT)
        if(NOT DEFINED RESPIN_${required} OR RESPIN_${required} STREQUAL "")
            message(FATAL_ERROR "respin_add_application requires ${required}")
        endif()
    endforeach()
    # The motivating typo: respin_add_application(App SOURCE src/main.cpp) --
    # SOURCE rather than SOURCES -- used to configure happily with no sources.
    if(RESPIN_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "respin_add_application received unknown arguments: "
            "${RESPIN_UNPARSED_ARGUMENTS}")
    endif()
    if(RESPIN_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "respin_add_application is missing values for: "
            "${RESPIN_KEYWORDS_MISSING_VALUES}")
    endif()

    file(GLOB_RECURSE respin_qml_files CONFIGURE_DEPENDS
        "${RESPIN_QML_ROOT}/*.qml"
        "${RESPIN_QML_ROOT}/*.js"
        "${RESPIN_QML_ROOT}/*.mjs"
    )
    set(respin_resources)
    if(RESPIN_ASSET_ROOT AND EXISTS "${RESPIN_ASSET_ROOT}")
        file(GLOB_RECURSE respin_resources CONFIGURE_DEPENDS
            "${RESPIN_ASSET_ROOT}/*"
        )
    endif()

    foreach(source IN LISTS respin_qml_files)
        file(RELATIVE_PATH alias "${RESPIN_QML_ROOT}" "${source}")
        set_source_files_properties("${source}" PROPERTIES QT_RESOURCE_ALIAS "${alias}")
    endforeach()

    # SINGLETONS names files relative to QML_ROOT. Qt records the singleton flag
    # in the module's generated qmldir, which respin now bundles for development
    # too, so a singleton is a singleton in both builds rather than only in the
    # compiled one.
    foreach(singleton IN LISTS RESPIN_SINGLETONS)
        set(respin_singleton_path "${RESPIN_QML_ROOT}/${singleton}")
        if(NOT EXISTS "${respin_singleton_path}")
            message(FATAL_ERROR
                "respin_add_application SINGLETONS lists '${singleton}', which does "
                "not exist under QML_ROOT (${RESPIN_QML_ROOT})")
        endif()
        if(NOT "${respin_singleton_path}" IN_LIST respin_qml_files)
            message(FATAL_ERROR
                "respin_add_application SINGLETONS lists '${singleton}', which is not "
                "one of the module's QML files")
        endif()
        set_source_files_properties("${respin_singleton_path}" PROPERTIES
            QT_QML_SINGLETON_TYPE TRUE
        )
    endforeach()
    foreach(source IN LISTS respin_resources)
        file(RELATIVE_PATH alias "${RESPIN_ASSET_ROOT}" "${source}")
        set_source_files_properties("${source}" PROPERTIES QT_RESOURCE_ALIAS "assets/${alias}")
    endforeach()

    qt_add_executable("${target}" ${RESPIN_SOURCES})

    # Keep application binaries in bin/ so that an application name can never
    # collide with a build-tree directory. CTest reserves a directory named
    # Testing/, which otherwise breaks linking for an application called
    # Testing. respin's dev command resolves the executable from this location.
    set_target_properties("${target}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    foreach(respin_config IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${respin_config}" respin_config_suffix)
        set_target_properties("${target}" PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${respin_config_suffix}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()

    # IMPORT_PATHS reaches qt_add_qml_module rather than being set afterwards
    # as a target property: Qt reads QT_QML_IMPORT_PATH eagerly while building
    # the generated qmllint/qmlcachegen command lines here, so a later
    # set_property never appears in them. This is how an application resolves
    # `import` of a QML module shipped by a separate package (loom, say) for
    # tooling; linking that package's plugin is what registers it at runtime.
    qt_add_qml_module("${target}"
        URI "${RESPIN_URI}"
        QML_FILES ${respin_qml_files}
        RESOURCES ${respin_resources}
        IMPORT_PATH ${RESPIN_IMPORT_PATHS}
    )
    target_link_libraries("${target}" PRIVATE Qt6::Quick)
    respin_enable_hot_reload(
        TARGET "${target}"
        URI "${RESPIN_URI}"
        ENTRY "${RESPIN_ENTRY}"
        QML_ROOT "${RESPIN_QML_ROOT}"
        ASSET_ROOT "${RESPIN_ASSET_ROOT}"
    )
endfunction()

# Installs an application built with respin_add_application into a prefix that
# can be packaged or shipped.
#
# Qt is not bundled, and there is currently no option to bundle it. Both routes
# were measured on Linux with Qt 6.11 against a distribution Qt rooted at /usr:
#
#   * qt_generate_deploy_qml_app_script produces a 247 MB tree that includes
#     ld-linux-x86-64.so.2 itself, and the resulting binary core-dumps.
#   * qt_deploy_runtime_dependencies with the system directories excluded and
#     the Qt libraries named back in -- the approach that successfully ships
#     respin's own CLI -- fails for a GUI QML application inside
#     file(GET_RUNTIME_DEPENDENCIES) with "file unknown error", with and
#     without ADDITIONAL_MODULES for the QML plugins.
#
# The difference appears to be console application versus GUI application with
# plugins, not the distribution Qt: respin's own bin/respin does bundle Qt this
# way and resolves it from its own lib/. Until that is understood, use a
# dedicated packaging tool (linuxdeploy, appimage-builder) over the installed
# prefix. What this function produces is a correct, ordinary install that runs
# against the Qt the host already has.
function(respin_install_application)
    set(oneValueArgs TARGET)
    cmake_parse_arguments(RESPIN "" "${oneValueArgs}" "" ${ARGN})

    if(NOT DEFINED RESPIN_TARGET OR RESPIN_TARGET STREQUAL "")
        message(FATAL_ERROR "respin_install_application requires TARGET")
    endif()
    if(RESPIN_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "respin_install_application received unknown arguments: "
            "${RESPIN_UNPARSED_ARGUMENTS}")
    endif()
    if(RESPIN_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "respin_install_application is missing values for: "
            "${RESPIN_KEYWORDS_MISSING_VALUES}")
    endif()
    if(NOT TARGET "${RESPIN_TARGET}")
        message(FATAL_ERROR
            "respin target '${RESPIN_TARGET}' does not exist")
    endif()

    include(GNUInstallDirs)

    # Application QML is compiled into the executable's resources by
    # qt_add_qml_module, so the binary is self-contained apart from Qt itself.
    install(TARGETS "${RESPIN_TARGET}"
        BUNDLE DESTINATION .
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

endfunction()
