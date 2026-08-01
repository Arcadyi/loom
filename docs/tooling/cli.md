# The `loom` command

One binary, twelve subcommands. `loom <command> --help` prints the same
information as the reference below.

| Command | Does |
| --- | --- |
| [`loom new`](#loom-new) | Scaffold a styled Qt/QML application |
| [`loom init`](#loom-init) | Add a `loom.json` to a project that already has CMake |
| [`loom doctor`](#loom-doctor) | Check the toolchain and both halves of the package |
| [`loom setup`](#loom-setup) | Print the remaining setup steps |
| [`loom dev`](#loom-dev) | Build, run, watch — the development loop |
| [`loom build`](#loom-build) | Configure and build |
| [`loom test`](#loom-test) | Build and run the project's tests |
| [`loom lint`](#loom-lint) | `qmllint` **and** the `Lo.style` class check |
| [`loom style`](#loom-style) | The class check alone, or the vocabulary as JSON |
| [`loom fmt`](#loom-fmt) | `qmlformat` over the project's QML |
| [`loom clean`](#loom-clean) | Remove the `.loom/` build and deploy trees |
| [`loom deploy`](#loom-deploy) | Install to a prefix, optionally packaged |

## Common options

Accepted by `build`, `test`, `lint`, `fmt`, `clean`, `dev` and `deploy`:

| Option | Default | Meaning |
| --- | --- | --- |
| `--target <platform>` | `desktop` | Platform to build for. **Only `desktop` builds today** — the others are accepted and then rejected; see [platforms.md](platforms.md) |
| `--config <configuration>` | `Debug` | CMake build type: `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |
| `--prefix <path>` | — | Extra CMake prefix path. loom finds its own package without this |
| `--app <target>` | the manifest's default | Which application to act on in a multi-application project |
| `--generator <name>` | `Ninja` | CMake generator, honoured only for a build tree with no cache yet |
| `--quiet` | off | Print only errors and results |
| `--verbose` | off | **Currently accepted and ignored.** The flag is parsed and nothing reads it |

## Exit status

| Code | Meaning |
| --- | --- |
| 0 | success |
| 1 | failure — a build error, a lint finding, an unreadable manifest |
| 2 | usage error — unknown command, bad flag, missing argument |
| 127 | a subprocess could not be started |
| 128 | a subprocess crashed |

`--json` on `loom doctor` is the only machine-readable output today; the other
commands emit human-readable text.

---

## `loom new`

```
loom new <name> [--org dev.example] [--directory path] [--ci github|none]
```

Scaffolds a complete, buildable, loom-styled application: `CMakeLists.txt`,
`CMakePresets.json`, `loom.json`, a design token file, `src/main.cpp`,
`qml/Main.qml`, a smoke test, `.gitignore` and `.clang-format`.

| Option | Default | Meaning |
| --- | --- | --- |
| `--org <id>` | `dev.example` | Reverse-DNS organization, used to build the application id and module URI |
| `--directory <path>` | `./<name>` | Where to create the project |
| `--ci <provider>` | `none` | Generate a CI workflow: `github` or `none` |

```console
$ loom new hello --org com.example && cd hello && loom dev
```

`--ci github` currently emits a workflow with a deliberate `TODO` in its
*Install loom* step, because loom has no published release to install from. The
generated workflow fails until you fill that step in, and the command says so.

## `loom init`

```
loom init [options] [--apply]
```

Adopts an existing CMake project: works out the module URI, target and QML
roots, writes a `loom.json`, and prints the CMake block to add.

**Previews by default.** Nothing is written without `--apply`, which appends a
`# loom: begin` / `# loom: end` block to your `CMakeLists.txt`.

| Option | Default | Meaning |
| --- | --- | --- |
| `--name <name>` | the directory name | Project name |
| `--target <name>` | detected | CMake target holding the QML module |
| `--uri <dotted.uri>` | detected | QML module URI |
| `--entry <type>` | `Main` | Root QML type name |
| `--qml-root <path>` | detected | Directory holding QML sources |
| `--asset-root <path>` | — | Directory holding bundled assets |
| `--app-id <id>` | derived | Reverse-DNS application identifier |
| `--app <target>` | — | Which application to emit CMake for, when `loom.json` already defines several |
| `--apply` | off | Write the integration block instead of only previewing |

It does **not** write a `design` key. Add one by hand to get design token hot
reload — see [manifest.md](manifest.md#design).

## `loom doctor`

```
loom doctor [--target platform] [--json]
```

Checks the toolchain and both halves of the loom package: the compiler, CMake,
Ninja, Qt and its modules, the loom CMake package, and the `Loom` QML module's
`qmldir` and `qmltypes`.

`--json` emits the report as a structured document, which is also what makes it
scriptable. This is the only command with machine-readable output.

`--target android|ios|embedded` inspects those toolchains — the only place
non-desktop targets do anything today.

Run it first when anything is unexpected; most entries in
[troubleshooting.md](troubleshooting.md) start here.

## `loom setup`

```
loom setup [--target platform]
```

Prints the remaining setup steps for a target, in the order to do them. It
**installs nothing** — it tells you what to install. Returns 1 while actions are
outstanding, so it can gate a script.

## `loom dev`

```
loom dev [options] [-- <app arguments>]
```

The development loop. Builds the application, launches it, and watches:

- **QML and assets** rebuild the bundle and reload the scene. C++ services stay
  alive, and declared state is captured and restored.
- **The design token file** repaints the running window *without recreating the
  scene at all* — nothing on screen loses state.
- **`src/`, `cmake/`, `CMakeLists.txt` and `loom.json`** trigger a full rebuild
  and restart.

```console
$ loom dev
[loom] reload server listening on 127.0.0.1:41337
[loom] design tokens changed: design/tokens.json
```

Everything after `--` is passed to the application rather than to loom.

Use a Debug configuration. The generated `main.cpp` enables the development
runtime only where `NDEBUG` is undefined, so `--config Release` builds and runs
but never hot-reloads; loom warns when you ask for one.

See [../reference/architecture.md](../reference/architecture.md) for the
mechanics and [troubleshooting.md](troubleshooting.md) when a reload does
nothing.

## `loom build`

```
loom build [options]
```

Configures and builds. The configure step runs every time, which is a fixed cost
even on a no-op build.

Output goes to `.loom/build/<target>-<config>/`, with binaries in its `bin/`
subdirectory — a contract `loom dev` relies on to find the executable.

## `loom test`

```
loom test [options]
```

Builds with `BUILD_TESTING=ON` and runs `ctest --output-on-failure
--no-tests=error`, so a project with no tests registered fails rather than
passing silently.

There is no way to pass arguments through to `ctest` — no test filter, no
parallelism flag. Run `ctest` directly in `.loom/build/<target>-<config>/` when
you need those.

## `loom lint`

```
loom lint [options]
```

Runs `qmllint` **and** the `Lo.style` class check. Both halves always run, and
the worse status wins, so a utility typo is never hidden behind an unrelated
qmllint complaint:

```console
$ loom lint
qml/Main.qml:44: unknown utility class 'bg-brand-999'
loom: 1 unknown class(es) in 1 file(s)
```

## `loom style`

```
loom style [--check | --catalogue] [--app target] [path...]
```

The class check on its own, or the vocabulary as JSON. `--check` is the default.
The rest of this page covers it in detail.

## `loom fmt`

```
loom fmt [--check] [options]
```

Runs `qmlformat` over the project's QML, rewriting in place. `--check` reports
unformatted files instead, for CI.

It formats QML only — it does not run `clang-format` over C++, even though
`loom new` scaffolds a `.clang-format`.

## `loom clean`

```
loom clean [--all] [options]
```

Removes the `.loom/` build and deploy trees for the selected configuration, or
every configuration with `--all`.

`--all` is the fix for a generator mismatch, and for the version-skew case in
[../reference/upgrading.md](../reference/upgrading.md#version-skew).

## `loom deploy`

```
loom deploy [--output path] [--package] [options]
```

Runs `cmake --install` into a prefix, and optionally produces an archive with
CPack.

| Option | Default | Meaning |
| --- | --- | --- |
| `--output <path>` | `.loom/dist/<target>-<config>` | Install prefix |
| `--package` | off | Also produce an archive with CPack |

**Qt is not bundled**, and there is no option to. The target host needs Qt. See
[why](platforms.md#why-qt-is-not-bundled), and use `linuxdeploy` or
`appimage-builder` over the installed prefix for a self-contained artifact.

---

## Checking utility classes

`Lo.style` is a string, so no editor completes inside it and no compiler rejects
a typo. Loom's own diagnostics are necessarily runtime ones: an unknown class
warns on the `loom.style` category when the styled item is created — in a log
nobody is watching, and only for code paths that actually run.

`loom style` closes both gaps offline. It is part of the `loom` binary and links
the styling library directly, so it always speaks exactly the vocabulary the
running application does.

This was a separate `loomstyle` executable before 0.2.0.

### Checking for typos

```console
$ loom style --check qml/
qml/TopBar.qml:28: unknown utility class 'text-forground'
qml/Card.qml:14: unknown utility class 'hoverr:bg-accent'
loom: 2 unknown class(es) in 7 file(s)
```

Inside a project the paths are optional — the manifest's `qmlRoots` are checked,
and the project's design tokens are loaded first so classes built from
project-defined tokens resolve:

```console
$ loom style
```

Paths may be files or directories; directories are searched recursively for
`*.qml`. Exit status is 0 when everything resolves, 1 for unknown classes, 2 for
usage errors — so it wires into a build or a CI step:

```cmake
add_test(NAME style_classes
    COMMAND loom style --check "${CMAKE_CURRENT_SOURCE_DIR}/qml")
```

### What it can and cannot see

Only **string literals** are checked. `Lo.style: someProperty` is skipped rather
than reported — the checker cannot evaluate a binding, and flagging it would
punish a legitimate pattern.

Bindings that wrap across lines are followed, whether the operator trails one
line or leads the next, up to eight lines.

A trailing dangling prefix is treated as a concatenation, not a typo, because
that is how a class gets built from a binding:

```qml
Lo.style: "bg-accent size-14 rounded-" + model.key   // `rounded-` is fine here
Lo.style: "bg- text-sm"                              // `bg-` is reported
```

Only the *last* class in a literal is exempt; a dangling prefix anywhere else is
still a typo.

The one false positive worth knowing: **every string literal in the binding is
checked**, including one that is not a class at all.

```qml
// Reports `brand` as an unknown class.
Lo.style: "bg-surface " + (Loom.theme === "brand" ? "rounded-none" : "rounded-lg")

// Fine — the theme name is out of the binding.
readonly property bool sharp: Loom.theme === "brand"
Lo.style: "bg-surface " + (sharp ? "rounded-none" : "rounded-lg")
```

## Completion data

```console
$ loom style --catalogue > loom.utilities.json
```

This one needs no project: an editor asking for completion data should not have
to be inside one.

```json
{
  "version": "0.2.1",
  "theme": "light",
  "variants": ["dark", "disabled", "focus", "hover", "lg", "md", "pressed", "sm", "xl"],
  "classes": ["bg-accent", "bg-amber-100", "...", "w-full"],
  "families": [
    { "prefix": "bg-", "scale": "color", "values": ["accent", "amber-100", "..."] }
  ],
  "numericPrefixes": ["border-"]
}
```

- `classes` — every class that can be enumerated, without variant prefixes.
  1550 with the default token set.
- `families` — the same data grouped by prefix, when you want to complete the
  prefix first and the value second.
- `numericPrefixes` — prefixes that additionally accept a bare number
  (`border-2`, `border-0.5`), which cannot be enumerated.
- `variants` — combine with `:` before any class.

The catalogue is generated from the parser's own tables and the live token
registry, never a second hand-maintained list, and a test asserts every class it
emits parses. It therefore reflects the **current** registry — both the token
set and the active theme affect the output.

Run inside a project, `loom style` loads the design token file named by the
manifest's `design` key first, so a project's own colours and spacing count:

```console
$ loom style              # checks qmlRoots, with the project's tokens loaded
$ loom style --catalogue  # the vocabulary, plus any project tokens
```

Outside a project, only the built-in tokens are known and project-defined
classes report as unknown. See
[../styling/configuration.md](../styling/configuration.md) for the token file
and [manifest.md](manifest.md#design) for the `design` key.

## From C++

The same data is available to any program linking loom, via
`<loom/loomcatalogue.h>`:

```cpp
const loom::StyleCatalogue catalogue = loom::styleCatalogue();
const QStringList bad = loom::unknownStyleClasses(QStringLiteral("bg-nope p-4"));
```

`unknownStyleClasses()` runs the same parse as `Lo.style` but neither warns nor
caches, so a checker can report per occurrence rather than once per unique
string. Full signatures in
[../reference/cpp-api.md](../reference/cpp-api.md#loomloomcatalogueh).

## Editor completion

There is no completion inside `Lo.style` today, in any editor. `qmlls` has no
extension point for it — its suggestions come from the QML type system, and a
string's contents are not in it — so completion means a front-end that consumes
the catalogue above. Two routes are practical:

- **A `qmlls` proxy.** Editors resolve `qmlls` from a configurable directory
  (CLion: *Settings → Languages & Frameworks → QML → Qt binaries*). A wrapper of
  that name can forward every request to the real `qmlls` and answer only
  `textDocument/completion` from the catalogue when the cursor sits inside an
  `Lo.style` literal. Editor-agnostic.
- **Editor snippets.** Generate live templates or snippets from `classes`. Not
  context-aware, but no plumbing.
