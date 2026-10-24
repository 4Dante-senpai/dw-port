#!/usr/bin/env python3
"""Copy a C source, encoding non-ASCII literal characters as CP932 octal bytes."""
import argparse
import re
from pathlib import Path
import sys


def convert(source, original):
    output = []
    state = back = 'code'
    include = False
    i = 0
    line = 1
    while i < len(source):
        c = source[i]
        pair = source[i:i + 2]
        if state == 'code':
            if c == '#' and not source[source.rfind('\n', 0, i) + 1:i].strip():
                state = 'directive'
                # A header name is not a string literal.
                include = re.match(r'#\s*include\b', source[i:]) is not None
            if pair in ('//', '/*'):
                state = pair
                output.append(pair)
                i += 2
                continue
            if c in ('"', "'"):
                state, back = c, 'code'
        elif state in ('//', 'directive'):
            splice = 3 if source.startswith('\\\r\n', i) else 2 if pair == '\\\n' else 0
            if splice:
                output.append(source[i:i + splice])
                line += 1
                i += splice
                continue
            if c == '\n':
                state = 'code'
            elif c == '"' and state == 'directive' and not include:
                # Macro bodies carry game strings too; apostrophes there are left alone (#error).
                state, back = c, 'directive'
        elif state == '/*':
            if pair == '*/':
                output.append(pair)
                state = 'code'
                i += 2
                continue
        else:
            if c == '\\':
                # Preserve existing C escapes, including escaped quotes and line splices.
                if i + 1 < len(source) and ord(source[i + 1]) < 128:
                    output.append(pair)
                    line += pair.count('\n')
                    i += 2
                    continue
            elif c == state:
                state = back
            if ord(c) >= 128:
                try:
                    encoded = c.encode('cp932')
                except UnicodeEncodeError:
                    raise ValueError(f'{original}:{line}: no Shift-JIS character for {c!r}') from None
                output.append(''.join(f'\\{byte:03o}' for byte in encoded))
                i += 1
                continue
        output.append(c)
        line += c == '\n'
        i += 1
    filename = str(original).replace('\\', '\\\\').replace('"', '\\"')
    return f'#line 1 "{filename}"\n' + ''.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        converted = convert(args.input.read_bytes().decode('utf-8'), args.input)
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(converted.encode('utf-8'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
