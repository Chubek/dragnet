"""Exercise the generated module and independent C-handle ownership."""
import copy
import json
import pathlib
import sys
import limestone as l

fixtures = pathlib.Path(sys.argv[1])
memory = l.limestone_memory_access()
memory.address_space = "heap"
memory.address_space = "private-frame"
assert memory.address_space == "private-frame"
memory.address_space = None
assert memory.address_space is None
memory.set_alias_sets([17, 23])
assert memory.alias_count == 2 and memory._alias_array[1] == 23
memory.set_alias_sets([])
assert memory.alias_count == 0
del memory
error = l.limestone_error()
status, value = l.limestone_module_instruction_id(None, 0, error)
assert status == l.LIMESTONE_INVALID_ARGUMENT and value == 0
status, value, physical = l.limestone_assignment_at(None, 0, error)
assert status == l.LIMESTONE_INVALID_ARGUMENT and value == physical == 0
slot = l.limestone_spill_slot()
try:
    slot.register_class = "G"
    raise AssertionError("assigned a borrowed output string")
except AttributeError:
    pass
with l.compile_source("((lambda x (add x 2)) 40)") as module:
    assert "#42" in module.text
    assert json.loads(module.exchange)["target"] == "portable-machineir"
    assert module.bytes == b"" and module.stages[-1] == "machine-ir"
    try:
        copy.copy(module)
        raise AssertionError("copied an owning handle")
    except TypeError:
        pass
module.close()
try:
    module.text
    raise AssertionError("read a closed handle")
except ValueError:
    pass

try:
    l.compile_source("(")
    raise AssertionError("accepted malformed TraceML")
except l.LimestoneError as error:
    assert error.code == l.LIMESTONE_PARSE and str(error)

with l.TraceRuntime("(lambda x (lambda y (add x (if y 2 0))))") as runtime:
    with runtime.invoke([40]) as result:
        closure = result.value
assert closure.callable
try:
    closure.integer
    raise AssertionError("inspected a closure as an integer")
except l.LimestoneError as error:
    assert error.code == l.LIMESTONE_UNSUPPORTED
with closure:
    with closure.apply((1,), l.trace_options(record_guards=True)) as result:
        with result.value as value:
            assert value.integer == 42
        assert "taken" in result.trace
        guards = result.guards
with guards[0] as guard:
    assert guard.expected
    with guard.resume(0) as result:
        with result.value as value:
            assert value.integer == 40
    try:
        guard.resume(1, l.trace_options(step_limit=0))
        raise AssertionError("ignored the continuation budget")
    except l.LimestoneError as error:
        assert error.code == l.LIMESTONE_RESOURCE_LIMIT
with l.TraceRuntime("((lambda ignored 42) (add 9223372036854775807 1))") as runtime:
    with runtime.invoke(options=l.trace_options(record_trace=False)) as result:
        assert result.trace == "" and result.guards == []
        with result.value as value:
            assert value.integer == 42
    for arguments in (None, [None], [None] * 65537):
        try:
            l.limestone_traceml_invoke(runtime._pointer(), arguments, None, l.limestone_error())
            raise AssertionError("accepted invalid runtime arguments")
        except (TypeError, ValueError):
            pass

with l.BURSDocument((fixtures / "selection.limeburg").read_text()) as document:
    selection = document.select()
with selection:
    assert selection.cost == 3 and "ADDI" in selection.text
    handoff = selection.text
with l.BURSDocument.from_file(fixtures / "selection-includes.limeburg") as document:
    analysis = document.analyze()
    with document.select() as selected:
        assert selected.cost == 3 and "ADDI" in selected.text
with analysis:
    copied_states = analysis.states
    copied_attempts = analysis.attempts
assert any(state["rule"] == 3 and state["cost"] == 3 for state in copied_states)
assert any(attempt["cost"] is None for attempt in copied_attempts)
with l.BURSDocument("ruleset x {nonterminal reg;terminal A(0);rule reg:A():i64 -> I;}tree t {node %7=A():f64;root %7:reg;}") as document:
    with document.analyze() as analyzed:
        assert analyzed.states == [] and "expected type i64" in analyzed.attempts[0]["reason"]
    try:
        document.select()
        raise AssertionError("selected a type-incompatible rule")
    except l.LimestoneError as error:
        assert error.code == l.LIMESTONE_UNSATISFIABLE
with l.compile_umd_file(fixtures / "arithmetic-includes.umd") as module:
    assert "ADD" in module.text
try:
    l.compile_umd_file(fixtures / "includes/cycle.umd")
    raise AssertionError("accepted cyclic includes")
except l.LimestoneError as error:
    assert error.code == l.LIMESTONE_CONFLICT
with l.SchedulingDocument(handoff) as document:
    schedule = document.schedule()
with schedule:
    assert len(schedule.issues) == 2

with l.SchedulingDocument((fixtures / "scheduling.schedrow").read_text()) as document:
    with document.schedule() as schedule:
        assert len(schedule.issues) == 3
        assert any("ALU" in issue[3] for issue in schedule.issues)
with l.SchedulingDocument((fixtures / "grouping.schedrow").read_text()) as document:
    grouped = document.schedule()
with grouped:
    assert grouped.groups[0]["kind"] == l.LIMESTONE_GROUP_BUNDLE
    assert grouped.groups[0]["members"] == [8, 2]
    assert grouped.issues[0][1] == grouped.issues[1][1]
    assert grouped.issues[0][2:] == (1, ["B"])

with l.UniselDocument.from_file(fixtures / "arithmetic-includes.umd") as document:
    model = document.analyze()
with model:
    assert len(model.candidates) == 3 and len(model.clauses) == 3
    assert "/includes/scalar-machine.umd:" in model.text
    selection = model.select()
with selection:
    assert selection.cost == 3 and [match["root"] for match in selection.matches] == [10, 20, 1]
    assert selection.matches[-1]["inputs"] == [10, 20]
    with l.SchedulingDocument(selection.text) as document:
        with document.schedule() as schedule:
            assert len(schedule.issues) == 3
with l.UniselDocument("machine x {} program y {node %1=unknown():i64;output %1;}") as document:
    with document.analyze() as model:
        assert model.candidates == [] and model.clauses == [[]]
        try:
            model.select()
            raise AssertionError("uncovered model solved")
        except l.LimestoneError as error:
            assert error.code == l.LIMESTONE_UNSATISFIABLE

with l.UniselDocument("machine x {operator const(0);instruction C {} pattern p:const():i64 binding imm -> C where {constraints=[{kind=power_of_two;operand=imm;}];};}"
                      "program graph {node %1=const(8):i64;output %1;}") as document:
    with document.analyze() as model:
        with model.select() as selection:
            assert selection.cost == 1 and selection.matches[0]["opcode"] == "C"
with l.BURSDocument('ruleset x {nonterminal r;terminal C(0);rule r:C() binding imm -> I where {constraints=[{kind=power_of_two;operand=imm;}];};}'
                    'tree t {node %1=C() immediate 7;root %1:r;}') as document:
    with document.analyze() as analysis:
        assert any("power_of_two failed" in attempt["reason"] for attempt in analysis.attempts)

proof_source = ('machine m {operator const(0);instruction C {latency=0;}pattern p:const():i64 -> C '
                'where {predicates=[{name=literal;parameters={label="symbol";};}];};}'
                'program p {node %1=const("symbol",42):i64 properties {metadata={mode=exact;};};output %1;}')
def literal_proof(context, parameters):
    root = context["root"]
    assert root["metadata"]["strings"] == [{"index": 0, "value": "symbol"}]
    assert root["metadata"]["properties"] == {"mode": "exact"}
    assert context["covered"][0]["id"] == root["id"]
    return root["immediate"] == 42 and parameters["label"] == "symbol"

with l.UniselDocument(proof_source) as document:
    try:
        document.analyze()
        raise AssertionError("unbound proof accepted")
    except l.LimestoneError as error:
        assert error.code == l.LIMESTONE_UNSUPPORTED
    with l.SelectionPredicate("literal", literal_proof) as predicate:
        document.attach_predicate(predicate)
    model = document.analyze()
with model:
    with model.select() as selected:
        assert "proved literal" in selected.matches[0]["reason"] and "symbol" in selected.text
with l.UniselDocument(proof_source) as document:
    with l.SelectionPredicate("literal", lambda *_: False) as predicate:
        document.attach_predicate(predicate)
    with document.analyze() as model:
        assert model.candidates == []
with l.UniselDocument(proof_source) as document:
    with l.SelectionPredicate("literal", lambda *_: 1) as predicate:
        document.attach_predicate(predicate)
    try:
        document.analyze()
        raise AssertionError("non-Boolean proof accepted")
    except l.LimestoneError as error:
        assert error.code == l.LIMESTONE_INVALID_ARGUMENT
with l.UniselDocument(proof_source) as document:
    def release_document(context, parameters):
        document.close()
        return literal_proof(context, parameters)
    with l.SelectionPredicate("literal", release_document) as predicate:
        document.attach_predicate(predicate)
    with document.analyze() as model:
        with model.select() as result:
            assert result.cost == 1
with l.BURSDocument('ruleset r {nonterminal r;terminal C(0);rule r:C() -> C where {predicates=[{name=literal;parameters={label="symbol";};}];};}'
                    'tree t {node %1=C() immediate 42 properties {strings=[{index=0;value="symbol";}];metadata={mode=exact;};};root %1:r;}') as document:
    with l.SelectionPredicate("literal", literal_proof) as predicate:
        document.attach_predicate(predicate)
    with document.select() as result:
        assert result.cost == 1 and "symbol" in result.text

with l.AllocationDocument((fixtures / "allocation.regtl").read_text()) as document:
    for algorithm in (l.LIMESTONE_ALLOCATE_LINEAR, l.LIMESTONE_ALLOCATE_GREEDY,
                      l.LIMESTONE_ALLOCATE_COLOR, l.LIMESTONE_ALLOCATE_CONSTRAINT, l.LIMESTONE_ALLOCATE_PBQP):
        with document.allocate(algorithm=algorithm) as assignment:
            assert assignment.registers[8] == 2 and not assignment.spilled
    assignment = document.allocate()
with assignment:
    assert assignment.registers and "physical" in assignment.text
with l.AllocationDocument((fixtures / "allocation-costs.regtl").read_text()) as document:
    assignment = document.allocate(function=None, algorithm=l.LIMESTONE_ALLOCATE_PBQP)
with assignment:
    assert assignment.registers == {1: 0} and assignment.spilled == [2] and assignment.cost == 2
with l.SchedulingDocument("machine_model M {issue_width=3;}region R {instruction %1 {opcode=wide;latency=0;issue={width=2;slots=[0,2];};}}") as document:
    with document.schedule() as schedule:
        assert schedule.slot_assignments == [[0, 2]]

with l.ObjectTarget((fixtures / "object-x86.isa").read_text()) as target:
    with target.create() as object:
        section = object.add_section(".data", b"\0"*8, flags=3, alignment=8)
        symbol = object.add_symbol("external")
        object.add_relocation(section, symbol, 1, 0, 2)
        assert object.elf_class == 64 and object.relocations[0]["implicit_addend"] is False
        assert object.sections[0]["data"] == b"\0"*8 and object.symbols[0]["name"] == "external"
        encoded = object.emit()
    with l.ObjectFile(encoded, "python.o") as loaded:
        assert loaded.emit() == encoded
        image = target.link([loaded], base_address=1027, externals={"external": 40})
        try:
            target.link([loaded])
            raise AssertionError("unresolved symbol accepted")
        except l.LimestoneError as error:
            assert error.code == l.LIMESTONE_NOT_FOUND
    with target.from_code(b"\xc3", "entry") as object:
        with target.link([object], base_address=1027) as result:
            assert result.symbols == {"entry": 1040} and result.bytes == b"\0"*13+b"\xc3"
with image:
    assert image.base_address == 1027 and image.bytes == b"\0"*5+b"\x2a"+b"\0"*7
with l.ObjectTarget((fixtures / "backend-machine.isa").read_text()) as target:
    config = l.limestone_configuration_create()
    error = l.limestone_error()
    assert l.limestone_configuration_set_pipeline(config, 1, 1, 1, 1, 0, error) == l.LIMESTONE_OK
    graph_target = l.limestone_target_load_isa((fixtures / "backend-machine.isa").read_text(), error)
    program = l.limestone_program_create()
    try:
        assert graph_target and program
        assert l.limestone_program_add_node(program, 1, "const", "i64", None, 0, 1, 42, 1, 1, error) == l.LIMESTONE_OK
        inputs = l.UInt32Array(1)
        inputs[0] = 1
        assert l.limestone_program_add_node(program, 2, "return", "", inputs.cast(), 1, 0, 0, 1, 0, error) == l.LIMESTONE_OK
        assert l.limestone_program_set_control(program, 2, l.LIMESTONE_FLOW_RETURN, None, 0, error) == l.LIMESTONE_OK
        module = l.limestone_compile_program_configured(program, graph_target, config, error)
        assert module
        with l.Module(module) as compiled:
            object = target.from_module(compiled, "pipeline")
            original = compiled.bytes
        with object:
            assert object.sections[0]["data"] == original
    finally:
        l.limestone_program_destroy(program)
        l.limestone_target_destroy(graph_target)
        l.limestone_configuration_destroy(config)

with l.BinaryArchitecture((fixtures / "byte-source.isa").read_text()) as source:
    config = l.limestone_configuration_create()
    error = l.limestone_error()
    native_isa = (fixtures / "native-constant.isa").read_text()
    target = l.limestone_target_load_isa(native_isa, error)
    assert target and l.limestone_configuration_set_pipeline(config, 1, 1, 1, 1, 0, error) == l.LIMESTONE_OK
    try:
        module = l.compile_target("((lambda x (add x 2)) 40)", target, config)
    finally:
        l.limestone_target_destroy(target)
        l.limestone_configuration_destroy(config)
    with module:
        assert module.bytes == b"\x48\xc7\xc0\x2a\0\0\0\xc3"
    assert source.translate(b"\x01\x01") == b"\x01\x01"
    assert "10: inc" in source.disassemble(b"\x01", address=16)
    with l.BinaryArchitecture((fixtures / "byte-target.isa").read_text()) as target:
        runtime = l.BinaryRuntime(source, target, hot_threshold=1, max_regions=1)
with runtime:
    region = runtime.prepare(b"\x01\x01", 100, 200)
    assert region.valid and not region.compiled and region.bytes == b"\x09\x09"
    assert region.guest_bytes == b"\x01\x01"
    with runtime.prepare(b"\x01", 300, 400) as other:
        assert runtime.resident_count == 1 and region.valid and other.valid
        assert runtime.invalidate(101, 1) == 1 and not region.valid and other.valid
        try:
            runtime.invoke(other)
            raise AssertionError("executed a region without a native installer")
        except l.LimestoneError as error:
            assert error.code == l.LIMESTONE_UNSUPPORTED
        assert region.bytes == b"\x09\x09"
    survivor = runtime.prepare(b"\x01", 500, 600)
with region:
    assert not region.valid and region.bytes == b"\x09\x09"
with survivor:
    assert not survivor.valid and survivor.guest_bytes == b"\x01"

with l.Optimizer("(operator add 2)(rule zero (add ?x 0) ?x)", "python.rules") as optimizer:
    assert optimizer.rule_count == 1
    optimizer.set_cost("add", 7)
    optimizer.set_cost(None, 2)
    optimized = optimizer.saturate("(add 42 0)")
    with optimizer.saturate("(add 41 1)") as ordinary:
        assert ordinary.expression == "(add 41 1)" and ordinary.info["cost"] == 11
    optimizer.set_limits(iterations=0, trace=False)
    with optimizer.saturate("(add 42 0)") as bounded:
        assert bounded.info["limit_reached"] and bounded.expression == "(add 42 0)" and bounded.trace == []
    try:
        optimizer.load_rules("(operator lost 1)(rule bad (lost ?x) ?unbound)", "bad.rules")
        raise AssertionError("accepted unbound rewrite variable")
    except l.LimestoneError as error:
        assert error.code == l.LIMESTONE_INVALID_ARGUMENT and "bad.rules" in str(error)
    assert optimizer.rule_count == 1
with l.ObjectTarget((fixtures / "object-i386.isa").read_text()) as target:
    with target.create() as object:
        assert object.elf_class == 32
        section = object.add_section(".data", b"\xfc\xff\xff\xff", flags=3, alignment=4)
        symbol = object.add_symbol("external")
        object.add_relocation(section, symbol, 1, 0, implicit_addend=True)
        assert object.relocations == [dict(section=0, symbol=0, type=1, offset=0, addend=0, implicit_addend=True)]
        try:
            object.add_relocation(section, symbol, 1, 0, 1, implicit_addend=True)
            raise AssertionError("accepted an explicit addend in a REL record")
        except l.LimestoneError as error:
            assert error.code == l.LIMESTONE_INVALID_ARGUMENT
        assert len(object.relocations) == 1
        encoded = object.emit()
    with l.ObjectFile(encoded) as decoded:
        assert decoded.elf_class == 32 and decoded.relocations[0]["implicit_addend"]
        copied = decoded.relocations
        with target.link([decoded], base_address=4096, externals={"external": 42}) as linked:
            assert linked.bytes == b"\x26\0\0\0"
        assert decoded.emit() == encoded
    assert copied[0]["implicit_addend"]
    with l.ObjectArchive() as archive:
        with target.from_code(b"\x01\x02", "external") as member:
            archive.add("member with spaces.o", member)
            try:
                archive.add("bad\nname", member)
                raise AssertionError("accepted an invalid archive name")
            except l.LimestoneError:
                pass
        assert archive.names == ["member with spaces.o"]
        data = archive.emit()
    with l.ObjectArchive(data) as archive, archive.member(0) as member, l.ObjectFile(encoded) as root:
        assert member.symbols[0]["name"] == "external"
        with target.link([root], base_address=4096, archives=[archive]) as linked:
            assert linked.symbols["external"] >= 4096

with optimized:
    assert optimized.expression == "42" and optimized.info["cost"] == 2
    assert optimized.info["rewrites"] > 0 and optimized.info["saturated"]
    assert any("python.rules" in step for step in optimized.trace)
