# Chapter 16. Exolayer Native Interoperability

[Previous: TraceML](15-traceml-and-metatracing.md) · [Contents](README.md) · [Next: VM generation](17-vmweave-vm-generation.md)

## 16.1 The host ABI boundary

Exolayer exposes a C-compatible registry, callback interface, native invocation,
natural aggregate descriptors, and extension loading. Its public symbols use
`exl_`, constants use `EXL_`, and the installed target is `Limestone::exolayer`.
Include [exolayer/exolayer.h](../exolayer/exolayer.h) from C or C++.

Exolayer's host invocation ABI is separate from Limestone's selected target ABI.
A compiler target describing a different CPU does not change how a native host
function is called. Calling conventions and layouts must be selected explicitly
for the actual loaded function.

The integration uses ExolangTk's ExtensionTk and InteropTk boundaries. The supplied
FFItk invocation entry reports `FFI_ENOSYS`; the isolated `native.c` adapter uses
libffi when native FFI is enabled. libffi classifies supported native calls, while
InteropTk independently checks primitives and flat scalar-record layouts.

## 16.2 Contexts, registry names, and diagnostics

`exl_context_create` returns an owning context. `exl_context_destroy` releases its
registrations and context-owned libraries; NULL is accepted. Names and signatures
are copied at registration. Duplicate names are conflicts.

Operations return integer status with context diagnostics available through
`exl_last_error`. That diagnostic string is borrowed until the next context
operation. Native APIs commonly distinguish `0` success, `-1` invalid arguments,
`-2` conflict/missing name, `-3` operational failure, and `-4` unsupported backend
or ABI. Consult each declaration for its exact set.

Independent contexts can be used separately. Synchronize shared-context operations
and keep the context alive through synchronous callbacks. Destruction from an
active callback is outside the context lifetime contract.

## 16.3 Host callbacks are a distinct function signature

`exl_native_fn` is the registry callback ABI:

```c
void callback(const exl_value_t *arguments, size_t count,
              exl_value_t *result, void *userdata);
```

The value kinds are `EXL_I64`, `EXL_F64`, and `EXL_PTR`, with `EXL_VOID` permitted
as a result. Pointer values are borrowed opaque host pointers. Calling a callback
does not invoke an arbitrary native signature through this value-array ABI.

```c
static void add_callback(const exl_value_t *args, size_t count,
                         exl_value_t *result, void *userdata) {
  (void)count;
  (void)userdata;
  result->kind = EXL_I64;
  result->as.i64 = args[0].as.i64 + args[1].as.i64;
}
```

This example assumes callers choose values whose sum fits `long long`; the callback
body owns its arithmetic semantics. Registering a typed signature verifies kinds
and count before entry, not an arbitrary callback's arithmetic correctness.

`exl_register_typed` copies exact argument/result kinds. Callback argument counts
are bounded to 65,536. Userdata remains borrowed and must outlive registration.
Calls are synchronous and may reenter the registry. C++ callback exceptions are
contained at the C boundary; an invalid callback result is a failure.

## 16.4 Scalar native calls

`exl_register_native` prepares an exact native address/signature. Native scalar
kinds mean signed-64-bit C scalar, C `double`, and C `void *`; void is result-only.
There are at most `EXL_NATIVE_MAX_ARGS`, currently 32, native arguments.

```c
static long long native_add(long long a, long long b) { return a + b; }

exl_kind_t kinds[] = {EXL_I64, EXL_I64};
exl_signature_t signature = {EXL_I64, kinds, 2};
int status = exl_register_native(context, "native_add",
    (exl_native_address_t)native_add, &signature, EXL_CC_HOST);
```

The fragment assumes a context and range-valid arguments. The erased address's
actual function signature must match the registered one. Registration does not
convert a function returning `int` into a function returning `long long`.

`exl_native_available` reports whether the optional backend is present.
`exl_kind_layout` inspects the host scalar size/alignment independently of an ISA.
Explicit conventions include host, System V/Windows x86-64, x86 CDECL/STDCALL/
FASTCALL, and ARM/AArch64 conventions; unsupported host/ABI combinations fail.

## 16.5 Exact variadic scalar shapes

`exl_register_native_variadic` and `exl_library_bind_variadic` prepare one concrete
call shape with all argument kinds and an explicit fixed-argument count. Register
each different shape separately.

Ellipsis values must already have C-promoted types supported by the scalar API:
`long long`, `double`, and `void *`. A format string does not cause Exolayer to
infer promotions. Fixed count lies between one and total count; unsupported
variadic calling conventions fail.

The ordinary `exl_call` verifies the complete registered count/kind sequence.
Native variadic aggregate classification is a different adapter boundary from
these exact scalar shapes.

## 16.6 Owning exact-width native types

The data-call interface supports exact scalar types:

```text
I8/U8, I16/U16, I32/U32, I64/U64,
F32/F64, PTR, and result-only VOID
```

`exl_native_type_scalar`, `exl_native_type_struct`, and `exl_native_type_array`
construct immutable owning descriptors. A parent snapshots its children, and a
prepared registration retains its own type ownership. Child/source handles can
be destroyed after construction or registration.

Struct fields follow natural host C declaration order, including alignment and
padding. Arrays are fixed embedded fields. Top-level array parameters decay to
pointers in C and are not admitted as by-value data-call arguments/results.

Query `exl_native_type_layout` for size/alignment and `exl_native_type_offset` for
field or element offset. Caller output storage stays unchanged on failure.
Void reports zero size/alignment and is not a field/argument type.

## 16.7 Constructing a natural record descriptor

For a host declaration:

```c
struct Pair { int32_t tag; double value; };
```

Construct the corresponding descriptor:

```c
exl_native_type_t *i32 = NULL, *f64 = NULL, *pair = NULL;
int status = exl_native_type_scalar(EXL_NATIVE_I32, &i32);
if (status == 0) status = exl_native_type_scalar(EXL_NATIVE_F64, &f64);
if (status == 0) {
  const exl_native_type_t *fields[] = {i32, f64};
  status = exl_native_type_struct(fields, 2, &pair);
}
exl_native_type_destroy(i32);
exl_native_type_destroy(f64);
if (status != 0) {
  exl_native_type_destroy(pair);
  return 1;
}
```

This fragment belongs in a C function with the public header included. Inspect
the resulting size/alignment and offsets against `sizeof`, `_Alignof`, and
`offsetof` for the actual host declaration. That comparison establishes the
layout used by your function rather than relying on a guessed padding pattern.

Nested natural records and fixed arrays are supported. Packed/over-aligned
records, unions, bitfields, vectors, foreign exception ABIs, and aggregate variadics
need separate adapters. The immutable model does not represent those layouts by
pretending they are ordinary natural structs.

## 16.8 Data signatures and by-value buffers

`exl_data_signature_t` holds an explicit result descriptor, argument descriptors,
and count. Register with `exl_register_native_data`, or bind a loaded library
symbol with `exl_library_bind_data`. Invoke through `exl_call_data`, not `exl_call`.

Continuing the record example:

```c
static struct Pair adjust_pair(struct Pair input) {
  input.tag += 1;
  input.value += 2.0;
  return input;
}

const exl_native_type_t *arguments[] = {pair};
exl_data_signature_t signature = {pair, arguments, 1};
int registered = exl_register_native_data(context, "adjust_pair",
    (exl_native_address_t)adjust_pair, &signature, EXL_CC_HOST);
exl_native_type_destroy(pair);
if (registered != 0) return 1;

struct Pair input = {7, 40.0};
struct Pair output = {0, 0.0};
exl_data_argument_t data[] = {{&input, sizeof input}};
int called = exl_call_data(context, "adjust_pair", data, 1,
                           &output, sizeof output);
```

The fragment assumes the verified descriptor/context from the previous section.
Registration retains independent type ownership. Input buffers contain exact
host representations including padding, and each size must equal its declared
type size. Void results require NULL result and size zero.

## 16.9 Aligned copies and transactional result writes

Data invocation copies each input to aligned temporary storage before native
execution. Caller input alignment is unrestricted. Argument/result overlap is
permitted because the inputs have already been copied. Result bytes are committed
only when the call succeeds.

Pointer representations are copied, but pointed-to storage remains borrowed.
This is by-value marshaling, not deep cloning or a pointee lifetime manager.
Native side effects are not rolled back if a later call failure occurs; the
transactional guarantee concerns result-buffer publication.

Type expansion is bounded to 1,024 nodes, depth 32, and 1 MiB of type bytes,
including padding and repeated array elements. Construction/native calls can
allocate temporary storage. Native type construction requires the enabled backend
and returns unavailability when it is absent.

## 16.10 Shared libraries and extension descriptors

`exl_library_open_native` opens an ordinary shared library through ExtensionTk.
Binding requires an explicit registry name, loader symbol, signature, and calling
convention. Bindings retain their library through active calls.

`exl_library_open` loads an Exolayer extension descriptor. The immutable export is
named `exl_extension_descriptor` with `EXL_EXTENSION_ABI_VERSION`, currently one,
and a validated list of callback symbols. Descriptor strings/functions/userdata
are borrowed from the library while it remains loaded. Validation precedes
transactional registration; no extension initialization callback is inferred.

`exl_library_close` unregisters its symbols and invalidates the library handle.
Closing an active library call is rejected, including reentrant close attempts.
Context teardown owns remaining library release. Invalid or foreign handles are
rejected through the public boundary.

## 16.11 Public implementation and module manifests

The header is usable from C and C++. Its optional implementation macro
`EXL_EXOLAYER_IMPLEMENTATION` belongs in exactly one C++20 translation unit when
using that integration pattern. Normal installed-library consumers simply include
the header and link the target.

The exported symbol inventory is
`third_party/exolangtk/manifests/Exolayer-Modules.yaml`. The manifest test checks
the declared API and `exl_` naming. See
[the vendor Exolayer guide](../third_party/exolangtk/docs/manual/19_exolayer.md)
for related integration conventions; the public header is authoritative for
current aggregate/data-call declarations.

The optional Python compiler bindings wrap the `limestone_*` C APIs; they do not
provide convenience Exolayer aggregate wrappers. Use an explicit native extension
or the Exolayer C/C++ interface for this host ABI boundary.

## 16.12 Interoperability verification

Test exact scalar extrema, natural padding, float/mixed records, nested records,
embedded arrays, large returns, descriptor destruction after registration,
unaligned and overlapping buffers, wrong counts/sizes, type limits, unavailable
backends, missing/duplicate symbols, active close, and reentrant callbacks.

Independent native fixture calls establish ABI behavior; descriptor round trips
alone establish only model consistency. Keep host ABI, compiler target ABI,
callback ABI, and executable lifetime as explicit parts of the integration.

[Previous: TraceML](15-traceml-and-metatracing.md) · [Contents](README.md) · [Next: VM generation](17-vmweave-vm-generation.md)
