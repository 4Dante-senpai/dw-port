"""Verify wrapper scopes without reading a disc or running a real audit."""
import contextlib
import io
import os
from pathlib import Path
import re
import runpy
import shlex
import sys
import tempfile
from unittest.mock import patch

wrapper = Path(__file__).with_name('data_leak_check')
deploy_script = wrapper.parents[1] / 'testing/deploy.sh'
copied = shlex.split(re.search(r'for file in (.+); do', deploy_script.read_text())[1])
expected = {name for name in copied if name != 'dw1.wasm' and not name.startswith('seed')}
with tempfile.TemporaryDirectory(prefix='dw1-scopes-') as tmp:
    root = Path(tmp)
    build = root / 'web'
    for name in ['us.pack', 'dw1.wasm', *expected,
                 'CMakeFiles/dw1_game_objs.dir/src/game.o', 'CMakeFiles/port.dir/port.o']:
        target = build / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.touch()
    for name in ['src/game.c', 'port/host.c']:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.touch()
    tracked = b'100644 hash 0\tsrc/game.c\0' + b'100644 hash 0\tport/host.c\0'
    with patch.object(sys, 'argv', [str(wrapper), str(build), str(root)]), \
            patch.dict(os.environ, {'DW1_DISC_DIR': str(root)}), \
            patch('subprocess.check_output', return_value=tracked), \
            patch('subprocess.run') as run, contextlib.redirect_stdout(io.StringIO()):
        run.return_value.returncode = 0
        try:
            runpy.run_path(str(wrapper), run_name='__main__')
        except SystemExit as result:
            assert result.code == 0, result.code
        else:
            raise AssertionError('wrapper did not exit')
    calls = [call.args[0] for call in run.call_args_list]
    assert len(calls) == 4, calls
    assert '--wasm-map' in calls[0]
    deploy = calls[1]
    assert deploy.index('--') == deploy.index('--exception') + 1, deploy
    assert set(deploy[deploy.index('--') + 1:]) == {str(build / name) for name in expected}
    assert all('--wasm-map' not in call for call in calls[1:])
print('data_leak_check_scopes: OK')
