# `respin.json`

The project manifest. `respin new` writes it, `respin init` creates one for an existing
project, and every project command reads it to decide what to build and what to watch.

respin searches for it in the current directory and upwards, stopping at a directory
containing `.git`, `.hg`, `.jj` or `.svn`, and never going past `$HOME`. Without those
bounds a stray `respin.json` in your home directory would become "your project" from
anywhere on the filesystem.

The resolved path is printed on every project command:

```
respin: using /home/you/src/MyApp/respin.json (application MyApp)
```

A JSON Schema ships alongside it at `share/respin/schemas/project-v1.schema.json`, and CI
validates a freshly generated manifest against it.

---

## Example

```json
{
    "$schema": "https://schemas.respin.dev/project/v1.json",
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

`platforms` is enforced: `respin build --target android` on an application that does not list
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
respin: this project defines 2 applications (Admin, Viewer); choose one with
--app <target>, or set project.defaultApplication in respin.json
```

Either pass `--app Viewer` or set `project.defaultApplication`. The selected application is
always named in the `respin: using …` line.

This matters because JSON object keys are sorted on load: before `--app` existed, adding an
`Admin` application to a `Viewer` project silently changed what `respin dev` ran, because
`Admin` sorts first.

---

## Editing by hand

Supported, and validated on load. Manifests are checked beyond the schema's structure:

- `id` must be present. The schema always required it; older respin versions did not check,
  so a hand-written manifest missing it loaded and produced an empty bundle identifier.
- `platforms` entries must be known values.
- `defaultApplication`, if present, must name an application that exists.
- `uri` must be a valid dotted identifier.
- `target`, `entry` and `qmlRoots` must be non-empty.

Failures name the application and the field.
