"""Exercise textual and command-line Optilisp authoring and execution."""
import pathlib
import subprocess
import sys
import tempfile

tool = pathlib.Path(sys.argv[1])
root = pathlib.Path(__file__).resolve().parents[2]


def run(*args, success=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stderr
    return result


output = run(root / "examples/blackstone.mlvm", "--passes=dce").stdout
assert '"value"="42"' in output and "apply " not in output
assert "apply " in run(root / "examples/blackstone.mlvm", "--print").stdout
run(root / "examples/blackstone.mlvm", "--blackstone-dir=/nonexistent", success=False)

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    source = directory / "custom.mlvm"
    source.write_text('''mlvm 1 { block(%x:"i32") {
      %same:"i32" = "arith.and"(%x, %x) [];
      %use:"i32" = "arith.muli"(%same, %x) [];
    } }''')
    custom = directory / "and-self.olisp"
    custom.write_text((root / "examples/kernels/and-self.olisp").read_text())
    options = [f"--kernel-file={custom}", "--apply-kernel=blackstone.hir.custom.and-self"]
    printed = run(source, *options, "--print").stdout
    assert "kernel " in printed and "apply " in printed
    inline = directory / "inline.mlvm"
    inline.write_text(printed)
    output = run(inline).stdout
    assert '"arith.and"' not in output and '"arith.muli"(%0, %0)' in output
    result = run(source, *options).stdout
    assert result == output
    run(source, *options, "-o", custom, success=False)
    assert custom.read_text() == (root / "examples/kernels/and-self.olisp").read_text()
    run(source, "--apply-kernel=unknown.kernel", success=False)
    run(source, *options, *options, success=False)
