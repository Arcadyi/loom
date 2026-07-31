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

A JSON Schema ships alongside it at `share/loom/schemas/project-v1.schema.json`, and CI
validates a freshly generated manifest against it.

---

## Example

```json
{
    "$schema": "https://raw.githubusercontent.com/Arcadyi/loom/main/schemas/project-v1.schema.json",
    "schemaVersion": 1,
    "project": {
        "name": "MyApp",
        "defaultApplication": "MyApp"
    },
    "qt": {
        "version": "6.11"
    },
    "applications": {
        "MyApp": {
            "name": "MyApp",
            "target": "MyApp",
            "id": "com.example.myapp",
            "uri": "com.example.MyApp",
            "entry": "Main",
            "qmlRoots": ["qml"],
            "assetRoots": ["assets"],
            "platforms": ["desktop", "android", "ios", "embedded"]
        }
    }
}
```

---

## Top level

| Field | Required | Meaning |
| --- | --- | --- |
| `schemaVersion` | yes | Must be `1`. Anything else is refused. |
| `project` | yes | See below. |
| `qt` | yes | `{ "version": "6.11" }`. Only `6.11` is accepted. |
| `applications` | yes | Map of target name to application. At least one. |
| `$schema` | no | Informational. |

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
| `platforms` | Which targets this application supports. Any of `desktop`, `android`, `ios`, `embedded`. |

`platforms` is enforced: `loom build --target android` on an application that does not list
`android` is refused, naming what it does list.

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
- `defaultApplication`, if present, must name an application that exists.
- `uri` must be a valid dotted identifier.
- `target`, `entry` and `qmlRoots` must be non-empty.

Failures name the application and the field.
