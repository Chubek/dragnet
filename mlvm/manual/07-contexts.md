# 7. Contexts, identifiers, and ownership

[Contents](README.md) · [Next: Diagnostics](08-diagnostics.md)

`mlvm::Context` owns interned strings and dialect/operation registrations. Its
identifiers are nonzero, monotonically allocated, and local to that context.
Identical spellings interned in one context resolve to the same identifier.

```cpp
mlvm::Context context;
auto type = context.intern("i32");
auto spelling = context.spelling(type);
```

The returned spelling view remains valid until the context is destroyed. Do not
interpret the numeric identifier as a portable serialized type or compare IDs
from unrelated contexts to establish semantic equality.

## Ownership hierarchy

A `Module` borrows its context. It owns its body, and regions own blocks whose
operations own nested regions. Module copies are independent value copies except
for the shared borrowed context. Declare the context before modules in a local
scope so destruction occurs in the correct order.

There are two separate ID spaces to remember:

- Context identifiers represent interned spellings and registry-related names.
- Module-local node and value IDs identify IR definitions and uses.

Node/value IDs survive append and erase operations for surviving entities.
References into vectors do not: growth or erasure can invalidate references to
blocks and operations. Save an ID or reacquire a reference after mutation instead
of retaining an `Operation&` while appending siblings.

## Synchronization and lifetime

API objects are externally synchronized. A module copy provides a transformation
candidate, not independent ownership of its context or thread-safe mutation of
shared registries. Keep registry setup separate from concurrent read-only use and
provide application-level synchronization where required.

Analysis results should likewise retain IDs and owned data, not pointers into a
candidate module that a transaction may discard.

Reference: [`core.hpp`](../include/mlvm/core.hpp), [ownership rules](../docs/API.md).
