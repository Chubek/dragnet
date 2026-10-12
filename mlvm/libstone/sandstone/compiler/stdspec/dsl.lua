-- dsl.lua — the DomSymAsCC specification DSL (run under DomMune, a Lua 5.4
-- subset). Each `.dsymspec` file requires this module, obtains a fresh
-- builder via `new()`, describes an assembler target, and prints the JSON
-- document returned by `emit()`.
--
--   local symas = require("dsl").new()
--   symas.target{ arch = "riscv64", ... }
--   symas.register{ name = "x0", class = "gpr", width = 64, enc = 0 }
--   ...
--   print(symas.emit())
--
-- The emitted JSON document is consumed by the C++ compiler
-- (`domlibs/domsymas/compiler/src/spec_document.cpp`) and mapped onto
-- `domsymas::Builder`. The schema is documented in `stdspec/README.md`.
--
-- Portability: restricted to the DomMune builtin subset — no `string.byte`,
-- `string.char`, string patterns, coroutines, `goto`, or comparator-based
-- `table.sort`. Tables are built with positional fields / `table.insert`
-- (array part) and `key = value` (hash part); `#` counts the array part.
--
-- Method binding: builder methods are plain functions `Builder.<name>(st, ...)`
-- over per-instance state; `new()` returns a facade whose methods close over
-- that state, so `symas.target{...}` works with dot-call syntax.

local M = {}

-- ---------------------------------------------------------------------------
-- Small utilities
-- ---------------------------------------------------------------------------

-- Tag a table as a JSON array (metatable marker; `pairs` ignores metatables).
local function array(t)
    t = t or {}
    return setmetatable(t, { __symas_array = true })
end

local function is_array(t)
    local mt = getmetatable(t)
    return mt ~= nil and rawget(mt, "__symas_array") == true
end

-- String lists used to reject typos early (schema validation on the DSL side).
local OP_KINDS = {
    reg = true, imm = true, disp = true, mem = true, abs = true,
    rel = true, sym = true, cond = true, reglist = true, implicit = true,
}
local REG_CLASSES = {
    gpr = true, seg = true, ctrl = true, debug = true, vec = true,
    mask = true, fp = true, status = true, special = true,
}
local REG_FLAGS = {
    readable = true, writable = true, imp_used = true, imp_mod = true, modedep = true,
}
local FORM_FLAGS = {
    privileged = true, branch = true, cond_branch = true, ["return"] = true,
    terminator = true, may_trap = true, reads_mem = true, writes_mem = true,
    delayed = true, implicit_ops = true, reloc = true, aliases = true,
}
local SECTION_FLAGS = {
    exec = true, write = true, alloc = true, readonly = true, zerofill = true,
    merge = true, discard = true, tls = true, debug = true, code = true,
    data = true, meta = true,
}
local SECTION_CONTENT = { unknown = true, code = true, data = true, meta = true }
local BINDINGS = {
    ["local"] = true, global = true, weak = true, undef = true,
    abs = true, common = true, tls = true,
}
local VISIBILITIES = { default = true, hidden = true, protected = true }
local SYMTYPES = { none = true, object = true, func = true, section = true }
local ADDENDS = { explicit = true, infield = true, implied = true }
local OVERFLOWS = { error = true, truncate = true }
local ACCESS = { read = true, write = true, readwrite = true }
local BYTE_ORDERS = { little = true, big = true, le = true, be = true }

-- Optimization sections (format 1.1+): instruction selection, scheduling,
-- functional units, machine models, per-model tuning, pair fusion.
local ISEL_KINDS = {
    node = true, pattern = true, pseudo = true, expansion = true, select = true,
    postisel = true, copy = true, aggressive = true,
}
local ISEL_FLAGS = {
    prefer = true, expand = true, needs_expand = true, variadic = true,
    commutes = true, chain = true, side_effects = true, unprofitable = true,
    convergent = true, tied = true,
}
local SCHED_FLAGS = {
    variadic = true, multicycle = true, serializing = true, barrier = true,
    may_fault = true, side_effects = true, fused = true, unmodeled = true, nop = true,
}
local UNIT_KINDS = {
    other = true, int = true, fp = true, load = true, store = true, branch = true,
    vector = true, mask = true, divider = true, mul = true, complex = true,
    pipe = true, system = true,
}
local UNIT_FLAGS = {
    shared = true, superscalar = true, pipelined = true, noncoincident = true,
}
local MODEL_FLAGS = {
    in_order = true, post_ra = true, complete = true, prefer_post_ram = true,
    no_itineraries = true, vliw = true, predicable = true, morphable = true,
    has_stalls = true, forwarding = true, out_of_order = true, bundle_width = true,
    default = true,
}
local TUNE_KINDS = {
    generic = true, latency = true, uops = true, cost = true, prefer = true,
    avoid = true, threshold = true, unroll = true, vec_width = true, pair = true,
    strategy = true, preroll = true, split = true,
}
local TUNE_FLAGS = {
    only_if = true, override = true, experimental = true, disable = true,
    negative = true,
}
local FUSION_KINDS = {
    macro = true, cmp_branch = true, branch_pair = true, mem_arith = true,
    mul_add = true, shift = true, mov = true, load_op = true, cmp_select = true,
    vsetvli = true, target = true,
}
local FUSION_FLAGS = {
    profitable = true, requires_flags = true, same_reg = true, adjacent = true,
    lock_eligible = true, suppress_unfuse = true, bidirectional = true,
}

local function fail(msg)
    error("dsl: " .. msg, 2)
end

local function check_set(name, value, set)
    if value == nil then return end
    if set[value] == nil then fail(name .. " has invalid value '" .. tostring(value) .. "'") end
end

local function as_list(value, name)
    if value == nil then return array() end
    if type(value) == "string" then return array { value } end
    if type(value) == "table" then
        local out = array()
        for i = 1, #value do out[#out + 1] = value[i] end
        return out
    end
    fail(name .. " must be a string or list")
end

local function flag_list(flags)
    local out = array()
    local seen = {}
    if flags == nil then return out end
    if type(flags) == "string" then flags = { flags } end
    for i = 1, #flags do
        local f = flags[i]
        if f ~= nil and not seen[f] then
            seen[f] = true
            out[#out + 1] = f
        end
    end
    return out
end

-- ---------------------------------------------------------------------------
-- JSON emitter
-- ---------------------------------------------------------------------------

local function esc(s)
    local out = {}
    local n = #s
    for i = 1, n do
        local c = string.sub(s, i, i)
        if c == '"' then
            out[#out + 1] = '\\"'
        elseif c == "\\" then
            out[#out + 1] = "\\\\"
        elseif c == "\n" then
            out[#out + 1] = "\\n"
        elseif c == "\t" then
            out[#out + 1] = "\\t"
        elseif c == "\r" then
            out[#out + 1] = "\\r"
        else
            out[#out + 1] = c
        end
    end
    return table.concat(out)
end

local function num_to_json(v)
    return tostring(v) -- DomMune appends ".0" for integral floats; Lua does too
end

local function to_json(v, out)
    if v == nil then
        out[#out + 1] = "null"
    elseif type(v) == "boolean" then
        out[#out + 1] = (v and "true" or "false")
    elseif type(v) == "number" then
        out[#out + 1] = num_to_json(v)
    elseif type(v) == "string" then
        out[#out + 1] = '"' .. esc(v) .. '"'
    elseif type(v) == "table" then
        if is_array(v) then
            out[#out + 1] = "["
            local n = #v
            for i = 1, n do
                if i > 1 then out[#out + 1] = "," end
                to_json(v[i], out)
            end
            out[#out + 1] = "]"
        else
            out[#out + 1] = "{"
            local first = true
            for k, val in pairs(v) do
                if not first then out[#out + 1] = "," end
                first = false
                out[#out + 1] = '"' .. esc(k) .. '":'
                to_json(val, out)
            end
            out[#out + 1] = "}"
        end
    else
        fail("cannot serialize a " .. type(v) .. " value")
    end
    return out
end

local function emit_value(v)
    return table.concat(to_json(v, {}))
end

-- ---------------------------------------------------------------------------
-- Builder methods (plain functions over per-instance state `st`)
-- ---------------------------------------------------------------------------

local Builder = {}

function Builder.target(st, t)
    t = t or {}
    if not t.arch then fail("target.arch is required") end
    check_set("target.byte_order", t.byte_order, BYTE_ORDERS)
    st.targets[#st.targets + 1] = {
        arch = t.arch,
        subarch = t.subarch or "",
        addr_size = t.addr_size or 0,
        op_size = t.op_size or 0,
        addr_def = t.addr_def or 0,
        align = t.align or 1,
        byte_order = t.byte_order or "little",
        obj_format = t.obj_format or "",
        abi = t.abi or "",
        calling_conv = t.calling_conv or "",
        features = flag_list(t.features),
    }
end

function Builder.feature(st, name, desc, flags)
    if type(name) == "table" then
        local t = name
        name = t.name
        desc = t.desc or ""
        flags = t.flags or 0
    end
    if not name then fail("feature name is required") end
    st.features[#st.features + 1] = {
        name = name,
        desc = desc or "",
        flags = flags or 0,
    }
end

function Builder.register(st, r)
    r = r or {}
    if not r.name then fail("register.name is required") end
    check_set("register.class", r.class, REG_CLASSES)
    for _, f in ipairs(flag_list(r.flags)) do
        check_set("register.flags", f, REG_FLAGS)
    end
    st.registers[#st.registers + 1] = {
        name = r.name,
        class = r.class or "gpr",
        width = r.width or 0,
        enc = r.enc or r.enc_num or 0,
        parent = r.parent or nil,
        sub_lo = r.sub_lo or 0,
        sub_hi = r.sub_hi or 0,
        flags = flag_list(r.flags),
    }
end

function Builder.mnemonic(st, name, aliases, ci)
    if type(name) == "table" then
        local t = name
        aliases = t.aliases
        ci = t.ci
        name = t.name
    end
    if not name then fail("mnemonic name is required") end
    st.mnemonics[#st.mnemonics + 1] = {
        name = name,
        aliases = as_list(aliases, "mnemonic.aliases"),
        ci = (ci == true),
    }
end

function Builder.encoder(st, e)
    e = e or {}
    if not e.name then fail("encoder.name is required") end
    if not e.source then fail("encoder.source is required") end
    st.encoders[#st.encoders + 1] = {
        name = e.name,
        source = e.source,
        flags = e.flags or 0,
        max_bytes = e.max_bytes or 64,
        max_relocs = e.max_relocs or 8,
    }
end

function Builder.form(st, f)
    f = f or {}
    if not f.mnemonic then fail("form.mnemonic is required") end
    if not f.encoder then fail("form.encoder is required") end
    local operands = array()
    local ops = f.operands or {}
    for i = 1, #ops do
        local o = ops[i] or {}
        check_set("operand.kind", o.kind, OP_KINDS)
        if o.kind == "reg" and o.class then
            check_set("operand.class", o.class, REG_CLASSES)
        end
        check_set("operand.access", o.access, ACCESS)
        operands[#operands + 1] = {
            kind = o.kind,
            width = o.width or 0,
            class = o.class or "",
            imm_min = o.imm_min or 0,
            imm_max = o.imm_max or 0,
            signed = (o.signed == true),
            role = o.role or o.enc_role or 0,
            access = o.access or "read",
            hidden = (o.hidden == true),
            constraint = o.constraint or "",
        }
    end
    for _, fl in ipairs(flag_list(f.flags)) do
        check_set("form.flags", fl, FORM_FLAGS)
    end
    st.forms[#st.forms + 1] = {
        mnemonic = f.mnemonic,
        ops = f.ops or #operands,
        flags = flag_list(f.flags),
        encoder = f.encoder,
        required = as_list(f.required, "form.required"),
        forbidden = as_list(f.forbidden, "form.forbidden"),
        syntaxes = as_list(f.syntaxes, "form.syntaxes"),
        operands = operands,
    }
end

function Builder.syntax(st, s)
    s = s or {}
    if not s.name then fail("syntax.name is required") end
    st.syntaxes[#st.syntaxes + 1] = {
        name = s.name,
        order = s.order or 0,
        reg_prefix = s.reg_prefix or "",
        imm_prefix = s.imm_prefix or "",
        comment = s.comment or "",
        separator = s.separator or ", ",
        tmpl = s.tmpl or "{mnemonic} {ops}",
        ci = (s.ci == true),
    }
end

function Builder.section(st, s)
    s = s or {}
    if not s.name then fail("section.name is required") end
    for _, fl in ipairs(flag_list(s.flags)) do
        check_set("section.flags", fl, SECTION_FLAGS)
    end
    check_set("section.content", s.content, SECTION_CONTENT)
    st.sections[#st.sections + 1] = {
        name = s.name,
        flags = flag_list(s.flags),
        align = s.align or 1,
        addrspace = s.addrspace or 0,
        content = s.content or "unknown",
    }
end

function Builder.linkage(st, l)
    l = l or {}
    if not l.name then fail("linkage.name is required") end
    check_set("linkage.binding", l.binding, BINDINGS)
    check_set("linkage.visibility", l.visibility, VISIBILITIES)
    check_set("linkage.symtype", l.symtype, SYMTYPES)
    st.linkages[#st.linkages + 1] = {
        name = l.name,
        binding = l.binding or "global",
        visibility = l.visibility or "default",
        symtype = l.symtype or "none",
        flags = l.flags or 0,
    }
end

function Builder.reloc(st, r)
    r = r or {}
    if not r.name then fail("reloc.name is required") end
    check_set("reloc.addend", r.addend, ADDENDS)
    check_set("reloc.overflow", r.overflow, OVERFLOWS)
    st.relocs[#st.relocs + 1] = {
        name = r.name,
        width = r.width or 0,
        offset = r.offset or 0,
        signed = (r.signed == true),
        pcrel = (r.pcrel == true),
        overflow = r.overflow or "error",
        addend = r.addend or "explicit",
        expr = r.expr or "",
    }
end

function Builder.debug(st, msg, kind, ref)
    if type(msg) == "table" then
        local t = msg
        kind = t.kind
        ref = t.ref
        msg = t.msg
    end
    st.debug[#st.debug + 1] = {
        msg = msg or "",
        kind = kind or "form",
        ref = ref or "",
    }
end

-- ---------------------------------------------------------------------------
-- Optimization sections (ISEL / SCHED / UNITS / MODELS / TUNING / FUSION)
--
-- Form references are strings resolved by the C++ compiler:
--   "add"            every form of the mnemonic `add`
--   "add#2"          the second form of that mnemonic (1-based)
--   "add/enc_add"    the form of that mnemonic that uses encoder `enc_add`
-- `sched` and `tune` expand to every form a reference matches; `fuse` emits
-- one record per (a, b) combination.
-- ---------------------------------------------------------------------------

function Builder.model(st, m)
    m = m or {}
    if not m.name then fail("model.name is required") end
    for _, fl in ipairs(flag_list(m.flags)) do
        check_set("model.flags", fl, MODEL_FLAGS)
    end
    st.models[#st.models + 1] = {
        name = m.name,
        vendor = m.vendor or "",
        issue_width = m.issue_width or 1,
        rob = m.rob or m.micro_op_buffer or 0,
        loop_buffer = m.loop_buffer or 0,
        load_latency = m.load_latency or 0,
        mispredict = m.mispredict or m.mispredict_penalty or 0,
        high_latency = m.high_latency or 0,
        high_uops = m.high_uops or 0,
        flags = flag_list(m.flags),
        features = as_list(m.features, "model.features"),
        tune = as_list(m.tune, "model.tune"),
        desc = m.desc or "",
    }
end

function Builder.unit(st, u)
    u = u or {}
    if not u.name then fail("unit.name is required") end
    check_set("unit.kind", u.kind, UNIT_KINDS)
    for _, fl in ipairs(flag_list(u.flags)) do
        check_set("unit.flags", fl, UNIT_FLAGS)
    end
    st.units[#st.units + 1] = {
        name = u.name,
        model = u.model or "",
        kind = u.kind or "other",
        count = u.count or 1,
        latency = u.latency or 0,
        throughput = u.throughput or 1,
        flags = flag_list(u.flags),
    }
end

function Builder.isel(st, i)
    i = i or {}
    if not i.node then fail("isel.node is required") end
    if not i.form then fail("isel.form is required") end
    check_set("isel.kind", i.kind, ISEL_KINDS)
    for _, fl in ipairs(flag_list(i.flags)) do
        check_set("isel.flags", fl, ISEL_FLAGS)
    end
    st.isels[#st.isels + 1] = {
        node = i.node,
        kind = i.kind or "node",
        form = as_list(i.form, "isel.form"),
        cost = i.cost or 0,
        complexity = i.complexity or 0,
        priority = i.priority or 0,
        flags = flag_list(i.flags),
        required = as_list(i.required, "isel.required"),
        forbidden = as_list(i.forbidden, "isel.forbidden"),
        alts = as_list(i.alts, "isel.alts"),
        pattern = i.pattern or "",
    }
end

function Builder.sched(st, s)
    s = s or {}
    if not s.form then fail("sched.form is required") end
    for _, fl in ipairs(flag_list(s.flags)) do
        check_set("sched.flags", fl, SCHED_FLAGS)
    end
    st.scheds[#st.scheds + 1] = {
        form = as_list(s.form, "sched.form"),
        name = s.name or "",
        latency = s.latency or 0,
        uops = s.uops or 1,
        cycles = s.cycles or 0,
        flags = flag_list(s.flags),
        writes = as_list(s.writes, "sched.writes"),
        reads = as_list(s.reads, "sched.reads"),
    }
end

function Builder.tune(st, t)
    t = t or {}
    if not t.model then fail("tune.model is required") end
    check_set("tune.kind", t.kind, TUNE_KINDS)
    for _, fl in ipairs(flag_list(t.flags)) do
        check_set("tune.flags", fl, TUNE_FLAGS)
    end
    st.tunes[#st.tunes + 1] = {
        model = t.model,
        kind = t.kind or "generic",
        name = t.name or t.kind or "generic",
        form = as_list(t.form or "", "tune.form"),
        form2 = as_list(t.form2 or "", "tune.form2"),
        value = t.value or 0,
        flags = flag_list(t.flags),
        features = as_list(t.features, "tune.features"),
        desc = t.desc or "",
    }
end

function Builder.fuse(st, f)
    f = f or {}
    if not f.name then fail("fuse.name is required") end
    if not f.a or not f.b then fail("fuse.a and fuse.b are required") end
    check_set("fuse.kind", f.kind, FUSION_KINDS)
    for _, fl in ipairs(flag_list(f.flags)) do
        check_set("fuse.flags", fl, FUSION_FLAGS)
    end
    st.fusions[#st.fusions + 1] = {
        name = f.name,
        a = as_list(f.a, "fuse.a"),
        b = as_list(f.b, "fuse.b"),
        kind = f.kind or "macro",
        latency = f.latency or 0,
        flags = flag_list(f.flags),
        features = as_list(f.features, "fuse.features"),
        pattern = f.pattern or "",
    }
end

-- ---------------------------------------------------------------------------
-- Referential integrity + emission
-- ---------------------------------------------------------------------------

local function set_of(records)
    local s = {}
    for i = 1, #records do s[records[i].name] = true end
    return s
end

function Builder.check(st)
    local features = set_of(st.features)
    local registers = set_of(st.registers)
    local mnemonics = set_of(st.mnemonics)
    local encoders = set_of(st.encoders)
    local syntaxes = set_of(st.syntaxes)
    local models = set_of(st.models)
    local units = set_of(st.units)

    for _, t in ipairs(st.targets) do
        for _, f in ipairs(t.features) do
            if not features[f] then fail("target references unknown feature '" .. f .. "'") end
        end
    end
    for _, r in ipairs(st.registers) do
        if r.parent and not registers[r.parent] then
            fail("register '" .. r.name .. "' has unknown parent '" .. r.parent .. "'")
        end
    end
    for _, f in ipairs(st.forms) do
        if not mnemonics[f.mnemonic] then fail("form references unknown mnemonic '" .. f.mnemonic .. "'") end
        if not encoders[f.encoder] then fail("form references unknown encoder '" .. f.encoder .. "'") end
        for _, x in ipairs(f.required) do
            if not features[x] then fail("form required feature '" .. x .. "' unknown") end
        end
        for _, x in ipairs(f.forbidden) do
            if not features[x] then fail("form forbidden feature '" .. x .. "' unknown") end
        end
        for _, x in ipairs(f.syntaxes) do
            if not syntaxes[x] then fail("form references unknown syntax '" .. x .. "'") end
        end
    end
    -- Optimization records reference mnemonics, encoders, models, units and
    -- features by name; the mnemonic is the part before '#' or '/'.
    local function check_form_ref(ref, what)
        if ref == nil or ref == "" then return end
        if type(ref) == "table" then
            for i = 1, #ref do check_form_ref(ref[i], what) end
            return
        end
        local base = ref
        for _, sep in ipairs({ "#", "/" }) do
            local at = string.find(base, sep, 1, true)
            if at then base = string.sub(base, 1, at - 1) end
        end
        if not mnemonics[base] then
            fail(what .. " references unknown mnemonic '" .. base .. "'")
        end
    end
    for _, m in ipairs(st.models) do
        for _, f in ipairs(m.features) do
            if not features[f] then
                fail("model '" .. m.name .. "' references unknown feature '" .. f .. "'")
            end
        end
        for _, f in ipairs(m.tune) do
            if not features[f] then
                fail("model '" .. m.name .. "' references unknown tuning feature '" .. f .. "'")
            end
        end
    end
    for _, u in ipairs(st.units) do
        if u.model ~= "" and not models[u.model] then
            fail("unit '" .. u.name .. "' references unknown model '" .. u.model .. "'")
        end
    end
    for _, i in ipairs(st.isels) do
        check_form_ref(i.form, "isel '" .. i.node .. "'")
        for _, x in ipairs(i.alts) do check_form_ref(x, "isel '" .. i.node .. "' alternative") end
        for _, x in ipairs(i.required) do
            if not features[x] then fail("isel '" .. i.node .. "' requires unknown feature '" .. x .. "'") end
        end
        for _, x in ipairs(i.forbidden) do
            if not features[x] then fail("isel '" .. i.node .. "' forbids unknown feature '" .. x .. "'") end
        end
    end
    for _, s in ipairs(st.scheds) do
        check_form_ref(s.form, "sched '" .. s.name .. "'")
        for _, u in ipairs(s.writes) do
            if not units[u] then fail("sched '" .. s.name .. "' writes unknown unit '" .. u .. "'") end
        end
        for _, u in ipairs(s.reads) do
            if not units[u] then fail("sched '" .. s.name .. "' reads unknown unit '" .. u .. "'") end
        end
    end
    for _, t in ipairs(st.tunes) do
        if not models[t.model] then fail("tune references unknown model '" .. t.model .. "'") end
        check_form_ref(t.form, "tune '" .. t.name .. "'")
        check_form_ref(t.form2, "tune '" .. t.name .. "' second form")
        for _, x in ipairs(t.features) do
            if not features[x] then fail("tune '" .. t.name .. "' references unknown feature '" .. x .. "'") end
        end
    end
    for _, f in ipairs(st.fusions) do
        check_form_ref(f.a, "fusion '" .. f.name .. "'")
        check_form_ref(f.b, "fusion '" .. f.name .. "'")
        for _, x in ipairs(f.features) do
            if not features[x] then fail("fusion '" .. f.name .. "' references unknown feature '" .. x .. "'") end
        end
    end
    return true
end

function Builder.emit(st)
    Builder.check(st)
    local doc = {
        schema = 1,
        targets = st.targets,
        features = st.features,
        registers = st.registers,
        mnemonics = st.mnemonics,
        encoders = st.encoders,
        forms = st.forms,
        syntaxes = st.syntaxes,
        sections = st.sections,
        linkages = st.linkages,
        relocs = st.relocs,
        models = st.models,
        units = st.units,
        isels = st.isels,
        scheds = st.scheds,
        tunes = st.tunes,
        fusions = st.fusions,
        debug = st.debug,
    }
    return emit_value(doc)
end

local METHOD_NAMES = {
    "target", "feature", "register", "mnemonic", "encoder", "form",
    "syntax", "section", "linkage", "reloc", "debug", "check", "emit",
    "model", "unit", "isel", "sched", "tune", "fuse",
}

function M.new()
    local st = {
        targets = array(), features = array(), registers = array(),
        mnemonics = array(), encoders = array(), forms = array(),
        syntaxes = array(), sections = array(), linkages = array(),
        relocs = array(), debug = array(),
        models = array(), units = array(), isels = array(), scheds = array(),
        tunes = array(), fusions = array(),
    }
    local self = {}
    for _, name in ipairs(METHOD_NAMES) do
        self[name] = function(...)
            return Builder[name](st, ...)
        end
    end
    return self
end

M.array = array

return M
