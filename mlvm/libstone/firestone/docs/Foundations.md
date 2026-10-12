# Arithmetic, concurrency, compute, graphics, and intrinsics

All APIs are header-only C++17, in `firestone::<component>`. CMake exports
`Firestone::bignum`, `Firestone::concurrency`, `Firestone::compute`,
`Firestone::gfx`, and `Firestone::intrin`; the umbrella target includes all five.
Invalid arguments, invalid state, range errors, and backend failures use standard
C++ exceptions. Resource release callbacks must not throw.

## Arbitrary-precision arithmetic

### BigInt

`libbignum/BigInt.hpp` provides signed-magnitude integers with little-endian
32-bit limbs and canonical unsigned zero. Operations include:

- construction from `int64_t` or a strict base-2..36 digit string;
- `+`, `-`, `*`, `/`, `%`, compound assignments, comparisons, unary negation;
- shifts, `bit_length()`, magnitude `test_bit()`, `abs()`, `sign()`;
- `to_string(base)`, checked `to_int64()`, stream output, `pow`, and `gcd`.

String parsing accepts one optional sign, with no whitespace, separators, or
implicit radix prefixes. Division truncates toward zero; the remainder has the
dividend's sign. `divmod(a,b)` satisfies `a == q*b+r` and `abs(r) < abs(b)`.
Right shift divides the magnitude by a power of two, truncating toward zero;
it is not a sign-extending machine shift. Bit queries inspect the magnitude.
Division by zero throws `domain_error`; narrowing overflow throws `overflow_error`.
Moved-from integers become canonical zero.

Multiplication is quadratic schoolbook multiplication; division is binary long
division. Storage grows with the value, subject to allocator/container limits.
Operations are variable-time and are not cryptographic primitives.

```cpp
using namespace firestone::bignum;
auto n = pow(BigInt(2), 256) - BigInt(1);
auto qr = divmod(n, BigInt(97));
auto hexadecimal = n.to_string(16);
```

### BigFloat

`libbignum/BigFloat.hpp` represents **finite decimal** floating point as
`coefficient * 10^exponent`. Precision is a per-value significant-decimal-digit
count, defaulting to 50; valid precision is 1..100000. Stored exponents lie in
[-100000,100000]. Out-of-range exponents throw rather than saturating.

- Strict decimal/scientific parsing, arithmetic, comparisons, compound assignments.
- Round-to-nearest, ties-to-even after construction and each arithmetic operation.
- Binary operations use the larger operand precision.
- Division rounds from its exact integer quotient/remainder; it does not round
  through a host floating-point type.
- Comparisons compare represented values exactly, independently of precision.
- `with_precision(n)` explicitly rounds to a different precision.
- `to_string()` emits canonical scientific notation, with no trailing zeroes;
  zero is `"0"`, and a zero scientific exponent is omitted.
- `coefficient()` and `exponent()` expose the normalized representation.

There are no NaNs, infinities, signed zero, transcendental functions, or implicit
binary-floating conversions. Addition may allocate proportional to the exponent
gap. Parsing limits literals to 100000 coefficient characters (including a sign).

```cpp
auto third = BigFloat(1, 80) / BigFloat(3, 80);
auto exact = BigFloat("0.1") + BigFloat("0.2"); // 0.3
auto rounded = BigFloat("1.245", 3);          // 1.24, ties to even
```

### Complex128

`Complex128` is `std::complex<double>` with two IEEE-754 binary64 components,
128 component bits total. Arithmetic and transcendental functions follow the
standard complex API. It is distinct from a pair of binary128 components.

## Concurrency

### Native synchronization and threads

`Atomic<T>`, mutex types, and lock templates retain the corresponding standard
library semantics. `Spinlock` is a nonrecursive acquire/release lock with bounded
spinning before yielding. `Semaphore(initial, maximum)` supports `acquire`,
`try_acquire`, `try_acquire_for`, and checked bulk `release`.

`Thread` owns a `std::thread`. `join()` propagates exceptions from the worker;
destruction and move assignment join outstanding work without propagating worker
exceptions. If destroyed by its own worker, it detaches; the captured exception
state has shared ownership. Referenced application data must outlive the task.

`PosixThread` adds POSIX availability checks and Linux `set_name` (15 bytes
maximum). `native_handle()` exposes the standard thread's native handle.
`CThreads` uses C11 `thrd_create`/`thrd_join` when `<threads.h>` is available;
its task returns `int`, returned by `join()`. It captures worker exceptions,
supports moving, and joins during destruction. Destroying it from its own worker
terminates. Creating a C11/POSIX thread on an unsupported platform throws.

### Channels

`Channel<T>(capacity)` is a mutex/condition-variable MPMC FIFO. Capacity zero
means unbounded, not rendezvous. Elements must have nonthrowing move construction.

| Operation | Contract |
| --- | --- |
| `send(value)` | Blocks for capacity; returns false if closed |
| `try_send(value)` | Returns false when full or closed |
| `receive()` | Blocks for a value or closure; returns `optional<T>` |
| `try_receive()` | Empty optional when no value is queued |
| `close()` | Idempotent; wakes all readers/writers; rejects future sends |
| `closed()`, `size()` | Synchronized snapshots |

Closure preserves queued elements for draining. An empty receive result after
closure marks end-of-stream. Send methods take ownership by value, including
failed sends. Before destroying a channel, join every thread accessing it;
closure does not itself synchronize object destruction. The same lifetime rule
applies to locks and semaphores. Waiters are not guaranteed FIFO fairness.

### Fibers and green threads

`Fiber` is a **stackless** state machine. A captured callable returns
`FiberStep::Yield` or `Complete`; each `resume()` invokes one step. States are
Ready, Running, Suspended, Complete, Cancelled, and Failed. Reentrant resume and
cancelling a running fiber throw. A failed fiber retains its exception; subsequent
resume rethrows. Terminal successful/cancelled fibers are not invoked again.

`GreenThread` schedules fibers round-robin on the calling thread. `spawn` returns
a monotonically increasing task ID. `run(budget)` executes at most that many
queued steps and returns the number executed. Tasks may spawn new tasks or cancel
other tasks. Cancelled tasks are removed when dequeued. `pending()` counts tasks
until removed, including cancelled tasks. A failed task is removed and its error
propagates; remaining tasks can run later. Recursive `run` is rejected.

These APIs provide cooperative execution with explicit captured state, not stack
switching, coroutine syntax, preemption, or cross-thread scheduling.

### Linux services

`LinuxProcess::spawn(argv)` uses `posix_spawnp` and inherited environment/stdio.
It performs no shell expansion. Use an explicit shell in argv when wanted.
Arguments containing NUL are rejected. The move-only process owns one child:
`wait()` reaps it and caches `ProcessStatus`; `poll()` returns an empty optional
while running. `exit_code` is -1 for signal termination; `signal` is zero for a
normal exit. Destruction waits for the child. Do not independently reap owned
children or configure automatic child reaping.

`LinuxSched` exposes affinity queries/updates and yielding. A PID of zero targets
the calling thread. Affinity uses the platform's fixed `CPU_SETSIZE` mask; larger
CPU-set environments may produce a system error. OS failures preserve errno via
`system_error`. Non-Linux builds compile the API but report it unavailable.

## Portable vector intrinsics

`Vector<T,N>` provides unaligned load/store, checked lane access, lane arithmetic,
comparison masks, shuffles, selection, fused floating multiply-add, and ordered
addition reduction. Initializer lists must supply exactly N lanes; use parentheses
for scalar broadcast. `Mask<N>` supports `all`, `any`, `count`, `!`, `&`, and `|`.

- Integer add/subtract/multiply wrap modulo the lane width, including signed lanes,
  without signed C++ overflow. Integer division by zero or minimum/-1 throws.
- Floating operations retain scalar C++/IEEE behavior; NaN comparisons remain
  unordered. Reduction folds left-to-right. FP compiler flags still apply.
- Pointer operations require at least N actual elements; an optional count checks
  caller-provided bounds. Masked selection does not evaluate memory accesses.
- `SSE4`, `AVX2`, `AVX512`, and `NEON` expose `Pack<T>` for 128/256/512/128-bit
  storage widths. Lane execution is portable regardless of ISA availability.
- `available()` is a hardware/OS feature query, not an indication of the chosen
  implementation. GCC/Clang x86 builtins account for OS-enabled vector state;
  NEON is reported when guaranteed by the compilation target. AVX512 tests the
  foundation feature only. Unknown targets conservatively report false.

The library does not force ISA-specific compiler flags or native instruction
selection. Compilers may autovectorize these operations. `FPU` supplies scalar
sqrt/fma and exception flags. `ScopedRounding` restores the calling thread's
rounding mode; use compiler flags that honor the floating-point environment when
arithmetic must observe it (for example GCC `-frounding-math`).

## Compute

### Host execution

`OpenMP::parallel_for(count, callable)` invokes the callable with indices
[0,count). With `FIRESTONE_ENABLE_OPENMP=ON`, it uses a static OpenMP schedule.
Without OpenMP it executes serially. `available()` reports native OpenMP support;
`max_threads()` returns one for serial execution.

`parallel_reduce(count, initial, map, reduce)` maps in parallel and folds results
in increasing index order. This costs O(count) temporary storage but makes the
fold deterministic. Mapping callables are shared between workers and must permit
concurrent invocation. Exceptions are captured inside the parallel region and
re-thrown on the caller after the region finishes; other iterations may run.

### CUDA, HIP, OpenCL, SYCL adapters

The named backend types share a checked synchronous contract implemented through
`BackendFunctions`. A default backend is unavailable; operations throw
`compute::Unavailable` until an SDK adapter is supplied. These types do not load
SDKs, discover devices, compile kernels, or emulate GPU execution themselves.

An adapter supplies device limits and callbacks for allocation/release, upload,
download, compilation/kernel release, launch, and synchronization. Every callback
must be present. Handles are nonzero `uintptr_t` tokens meaningful to the adapter;
returning zero from allocation/compilation fails. Release callbacks must not throw.

- Capture native context ownership in the callbacks. Buffers, kernels, arguments,
  and backend copies share the context lifetime. Each resource is released once.
- Copies of a buffer/kernel share the native allocation/kernel. Moved-from or
  default-constructed resources are empty and cannot be used for transfers.
- Callbacks are synchronous: launches/transfers complete before returning. An
  asynchronous native SDK must wait within its adapter callback.
- Buffer transfers check offset/count with overflow-safe bounds. Null pointers
  are accepted only for zero-byte transfers. Zero-byte allocations are rejected.
- `NDRange` holds global work-item counts and local group sizes in three axes.
  All dimensions must be positive and global dimensions divisible by local
  dimensions. Local dimensions/product must satisfy the declared device limits.
- `Argument::scalar` copies a trivially copyable nonpointer value's host byte
  representation. Callers and adapters establish device ABI/layout agreement.
  `Argument::buffer` retains its buffer. `Argument::local` declares scratch bytes;
  backend-specific local-memory limits and argument signatures belong to the adapter.
- Dispatch rejects kernels and buffer arguments from a different context, even
  when the native device is identical. Copies of one backend share a context.
- Callback exceptions propagate. Adapter implementations must clean up native
  allocations made before throwing and provide any required thread synchronization.

## Graphics

`GLSL`, `HLSL`, `MSL`, and `WGSL` wrap shader text, stage, and entry-point metadata.
They check nonempty/NUL-free source, identifier syntax, and supported stage
families. GLSL takes a body and prepends a desktop core version directive;
HLSL supplies shader-model profile names. `ShaderCompiler` invokes a supplied
compiler callback and rejects empty artifacts. Source-language parsing, type
checking, reflection, and driver compilation belong to that callback.

`SPIRV` reads/writes portable word streams. It validates magic, version 1.0..1.6,
nonzero ID bound, zero schema, and bounded nonzero instruction lengths. It accepts
either byte order and emits little-endian bytes. `allocate_id` updates the bound;
`append` adds an opcode and operand words; `instructions` exposes framed records.
This is **structural validation**, not SPIR-V semantic validation: capabilities,
IDs, operand counts/types, and control flow require a full external validator.
The caller must allocate IDs or supply a correct bound when importing words.

`Wayland`, `X11`, and `DRM` are native display adapters using `DisplayFunctions`:
create/destroy a surface, synchronously present pixels, and poll events.
The caller supplies the native implementation and owns connection state through
callback captures. Default adapters report unavailable.

`Surface` is move-only and retains the adapter after the backend object dies.
Frames support RGBA8/BGRA8, explicit dimensions, byte counts and row strides.
Validation requires storage for every complete stride and a matching surface
extent. A resize event updates the expected extent. Present must finish consuming
the supplied memory before returning. Poll is nonblocking and returns an empty
optional when idle. Destroy callbacks must not throw; native event-loop/thread
affinity and synchronization rules remain the adapter's responsibility.

## Verification

The five component test suites exercise arithmetic identities and rounding,
modular vector boundaries, threaded FIFO traffic and shutdown, failed tasks,
process exit status, SDK adapter dispatch and ownership, malformed SPIR-V,
shader contracts, and display events/frames. GPU/display tests use in-process
test adapters and require no hardware or SDK. All public headers compile alone;
the relocated installed-package consumer exercises the five exported targets.
