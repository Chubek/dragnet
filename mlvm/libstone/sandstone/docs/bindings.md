# Language bindings

`bindings/sandstone.i` is a SWIG 4.0 interface over the two C APIs
(`sandstone.h`, `assembler.h`). `bindings/generate-bindings.sh` drives
SWIG:

```sh
./bindings/generate-bindings.sh --language python --out build/bindings
./bindings/generate-bindings.sh --all --out build/bindings
./bindings/generate-bindings.sh --language python --out build/bindings --build
```

Languages: `python` (default), `lua`, `java`. Each output directory holds
the generated wrapper plus a smoke test (`smoke.py` for Python).
`--build` compiles the Python extension against `libsandstone.a` from the
CMake build tree (needs Python development headers).

Ownership follows the C APIs: handles are caller-owned, record pointers
are borrowed from the bank, and encode/disassemble outputs must be
released with `domsymas_free*()`. The interface instantiates an indexed
`uint32` array helper (`new/delete_sandstone_u32array`,
`sandstone_u32array_get/set`) for the `find_mnemonic`/`resolve`/`find_isel`
out-parameters.

```python
import sandstone as s
assert s.domsymas_probe_magic(open("x86.symas", "rb").read(3), 3) == 1
```
