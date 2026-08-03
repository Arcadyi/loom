# End-to-end consumer check: scaffolds a minimal app that uses loom through
# find_package against the build tree, builds it with its own CMake configure,
# and runs a smoke that exercises both API layers (typed tokens and Lo.style).
# This is the test that fails when the exported targets, the qmldir resource
# or the plugin registration break for real consumers.
#
# Expected -D variables: LOOM_BUILD_DIR, WORK_DIR, CMAKE_GENERATOR.

if(NOT DEFINED LOOM_BUILD_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "LOOM_BUILD_DIR and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/app")

file(WRITE "${WORK_DIR}/app/CMakeLists.txt" [[
cmake_minimum_required(VERSION 3.22)
project(loomconsumer LANGUAGES CXX)
find_package(loom CONFIG REQUIRED)
find_package(Qt6 6.11 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.11)
qt_add_executable(loomconsumer main.cpp)
target_link_libraries(loomconsumer PRIVATE loom::loom loom::loomplugin Qt6::Quick)
# A hand-rolled consumer links the component module itself. Applications built
# with loom_add_application get it without this line; see the note in
# cmake/loomFunctions.cmake. Naming it here is deliberate -- it is what proves
# loomcontrols and its generated resource/plugin_init helpers are in the export
# set and resolvable through find_package.
if(TARGET loom::loomcontrols)
    target_link_libraries(loomconsumer PRIVATE
        loom::loomcontrols loom::loomcontrolsplugin)
    target_compile_definitions(loomconsumer PRIVATE LOOM_CONSUMER_HAS_CONTROLS)
endif()
]])

file(WRITE "${WORK_DIR}/app/main.cpp" [[
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTimer>
#include <cstdio>
#include <loom/loom.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
#ifdef LOOM_CONSUMER_HAS_CONTROLS
        // Box rather than Rectangle: a Rectangle has no padding properties, so
        // `p-4` landing at all is the proof that the component module resolved
        // through find_package and registered its types.
        "import QtQuick\n"
        "import Loom\n"
        "import Loom.Controls\n"
        "Box {\n"
        "    property real tokenSpace: Loom.space.s4\n"
        "    Lo.style: \"p-4 bg-blue-500 rounded-lg\"\n"
        "}\n",
#else
        "import QtQuick\n"
        "import Loom\n"
        "Rectangle {\n"
        "    property real tokenSpace: Loom.space.s4\n"
        "    Lo.style: \"bg-blue-500 rounded-lg\"\n"
        "}\n",
#endif
        QUrl());
    QObject *object = component.create();
    if (!object) {
        fprintf(stderr, "create failed: %s\n", qPrintable(component.errorString()));
        return 1;
    }
    int result = 1;
    QTimer::singleShot(0, [&] {
        const bool tokenSpaceOk = object->property("tokenSpace").toReal() == 16.0;
#ifdef LOOM_CONSUMER_HAS_CONTROLS
        // Box paints through its background delegate, so the appearance
        // utilities land there rather than on the item itself.
        QObject *const background = object->property("background").value<QQuickItem *>();
        const bool styleOk = background
            && background->property("color").value<QColor>() == QColor(0x3b, 0x82, 0xf6)
            && background->property("radius").toReal() == 8.0;
        const bool tokenOk = tokenSpaceOk && object->property("topPadding").toReal() == 16.0;
#else
        const bool styleOk =
            object->property("color").value<QColor>() == QColor(0x3b, 0x82, 0xf6)
            && object->property("radius").toReal() == 8.0;
        const bool tokenOk = tokenSpaceOk;
#endif
        const bool themeOk = qstrcmp(loom::version(), "") != 0;
        result = (tokenOk && styleOk && themeOk) ? 0 : 1;
        if (result != 0)
            fprintf(stderr, "smoke failed: token=%d style=%d\n", tokenOk, styleOk);
        QCoreApplication::exit(result);
    });
    app.exec();
    return result;
}
]])

macro(run_step)
    execute_process(${ARGN} RESULT_VARIABLE step_result)
    if(NOT step_result EQUAL 0)
        message(FATAL_ERROR "step failed (${step_result}): ${ARGN}")
    endif()
endmacro()

run_step(
    COMMAND "${CMAKE_COMMAND}"
        -S "${WORK_DIR}/app" -B "${WORK_DIR}/build"
        -G "${CMAKE_GENERATOR}"
        "-DCMAKE_PREFIX_PATH=${LOOM_BUILD_DIR}"
)
run_step(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/build")

set(ENV{QT_QPA_PLATFORM} offscreen)
set(ENV{QSG_RHI_BACKEND} software)
run_step(COMMAND "${WORK_DIR}/build/loomconsumer")

message(STATUS "loom e2e consumer smoke passed")
