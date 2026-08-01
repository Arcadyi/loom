# Platform roadmap

## Desktop

**Linux is the validated platform.** It is what CI builds and tests, and what
the end-to-end scaffold-build-test suite runs against.

macOS and Windows are *untested*. The desktop adapter is written to be portable
-- the same configure/build workflow and the same outbound loopback TCP
connection -- and the known platform-specific paths are handled (application
bundles on macOS, `.exe` suffixes on Windows), but nothing verifies them. Treat
them as unsupported until CI covers them.

### Deployment

`loom deploy` performs an ordinary `cmake --install` into a prefix and can
produce a CPack archive. It does **not** bundle Qt, and there is currently no
option to. Producing a self-contained artifact is left to a dedicated packaging
tool such as `linuxdeploy` or `appimage-builder`, run over the installed prefix.

#### Why Qt is not bundled

Both routes Qt offers were measured on Linux with Qt 6.11, against a
distribution Qt rooted at `/usr`:

- `qt_generate_deploy_qml_app_script` produces a 247 MB tree that includes
  `ld-linux-x86-64.so.2` itself, and the resulting binary core-dumps.
- `qt_deploy_runtime_dependencies`, with the system directories excluded and the
  Qt libraries named back in — the approach that successfully ships loom's own
  CLI — fails inside `file(GET_RUNTIME_DEPENDENCIES)` with "file unknown error",
  with and without `ADDITIONAL_MODULES` for the QML plugins.

The difference appears to be console application versus GUI application with
plugins, rather than anything about the distribution Qt: loom's own `bin/loom`
does bundle Qt this second way and resolves it from its own `lib/`. Until that
is understood, `loom_install_application` produces a correct, ordinary install
that runs against the Qt the host already has.

## Android

The doctor checks CMake, Ninja, Qt 6.11, Java, `sdkmanager`, and ADB. The setup
provider will install API 36, build-tools 36.0.0, platform-tools, NDK
27.2.12479018, and the selected Qt Android ABI after the user confirms each
provider and accepts its licenses. Development transport will use `adb reverse`
so the application can retain the same outbound loopback protocol.

## iOS

iOS configuration must run on macOS with Xcode and a Qt 6.11 iOS kit. The tool
can validate and invoke these tools, but cannot create an Apple developer account
or silently manage signing and provisioning. Simulator transport uses the host;
physical devices require an explicitly selected LAN address and the generated
local-network usage declaration.

## Embedded Linux

Projects provide a named profile containing the target Qt `qt-cmake`, sysroot,
SSH host, and remote application directory. The adapter will build with the
target toolchain, transfer with rsync, create an SSH tunnel for reload traffic,
and launch the remote process. Board-vendor SDK installation stays provider
specific.

## Excluded from v1

Qt for WebAssembly and Qt for MCUs require different runtime, packaging, and
transport assumptions and are not represented as aliases for the native
adapters.
