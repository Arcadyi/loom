# Contributing

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite has two tiers:

```sh
ctest --test-dir build -LE e2e    # fast: under 10 seconds
ctest --test-dir build -L  e2e    # end to end: installs, scaffolds, compiles, runs
```

The end-to-end tests install loom to a throwaway prefix, scaffold a project, build it with
a real compiler, and run the result. They are the only tests that compile generated output.
One of them scaffolds an application named `Testing` on purpose — that name collides with the
directory `ctest` creates, and shipping that collision is what started this work.

Keep the fast tier fast. When a test needs a timeout longer than a second or so, make the
timeout injectable rather than waiting it out — `DevServer::setHeartbeat` exists for exactly
that reason.

## Before opening a change

```sh
# Warnings are errors in CI
cmake -S . -B build-werror -G Ninja -DLOOM_WERROR=ON
cmake --build build-werror

# Sanitizers (one narrow leak suppression, see below)
cmake -S . -B build-asan -G Ninja -DLOOM_SANITIZE=ON -DLOOM_BUILD_E2E_TESTS=OFF
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -LE e2e

# Formatting
find src include tests examples -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
```

The schema test needs `jsonschema`; without it the test skips rather than failing, so CI
installs it.

There is **exactly one leak suppression**, and a second needs the same justification. A
leaked QML root object allocates through Qt frames, so a broad `leak:libQt6` suppression
hides the exact bug the sanitizer job exists to catch — measured: it turned a reproduced
root-object leak back into a pass.

The exception is `tests/lsan.supp`, `leak:libQt6QuickEffects`. That library leaks internally
on any `RectangularShadow` create/destroy, reproduced with no Loom code involved. It is wired
in through the test `ENVIRONMENT` in `tests/CMakeLists.txt` rather than globally, so it covers
that one library. Leaks through QtQuick and QtQml frames still fail the job.

If Qt ever forces another, reproduce it without Loom first, suppress the narrowest possible
frame, and record it here.

Never run `clang-format` on a `.cmake` file. It is a C++ formatter and will destroy it.

## The negative-control convention

**A test that cannot fail is not a test.** Every behavioural fix in this repository has been
checked by reverting the fix and confirming the new test goes red.

The convention, and it is worth following exactly:

1. Write the fix and the test; confirm green.
2. Revert the fix — only the fix.
3. **Confirm the build succeeded.** A patch that does not compile leaves a stale binary
   passing, which reads as a valid control and is not.
4. Confirm the test is red, and that it fails for the stated reason.
5. Restore, and diff against a backup to confirm the file is byte-identical.

This has repeatedly caught bad tests rather than bad code. Examples from the history:

- A reload test compared root-object pointers, which passed with the fix reverted because
  the allocator handed back the same address. Rewritten to use a `QPointer`, which observes
  destruction rather than address reuse.
- A dev-server test asserted only "was the client dropped?", which the authentication
  deadline satisfied a second later regardless. Rewritten to assert on the logged reason,
  within a window shorter than that deadline.
- A qmldir control compared a live file with a compiled copy that had just been rebuilt from
  it, so both were identical and the control proved nothing. Rewritten so they differ.

If a control passes when it should fail, the test is wrong. Fix the test.

## Verify claims, do not infer them

Documentation examples are executed against a real scaffold, not proofread. The 0.1.0 README
documented an install path that did not exist, which is the failure this rule exists to
prevent.

The same applies to reasoning about behaviour: measure it. A hot-reload bug was once reported
here as proven, on the strength of one process exiting, and was wrong — the process had been
closed on the desktop. Prefer a script that observes the thing directly.

Beware shell pipelines when checking exit codes: `cmd | head` reports `head`'s status, not
`cmd`'s. Capture to a file instead.

## Style

`clang-format` with the repository's `.clang-format`, which describes the style the code is
already written in rather than imposing a new one. `.clang-tidy` is opt-in and not a CI gate:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/cli/commands.cpp
```

Comments should explain **why**, especially where the code looks like it could be simpler.
Much of this codebase's subtlety is load-bearing — the `prefer` line stripped from a bundled
qmldir, `isFile()` rather than `exists()`, `POST_INCLUDE_FILES` after a blanket exclusion —
and a comment is what stops the next person removing it.

## Layout

Loom is two halves that ship as one package: a styling layer (`import Loom`) and the build
and hot-reload tooling (the `loom` command).

| Path | Contents |
| --- | --- |
| `src/tokens/` | The token registry and typed `Loom` singleton. `loomtokendata.h` is the X-macro source of truth for every token |
| `src/style/` | The `Lo.style` compiler and the per-item apply engine |
| `src/protocol/` | Wire format and bundle validation (`loom::Protocol`) |
| `src/runtime/` | Engine bootstrap and reload controller (`loom::Runtime`), linked into user applications |
| `src/cli/` | The `loom` tool; all of it in `loom_cli_core` so tests can link it |
| `cmake/` | The installed CMake package: targets, `loomFunctions.cmake` |
| `schemas/` | JSON Schema for `loom.json` and for a design token file |
| `templates/app/` | What `loom new` generates, embedded as a Qt resource |
| `examples/gallery/` | The `loomgallery` demo, and the fixture the qmllint test runs against |
| `tests/` | Unit tests plus `.cmake` driver scripts for the CLI and end-to-end tests |
| `docs/` | Reference documentation |

The two halves meet in three places worth knowing about:

- `loom_runtime` links `loom` publicly, so an application with hot reload also has the
  styling layer. That is what lets design tokens reload in-process.
- `loom_cli_core` links `loom` for the style catalogue behind `loom style` and `loom lint`.
  The CLI stays a `QCoreApplication` — the catalogue needs no QML engine.
- The manifest's `design` key names a token file that `loom dev` watches and pushes over the
  reload connection as `MessageType::Design`, applied without recreating the scene.

The library target is named `loom`, so the CLI executable target is `loom_cli` with
`OUTPUT_NAME loom`. The installed binary is still `bin/loom`.

`loom::Runtime` is a **static** library installed with its headers, so any header change
forces a user rebuild and there is no detection for a stale `.a` against new headers. Treat
`include/loom/` as a public surface.
