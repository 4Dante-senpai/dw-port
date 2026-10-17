"""Regression: detect callback conversions and missing consumed K&R arguments."""
import importlib.util
from pathlib import Path
import shlex
import tempfile

spec = importlib.util.spec_from_file_location('fnptr_inventory', Path(__file__).with_name('inventory.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
with tempfile.TemporaryDirectory(dir=audit.ROOT / 'build') as directory:
    source = Path(directory) / 'calls.c'
    source.write_text("""
void accept(void (*tick)(int));
void callback(void) {}
void knr(value) int value; { (void)value; }
void invoke(void) {
    void (*tick)(int) = (void (*)(int))callback;
    tick(7);
    knr();
    knr(1, 2);
    accept(callback);
}
""")
    command = {'directory': str(audit.ROOT),
               'command': f'emcc -std=gnu99 -w -o unused.o -c {shlex.quote(str(source))}'}
    findings = audit.scan(command, False)
    assert sum(row[0] == 'callback' for row in findings) == 2, findings
    assert sum(row[0] == 'arity' for row in findings) == 2, findings
print('fnptr scanner: callback cast and missing consumed argument detected')
