# Exolayer

Exolayer is Limestone's C ABI/FFI boundary. Public symbols use `exl_`; link
`Limestone::exolayer` and include `<exolayer/exolayer.h>`.

An opaque context owns symbol registrations. `exl_register` registers a host
callback; `exl_register_typed` additionally validates scalar argument/result kinds.
Callbacks have the `exl_native_fn` ABI: value array/count, result pointer, and host
userdata. This callback ABI is distinct from a foreign native function signature.

`exl_register_native` prepares a scalar native address/signature with an explicit
`exl_callconv_t`. `exl_library_open_native` and `exl_library_bind` retain library
ownership while symbols are registered/invoked. Supported argument kinds are
`EXL_I64`, `EXL_F64`, and `EXL_PTR`, with scalar/void results and at most
`EXL_NATIVE_MAX_ARGS` arguments. `exl_native_available` reports adapter availability.
`exl_register_native_variadic` and `exl_library_bind_variadic` prepare one exact
variadic call shape with an explicit fixed-argument count. Ellipsis argument
types are already C-promoted: `long long`, `double`, and `void *`. Each distinct
argument shape is registered separately; calls validate every argument kind and
the total count. The same ownership and library-close rules apply.

`exl_native_type_scalar`, `exl_native_type_struct`, and `exl_native_type_array`
create immutable owning descriptors for exact-width scalar values, natural C
records, and embedded fixed arrays. Children and source handles may be destroyed
after construction or registration. `exl_native_type_layout` and
`exl_native_type_offset` expose checked host layouts. Expanded types are bounded
to 1,024 nodes, 32 nesting levels, and 1 MiB.

`exl_register_native_data` / `exl_library_bind_data` prepare exact nonvariadic
scalar/aggregate signatures; `exl_call_data` passes buffers by value. It copies
inputs into aligned temporary storage, permits argument/result overlap, and
writes result bytes only on success. Pointer pointees stay borrowed. libffi owns
aggregate ABI classification; InteropTk independently checks primitives and
flat scalar records. Nested natural structs and embedded arrays are supported;
top-level arrays, packed/over-aligned records, unions, bitfields, vectors,
and foreign exceptions require separate ABI adapters.

`exl_register_native_data_variadic` / `exl_library_bind_data_variadic` use the
same owning descriptors and `exl_call_data` for a concrete variadic signature.
The fixed-argument count is explicit. Ellipsis scalars must already be promoted:
`float` and integers narrower than host `int` are rejected, not silently changed.
Named arguments retain their declared types. Aggregate fields are not promoted;
natural structs, nested records, and embedded arrays can occur on either side of
the ellipsis and as results. libffi handles register/stack classification with
`ffi_prep_cif_var`. stdcall/fastcall variadics are rejected. Failed registration
does not reserve the name or change existing bindings.

The supplied FFItk entry point returns `FFI_ENOSYS`; the isolated `native.c`
adapter uses libffi when `LIMESTONE_ENABLE_NATIVE_FFI=ON`. Disabling it keeps
callbacks, layouts, and extension loading, while native registration reports
unavailability. Native declaration/call failures return errors through the stable
C interface.

Extension callbacks retain their library through active calls, including unload
and registration changes. `EXL_EXTENSION_ABI_VERSION` is 1. Scalar layouts
are independently inspectable and validated through `exl_kind_layout`.
Owning contexts and library handles have explicit release paths.

The full API, conventions, extension contract, and native examples are in
[`19_exolayer.md`](../third_party/exolangtk/docs/manual/19_exolayer.md). The
authoritative symbol list is
`third_party/exolangtk/manifests/Exolayer-Modules.yaml`, checked by CTest.
