# Built as part of the loom tree (add_subdirectory) where the loom targets
# already exist. A standalone consumer build is exercised by the e2e test
# instead, which uses find_package(loom).
qt_add_executable(loomgallery main.cpp)
set(loomgallery_qml_files
    qml/Main.qml
    qml/SectionTitle.qml
    qml/TokensPage.qml
    qml/UtilitiesPage.qml
    qml/StatesPage.qml
    qml/ResponsivePage.qml
    qml/ThemingPage.qml
    qml/FeaturesPage.qml
)
foreach(qml_file IN LISTS loomgallery_qml_files)
    cmake_path(GET qml_file FILENAME qml_file_name)
    set_source_files_properties("${qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "${qml_file_name}"
    )
endforeach()
qt_add_qml_module(loomgallery
    URI LoomGallery
    VERSION 1.0
    QML_FILES ${loomgallery_qml_files}
)
qt_add_resources(loomgallery gallery_design
    PREFIX "/gallery/design"
    FILES design/tokens.json
)
target_link_libraries(loomgallery PRIVATE loom loomplugin Qt6::Quick)
loom_set_target_defaults(loomgallery)
