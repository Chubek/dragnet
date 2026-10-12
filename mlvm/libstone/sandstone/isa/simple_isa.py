"""Small, executable ISA descriptions in ``simple-isa/*.isa``.

This is a separate source of architecture data from the SymAS ``.dsymspec``
databanks. Encodings cover the explicitly listed instruction forms only.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
from typing import Mapping


class ISAError(ValueError):
    """An invalid spec or instruction operand."""


@dataclass(frozen=True)
class Register:
    name: str
    width: int
    number: int
    regclass: str


@dataclass(frozen=True)
class Field:
    source: str
    low: int
    bits: int
    source_low: int = 0


@dataclass(frozen=True)
class Encoding:
    name: str
    width: int | str | None
    base: int
    fields: tuple[Field, ...]
    directives: Mapping[str, str]


# Variable-width (amd64) encodings may carry immediates of a fixed size.
_IMM_DIRECTIVE = re.compile(r"imm(8|16|32|64)$")


@dataclass(frozen=True)
class Operation:
    name: str
    category: str
    encoding: Encoding
    operands: tuple[tuple[str, str], ...]
    syntax: str
    sp: tuple[str, ...] = ()


# Memory operand grammar, Intel order: "[base+index*scale+disp]".
_MEM_TERM = re.compile(r"(?:([A-Za-z]\w*)(?:\s*\*\s*(\d+))?|(-?0[xX][0-9a-fA-F]+|-?\d+))")


@dataclass
class ISA:
    name: str
    word_size: int
    addr_size: int
    endian: str
    align: int
    registers: dict[str, Register]
    aliases: dict[str, str]
    encodings: dict[str, Encoding]
    operations: dict[str, Operation]

    def register(self, name: str) -> Register:
        key = name.lower()
        key = self.aliases.get(key, key)
        try:
            return self.registers[key]
        except KeyError as exc:
            raise ISAError(f"{self.name}: unknown register {name!r}") from exc

    def operation(self, name: str) -> Operation:
        try:
            return self.operations[name.lower()]
        except KeyError as exc:
            raise ISAError(f"{self.name}: unknown operation {name!r}") from exc

    def render(self, name: str, **operands: int | str) -> str:
        op = self.operation(name)
        values = self._values(op, operands)
        if self.name in ("ppc32", "ppc64"):
            return op.syntax.format_map({n: values[n] if k == "GPR" else operands[n]
                                         for n, k in op.operands})
        return op.syntax.format_map(operands)

    def _values(self, op: Operation, operands: Mapping[str, int | str]) -> dict[str, int | str]:
        expected = {name for name, _ in op.operands}
        if set(operands) != expected:
            raise ISAError(f"{op.name}: expected operands {sorted(expected)}, got {sorted(operands)}")
        out: dict[str, int | str] = {}
        for name, kind in op.operands:
            value = operands[name]
            if kind in ("GPR", "FPR"):
                if not isinstance(value, str):
                    raise ISAError(f"{op.name}.{name}: expected a register name")
                reg = self.register(value)
                if reg.regclass != kind:
                    raise ISAError(f"{op.name}.{name}: expected {kind}, got {reg.regclass}")
                if self.name == "aarch64" and reg.number == 31:
                    sp_allowed = name in op.sp
                    if (reg.name == "sp") != sp_allowed:
                        raise ISAError(f"{op.name}.{name}: use {'sp' if sp_allowed else 'xzr'} for register 31")
                if self.name == "m68k" and op.name in ("move_l", "add_l", "sub_l") and not reg.name.startswith("d"):
                    raise ISAError(f"{op.name}.{name}: data register required")
                if self.name == "tricore" and op.name in ("add", "sub", "mov") and not reg.name.startswith("d"):
                    raise ISAError(f"{op.name}.{name}: data register required")
                if self.name == "nvptx" and op.name.endswith("_u32") and reg.width != 32:
                    raise ISAError(f"{op.name}.{name}: 32-bit PTX register required")
                out[name] = reg.number
            elif kind == "mem":
                if not isinstance(value, str) or not value.startswith("["):
                    raise ISAError(f"{op.name}.{name}: expected a [base+index*scale+disp] reference")
                _memory(self, op, name, value)
                out[name] = value
            elif kind in ("imm", "rel"):
                if not isinstance(value, int):
                    raise ISAError(f"{op.name}.{name}: expected an integer")
                out[name] = value
            else:
                raise ISAError(f"{op.name}: unknown operand type {kind}")
        return out

    def encode(self, name: str, **operands: int | str) -> bytes:
        op = self.operation(name)
        values = self._values(op, operands)
        enc = op.encoding
        if enc.width == "text":
            raise ISAError(f"{self.name}: {op.name} is text-only; use render()")
        if enc.width == "stream":
            return self._encode_stream(op, values)
        if enc.width is None:
            return self._encode_x86(op, values)
        word = enc.base
        occupied = 0
        used_bits: dict[str, int] = {}
        for field in enc.fields:
            mask = (1 << field.bits) - 1
            bits = mask << field.low
            if occupied & bits or enc.base & bits:
                raise ISAError(f"{enc.name}: overlapping encoding fields")
            occupied |= bits
            if field.source not in values:
                raise ISAError(f"{op.name}: encoding references unknown operand {field.source}")
            value = int(values[field.source])
            used_bits[field.source] = max(used_bits.get(field.source, 0), field.source_low + field.bits)
            word |= ((value >> field.source_low) & mask) << field.low
        for name, kind in op.operands:
            if kind not in ("imm", "rel") or name not in used_bits:
                continue
            bits = used_bits[name]
            value = int(values[name])
            signed = enc.directives.get("signed") == "1"
            minimum = -(1 << (bits - 1)) if signed else 0
            maximum = (1 << (bits - 1)) if signed else (1 << bits)
            if not minimum <= value < maximum:
                raise ISAError(f"{op.name}.{name}: value does not fit {bits} bits")
        if "alignment" in enc.directives:
            step = int(enc.directives["alignment"], 0)
            for n, k in op.operands:
                if k in ("rel", "imm") and int(values[n]) % step:
                    raise ISAError(f"{op.name}.{n}: displacement must be aligned to {step}")
        if word >= 1 << enc.width:
            raise ISAError(f"{enc.name}: encoding exceeds {enc.width} bits")
        return word.to_bytes(enc.width // 8, self.endian)

    def _encode_stream(self, op: Operation, values: Mapping[str, int | str]) -> bytes:
        out = bytearray()
        bit_limit = int(op.encoding.directives.get("leb_bits", "64"), 0)
        for kind, arg in re.findall(r"(byte|word|uleb|sleb)\((0[xX][0-9a-fA-F]+|\w+)\)",
                                    op.encoding.directives["sequence"]):
            value = int(values[arg]) if arg in values else int(arg, 0)
            if kind in ("byte", "word"):
                size = 1 if kind == "byte" else 4
                if not 0 <= value < 1 << (size * 8):
                    raise ISAError(f"{op.name}: {arg} does not fit {size * 8} bits")
                out.extend(value.to_bytes(size, self.endian))
            else:
                minimum = -(1 << (bit_limit - 1)) if kind == "sleb" else 0
                maximum = (1 << (bit_limit - 1)) if kind == "sleb" else (1 << bit_limit)
                if not minimum <= value < maximum:
                    raise ISAError(f"{op.name}: LEB128 value is out of range")
                while True:
                    octet = value & 0x7f
                    value >>= 7
                    more = (value != 0) if kind == "uleb" else (
                        not ((value == 0 and octet & 0x40 == 0) or
                             (value == -1 and octet & 0x40 != 0)))
                    out.append(octet | (0x80 if more else 0))
                    if not more:
                        break
        return bytes(out)

    def _encode_x86(self, op: Operation, values: Mapping[str, int | str]) -> bytes:
        enc = op.encoding
        d = enc.directives
        if self.name != "amd64":
            raise ISAError("variable width encodings require amd64")
        reg = rm = 0
        mem: tuple | None = None
        if "modrm" in d:
            parts = [x.strip() for x in d["modrm"].split(",")]
            if len(parts) != 2 or any(x not in values for x in parts):
                raise ISAError(f"{enc.name}: modrm requires a register and an r/m operand")
            if dict(op.operands).get(parts[0]) == "imm" and not 0 <= int(values[parts[0]]) <= 7:
                raise ISAError(f"{op.name}: ModR/M opcode selector must be in 0..7")
            if dict(op.operands).get(parts[1]) == "mem":
                reg, mem = int(values[parts[0]]), _memory(self, op, parts[1], values[parts[1]])
                rm = mem["rm"]
            else:
                reg, rm = (int(values[x]) for x in parts)
        opreg = int(values[d["opreg"]]) if "opreg" in d else 0
        rex = int(d.get("rex_w", "0")) << 3
        if reg >= 8:
            rex |= 1 << 2
        if mem is not None:
            if mem["base"] is not None and mem["base"] >= 8:
                rex |= 1
            if mem["index"] is not None and mem["index"] >= 8:
                rex |= 1 << 1
        elif rm >= 8:
            rex |= rm >> 3
        if opreg >= 8:
            rex |= 1
        out = bytearray()
        if "prefix" in d:
            out.extend(int(byte, 0) & 0xFF for byte in d["prefix"].split(","))
        if rex:
            out.append(0x40 | rex)
        opcode = int(d["opcode"], 0)
        if "opreg" in d:
            opcode += int(values[d["opreg"]]) & 7
        out.extend(opcode.to_bytes(max(1, (opcode.bit_length() + 7) // 8), "big"))
        if "modrm" in d:
            mod = (0xC0 if mem is None else mem["mod"] << 6)
            out.append(mod | ((reg & 7) << 3) | (rm & 7))
            if mem is not None:
                if mem["sib"]:
                    out.append(mem["scale"] << 6 | ((mem["index"] or 4) << 3) |
                               ((mem["base"] or 5) & 7))
                if mem["disp_bytes"] == 1:
                    out.append(mem["disp"] & 0xFF)
                elif mem["disp_bytes"] == 4:
                    out.extend((mem["disp"] & 0xFFFFFFFF).to_bytes(4, "little"))
        for directive in sorted(k for k in d if _IMM_DIRECTIVE.fullmatch(k)):
            size = int(_IMM_DIRECTIVE.fullmatch(directive).group(1)) // 8
            value = int(values[d[directive]])
            if not -(1 << (size * 8 - 1)) <= value < 1 << (size * 8):
                raise ISAError(f"{op.name}: {directive} value does not fit {size} bytes")
            out.extend(value.to_bytes(size, "little", signed=value < 0))
        if "rel32" in d:
            value = int(values[d["rel32"]])
            if not -(1 << 31) <= value < (1 << 31):
                raise ISAError(f"{op.name}: rel32 displacement out of range")
            out.extend(value.to_bytes(4, "little", signed=True))
        return bytes(out)


def _memory(isa: "ISA", op: Operation, operand: str, text: str) -> dict:
    """Decode an Intel-order memory reference into ModR/M, SIB and displacement."""
    if not text.endswith("]") or "]" in text[1:-1]:
        raise ISAError(f"{op.name}.{operand}: unbalanced memory reference {text!r}")
    body = text[1:-1]
    parts = [p.strip() for p in re.split(r"(?=[+-])", body) if p.strip()]
    base: int | None = None
    index: int | None = None
    scale: int | None = None
    disp = 0
    rip = False
    sign = 1
    for part in parts:
        if part[0] in "+-":
            sign = -1 if part[0] == "-" else 1
            part = part[1:].strip()
            if not part:
                continue
        match = _MEM_TERM.fullmatch(part)
        if match is None:
            raise ISAError(f"{op.name}.{operand}: bad memory term {part!r}")
        reg, factor, number = match.groups()
        if reg is not None:
            if reg.lower() == "rip":
                if sign < 0 or factor is not None or base is not None or index is not None:
                    raise ISAError(f"{op.name}.{operand}: rip may only stand alone as the base")
                rip = True
                continue
            value = _memory_register(isa, op, operand, reg)
            if factor is not None:
                if index is not None or value & 7 == 4:
                    raise ISAError(f"{op.name}.{operand}: the index needs a low GPR other than rsp")
                index, scale = value, int(factor)
            elif base is None:
                base = value
            elif index is None:
                index, scale = value, 1
            else:
                raise ISAError(f"{op.name}.{operand}: too many address registers")
        else:
            disp += sign * int(number, 0)
        sign = 1
    if not -(1 << 31) <= disp < (1 << 31):
        raise ISAError(f"{op.name}.{operand}: displacement out of range")
    if rip:
        return {"mod": 0, "rm": 5, "base": None, "index": None, "sib": False, "scale": 0,
                "disp": disp, "disp_bytes": 4}
    if index is not None and scale is None:
        scale = 1
    if scale is not None and scale not in (1, 2, 4, 8):
        raise ISAError(f"{op.name}.{operand}: scale must be 1, 2, 4 or 8")
    # A scaled index and rsp or r12 need a SIB byte, and rbp or r13 as the only
    # register always needs a displacement.
    if base is None and index is None:
        raise ISAError(f"{op.name}.{operand}: a 64-bit address needs a base, an index or rip")
    sib = index is not None or (base & 7) == 4
    if base is None:
        mod, rm, disp_bytes = 0, 4, 4
    elif disp == 0 and base & 7 != 5:
        mod, rm, disp_bytes = 0, (4 if sib else base), 0
    elif -(1 << 7) <= disp < (1 << 7):
        mod, rm, disp_bytes = 1, (4 if sib else base), 1
    else:
        mod, rm, disp_bytes = 2, (4 if sib else base), 4
    return {"mod": mod, "rm": rm, "base": base, "index": index, "sib": sib, "disp": disp,
            "disp_bytes": disp_bytes, "scale": (1, 2, 4, 8).index(scale or 1) if sib else 0}


def _memory_register(isa: "ISA", op: Operation, operand: str, name: str) -> int:
    if name.lower() == "rip":
        return 0
    reg = isa.register(name)
    if reg.regclass != "GPR":
        raise ISAError(f"{op.name}.{operand}: address registers must be GPR")
    if reg.number > 15:
        raise ISAError(f"{op.name}.{operand}: {name!r} has no address encoding")
    return reg.number


_BLOCK_START = re.compile(r"\b(arch|regclass|encoding|op)\s+([A-Za-z][\w.]*)\s*\{")
_REGISTER = re.compile(r"([A-Za-z][\w]*?)\s*\(\s*(\d+)\s*\)\s*=\s*(\d+)\s*,?\s*$")
_FIELD = re.compile(r"([A-Za-z]\w*)(?:\[(\d+):(\d+)\])?\s*=\s*(\d+):(\d+)$")
_PLACEHOLDER = re.compile(r"\{([A-Za-z]\w*)\}")


def _statements(body: str) -> dict[str, str]:
    result: dict[str, str] = {}
    items: list[str] = []
    start = 0
    quoted = False
    for pos, char in enumerate(body):
        if char == '"' and (pos == 0 or body[pos - 1] != "\\"):
            quoted = not quoted
        if char == ";" and not quoted:
            items.append(body[start:pos])
            start = pos + 1
    if quoted:
        raise ISAError("unterminated string")
    items.append(body[start:])
    for item in items:
        item = item.strip()
        if not item:
            continue
        if "=" not in item:
            raise ISAError(f"invalid assignment {item!r}")
        key, value = (part.strip() for part in item.split("=", 1))
        if key in result:
            raise ISAError(f"duplicate assignment {key}")
        result[key] = value
    return result


def parse(source: str) -> ISA:
    """Parse one populated .isa source, rejecting unknown and incomplete forms."""
    source = re.sub(r"(?m)^\s*#[^\n]*", "", source)
    matches: list[tuple[str, str, str]] = []
    residual = []
    cursor = 0
    while m := _BLOCK_START.search(source, cursor):
        residual.append(source[cursor:m.start()])
        depth = 1
        quote = False
        pos = m.end()
        start = pos
        while pos < len(source) and depth:
            ch = source[pos]
            if ch == '"' and (pos == 0 or source[pos - 1] != "\\"):
                quote = not quote
            elif not quote and ch == "{":
                depth += 1
            elif not quote and ch == "}":
                depth -= 1
            pos += 1
        if depth or quote:
            raise ISAError(f"unterminated {m.group(1)} {m.group(2)} block")
        matches.append((m.group(1), m.group(2), source[start:pos - 1]))
        cursor = pos
    residual.append(source[cursor:])
    remainder = "".join(residual)
    aliases: dict[str, str] = {}
    for match in re.finditer(r"\balias\s+(\w+)\s*=\s*(\w+)\s*;", remainder):
        key, target = match.groups()
        aliases[key.lower()] = target.lower()
    remainder = re.sub(r"\balias\s+\w+\s*=\s*\w+\s*;", "", remainder)
    if remainder.strip():
        raise ISAError(f"unrecognized ISA text: {remainder.strip()[:80]!r}")
    if not matches or matches[0][0] != "arch":
        raise ISAError("expected arch block first")
    arch = matches[0]
    settings = _statements(arch[2])
    if set(settings) != {"word_size", "addr_size", "endian", "align"}:
        raise ISAError("arch requires word_size, addr_size, endian, align")
    isa = ISA(arch[1], int(settings["word_size"]), int(settings["addr_size"]),
              settings["endian"], int(settings["align"]), {}, aliases, {}, {})
    if isa.endian not in ("little", "big") or isa.align <= 0:
        raise ISAError("invalid arch endian or alignment")
    for block in matches[1:]:
        typ, name, body = block
        if typ == "arch":
            raise ISAError("multiple arch blocks")
        if typ == "regclass":
            if name not in ("GPR", "FPR"):
                raise ISAError(f"unknown register class {name}")
            for line in body.splitlines():
                line = line.strip()
                if not line:
                    continue
                m = _REGISTER.fullmatch(line)
                if not m:
                    raise ISAError(f"invalid register {line!r}")
                regname, width, number = m.groups()
                key = regname.lower()
                if key in isa.registers:
                    raise ISAError(f"duplicate register {regname}")
                isa.registers[key] = Register(regname, int(width), int(number), name)
        elif typ == "encoding":
            props = _statements(body)
            width_text = props.pop("width", None)
            if width_text is None:
                raise ISAError(f"{name}: missing width")
            width = (None if width_text == "variable" else width_text
                     if width_text in ("text", "stream") else int(width_text))
            if width not in (None, "text", "stream", 16, 32, 48, 64):
                raise ISAError(f"{name}: unsupported width")
            base = int(props.pop("base", "0"), 0)
            fields = []
            for key in list(props):
                m = _FIELD.fullmatch(key + " = " + props[key])
                if m:
                    src, hi, lo, pos, bits = m.groups()
                    source_low = int(lo) if lo is not None else 0
                    if hi is not None and int(hi) - source_low + 1 != int(bits):
                        raise ISAError(f"{name}: slice width mismatch: {key}")
                    fields.append(Field(src, int(pos), int(bits), source_low))
                    del props[key]
            if width == "text":
                if fields or props or base:
                    raise ISAError(f"{name}: text encoding cannot contain binary fields")
            elif width == "stream":
                if fields or base or set(props) - {"sequence", "leb_bits"} or "sequence" not in props:
                    raise ISAError(f"{name}: stream encoding requires sequence")
                if props.get("leb_bits", "64") not in ("32", "64"):
                    raise ISAError(f"{name}: leb_bits must be 32 or 64")
                pattern = r"(?:byte|word|uleb|sleb)\((?:0[xX][0-9a-fA-F]+|\w+)\)"
                if not re.fullmatch(pattern + r"(?:\s*,\s*" + pattern + r")*", props["sequence"]):
                    raise ISAError(f"{name}: invalid stream sequence")
            elif width is not None:
                if any(k not in ("alignment", "signed", "unsigned") for k in props):
                    raise ISAError(f"{name}: unknown directives {sorted(props)}")
                if (props.get("signed", "0") not in ("0", "1") or
                        props.get("unsigned", "0") not in ("0", "1") or
                        (props.get("signed") == "1" and props.get("unsigned") == "1")):
                    raise ISAError(f"{name}: invalid signedness")
                if "alignment" in props and int(props["alignment"], 0) <= 0:
                    raise ISAError(f"{name}: alignment must be positive")
                mask = 0
                for f in fields:
                    part = ((1 << f.bits) - 1) << f.low
                    if f.low < 0 or f.bits < 1 or f.low + f.bits > width or mask & part or base & part:
                        raise ISAError(f"{name}: invalid or overlapping fields")
                    mask |= part
                if base >= 1 << width:
                    raise ISAError(f"{name}: base does not fit")
            elif (isa.name != "amd64" or fields or
                  set(props) - {"prefix", "rex_w", "opcode", "modrm", "opreg", "rel32"} -
                  {k for k in props if _IMM_DIRECTIVE.fullmatch(k)} or
                  "opcode" not in props):
                raise ISAError(f"{name}: invalid x86 directives")
            elif (int(props["opcode"], 0) < 0 or
                  ("prefix" in props and (
                      any(not 0 <= int(byte, 0) <= 255
                          for byte in props["prefix"].split(",")) or
                      not all(re.fullmatch(r"\s*(?:0[xX][0-9a-fA-F]+|\d+)\s*", byte)
                              for byte in props["prefix"].split(",")))) or
                  props.get("rex_w", "0") not in ("0", "1")):
                raise ISAError(f"{name}: invalid x86 opcode or prefix")
            if name in isa.encodings:
                raise ISAError(f"duplicate encoding {name}")
            isa.encodings[name] = Encoding(name, width, base, tuple(fields), props)
        else:
            props = _statements(body)
            if not {"class", "encoding", "operands", "syntax"} <= set(props) or \
                    set(props) - {"class", "encoding", "operands", "syntax", "sp"}:
                raise ISAError(f"{name}: requires class, encoding, operands, syntax")
            enc = isa.encodings.get(props["encoding"])
            if enc is None:
                raise ISAError(f"{name}: unknown encoding {props['encoding']}")
            args = []
            if props["operands"]:
                for arg in props["operands"].split(","):
                    m = re.fullmatch(r"\s*(\w+)\s*:\s*(GPR|FPR|imm|rel|mem)\s*", arg)
                    if not m:
                        raise ISAError(f"{name}: invalid operand {arg!r}")
                    args.append(m.groups())
            syntax = props["syntax"]
            if not (syntax.startswith('"') and syntax.endswith('"')):
                raise ISAError(f"{name}: syntax must be quoted")
            syntax = syntax[1:-1]
            args_set = {n for n, _ in args}
            if len(args_set) != len(args) or set(_PLACEHOLDER.findall(syntax)) != args_set:
                raise ISAError(f"{name}: syntax placeholders must match operands")
            if any(f.source not in args_set for f in enc.fields):
                raise ISAError(f"{name}: encoding fields must name operands")
            if enc.width == "stream":
                references = re.findall(r"(?:byte|word|uleb|sleb)\((\w+)\)",
                                        enc.directives["sequence"])
                if any(ref not in args_set and not ref.isdigit() and
                       not ref.lower().startswith("0x") for ref in references):
                    raise ISAError(f"{name}: stream references unknown operand")
            for directive, target in list(enc.directives.items()) + list(props.items()):
                if directive not in ("modrm", "opreg", "rel32", "sp") and \
                        not _IMM_DIRECTIVE.fullmatch(directive):
                    continue
                if directive == "sp":
                    named = [n.strip() for n in target.split(",") if n.strip()]
                else:
                    named = [n.strip() for n in target.split(",")]
                if any(n not in args_set for n in named):
                    raise ISAError(f"{name}: {directive} names an unknown operand")
                if directive == "sp" and any(k != "GPR" for n, k in args if n in named):
                    raise ISAError(f"{name}: sp applies to GPR operands only")
            if enc.width is None and "modrm" in enc.directives:
                parts = [x.strip() for x in enc.directives["modrm"].split(",")]
                kinds = dict(args)
                if len(parts) != 2 or any(p not in kinds for p in parts):
                    raise ISAError(f"{name}: modrm needs a register and an r/m operand")
                if kinds[parts[0]] not in ("GPR", "FPR", "imm") or \
                        kinds[parts[1]] not in ("GPR", "FPR", "mem"):
                    raise ISAError(f"{name}: modrm requires a register/selector and a register or mem")
            if name in isa.operations:
                raise ISAError(f"duplicate operation {name}")
            stack = tuple(n.strip() for n in props.get("sp", "").split(",") if n.strip())
            isa.operations[name] = Operation(name, props["class"], enc, tuple(args),
                                              syntax, stack)
    for key, target in aliases.items():
        if key in isa.registers or target not in isa.registers:
            raise ISAError(f"invalid alias {key} = {target}")
    return isa


def load(path: str | Path) -> ISA:
    """Load an ISA by path; empty placeholder files produce a clear error."""
    path = Path(path)
    try:
        return parse(path.read_text(encoding="utf-8"))
    except ISAError as exc:
        raise ISAError(f"{path}: {exc}") from exc


def available(directory: str | Path | None = None) -> dict[str, Path]:
    directory = Path(directory) if directory else Path(__file__).with_name("simple-isa")
    return {p.stem: p for p in sorted(directory.glob("*.isa"))
            if re.search(r"(?m)^arch\s+\w+\s*\{", p.read_text(encoding="utf-8"))}


def load_arch(name: str, directory: str | Path | None = None) -> ISA:
    try:
        return load(available(directory)[name])
    except KeyError as exc:
        raise ISAError(f"no populated simple ISA named {name!r}") from exc


def main() -> None:
    cli = argparse.ArgumentParser(description="Inspect and validate simple ISA specifications")
    cli.add_argument("file", nargs="?", help="path to a .isa file")
    cli.add_argument("--list", action="store_true", help="list populated bundled specs")
    args = cli.parse_args()
    if args.list:
        print("\n".join(available()))
    elif args.file:
        isa = load(args.file)
        print(f"{isa.name}: {len(isa.registers)} registers, {len(isa.operations)} operations")
    else:
        cli.error("provide a .isa file or --list")


if __name__ == "__main__":
    main()
