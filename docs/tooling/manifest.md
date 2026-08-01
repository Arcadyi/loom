# `loom.json`

The project manifest. `loom new` writes it, `loom init` creates one for an existing
project, and every project command reads it to decide what to build and what to watch.

loom searches for it in the current directory and upwards, stopping at a directory
containing `.git`, `.hg`, `.jj` or `.svn`, and never going past `$HOME`. Without those
bounds a stray `loom.json` in your home directory would become "your project" from
anywhere on the filesystem.

The resolved path is printed on every project command:

```
loom: using /home/you/src/MyApp/loom.json (application MyApp)
```

A JSON Schema ships alongside it at `share/loom/schemas/project-v2.schema.json`, and CI
validates a freshly generated manifest against it.

---

## Example

```json
{
    "$schema": "https://raw.githubusercontent.com/Arcadyi/loom/master/schemas/project-v2.schema.json",
    "schemaVersion": 2,
    "project": {
        "name": "MyApp",
        "defaultApplication": "MyApp"
    },
    "qt": {
        "version": "6.11"
    },
    "design": "design/tokens.json",
    "applications": {
        "MyApp": {
            "name": "MyApp",
            "target": "MyApp",
            "id": "com.example.myapp",
            "uri": "com.example.MyApp",
            "entry": "Main",
            "qmlRoots": ["qml"],
            "assetRoots": ["assets"],
          "platforms": {
                "desktop": {},
                "android": { "abi": "arm64-v8a", "api": 36 },
                "ios": { "destination": "simulator" },
                "embedded": { "profile": "board" }
            }
        }
    },
    "embeddedProfiles": {
        "board": {
            "toolchainFile": "toolchains/board.cmake",
            "sysroot": "/opt/board/sysroot",
            "host": "board.local",
            "remoteDir": "/opt/myapp"
        }
    }
}
```

---

## Top level

| Field | Required | Meaning |
| --- | --- | --- |
| `schemaVersion` | yes | Must be `2`. Use `loom migrate --to 2` for a v1 project. |
| `project` | yes | See below. |
| `qt` | yes | Minimum Qt version. Loom requires `6.11` or newer. |
| `applications` | yes | Map of target name to application. At least one. |
| `design` | no | Path to a design token file, relative to this manifest. See below. |
| `embeddedProfiles` | no | Named cross-toolchain, sysroot and SSH deployment profiles. |
| `$schema` | no | Informational. |

### `design`

Names the project's design token file — colours, spacing, breakpoints and
themes. A separate file rather than a section of this one, so a design system
can be shared between projects and loaded on its own with `loom::loadConfig()`.
Its own schema is `share/loom/schemas/design-v2.schema.json`, and its contents
are documented in [configuration.md](../styling/configuration.md).

Three things read it, and they must agree:

- `loom_add_application(... DESIGN <path>)` compiles it into the application's
  resources for release builds.
- `loom dev` watches it, and pushes changes to the running application. Tokens
  are applied in place: the window repaints without the scene being recreated,
  so nothing on screen loses its state.
- `loom style` and `loom lint` load it before checking, so classes built from
  project-defined tokens (`bg-brand-500`) are not reported as unknown.

Omit the key entirely if the project has no custom tokens; the built-in set is
always available.

### `project`

| Field | Required | Meaning |
| --- | --- | --- |
| `name` | yes | Display name. Non-empty. |
| `defaultApplication` | no | Which application commands act on when `--app` is not given. Must name one of `applications`. |

### `applications.<target>`

Every field is required.

| Field | Meaning |
| --- | --- |
| `name` | Display name for this application. |
| `target` | CMake target name. Must match the map key. |
| `id` | Reverse-DNS identifier, e.g. `com.example.myapp`. Used as the macOS bundle identifier. |
| `uri` | QML module URI, dotted, e.g. `com.example.MyApp`. |
| `entry` | Root QML type, without `.qml`. |
| `qmlRoots` | Directories, relative to the manifest, holding QML. Watched and bundled during development. At least one. |
| `assetRoots` | Directories bundled under `assets/`. May be empty. |
| `platforms` | Object of enabled targets and adapter settings. Keys are `desktop`, `android`, `ios`, `embedded`. |

`platforms` must enable at least one target and is enforced: `loom build --target android` on an application that does not contain
`android` is refused, naming what it does list.

Android accepts `qtPath`, `hostQtPath`, `abi`/`abis`, `api`, and an optional ADB
`device`. iOS accepts `qtPath`, `hostQtPath`, `sdk`, `destination`, `device`,
signing `team`, and the host address used by a physical device. Embedded selects a named
`embeddedProfiles` entry. Relative kit/toolchain paths resolve from `loom.json`.

---

## Several applications

`applications` is a map, so a project can define more than one:

```json
"applications": {
    "Viewer": { "...": "..." },
    "Admin":  { "...": "..." }
}
```

With more than one and no `defaultApplication`, commands refuse to guess:

```
loom: this project defines 2 applications (Admin, Viewer); choose one with
--app <target>, or set project.defaultApplication in loom.json
```

Either pass `--app Viewer` or set `project.defaultApplication`. The selected application is
always named in the `loom: using …` line.

This matters because JSON object keys are sorted on load: before `--app` existed, adding an
`Admin` application to a `Viewer` project silently changed what `loom dev` ran, because
`Admin` sorts first.

---

## Editing by hand

Supported, and validated on load. Manifests are checked beyond the schema's structure:

- `id` must be present. The schema always required it; older loom versions did not check,
  so a hand-written manifest missing it loaded and produced an empty bundle identifier.
- `platforms` entries must be known values.
- an application's `target` must match its key in the `applications` map.
- `defaultApplication`, if present, must name an application that exists.
- `uri` must be a valid dotted identifier.
- `target`, `entry` and `qmlRoots` must be non-empty.

Failures name the application and the field.
