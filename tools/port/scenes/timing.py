"""Frame-0 checkpoint comparison and exclusive elapsed-cycle diagnostics."""
import json
from pathlib import Path
import tomllib

SOURCES = ('cd', 'gpu', 'mdec', 'card', 'psyq', 'wait', 'cpu', 'spu')
# Shared checkpoints retain the same gate in scenes that reach them.
GATES = set()
for scene_id in ('title', 'movie0', 'movie0_skip', 'map_load'):
    scene = tomllib.loads(Path(__file__).with_name(scene_id + '.toml').read_text())
    GATES.update([scene['mark'], *scene.get('checkpoints', [])])


def read(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    if not rows or not isinstance(rows[0], dict) or rows[0].get('event') != 'frame0':
        raise ValueError(f'{path}: missing frame 0')
    previous_cycle = previous_vblank = 0
    previous_costs = dict.fromkeys(SOURCES, 0)
    points = {}
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"{path}: timing record must be an object")
        if (row.get('event') not in ('frame0', 'mark', 'checkpoint', 'vblank')
                or (row.get('event') == 'frame0' and index != 0)
                or not isinstance(row.get('label'), str)
                or type(row.get('vblank')) is not int or row['vblank'] < previous_vblank
                or type(row.get('cycle')) is not int or row['cycle'] < previous_cycle
                or not isinstance(row.get('costs'), dict)
                or set(row['costs']) != set(SOURCES)):
            raise ValueError(f'{path}: invalid timing record: {row}')
        if any(type(row['costs'][s]) is not int or row['costs'][s] < previous_costs[s] for s in SOURCES):
            raise ValueError(f'{path}: backwards/invalid cost counters')
        if sum(row['costs'].values()) != row['cycle']:
            raise ValueError(f'{path}: exclusive costs do not sum to elapsed cycles')
        previous_cycle, previous_vblank, previous_costs = row['cycle'], row['vblank'], row['costs']
        if row['event'] != 'vblank':
            key = (row['event'], row['label'])
            if key in points:
                raise ValueError(f'{path}: duplicate checkpoint {key}')
            points[key] = row
    if rows[0]['vblank'] or rows[0]['cycle']:
        raise ValueError(f'{path}: frame 0 must have zero VBlank and cycle')
    return points


def compare(port, reference, mark, checkpoints, strict=False):
    sides = read(reference), read(port)
    expected = [('mark', mark), *(('checkpoint', cp) for cp in checkpoints)]
    first = None
    previous = [dict.fromkeys(SOURCES, 0), dict.fromkeys(SOURCES, 0)]
    report = []
    print('checkpoint ref port diff')
    for event, label in expected:
        key = event, label
        missing = ['reference' if i == 0 else 'port' for i, side in enumerate(sides) if key not in side]
        if missing:
            raise ValueError(f'missing checkpoint {label} in {", ".join(missing)}')
        ref, actual = (side[key] for side in sides)
        diff = actual['vblank'] - ref['vblank']
        print(f'{event}:{label} {ref["vblank"]} {actual["vblank"]} {diff:+d}')
        costs = [{s: row['costs'][s] - prior[s] for s in SOURCES}
                 for row, prior in zip((ref, actual), previous)]
        report.append(dict(event=event, label=label, reference=ref['vblank'], port=actual['vblank'],
                           diff=diff, reference_costs=costs[0], port_costs=costs[1]))
        if diff and first is None:
            first = label, costs
        previous = ref['costs'], actual['costs']
    if first:
        label, costs = first
        print(f'first timing difference: {label}; reference cycles={costs[0]}; port cycles={costs[1]}')
    failed = next((row for row in report if row['diff'] and (strict or row['label'] in GATES)), None)
    if failed:
        raise ValueError(f'checkpoint {failed["label"]}: reference={failed["reference"]} '
                         f'port={failed["port"]} diff={failed["diff"]}; '
                         f'reference cycles={failed["reference_costs"]}; port cycles={failed["port_costs"]}')
    return report
