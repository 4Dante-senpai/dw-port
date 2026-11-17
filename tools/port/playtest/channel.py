"""Client of the live state channel of dw1 (docs/port/playtest.md, "Canal de estado").

run() launches the binary with a state pipe and, for lockstep, an input pipe; parses the header and
one record per VBlank by field name; calls respond(record) whenever record['want'] is 1 and answers
with its (mask, hold) or a raw line (str). respond returning None closes the input pipe (the binary stops cleanly).
"""
import os
import subprocess
import tempfile

VERSION = '1'


def parse_header(line):
    words = line.split()
    if words[:2] != ['#', 'dw1-state'] or len(words) < 4 or words[2] != VERSION:
        raise ValueError(f'unsupported state channel header: {line!r}')
    return words[3:]


def parse_record(names, line):
    values = line.split()
    if len(values) != len(names):
        raise ValueError(f'record has {len(values)} fields, expected {len(names)}: {line!r}')
    return {name: int(value, 16) if name == 'cond' else int(value) for name, value in zip(names, values)}


def _fd_args(state, reply):
    return ['--state-fd', str(state)] + (['--input-fd', str(reply)] if reply is not None else [])


def run(argv, respond=None, fd_args=_fd_args, timeout=None, **popen):
    """Returns (records, returncode, stdout, stderr). Without respond it only observes."""
    state_r, state_w = os.pipe()
    reply_r, reply_w = (os.pipe() if respond else (None, None))
    cmd = list(argv) + list(fd_args(state_w, reply_r))
    pass_fds = [state_w] + ([reply_r] if respond else [])
    records = []
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        process = subprocess.Popen(cmd, pass_fds=pass_fds, stdout=out, stderr=err, **popen)
        os.close(state_w)
        if respond:
            os.close(reply_r)
        try:
            with os.fdopen(state_r) as state:
                first = state.readline()
                if not first:  # the binary exited before the header
                    returncode = process.wait(timeout)
                    out.seek(0)
                    err.seek(0)
                    return [], returncode, out.read(), err.read()
                names = parse_header(first)
                for line in state:
                    record = parse_record(names, line)
                    records.append(record)
                    if respond and record['want'] and reply_w is not None:
                        answer = respond(record)
                        if answer is None:
                            os.close(reply_w)
                            reply_w = None
                        else:
                            text = answer if isinstance(answer, str) else f'{answer[0]:x} {answer[1]}'
                            try:
                                os.write(reply_w, (text + '\n').encode())
                            except BrokenPipeError:  # the binary is gone: keep draining records
                                os.close(reply_w)
                                reply_w = None
            returncode = process.wait(timeout)
        finally:
            if reply_w is not None:
                os.close(reply_w)
            if process.poll() is None:
                process.kill()
                process.wait()
        out.seek(0)
        err.seek(0)
        return records, returncode, out.read(), err.read()
