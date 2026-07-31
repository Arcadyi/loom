# CMake integration

One package provides both halves of loom. `find_package(loom)` gives you the
styling targets, the runtime, and the `loom_*` functions.

## Styling only

```cmake
find_package(loom CONFIG REQUIRED)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

- `loom::loom` — the static backing library (C++ API in `<loom/loom.h>`,
  QML types).
- `loom::loomplugin` — the static QML plugin; linking it pulls in the
  generated registration object, so `import Loom` resolves from the
  `:/qt/qml/Loom` resource with **zero runtime configuration**.

This is all a project needs if it manages its own build and does not want hot
reload.

## Styling and hot reload

```cmake
find_package(loom CONFIG REQUIRED)

loom_add_application(YourApp
    URI com.example.YourApp
    ENTRY Main
    QML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DESIGN "${CMAKE_CURRENT_SOURCE_DIR}/design/tokens.json"
    SOURCES src/main.cpp
)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

This is what `loom new` generates. See the function reference in
[cmake-api.md](cmake-api.md); the three that matter are
`loom_add_application`, `loom_enable_hot_reload` and
`loom_install_application`.

`DESIGN` compiles the token file into the application's resources and defines
`LOOM_APP_DESIGN`, which `loom::Application` loads before the scene. Under
`loom dev` the on-disk file supersedes it on every save.

`IMPORT_PATHS` defaults to `LOOM_QML_IMPORT_DIR`, so `import Loom` resolves for
qmllint and qmlls with no extra wiring.

## Exported targets

| Target | Is |
| --- | --- |
| `loom::loom` | Styling library: tokens, `Lo.style`, the `Loom` singleton |
| `loom::loomplugin` | Static QML plugin registering `import Loom` |
| `loom::Runtime` | Engine bootstrap and reload controller, linked into your app |
| `loom::Protocol` | The dev-server wire format, if you embed it yourself |

`loom::Runtime` links `loom::loom` publicly, so `loom_add_application` gives you
the styling layer too.

Both the install tree and the build tree provide the package:

```sh
cmake -S app -B app/build -DCMAKE_PREFIX_PATH=$HOME/src/loom/build   # uninstalled
cmake -S app -B app/build -DCMAKE_PREFIX_PATH=$HOME/loom-prefix     # installed
```

## Tooling

`loomConfig.cmake` sets `LOOM_QML_IMPORT_DIR` — the directory containing
`Loom/qmldir` and `Loom/loom.qmltypes`. Point qmllint or Qt Creator at it
when they do not pick the import up automatically:

```sh
qmllint -I "$LOOM_QML_IMPORT_DIR" src/qml/*.qml
```

Projects using `qt_add_qml_module` get this for free through the generated
`<target>_qmllint` targets when loom's QML output directory is on the import
path.

## Building loom itself

| Option | Default | Meaning |
| --- | --- | --- |
| `LOOM_BUILD_TESTS` | ON | unit + e2e tests (`ctest -LE e2e` / `-L e2e`) |
| `LOOM_BUILD_E2E_TESTS` | ON | the scaffold-then-compile tier specifically |
| `LOOM_BUILD_CLI` | ON | the `loom` command. OFF builds a styling-only package |
| `LOOM_BUILD_EXAMPLES` | ON | the gallery app (`build/examples/gallery/loomgallery`) |
| `LOOM_WERROR` | OFF | warnings as errors |
| `LOOM_SANITIZE` | OFF | ASan + UBSan |
