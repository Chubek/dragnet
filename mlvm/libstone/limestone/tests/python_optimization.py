"""Connect an independently compiled native proof callback to the Python API."""
import pathlib
import sys
import limestone as l
import optimization_fixture as host

fixtures = pathlib.Path(sys.argv[1])
rules = "(operator set 2)(operator add 2)(operator sub 2)(rule increment (add ?x 1) (sub ?x -1))"
source = l.BinaryArchitecture((fixtures / "byte-source.isa").read_text())
target = l.BinaryArchitecture((fixtures / "byte-target.isa").read_text().replace(
    "(set counter (add counter 1))", "(set counter (sub counter -1))"))
with l.Optimizer(rules, "python-binary.rules") as optimizer:
    optimizer.set_cost("add", 20)
    optimizer.set_cost("sub", 1)
    with optimizer.binary_transform("wrapping-counter:1", host.counter_legality()) as transform:
        assert transform.cacheable and "wrapping-counter:1" in transform.identity
        assert source.translate(b"\x01\x01\x01", target, 100, 200, transform) == b"\x09\x09\x09"
        runtime = l.BinaryRuntime(source, target, transform=transform)
        optimizer.set_limits(time_ms=1)
        with optimizer.binary_transform("wrapping-counter:1", host.counter_legality()) as bounded:
            assert not bounded.cacheable and bounded.identity != transform.identity
    try:
        optimizer.binary_transform("wrapping-counter:1", None)
        raise AssertionError("missing legality proof accepted")
    except l.LimestoneError as error:
        assert error.code == l.LIMESTONE_UNSUPPORTED
source.close()
target.close()
with runtime:
    retained = runtime.prepare(b"\x01\x01\x01", 100, 200)
    assert retained.valid and retained.bytes == b"\x09\x09\x09"
    assert retained.guest_bytes == b"\x01\x01\x01" and not retained.compiled
    with runtime.prepare(b"\x01\x01\x01", 100, 200) as repeated:
        assert repeated.bytes == retained.bytes
    assert runtime.resident_count == 1
with retained:
    assert not retained.valid and retained.bytes == b"\x09\x09\x09"
