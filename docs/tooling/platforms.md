# Platforms

## Status at a glance

| Target | `loom doctor` | `loom build` / `dev` / `deploy` | Validated by CI |
| --- | --- | --- | --- |
| `desktop` — Linux | yes | yes | **yes** |
| `desktop` — macOS | yes | yes | no |
| `desktop` — Windows | yes | yes | no |
| `android` | yes | **no** | no |
| `ios` | yes | **no** | no |
| `embedded` | yes | **no** | no |

`--target android|ios|embedded` is accepted by the argument parser and then
refused:

```console
$ loom build --target android
loom: the android adapter is not implemented in this build
```

Only `loom doctor` does real work for those targets, reporting on the toolchain
it would need. Everything below the desktop section is a **roadmap**, not a
description of what ships.

## Desktop

**Linux is the validated platform.** It is what CI builds and tests, and what
the end-to-end scaffold-build-test suite runs against.

macOS and Windows are *untested*. The desktop adapter is written to be portable
— the same configure/build workflow and the same outbound loopback TCP
connection — and the known platform-specific paths are handled (application
bundles on macOS, `.exe` suffixes on Windows), but nothing verifies them.

Two known gaps if you try:

- the install step's system-library exclusions are Linux FHS paths, so a
  `cmake --install` on another platform may try to vendor system libraries;
- `loom dev` has no signal handling on non-Unix platforms, so a non-interactive
  kill can orphan the child application holding the reload port.

Treat both as unsupported until CI covers them.

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

---

# Roadmap

Nothing in this section is implemented. It records the intended design so the
shape is not re-invented later.

## Android

`loom doctor --target android` **works today**: it checks CMake, Ninja,
Qt 6.11, Java, `sdkmanager` and ADB, and reports what is missing.

Planned: the setup provider will install API 36, build-tools 36.0.0,
platform-tools, NDK 27.2.12479018, and the selected Qt Android ABI, after the
user confirms each provider and accepts its licenses. Development transport will
use `adb reverse`, so the application can keep the same outbound loopback
protocol it uses on desktop.

## iOS

`loom doctor --target ios` **works today** on macOS.

Planned: configuration will require macOS with Xcode and a Qt 6.11 iOS kit. The
tool can validate and invoke those, but cannot create an Apple developer account
or silently manage signing and provisioning. Simulator transport will use the
host; physical devices will require an explicitly selected LAN address and a
generated local-network usage declaration.

## Embedded Linux

`loom doctor --target embedded` **works today**.

Planned: projects will provide a named profile containing the target Qt
`qt-cmake`, sysroot, SSH host and remote application directory. The adapter will
build with the target toolchain, transfer with rsync, create an SSH tunnel for
reload traffic, and launch the remote process. Board-vendor SDK installation
stays provider-specific.

## Excluded from v1

Qt for WebAssembly and Qt for MCUs require different runtime, packaging and
transport assumptions, and are not represented as aliases for the native
adapters.
