# CMake API

respin installs a CMake package. `find_package(respin CONFIG REQUIRED)` brings in three
functions and two imported targets.

Generated projects call `respin_add_application` and `respin_install_application`.
Existing projects that already have their own `qt_add_qml_module` call
`respin_enable_hot_reload` instead.

Imported targets:

| Target | Contents |
| --- | --- |
| `respin::Runtime` | The QML engine bootstrap and the development reload controller. Static. |
| `respin::Protocol` | The framed transport and bundle validation. Static; a dependency of `respin::Runtime`. |

---

## `respin_add_application`

Creates a Qt/QML application: the executable, its QML module, the resource aliases, the
`bin/` output location, and hot reload.

```cmake
respin_add_application(MyApp
    URI com.example.MyApp
    ENTRY Main
    QML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    SOURCES src/main.cpp
    SINGLETONS Theme.qml
)
```

| Argument | Required | Meaning |
| --- | --- | --- |
| *(first positional)* | yes | Target name. Also the executable name. |
| `URI` | yes | QML module URI, dotted. Becomes the resource path `qt/qml/<uri as path>/`. |
| `ENTRY` | yes | Root QML type, without `.qml`. `Main` by convention. |
| `QML_ROOT` | yes | Directory globbed for `*.qml`, `*.js`, `*.mjs`. Paths are aliased relative to it. |
| `ASSET_ROOT` | no | Directory globbed wholesale and aliased under `assets/`. Skipped if absent. |
| `SOURCES` | no | C++ sources for the executable. |
| `SINGLETONS` | no | QML files, relative to `QML_ROOT`, to register as singletons. |
| `IMPORT_PATHS` | no | Extra QML import paths for tooling (qmllint, qmlls). |

### `SINGLETONS`

Each named file gets `QT_QML_SINGLETON_TYPE`, which Qt records in the module's generated
qmldir. respin bundles that qmldir for development too, so a singleton behaves the same in
both builds:

```cmake
respin_add_application(MyApp
    ...
    SINGLETONS Theme.qml Settings.qml
)
```

```qml
// qml/Theme.qml
pragma Singleton
import QtQuick

QtObject {
    property color accent: "purple"
}
```

```qml
// any other file in the module
Rectangle { color: Theme.accent }
```

A file named here must exist under `QML_ROOT` and be one of the module's QML files;
otherwise configuration fails with a message naming it. Without `SINGLETONS`, a file
containing `pragma Singleton` is not registered as one and referring to it by type name
fails.

### `IMPORT_PATHS`

QML modules shipped by another package — a styling library, a shared component
set — register themselves at runtime when you link their plugin, but `respin
lint` and qmlls resolve imports statically and need to be told where the
module's `qmldir` and `.qmltypes` live:

```cmake
find_package(loom CONFIG REQUIRED)

respin_add_application(MyApp
    ...
    IMPORT_PATHS "${LOOM_QML_IMPORT_DIR}"
)
target_link_libraries(MyApp PRIVATE loom::loom loom::loomplugin)
```

The paths reach `qt_add_qml_module` itself rather than being appended to the
target afterwards: Qt reads `QT_QML_IMPORT_PATH` while it builds the qmllint
and qmlcachegen command lines, so a later `set_property` never shows up in
them. `respin new --loom` generates exactly this wiring.

### Output location

Application and test binaries are written to `<build>/bin/`. This is a contract:
`respin dev` resolves the executable from exactly that path.

It exists because a target whose name matches a directory the build tree creates cannot be
linked — `ctest` creates `Testing/` on every run, so an application called `Testing` failed
with `cannot open output file Testing: Is a directory`.

---

## `respin_enable_hot_reload`

Adds hot reload to a target that already has its own `qt_add_qml_module`. It does not
replace `qt_add_qml_module`; it augments the target.

```cmake
find_package(respin CONFIG REQUIRED)
respin_enable_hot_reload(
    TARGET my_app
    URI com.example.MyApp
    ENTRY Main
    QML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
```

| Argument | Required | Meaning |
| --- | --- | --- |
| `TARGET` | yes | An existing target. |
| `URI` | yes | Must match the URI given to `qt_add_qml_module`. |
| `ENTRY` | yes | Root QML type name. |
| `QML_ROOT` | yes | Directory the development server watches. |
| `ASSET_ROOT` | no | Additional directory to watch and bundle. |

It links `respin::Runtime`, defines `RESPIN_APP_URI` and `RESPIN_APP_ENTRY` for the target,
and records the roots as target properties.

It also warns when the target name collides with a directory the build tree creates
(`Testing`, `CMakeFiles`, `bin`, `lib`, …). Unlike `respin_add_application` it cannot fix
this for you — your project owns its output paths — so set `RUNTIME_OUTPUT_DIRECTORY`
yourself or rename the target.

---

## `respin_install_application`

Installs the application into a prefix that can be packaged. `respin deploy` drives it.

```cmake
respin_install_application(TARGET MyApp)
```

| Argument | Required | Meaning |
| --- | --- | --- |
| `TARGET` | yes | An existing target. |

Application QML is compiled into the executable by `qt_add_qml_module`, so the result is a
single self-contained binary apart from Qt itself.

**Qt is not bundled**, and there is no option to bundle it. See
[deploying](getting-started.md#deploying) for the measurements behind that, and use
`linuxdeploy` or `appimage-builder` over the installed prefix if you need a self-contained
artifact.

---

## Argument checking

All three functions reject unknown arguments and missing values rather than ignoring them:

```
respin_add_application received unknown arguments: SOURCE;src/main.cpp
```

That message is the result of writing `SOURCE` instead of `SOURCES`, which previously
configured happily and built an application with no sources.

Required arguments are checked with `NOT DEFINED ... OR ... STREQUAL ""` rather than a
truthiness test, so a legitimate value of `OFF`, `NO`, `0` or anything ending in `-NOTFOUND`
is not mistaken for a missing one.
