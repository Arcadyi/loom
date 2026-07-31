# CMake integration

## Consuming

```cmake
find_package(loom CONFIG REQUIRED)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

- `loom::loom` — the static backing library (C++ API in `<loom/loom.h>`,
  QML types).
- `loom::loomplugin` — the static QML plugin; linking it pulls in the
  generated registration object, so `import Loom` resolves from the
  `:/qt/qml/Loom` resource with **zero runtime configuration**.

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
| `LOOM_BUILD_EXAMPLES` | ON | the gallery app (`build/examples/gallery/loomgallery`) |
| `LOOM_WERROR` | OFF | warnings as errors |
| `LOOM_SANITIZE` | OFF | ASan + UBSan |
