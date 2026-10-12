# mlvm-as

AT&T assembly driver for the documented x86-64 subset, producing verified ELF64
relocatable objects through the shared machine encoder and object API. Supports
labels, section/data/alignment directives, strong/weak symbols and real relocation
records; unsupported instructions and targets are diagnosed. `-o FILE` is required.
See [TOOLS.md](../../docs/TOOLS.md) for exact encoding and syntax support.
