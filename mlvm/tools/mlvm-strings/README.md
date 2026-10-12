# mlvm-strings

Scans complete files for printable ASCII code points (32..126), including final
EOF runs. Library-backed command options:

- `-n LENGTH` / `--bytes=LENGTH`: minimum code points, default 4.
- `-t d|o|x`: byte offsets; `-f`: file-name prefixes.
- `-e s|l|b`: ASCII or aligned UTF-16LE/BE ASCII code points, not general Unicode.

Uses shared streaming string scanning, bounded I/O and vendored klib parsing.
See [TOOLS.md](../../docs/TOOLS.md) for CLI/build contracts.
