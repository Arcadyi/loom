# Tooling — `loomstyle`

`Lo.style` is a string, so no editor completes inside it and no compiler
rejects a typo. Loom's own diagnostics are necessarily runtime ones: an unknown
class warns (category `loom.style`) when the styled item is created, in a log
nobody is watching, and only for code paths that actually run.

`loomstyle` closes both gaps offline. It ships with the library and links it,
so it always speaks exactly the vocabulary the running application does.

## Checking for typos

```console
$ loomstyle --check qml/
qml/TopBar.qml:28: unknown utility class 'text-forground'
qml/Card.qml:14: unknown utility class 'hoverr:bg-accent'
loomstyle: 2 unknown class(es) in 7 file(s)
```

Exit status is 0 when everything resolves, 1 for unknown classes, 2 for usage
errors — wire it into a build or a CI step:

```cmake
add_test(NAME style_classes
    COMMAND loomstyle --check "${CMAKE_CURRENT_SOURCE_DIR}/qml")
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
$ loomstyle --dump-catalogue > loom.utilities.json
```

The vocabulary as JSON, for editor integrations:

```json
{
  "version": "0.1.0",
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
it emits parses. It therefore reflects the **current** registry: pass
`--config` to include a project's custom colors and spacing.

```console
$ loomstyle --config loom.config.json --dump-catalogue
$ loomstyle --config loom.config.json --check qml/
```

Without `--config`, project-defined tokens are absent from the catalogue and
report as unknown classes.

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
