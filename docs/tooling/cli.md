# The `loom` command

One binary, fourteen subcommands. `loom <command> --help` prints the same
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
| [`loom lsp`](#loom-lsp) | Preserve `qmlls` and add `Lo.style` IntelliSense |
| [`loom fmt`](#loom-fmt) | `qmlformat` over the project's QML |
| [`loom clean`](#loom-clean) | Remove the `.loom/` build and deploy trees |
| [`loom deploy`](#loom-deploy) | Install to a prefix, optionally packaged |
| [`loom migrate`](#loom-migrate) | Preview or apply the schema-v1 to v2 migration |

## Common options

Accepted by `build`, `test`, `lint`, `fmt`, `clean`, `dev` and `deploy`:

| Option | Default | Meaning |
| --- | --- | --- |
| `--target <platform>` | `desktop` | `desktop`, `android`, `ios`, or `embedded`; see [platforms.md](platforms.md) |
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

`loom doctor --json` and `loom style --check --json` emit structured output.
`loom lsp` reserves stdout for its long-running protocol transport.

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
scriptable. The style checker has its own JSON diagnostics; `loom lsp` is a
persistent protocol server rather than a report.

`--target android|ios|embedded` adds the platform-specific prerequisites used
by the corresponding build and deployment adapter.

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

In development builds, the visual inspector is installed automatically. Press
Ctrl+Shift+I to toggle it, hover to inspect a styled Item, and click to lock the
selection. It shows the type, object name, source utility string, active theme
and states, and resolved property writes. Set `LOOM_INSPECTOR=0` to disable it.
The overlay is not compiled into release behavior.

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
`--json` produces diagnostics with file, line, column, severity, code, class,
and message. `--design <path>` loads tokens, recipes, breakpoints, themes, and
the arbitrary-value policy even when checking explicit paths outside a project:

```console
loom style --check --json --design design/tokens.json qml/
loom style --catalogue --design design/tokens.json
```

The checker uses Qt's QML AST rather than regular expressions. It reads literal
fragments in nested expressions, concatenations, and ternary results while
ignoring condition strings. Fully data-generated class names remain a runtime
concern.

## `loom lsp`

```
loom lsp [--qmlls path] [-- qmlls arguments...]
```

Runs a Language Server Protocol proxy. It forwards ordinary QML intelligence
to Qt's `qmlls` and adds these features inside literal portions of `Lo.style`:

- utility and chained-variant completion, including project-defined tokens;
- live unknown-class diagnostics and confidence-gated replacement fixes;
- hover descriptions with resolved token values and variant conditions;
- background, text, and border color previews, including `/opacity` modifiers.

Configure an editor that accepts a command plus arguments to run `loom lsp`.
Anything after `--` goes to the real `qmlls`, so its build and import
configuration stays available:

```sh
loom lsp -- --build-dir .loom/build/desktop-debug --no-cmake-calls
loom lsp --qmlls /opt/Qt/6.11/gcc_64/bin/qmlls -- -I build/qml
```

Without `--qmlls`, Loom checks `LOOM_QMLLS_PATH`, the bin directory of the Qt
installation it runs against, `qtpaths6`/`qtpaths`, and finally `PATH`. A missing
server exits 127 with the reason on stderr; stdout is reserved for LSP frames.

The proxy discovers the nearest `loom.json` for every open QML file. It watches
that manifest and its `design` file, recomputes diagnostics when either changes,
and retains the last valid token vocabulary while a design file is temporarily
malformed. Files outside a Loom project use the built-in vocabulary.

For a generic LSP client, the command vector is equivalent to:

```
["loom", "lsp", "--", "--build-dir", ".loom/build/desktop-debug"]
```

### CLion

CLion discovers the QML language server as an executable named `qmlls`, so a
Loom installation also provides a thin compatibility executable at:

```text
<install-prefix>/<libexec>/loom/qmlls
```

On a normal Linux `/usr/local` installation that is
`/usr/local/libexec/loom/qmlls`. The install command prints the exact path as
`CLion qmlls proxy: ...`. Point CLion's QML language-server/tool path at that
executable (or its containing directory, depending on the CLion version), then
enable both **Enable QML language server** and **Use completion from QML
language server** under **Settings | Languages & Frameworks | QML**.

The compatibility executable accepts every argument CLion normally gives
`qmlls` and forwards it unchanged. It then locates Qt's real server using the
same search order as `loom lsp`. If CLion is using a different Qt installation
than Loom finds automatically, add `LOOM_QMLLS_PATH=/absolute/path/to/qt/bin/qmlls`
to that CLion toolchain or CMake profile environment.

The shim is deliberately outside the normal `bin` directory: installing Loom
does not overwrite or shadow Qt's own `qmlls`. Selecting the shim in the IDE is
therefore an explicit, reversible setting.

Dynamic expressions remain dynamic: Loom completes and checks quoted result
segments in concatenations and ternaries, but cannot infer classes produced by
arbitrary JavaScript at run time.

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

On desktop, runs `cmake --install` into a prefix and optionally invokes CPack.
On Android, installs the APK with ADB. On iOS, installs the app bundle with
`simctl` or `devicectl`. On embedded Linux, creates the remote directory and
transfers the executable with rsync over SSH.

| Option | Default | Meaning |
| --- | --- | --- |
| `--output <path>` | `.loom/dist/<target>-<config>` | Install prefix |
| `--package` | off | Produce DEB, DMG, or MSI with CPack; desktop only |

See [platforms.md](platforms.md) for toolchain options, transport, native
package prerequisites, signing, and target-side Qt responsibilities.

## `loom migrate`

```
loom migrate --to 2 [--apply]
```

Converts a schema-v1 `loom.json` and its referenced design file to the clean
schema-v2 shape. Preview is the default and writes nothing. `--apply` writes
both documents with `QSaveFile`; if the manifest commit fails after the design
commit, Loom restores the original design document.

The migration changes platform arrays to option objects, nests design scales
under `tokens`, nests theme overrides under `tokens.colors`, and replaces
`defaultTheme` with the `theme` object.

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

The checker walks Qt's QML AST. `Lo.style: someProperty` is skipped rather than
reported — it cannot evaluate runtime data — but quoted fragments are followed
at any nesting depth through concatenations, logical/coalescing expressions,
and ternary results. Ternary conditions are deliberately ignored.

A trailing dangling prefix is treated as a concatenation, not a typo, because
that is how a class gets built from a binding:

```qml
Lo.style: "bg-accent size-14 rounded-" + model.key   // `rounded-` is fine here
Lo.style: "bg- text-sm"                              // `bg-` is reported
```

A quoted fragment ending in a utility-family prefix is exempt because it may be
the static half of a concatenation. A complete unknown class is still reported.

```qml
// The AST checker ignores the condition and checks both result strings.
Lo.style: "bg-surface " + (Loom.theme === "brand" ? "rounded-none" : "rounded-lg")
```

## Completion data

```console
$ loom style --catalogue > loom.utilities.json
```

This needs no project for the built-in vocabulary. Pass
`--design design/tokens.json` to include project token and recipe names.

```json
{
  "version": "0.4.0",
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
- `families` — the same data grouped by prefix, when you want to complete the
  prefix first and the value second.
- `numericPrefixes` — prefixes that additionally accept a bare number
  (`border-2`, `border-0.5`), which cannot be enumerated.
- `variants` — combine with `:` before any class.

The catalogue is generated from the parser's own tables and the live token
registry, never a second hand-maintained list, and a test asserts every class it
emits parses. It reflects the **current** registry and includes token names from
every configured theme, which keeps `theme-name:` completion stable across
theme switches.

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

`loom lsp` is the context-aware consumer of this catalogue. It delegates normal
QML requests and diagnostics to `qmlls`, then merges Loom's results for
`Lo.style`; see [`loom lsp`](#loom-lsp) for setup. The JSON form remains useful
for third-party integrations and generated snippets.
