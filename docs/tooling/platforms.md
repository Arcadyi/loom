# Platform adapters

The same `loom build`, `dev`, `test`, and `deploy` commands select a platform
with `--target`. Each application opts into targets and provides target options
under `applications.<target>.platforms` in `loom.json`.

| Target | Build | Development transport | Deploy | Hosted validation |
| --- | --- | --- | --- | --- |
| Linux desktop | CMake/Ninja | loopback TCP | install + DEB | build/test/package |
| macOS desktop | CMake/Ninja | loopback TCP | app bundle + DMG | build/test/package |
| Windows desktop | CMake/Ninja | loopback TCP | executable + MSI | build/test/package |
| Android | Qt Android toolchain | authenticated TCP through `adb reverse` | `adb install -r` | emulator build/install/launch |
| iOS | Qt iOS toolchain/Xcode | simulator host or explicitly enabled device LAN | `simctl` / `devicectl` | simulator build/install/launch |
| Embedded Linux | project toolchain/sysroot | authenticated TCP through SSH reverse tunnel | rsync + SSH | project/board owned |

`loom doctor --target ...` checks host prerequisites. Platform paths can be
relative to the manifest or supplied by the documented environment variables.

## loom itself on cross targets

An application links loom's libraries, so every cross target needs a loom built
for it -- the installation the CLI came from is a host build and cannot be
linked into an Android or iOS binary. Build and install one per target, then
name its prefix:

```sh
cmake -S <loom> -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake \
    -DQT_HOST_PATH=$QT_HOST -DANDROID_ABI=arm64-v8a -DLOOM_BUILD_CLI=OFF
cmake --build build-android
cmake --install build-android --prefix /opt/loom-android

loom build --target android --prefix /opt/loom-android
```

`--prefix` goes on the front of `CMAKE_PREFIX_PATH`, and on cross targets into
`CMAKE_FIND_ROOT_PATH` as well: a sysroot build sets
`CMAKE_FIND_ROOT_PATH_MODE_PACKAGE` to `ONLY`, which otherwise hides any prefix
outside the sysroot from `find_package`. Desktop targets need none of this and
keep using the package recorded when `loom new` generated the project.

## Desktop and native packages

Desktop uses the ordinary CMake configure/build/test flow. `loom deploy`
installs into `.loom/dist/desktop-<config>` by default. `--package` invokes
CPack and deliberately supports native desktop packages only:

- Linux: DEB;
- macOS: DragNDrop DMG;
- Windows: WiX MSI.

The generated application enables the appropriate CPack generator. Packaging
requires its host tool (`dpkg`, platform disk-image tools, or WiX). Qt runtime
deployment remains governed by `loom_install_application`; inspect and sign the
result as required by the destination platform.

## Android

```json
"android": {
  "qtPath": "/opt/Qt/6.11.1/android_arm64_v8a",
  "hostQtPath": "/opt/Qt/6.11.1/gcc_64",
  "abi": "arm64-v8a",
  "api": 36,
  "device": "emulator-5554"
}
```

`qtPath` may instead come from `QT_ANDROID_PATH`, and `hostQtPath` from
`QT_HOST_PATH`. Supported ABIs are `arm64-v8a`, `armeabi-v7a`, and `x86_64`.
The adapter passes Qt's Android toolchain, ABI, and API level to CMake.

- `loom build --target android` produces the APK.
- `loom deploy --target android` installs it with ADB.
- `loom dev --target android` installs it, establishes `adb reverse`, and
  starts the activity with the authenticated reload connection arguments.

The package name is the application's `id` from `loom.json`, carried into the
build by the `QT_ANDROID_PACKAGE_NAME` property the generated `CMakeLists.txt`
sets. Deploy and dev install and launch by that same id, so a project that drops
the property ships under Qt's default `org.qtproject.example.<target>` and
neither can find what it has just installed. Renaming an installed application
leaves the old package behind on the device.

Native source changes rebuild, reinstall, and restart the application. QML,
asset, and design changes use the normal hot-reload protocol.

## iOS

```json
"ios": {
  "qtPath": "/Users/me/Qt/6.11.1/ios",
  "hostQtPath": "/Users/me/Qt/6.11.1/macos",
  "sdk": "iphonesimulator",
  "destination": "simulator",
  "device": "booted"
}
```

`QT_IOS_PATH` and `QT_HOST_PATH` are the path fallbacks. iOS builds require
macOS, Xcode, a Qt iOS kit, and its matching host Qt. Simulator builds default
to the Xcode generator and use `simctl` for installation and launch.

For a physical device, set `destination` to `device`, select a `device`, and
provide `host`, the device-reachable address of the development machine. Loom
then explicitly binds the reload server for remote access and launches through
`devicectl`. Authentication remains mandatory; remote listening is never
enabled merely because an iOS target exists. Signing, provisioning, developer
accounts, and local-network usage declarations remain application-owned Apple
configuration.

## Embedded Linux

Embedded applications select a named, project-owned profile:

```json
{
  "embeddedProfiles": {
    "panel": {
      "toolchainFile": "cmake/panel-toolchain.cmake",
      "hostQtPath": "/opt/Qt/6.11.1/gcc_64",
      "sysroot": "/opt/panel/sysroot",
      "host": "panel.local",
      "user": "root",
      "remoteDir": "/opt/apps/myapp",
      "environment": { "QT_QPA_PLATFORM": "eglfs" },
      "launchCommand": "./MyApp"
    }
  },
  "applications": {
    "MyApp": {
      "platforms": { "embedded": { "profile": "panel" } }
    }
  }
}
```

The adapter configures with the profile's CMake toolchain and sysroot, copies
the executable with rsync, and launches over SSH. Development adds an SSH
reverse tunnel so the target still makes an outbound authenticated connection
from its point of view. `port` defaults to 22; `user`, `hostQtPath`,
`environment`, and `launchCommand` are optional.

Vendor SDK acquisition, target-side Qt installation, graphics drivers, and
board access are necessarily profile-specific. The hosted workflows therefore
validate the adapter code and schema, while a real board job belongs in the
consuming project's CI.

## Tests on cross targets

`loom test --target android|ios|embedded` cross-compiles the application's test
targets. It does not pretend host CTest can execute target binaries. The Loom
repository's emulator/simulator workflows supply launch smoke coverage;
application projects can add device-native test runners appropriate to their
hardware and signing environment.

Qt for WebAssembly and Qt for MCUs are intentionally not aliases for these
native adapters; their runtime, packaging, and transport assumptions differ.
