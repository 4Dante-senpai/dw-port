"""L1: gen_stubs counts a port-defined libc symbol as implemented, not as host libc."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from gen_stubs import classify  # noqa: E402

lib_of = {"rand": "libc", "strlen": "libc", "malloc3": "libapi", "PadRead": "libetc"}
used, implemented, host_libc, stubs = classify(
    {"rand", "strlen", "malloc3", "PadRead", "port_gte_rtps", "game_local"},
    {"rand", "malloc3"}, lib_of)
assert used == ["PadRead", "malloc3", "rand", "strlen"], used
assert implemented == ["malloc3", "rand"], implemented
assert host_libc == ["strlen"], host_libc
assert stubs == ["PadRead", "port_gte_rtps"], stubs
print("test_gen_stubs: OK")

# Exercise void, integer, pointer and function-pointer returns even when the current
# game has no remaining PsyQ stubs. Compile against the original declarations.
from gen_stubs import typed_stubs
import subprocess
headers_and_definitions = typed_stubs(
    ['MemCardInit', 'VSync', 'CdSearchFile', 'CdReadCallback'], {})
source = 'void port_stub_hit(const char *, int *);\n' + '\n'.join(headers_and_definitions)
root = pathlib.Path(__file__).resolve().parents[3]
for compiler, abi in [('clang', ['-m32']), ('emcc', [])]:
    result = subprocess.run([compiler, *abi, '-x', 'c', '-std=gnu99', '-w',
        '-DLANGUAGE_C', '-I' + str(root / 'external/psyq_headers/mw_lib41/include'),
        '-fsyntax-only', '-'], input=source, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
print('test_gen_stubs: typed definitions match native and wasm prototypes')

try:
    classify({"DecDCTin"}, set(), {"DecDCTin": "libpress"})
except ValueError as error:
    assert "cannot generate video stubs" in str(error)
else:
    raise AssertionError("video stub regression was accepted")
