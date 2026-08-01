# CMake API

loom installs a CMake package. `find_package(loom CONFIG REQUIRED)` brings in three
functions and four imported targets.

Generated projects call `loom_add_application` and `loom_install_application`.
Existing projects that already have their own `qt_add_qml_module` call
`loom_enable_hot_reload` instead.

Imported targets:

| Target | Contents |
| --- | --- |
| `loom::loom` | The styling library: the token registry and the `Lo.style` attached type. Static. |
| `loom::loomplugin` | The `Loom` QML module's static plugin. Link it alongside `loom::loom` and `import Loom` resolves with no import-path setup. |
| `loom::Runtime` | The QML engine bootstrap and the development reload controller. Static. |
| `loom::Protocol` | The framed transport and bundle validation. Static; a dependency of `loom::Runtime`. |

---

## `loom_add_application`

Creates a Qt/QML application: the executable, its QML module, the resource aliases, the
`bin/` output location, and hot reload.

```cmake
loom_add_application(MyApp
    URI com.example.MyApp
    ENTRY Main
    QML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DESIGN "${CMAKE_CURRENT_SOURCE_DIR}/design/tokens.json"
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
| `DESIGN` | no | Design token file. Compiled into the module's resources and loaded before the scene. See below. |
| `SOURCES` | no | C++ sources for the executable. |
| `SINGLETONS` | no | QML files, relative to `QML_ROOT`, to register as singletons. |
| `IMPORT_PATHS` | no | Extra QML import paths for tooling (qmllint, qmlls). |

### `DESIGN`

The file is aliased into the module's resources as `loom-design.json` and the
path is baked in as `LOOM_APP_DESIGN`, which `loom::Application::run` loads
before the engine initializer and before the scene. Tokens resolved during a
binding's first evaluation are the ones the window paints with, so loading later
would show as a repaint on the first frame.

Configuration fails if the file does not exist, rather than producing an
application that silently starts with default tokens.

Point it at the same file the manifest's `design` key names. Under `loom dev`
the on-disk file supersedes the compiled copy on every save, applied without
recreating the scene — see [architecture.md](../reference/architecture.md#design-token-reload).

### `SINGLETONS`

Each named file gets `QT_QML_SINGLETON_TYPE`, which Qt records in the module's generated
qmldir. loom bundles that qmldir for development too, so a singleton behaves the same in
both builds:

```cmake
loom_add_application(MyApp
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
set — register themselves at runtime when you link their plugin, but `loom
lint` and qmlls resolve imports statically and need to be told where the
module's `qmldir` and `.qmltypes` live:

```cmake
find_package(loom CONFIG REQUIRED)

loom_add_application(MyApp
    ...
)
target_link_libraries(MyApp PRIVATE loom::loom loom::loomplugin)
```

`IMPORT_PATHS` defaults to `LOOM_QML_IMPORT_DIR`, which `loomConfig.cmake` sets
to the directory holding `Loom/qmldir` and `Loom/loom.qmltypes`, so `import
Loom` resolves for qmllint and qmlls with nothing to wire up. Pass it
explicitly only to add paths for *other* QML packages.

The paths reach `qt_add_qml_module` itself rather than being appended to the
target afterwards: Qt reads `QT_QML_IMPORT_PATH` while it builds the qmllint
and qmlcachegen command lines, so a later `set_property` never shows up in
them.

### Output location

Application and test binaries are written to `<build>/bin/`. This is a contract:
`loom dev` resolves the executable from exactly that path.

It exists because a target whose name matches a directory the build tree creates cannot be
linked — `ctest` creates `Testing/` on every run, so an application called `Testing` failed
with `cannot open output file Testing: Is a directory`.

---

## `loom_enable_hot_reload`

Adds hot reload to a target that already has its own `qt_add_qml_module`. It does not
replace `qt_add_qml_module`; it augments the target.

```cmake
find_package(loom CONFIG REQUIRED)
loom_enable_hot_reload(
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

It links `loom::Runtime`, defines `LOOM_APP_URI` and `LOOM_APP_ENTRY` for the target,
and records the roots as target properties.

It also warns when the target name collides with a directory the build tree creates
(`Testing`, `CMakeFiles`, `bin`, `lib`, …). Unlike `loom_add_application` it cannot fix
this for you — your project owns its output paths — so set `RUNTIME_OUTPUT_DIRECTORY`
yourself or rename the target.

---

## `loom_install_application`

Installs the application into a prefix that can be packaged. `loom deploy` drives it.

```cmake
loom_install_application(TARGET MyApp)
```

| Argument | Required | Meaning |
| --- | --- | --- |
| `TARGET` | yes | An existing target. |

Application QML is compiled into the executable by `qt_add_qml_module`, so the result is a
single self-contained binary apart from Qt itself.

**Qt is not bundled**, and there is no option to bundle it. See
[platforms.md](../tooling/platforms.md#why-qt-is-not-bundled) for the measurements behind that, and
use `linuxdeploy` or `appimage-builder` over the installed prefix if you need a
self-contained artifact.

---

## Argument checking

All three functions reject unknown arguments and missing values rather than ignoring them:

```
loom_add_application received unknown arguments: SOURCE;src/main.cpp
```

That message is the result of writing `SOURCE` instead of `SOURCES`, which previously
configured happily and built an application with no sources.

Required arguments are checked with `NOT DEFINED ... OR ... STREQUAL ""` rather than a
truthiness test, so a legitimate value of `OFF`, `NO`, `0` or anything ending in `-NOTFOUND`
is not mistaken for a missing one.
