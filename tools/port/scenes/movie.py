"""STR sequence evidence: every display flip, integer RGB tolerances and A/V bounds."""
import hashlib
import json
import operator
from pathlib import Path
import re
import struct


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def sequence(directory):
    rows = [json.loads(line) for line in (directory / 'movie.jsonl').read_text().splitlines()]
    if len(rows) < 3 or rows[0].get('event') != 'start' or rows[-1].get('event') != 'end':
        raise ValueError('incomplete movie trace')
    frames = rows[1:-1]
    if any(row.get('event') != 'frame' or row.get('index') != i for i, row in enumerate(frames, 1)):
        raise ValueError('missing/duplicate movie frame')
    if any(a['cycle'] > b['cycle'] for a, b in zip(rows, rows[1:])):
        raise ValueError('backwards movie clock')
    if any(a['vblank'] > b['vblank'] for a,b in zip(frames,frames[1:])):
        raise ValueError('backwards movie VBlank')
    paths = sorted(directory.glob('frame-*.ppm'))
    if [p.name for p in paths] != [f'frame-{i:04d}.ppm' for i in range(1, len(frames)+1)]:
        raise ValueError('incomplete movie frame files')
    return rows[0], frames, rows[-1], paths


def evidence(directory):
    # CI caches manifests, while external /tmp captures may have been removed.
    # An absent sequence must invalidate reuse and trigger a fresh capture.
    if not (directory/'movie.jsonl').is_file(): return {}
    sequence(directory)
    return {str(p.relative_to(directory.parent)): digest(p)
            for p in [directory/'movie.jsonl', *sorted(directory.glob('frame-*.ppm'))]}


def deterministic(first, second):
    a, b = sequence(first)[3], sequence(second)[3]
    if len(a) != len(b) or any(digest(x) != digest(y) for x,y in zip(a,b)):
        raise ValueError('non-deterministic movie pixels')
    # Timing and return records must also repeat exactly, including final XA delivery.
    if (first/'movie.jsonl').read_bytes() != (second/'movie.jsonl').read_bytes():
        raise ValueError('non-deterministic movie timing')


def finish_reference(directory, raw_xa):
    start, _, end, _ = sequence(directory)
    last = None
    with raw_xa.open('rb') as stream:
        if stream.read(7) != b'DW1XA1\n': raise ValueError('missing XA tap header')
        while row := stream.read(12):
            if len(row) != 12: raise ValueError('truncated XA tap')
            cycle = struct.unpack('<Qhh',row)[0]
            if start['cycle'] <= cycle < end['cycle']: last = cycle
    if last is None: raise ValueError('movie has no XA samples')
    rows = (directory/'movie.jsonl').read_text().splitlines()
    end['xa_cycle'] = last
    rows[-1] = json.dumps(end, separators=(',',':'))
    (directory/'movie.jsonl').write_text('\n'.join(rows)+'\n')


def pixels(path):
    data = path.read_bytes()
    header = b'P6\n320 240\n255\n'
    if not data.startswith(header) or len(data) != len(header)+320*240*3:
        raise ValueError(f'invalid movie RGB24 frame: {path}')
    return data[len(header):]


def allowances(scene):
    """Exact scene-owned component pairs; never alter the per-channel means."""
    result = {}
    for item in scene.get('mdec_allowances', []):
        keys = {'frame', 'x', 'y', 'channel', 'port', 'reference'}
        if set(item) != keys or any(type(item[k]) is not int for k in keys - {'channel'}):
            raise ValueError('invalid MDEC allowance')
        if (item['frame'] < 1 or not 0 <= item['x'] < 320 or not 0 <= item['y'] < 240
                or item['channel'] not in ('R', 'G', 'B')
                or not 0 <= item['port'] <= 255 or not 0 <= item['reference'] <= 255
                or abs(item['port'] - item['reference']) <= 4):
            raise ValueError('invalid MDEC allowance bounds')
        key = (item['frame'], (item['y'] * 320 + item['x']) * 3 + 'RGB'.index(item['channel']))
        if key in result:
            raise ValueError('duplicate MDEC allowance')
        result[key] = item
    return result


def compare(actual, reference, scene, diff_dir):
    _, af, ae, ap = sequence(actual)
    _, rf, re_, rp = sequence(reference)
    failures = []
    listed = allowances(scene)
    accepted = []
    expected = scene.get('expected_frames')
    # Start gates return/VBlank; only full playback requires equal frame counts.
    if scene['expected_return'] != 0 and (len(ap) != len(rp) or (expected is not None and (len(ap) != expected or len(rp) != expected))):
        failures.append(f'frame count: port={len(ap)}, reference={len(rp)}, expected={expected}')
    for label, end in [('port',ae),('reference',re_)]:
        if end['return'] != scene['expected_return']:
            failures.append(f'{label} return={end["return"]}')
    if scene['expected_return'] == 0 and ae['vblank'] != re_['vblank']:
        failures.append(f'skip VBlank: port={ae["vblank"]}, reference={re_["vblank"]}')
    metrics=[]
    worst=None
    worst_score=(-1,-1)
    for index,(a,b) in enumerate(zip(ap,rp),1):
        left,right=pixels(a),pixels(b)
        if left == right: peak,means,delta=0,[0.0]*3,None
        else:
            delta=bytes(map(abs,map(operator.sub,left,right)))
            peak=max(delta)
            means=[sum(delta[c::3])/(320*240) for c in range(3)]
        mean=max(means)
        metrics.append(dict(index=index,maximum=peak,mean=mean,channel_means=means,port_sha256=digest(a),reference_sha256=digest(b)))
        unexpected = []
        if peak > 4:
            for component, error in enumerate(delta):
                if error <= 4: continue
                item = listed.get((index, component))
                if item and (left[component], right[component]) == (item['port'], item['reference']):
                    accepted.append(dict(item, error=error))
                else:
                    unexpected.append(dict(x=component//3%320, y=component//960,
                                           channel='RGB'[component%3], port=left[component],
                                           reference=right[component], error=error))
        if unexpected or mean>0.5:
            failures.append(f'frame {index}: max={peak}, mean={mean}, unexpected={unexpected}')
            if (peak,mean)>worst_score:
                worst_score=(peak,mean); worst=(index,delta)
    if worst:
        diff_dir.mkdir(parents=True,exist_ok=True)
        (diff_dir/f'worst-frame-{worst[0]:04d}.ppm').write_bytes(b'P6\n320 240\n255\n'+worst[1])
    period=(rf[-1]['cycle']-rf[0]['cycle'])/(len(rf)-1) if len(rf)>1 else 0
    if not ae.get('xa_cycle') or not re_.get('xa_cycle'):
        raise ValueError('missing last XA delivery cycle')
    offset_port=af[-1]['cycle']-ae['xa_cycle']
    offset_ref=rf[-1]['cycle']-re_['xa_cycle']
    error=abs(offset_port-offset_ref)
    if error>period: failures.append(f'A/V offset error={error} cycles, limit={period}')
    return dict(frames=metrics,worst_frame=worst[0] if worst else None,port_frames=len(ap),reference_frames=len(rp),
                port_end=ae,reference_end=re_,av=dict(port=offset_port,reference=offset_ref,error=error,limit=period),
                failures=failures,accepted_mdec_allowances=accepted)
