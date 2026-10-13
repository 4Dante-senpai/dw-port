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
