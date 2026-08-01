# Tooling — the `loom` command

| Command | Does |
| --- | --- |
| `loom new <name>` | Scaffold a styled Qt/QML application |
| `loom init` | Add a `loom.json` to a project that already has CMake |
| `loom doctor` | Check the toolchain and both halves of the package (`--json`) |
| `loom setup` | Print the confirmed setup plan |
| `loom dev` | Build, run, watch. QML reloads the scene; design tokens repaint in place |
| `loom build` | Configure and build |
| `loom test` | Build and run the project's tests |
| `loom lint` | `qmllint` **and** the `Lo.style` class check |
| `loom style` | The class check alone, or `--catalogue` for the vocabulary |
| `loom fmt` | `qmlformat` over the project's QML (`--check` to report only) |
| `loom clean` | Remove the `.loom/` build and deploy trees |
| `loom deploy` | Install to a prefix, optionally packaged (`--package`) |

Common options: `--target`, `--config`, `--prefix`, `--app`, `--generator`,
`--verbose`, `--quiet`. Exit status is 0 on success, 1 on failure, 2 on a usage
error.

The rest of this page is about the styling half; see
[architecture.md](architecture.md) for what `loom dev` does.

## Checking utility classes

`Lo.style` is a string, so no editor completes inside it and no compiler
rejects a typo. Loom's own diagnostics are necessarily runtime ones: an unknown
class warns (category `loom.style`) when the styled item is created, in a log
nobody is watching, and only for code paths that actually run.

`loom style` closes both gaps offline. It is part of the `loom` binary and
links the styling library directly, so it always speaks exactly the vocabulary
the running application does.

This was a separate `loomstyle` executable before 0.2.0.

## Checking for typos

```console
$ loom style --check qml/
qml/TopBar.qml:28: unknown utility class 'text-forground'
qml/Card.qml:14: unknown utility class 'hoverr:bg-accent'
loom: 2 unknown class(es) in 7 file(s)
```

Inside a project, the paths are optional — the manifest's `qmlRoots` are checked
and the project's design tokens are loaded first, so classes built from
project-defined tokens resolve:

```console
$ loom style
```

`loom lint` runs this **and** `qmllint`, reporting both. Both halves always run,
so a utility typo is never hidden behind an unrelated qmllint complaint:

```console
$ loom lint
qml/Main.qml:44: unknown utility class 'bg-brand-999'
loom: 1 unknown class(es) in 1 file(s)
```

Exit status is 0 when everything resolves, 1 for unknown classes, 2 for usage
errors — wire it into a build or a CI step:

```cmake
add_test(NAME style_classes
    COMMAND loom style --check "${CMAKE_CURRENT_SOURCE_DIR}/qml")
```

Paths may be files or directories; directories are searched recursively for
`*.qml`.

### What it can and cannot see

Only **string literals** are checked. `Lo.style: someProperty` is skipped
rather than reported — the checker cannot evaluate a binding, and flagging it
would punish a legitimate pattern.

Bindings that wrap across lines are followed, whether the operator trails one
line or leads the next, up to eight lines.

A trailing dangling prefix is treated as a concatenation, not a typo, because
that is how a class gets built from a binding:

```qml
Lo.style: "bg-accent size-14 rounded-" + model.key   // `rounded-` is fine here
Lo.style: "bg- text-sm"                              // `bg-` is reported
```

Only the *last* class in a literal is exempt; a dangling prefix anywhere else
is still a typo.

## Completion data

```console
$ loom style --catalogue > loom.utilities.json
```

This one needs no project: an editor asking for completion data should not have
to be inside one.

The vocabulary as JSON, for editor integrations:

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
  Roughly 1500 with the default token set.
- `families` — the same data grouped by prefix, when you want to complete the
  prefix first and the value second.
- `numericPrefixes` — prefixes that additionally accept a bare number
  (`border-2`, `border-0.5`), which cannot be enumerated.
- `variants` — combine with `:` before any class.

The catalogue is generated from the parser's own tables and the live token
registry, never a second hand-maintained list, and a test asserts every class
it emits parses. It therefore reflects the **current** registry.

Run inside a project, `loom style` loads the design token file named by the
manifest's `design` key first, so a project's own colors and spacing count:

```console
$ loom style              # checks qmlRoots, with the project's tokens loaded
$ loom style --catalogue  # the built-in vocabulary
```

Outside a project, or with explicit paths, only the built-in tokens are known
and project-defined classes report as unknown. See
[configuration.md](configuration.md) for the token file, and
[manifest.md](manifest.md) for the `design` key.

## From C++

The same data is available to any program linking loom, via
`<loom/loomcatalogue.h>`:

```cpp
const loom::StyleCatalogue catalogue = loom::styleCatalogue();
const QStringList bad = loom::unknownStyleClasses(QStringLiteral("bg-nope p-4"));
```

`unknownStyleClasses()` runs the same parse as `Lo.style` but neither warns nor
caches, so a checker can report per occurrence rather than once per unique
string.

## Editor completion

There is no completion inside `Lo.style` today, in any editor. `qmlls` has no
extension point for it — its suggestions come from the QML type system, and a
string's contents are not in it — so completion means a front-end that consumes
the catalogue above. Two routes are practical:

- **A `qmlls` proxy.** Editors resolve `qmlls` from a configurable directory
  (CLion: *Settings → Languages & Frameworks → QML → Qt binaries*). A wrapper
  of that name can forward every request to the real `qmlls` and answer only
  `textDocument/completion` from the catalogue when the cursor sits inside an
  `Lo.style` literal. Editor-agnostic.
- **Editor snippets.** Generate live templates or snippets from `classes`.
  Not context-aware, but no plumbing.
