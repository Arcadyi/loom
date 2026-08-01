# Upgrading

What changes between loom versions and what you have to do about it. The
[CHANGELOG](../../CHANGELOG.md) is the complete record; this page covers only
what needs action on your side.

## Version policy

loom is **pre-1.0**. A minor bump — 0.1 to 0.2 — may break API, and 0.2.0 did:
it renamed the entire public surface. Patch bumps are additive or corrective.

Pin accordingly:

```cmake
find_package(loom 0.2.1 CONFIG REQUIRED)
```

The installed package config uses `SameMajorVersion` compatibility, which at
version 0.x means `find_package(loom 0.2)` is also satisfied by a future 0.9.
Until loom reaches 1.0, request the exact version you tested against.

## Checking what you have

```console
$ loom --version
$ loom doctor            # both halves, including the Loom QML module
```

From C++, `loom::version()` reports the library you linked against rather than
the headers you compiled with — useful for catching the skew described below.

---

## 0.2.0 → 0.2.1

A correctness release. Nothing was renamed, but three changes alter behaviour
you may have been relying on.

### Rebuild everything, including your application

`ProtocolVersion` went from 1 to 2. Because `loom::Runtime` is statically
linked, an application built against 0.2.0 will fail the handshake against a
0.2.1 `loom dev`:

```
[loom] protocol version mismatch: this loom speaks v2, the application speaks v1
```

That is the intended diagnosis, not a bug. Rebuild the application. `loom dev`
does this for you on the next native change, but a stale binary from before the
upgrade needs one explicit `loom build`.

The change: the `Design` frame's payload became a CBOR map carrying the
document's project path alongside the tokens, so a relative `iconRoot` resolves
against the project rather than against the runtime's staging directory.

### Check styles that mix `hover:` and `md:`

Specificity now ranks states and breakpoints on **two independent axes**, with
states winning. Before 0.2.1 both counted as "one variant prefix", so the class
written later won.

```qml
Lo.style: "bg-white hover:bg-black md:bg-red-500"
```

- **Before:** at ≥ 768 px the `md:` rule won and hovering did nothing.
- **Now:** hovering wins at every width, which is almost certainly what the
  string was meant to say.

If you worked *around* the old behaviour — reordering classes so the state came
last, or dropping a breakpoint rule because it broke hover — that workaround is
now doing the opposite of what you want. Grep for strings containing both a
state variant and a breakpoint variant:

```console
$ grep -rnE 'Lo\.style.*(hover|pressed|focus|disabled|dark):.*\b(sm|md|lg|xl):' qml/
```

Full ordering: [../styling/utilities.md](../styling/utilities.md#specificity--which-rule-wins).

### Check code that walks `children`

The managed shadow moved from being a **sibling** of the styled item to being a
**child** of it, at `z: -1`. It is no longer in the parent's child list and is
now in the target's.

This fixed shadows inside `Row`, `Column`, `Grid` and the Layouts, where the
sibling took a layout slot of its own. If you have code that iterates
`childItems()` and assumed a styled Rectangle had no children, it now sees one.
Filter on the type, or on `z < 0`.

One new consequence: a target with `clip: true` now clips its own shadow. Use a
border instead inside a clipping view.

### Other behaviour changes

| Change | Action |
| --- | --- |
| `border-{n}` rejects negatives, `nan` and `inf` | these were silently written before; `loom lint` now reports them |
| `tracking-*` resolves against the size set in the same string regardless of class order | letter spacing may change where `tracking-` preceded `text-` |
| A reload clears an `iconRoot` the design file no longer defines | previously the old root stayed live |
| `qt.version` in `loom.json` is a **minimum**, not an exact match | no action; `"6.11"` keeps working and `"6.12"` now also does |
| `hidden` accepted as a synonym for `invisible` | no action |

### New in this release

- `loom::reloadConfigData(json, basePath)` for applying tokens that are not on
  disk where they belong.
- `LOOM_STRICT_SCHEMA_TEST=ON` makes the JSON-schema test fail rather than skip
  when `jsonschema` is missing. Turn it on in CI.
- Warnings name the utility family and token rather than printing an empty key.

---

## 0.1.0 → 0.2.0

**loom absorbed respin.** The two projects — utility-first styling, and the
Qt/QML build and hot-reload tooling — became one framework, one package, one
binary. If you used only the styling half, the rename below is the whole
upgrade.

### Renames

| 0.1.0 | 0.2.0 |
| --- | --- |
| `respin new` | `loom new` |
| `respin.json` | `loom.json` |
| `respin::Application` | `loom::Application` |
| `respin_add_application` | `loom_add_application` |
| `RESPIN_DEV_*` | `LOOM_DEV_*` |
| two packages | one `find_package(loom)` |

The exported targets became `loom::loom`, `loom::loomplugin`, `loom::Runtime`
and `loom::Protocol` under a single `loomTargets` export.

### New capability

`loom.json` gained a `design` key naming a token file.
[`loom dev`](../tooling/cli.md) watches it, and editing it **repaints the running
window without recreating the scene** — nothing on screen loses its state. See
[../styling/configuration.md](../styling/configuration.md).

Two semantics worth knowing if you adopt it:

- `loom::reloadConfig()` **replaces** rather than merges, so a token deleted
  from the file stops resolving. A file that fails to parse changes nothing.
- On reload the theme you are currently viewing wins over the file's
  `defaultTheme`.

### Tooling

`loomstyle` folded into the CLI as `loom style --check` and
`loom style --catalogue`. `loom lint` now runs `qmllint` **and** the
utility-class check, always both.

---

## Version skew

The one failure mode worth naming explicitly, because it produces confusing
symptoms rather than a clear error.

`loom::Runtime` is a **static** library. An application links a copy of it at
build time. If you upgrade loom's headers and CMake package but do not rebuild
the application, the application keeps running the old runtime while your `loom`
CLI is new.

Since 0.2.1 the protocol version catches the case that matters — the handshake
fails by name. What it cannot catch is a mismatch *within* one version, such as
a rebuilt `libloom_runtime.a` against headers from a different checkout. There
is no detection for that; the symptom is behaviour that matches neither version.

The reliable fix is the blunt one:

```console
$ loom clean --all
$ loom build
```

If loom itself was installed to a prefix, reinstall it before rebuilding the
application, so the package config and the archives agree.
