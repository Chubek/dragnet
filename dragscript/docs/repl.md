# Dragscript REPL

`dragscript-repl` is an interactive shell for the Dragscript subset, built on
[`third_party/replxx`](../third_party/replxx) for line editing, history and
syntax highlighting.

## Build

```sh
cmake -S dragscript -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/dragscript-repl
```

Options:

| Option | Default | Meaning |
| --- | --- | --- |
| `DRAGSCRIPT_BUILD_REPL` | `ON` | Build the `dragscript-repl` executable. |
| `DRAGSCRIPT_BUILD_REPL_TESTS` | `ON` | Build `dragscript-repl-tests`, registered with CTest. |
| `DRAGSCRIPT_DIFFERENTIAL_TESTS` | `OFF` | Also build the QuickJS differential oracle. |

Command line:

```
dragscript-repl [--backend dsir|mlvm] [--history FILE] [--no-history] [--no-multiline]
```

## Session model

`Engine::run` gives every run a fresh script environment, so the REPL does **not**
rely on engine state carrying over. Instead it keeps the text of every accepted
submission and recompiles the accumulated session for each new one:

```text
function print(value) { return value; }   <- preamble (see below)
let a = 1                                  <- accepted submission 1
let b = 2                                  <- accepted submission 2
print(a + b)                               <- current submission
```

Consequences, all of which are covered by tests:

- `let`, `const` and `function` declarations **do** persist across submissions,
  because the whole session is recompiled each time.
- A submission that fails to compile or run is **not** folded into the context, so a
  rejected line cannot corrupt later submissions.
- Recompilation cost grows with session length. Context is capped at 64 KiB; beyond
  that the REPL reports `DS_LIMIT` and asks for `.reset`.
- `-0` edge cases follow the engine, not the REPL: each `run` starts from a fresh
  environment, so a binding is only observable through a recompiled session.

### Line numbers in diagnostics

Diagnostics are reported against the *submission*, not the accumulated program.
Because the preamble and context are newline-terminated, the number of preceding
lines is exactly the count of their newlines. Adding a further separator would
shift every reported line by one; `repl_detail::compose_session` and
`submission_line` in `repl/src/session.cpp` own this arithmetic and are unit tested.

### The `print` preamble

The engine has no stdio host, so the REPL prepends
`function print(value) { return value; }` to every session. The last expression
value of the program is printed in green. A submission may declare its own
`print`, which shadows the preamble one, since later declarations win.

## Commands

| Command | Effect |
| --- | --- |
| `.help` | List commands. |
| `.quit`, `.exit` | Save history and exit. |
| `.clear` | Clear the screen. |
| `.history` | Print the session history, oldest first. |
| `.reset` | Discard the session context and rebuild the engine on the current backend. |
| `.backend` | Print the active backend. |
| `.backend dsir` / `.backend mlvm` | Switch backend and rebuild the engine. |

## Multiline input

A submission that opens a bracket, a block comment or a string continues on the
`...>` prompt (`--no-multiline` disables this). The scanner in
`repl/src/highlighter.cpp` is shared with the syntax highlighter, so continuation
detection and colouring cannot disagree. Mismatched closers such as `let a = (]`
are treated as malformed input rather than as "needs more", and are reported by the
compiler instead.

## Backends

Both `Backend::dsir` and `Backend::mlvm_ir` are selectable at runtime and at
startup. The backend is fixed per engine: `EngineOptions` is not exposed for
mutation through the public API, so `.backend` constructs a new engine rather than
editing shared options. Because the session context is plain text, switching
backends keeps the accumulated declarations.

## Known limitations

- Only the documented subset is accepted. Unsupported syntax such as `2 ** 0`
  fails at compile time with `DS_UNSUPPORTED`; the REPL does not silently ignore it.
- `Engine::run` is synchronous. There is no event loop, so promises, timers and
  `await` are not available in the REPL.
- Completion and highlighting are lexical and ASCII-oriented; they cover keywords,
  commands and literals rather than user-defined bindings.
- Terminal behaviour (key bindings, paging, colours) is provided by replxx defaults
  and is not covered by the headless test suite; the tests cover the scanner,
  completion candidates, session composition and the compile path.