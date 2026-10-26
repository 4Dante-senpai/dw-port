#!/usr/bin/env python3
"""Audit USA main disassembly and compare SHOP opcodes; emit metadata only.

Direct targets and pointer candidates are exhaustive in main's configured text/data
ranges. Register propagation is a linear, conservative candidate search, not a
proof about arbitrary computed addresses or indirect calls.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[3]
BASE, LO, HI = 0x80090000, 0x80080800, 0x80088610


def words(data):
    return list(struct.unpack(f'<{len(data)//4}I', data))


def signed(value):
    return value - 65536 if value & 0x8000 else value


def scan(code):
    direct, pointers, registers = [], [], {}
    delay_slot = None
    for pc, word in sorted(code.items()):
        op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        immediate = word & 0xffff
        if op in (2, 3):
            target = ((pc+4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
            if LO <= target < HI:
                direct.append(dict(pc=hex(pc), kind='jal' if op == 3 else 'j', target=hex(target)))
        if op == 15:
            registers[rt] = (immediate << 16, pc)
        elif op in (9, 13, 0x23) and rs in registers:
            high, origin = registers[rs]
            address = (high | immediate) if op == 13 else (high + signed(immediate)) & 0xffffffff
            if LO <= address < HI:
                pointers.append(dict(lui=hex(origin), pc=hex(pc), kind={9:'addiu',13:'ori',0x23:'lw'}[op], address=hex(address)))
            if op in (9,13):
                registers[rt] = (address, origin)
            else:
                registers.pop(rt, None)
        elif op == 0:
            registers.pop((word >> 11) & 31, None)
        elif op == 0x12 and rs in (0, 2):
            registers.pop(rt, None)
        elif op not in (2,3,4,5,6,7,1,0x28,0x29,0x2a,0x2b,0x2e,0x12):
            registers.pop(rt, None)
        registers.pop(0, None)
        # The delay slot still sees pre-branch facts; discard them afterwards.
        if pc == delay_slot or pc+4 not in code:
            registers.clear()
        delay_slot = pc+4 if op in (1,2,3,4,5,6,7) or (op == 0 and (word & 63) in (8,9)) else None
    return direct, pointers


def normalize(code):
    result = list(code)
    for i, word in enumerate(code):
        op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        if op in (2,3):
            result[i] &= 0xfc000000
        if rs == 28 and op in (9,0x20,0x21,0x23,0x24,0x25,0x28,0x29,0x2b):
            result[i] &= 0xffff0000
        if op == 15 and 0x8000 <= (word & 0xffff) <= 0x801f:
            result[i] &= 0xffff0000
            # Mask the low relocation only when its base register is this LUI.
            for j in range(i+1, min(i+5,len(code))):
                next_word = code[j]
                next_op, next_rs, next_rt = next_word >> 26, (next_word >> 21) & 31, (next_word >> 16) & 31
                if next_rs == rt and next_op in (9,13,0x20,0x21,0x23,0x24,0x25,0x28,0x29,0x2b):
                    result[j] &= 0xffff0000
                if next_rt == rt and next_op in (9,13,15,0x20,0x21,0x23,0x24,0x25):
                    break
                if next_op == 0 and (next_word >> 11) & 31 == rt:
                    break
    return result


def audit(main, shop):
    layout = (ROOT / 'config/us/main.yaml').read_text()
    segments = [(int(offset,16),kind,name.strip()) for offset,kind,name in re.findall(
        r'^\s+- \[(0x[0-9A-Fa-f]+),\s*([^,]+),\s*([^,\]]+)',layout,re.M)]
    code_end = next(offset for offset,kind,_ in segments if kind=='bin')
    code = dict(zip(range(BASE+0x800,BASE+code_end,4),words(main[0x800:code_end])))
    # Verify the original input against all available disassembly instruction words.
    verified = set()
    for path in (ROOT / 'asm/us/main').rglob('*.s'):
        for address, raw in re.findall(r'/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/',path.read_text()):
            pc = int(address,16)
            if pc in code:
                if code[pc] != int.from_bytes(bytes.fromhex(raw),'little'):
                    raise ValueError(f'main differs from USA asm at {pc:#x}')
                verified.add(pc)
    if len(verified) != len(code):
        raise ValueError('incomplete USA main disassembly coverage')
    direct, pointers = scan(code)
    symbols = {}
    for path in (ROOT / 'config/us').glob('symbols*.txt'):
        for name, address in re.findall(r'^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);',path.read_text(),re.M):
            symbols.setdefault(int(address,16),set()).add(name)
    for row in direct:
        row['target_symbols'] = sorted(symbols.get(int(row['target'],16),[]))
    data = []
    for i,(offset,kind,name) in enumerate(segments[:-1]):
        if kind.lstrip('.') not in ('data','rodata','sdata'):
            continue
        end = segments[i+1][0]
        for position in range(offset, end-3,4):
            value = struct.unpack_from('<I',main,position)[0]
            if LO <= value < HI:
                data.append(dict(pc=hex(BASE+position),section=kind,owner=name,target=hex(value)))
    main_symbols = {name:int(address,16) for name,address in re.findall(
        r'^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);',(ROOT/'config/us/symbols.txt').read_text(),re.M)}
    loader_start = main_symbols['loadShopLibrary']
    loader_end = main_symbols['checkArenaMap']
    loader_code = [code[pc] for pc in range(loader_start,loader_end,4)]
    target = main_symbols['loadDynamicLibrary']
    loader = dict(address=hex(loader_start),lib_11=0x2404000b in loader_code,
                  async_1=0x24060001 in loader_code,
                  calls_loadDynamicLibrary=(0x0c000000 | ((target>>2)&0x3ffffff)) in loader_code)
    if not all(loader[key] for key in ('lib_11','async_1','calls_loadDynamicLibrary')):
        raise ValueError('USA loadShopLibrary no longer matches the observed async loader')
    start = next(offset for offset,kind,name in segments if kind=='c' and name=='../shop/script_menu')
    end = next(offset for offset,_,_ in segments if offset>start)
    shop_main = words(main[start:end])
    # Infer a comparison bound from the final JR RA / delay slot. This is
    # not a section map; the report explicitly records that limitation.
    overlay = words(shop[0x40:])
    last_return = max(i for i,word in enumerate(overlay[:-1]) if word == 0x03e00008)
    overlay = overlay[:last_return+2]
    blocks = [block for block in difflib.SequenceMatcher(None,normalize(shop_main),normalize(overlay),
                                                        autojunk=False).get_matching_blocks() if block.size>=16]
    return dict(main_sha256=hashlib.sha256(main).hexdigest(),shop_sha256=hashlib.sha256(shop).hexdigest(),
        shop_size=len(shop),asm_verified_words=len(verified),text_words=len(code),
        direct_targets=direct,address_candidates=pointers,data_candidates=data,
        loadShopLibrary=loader,
        comparison=dict(main_start=hex(BASE+start),main_end=hex(BASE+end),main_words=len(shop_main),
            overlay_start=hex(LO+0x40),overlay_end=hex(LO+0x40+len(overlay)*4),overlay_words=len(overlay),
            normalized_matching_words=sum(block.size for block in blocks),minimum_run_words=16,
            runs=[dict(main_pc=hex(BASE+start+block.a*4),shop_pc=hex(LO+0x40+block.b*4),words=block.size) for block in blocks]),
        limitations=['Normalized runs mask J/JAL targets, RAM LUI/low relocations within four instructions and GP-relative offsets; they are structural matches, not proof of equivalent behavior.',
                     'Overlay code end is inferred from the final JR RA; data containing that opcode could extend the bound.',
                     'Linear address candidates do not resolve arbitrary computed pointers or indirect calls.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--main',type=Path,default=ROOT/'disks/us/SLUS_010.32')
    parser.add_argument('--shop',type=Path,default=ROOT/'disks/us/SHOP_REL.BIN')
    args=parser.parse_args()
    report=audit(args.main.read_bytes(),args.shop.read_bytes())
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
