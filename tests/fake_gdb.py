#!/usr/bin/env python3
"""Deterministic MI peer for transport/UI tests; never connects to hardware."""
import json
import os
import re
import shlex
import sys
import time

mode = os.environ.get('PICODBG_TEST_GDB_MODE', '')
log = os.environ.get('PICODBG_TEST_GDB_LOG')
chip = 0x4927 if mode in ('rp2350', 'wrong_arch') else 0x2927
pc = [0x10000200, 0x10000220]
breakpoints = {}
next_id = 0
elf_loaded = False


def send(text):
    # Exercise records split across process reads, including async records.
    mid = len(text) // 2
    sys.stdout.write(text[:mid]); sys.stdout.flush()
    time.sleep(0.001)
    sys.stdout.write(text[mid:] + '\n'); sys.stdout.flush()


def quote(text):
    return json.dumps(str(text), ensure_ascii=True)


send('(gdb)')
for line in sys.stdin:
    if log:
        with open(log, 'a', encoding='utf-8') as out:
            out.write(line)
    match = re.match(r'(\d+)(.*)', line.strip())
    if not match:
        sys.exit(3)
    token, command = match.groups()
    args = shlex.split(command)
    op = args[0]
    result = '^done'
    after = []
    if op == '-test-timeout':
        continue
    if op == '-test-exit':
        sys.exit(7)
    if op == '-test-unknown-token':
        send('999999^done'); continue
    if op == '-target-select':
        # GDB passes these arguments to the CLI without removing quotes.
        if not re.fullmatch(r'-target-select extended-remote [a-zA-Z0-9_.:\[\]-]+:[0-9]+', command):
            result = '^error,msg="Cannot resolve remote endpoint"'
        else:
            send('*stopped,reason="signal-received",signal-name="SIGTRAP",thread-id="1"')
            result = '^connected'
    elif op == '-thread-info':
        names = ['rp2040.core0', 'rp2040.core1']
        if mode == 'rp2350':
            names = ['rp2350.cm0', 'rp2350.cm1']
        if mode == 'unmapped':
            names = ['RTOS task A', 'RTOS task B']
        threads = ','.join(
            '{id=' + quote(i + 1) + ',target-id=' + quote(f'Thread {i + 1}')
            + (',name=' + quote(name) if i == 0 else ',details=' + quote(f'Name: {name}, state: debug-request'))
            + ',state="stopped"}'
            for i, name in enumerate(names)
        )
        result += ',threads=[' + threads + '],current-thread-id="1"'
    elif op == '-data-list-register-names':
        names = ['r' + str(i) for i in range(13)] + ['sp', 'lr', 'pc', 'xPSR', 'msp', 'psp', 'control', 'primask', 'fpscr', 's0']
        if mode == 'wrong_arch':
            names = ['zero', 'ra', 'sp', 'pc']
        result += ',register-names=[' + ','.join(map(quote, names)) + ']'
    elif op == '-file-exec-and-symbols':
        if len(args) != 2 or not os.path.isfile(args[1]):
            result = '^error,msg="ELF file not found"'
        elif mode == 'symbols_error' and elf_loaded:
            result = '^error,msg="Cannot load ELF symbols"'
        else:
            elf_loaded = True
    elif op == '-data-list-register-values':
        core = int(args[args.index('--thread') + 1]) - 1
        values = [i + core * 100 for i in range(13)] + [0x20041000, 0xffffffff, pc[core], 0x01000000, 0x20041000, 0, 0, 0, 0, 0x3f800000]
        result += ',register-values=[' + ','.join('{number=' + quote(i) + ',value=' + quote('<unavailable>' if i == 18 else hex(v)) + '}' for i, v in enumerate(values)) + ']'
    elif op == '-data-read-memory-bytes':
        address, length = int(args[-2], 0), int(args[-1])
        if mode == 'drop' and address == 0x20000000:
            sys.exit(9)
        if address == 0x40000000:
            data = (0x12345678 if mode == 'wrong_chip' else chip).to_bytes(4, 'little')
        elif address == 0x20001000:
            result = '^error,msg="Cannot access memory"'; data = None
        else:
            data = bytes((0x78, 0x56, 0x34, 0x12)) * ((length + 3) // 4)
            data = data[:length]
        if data is not None:
            result += ',memory=[{begin=' + quote(hex(address)) + ',offset="0",end=' + quote(hex(address + length)) + ',contents=' + quote(data.hex()) + '}]'
    elif op == '-data-disassemble':
        address = int(args[args.index('-s') + 1], 0)
        result += ',asm_insns=[' + ','.join('{address=' + quote(hex(address + 2 * i)) + ',func-name="Reset_Handler",offset=' + quote(2 * i) + ',inst="nop",opcodes="00 bf"}' for i in range(64)) + ']'
    elif op in ('-exec-continue', '-exec-step-instruction'):
        if mode == 'run_error':
            result = '^error,msg="Cannot insert hardware breakpoint: no resources"'
        else:
            result = '^running'
            after.append('*running,thread-id="all"')
            if op == '-exec-step-instruction':
                core = int(args[args.index('--thread') + 1]) - 1
                pc[core] += 2
                after.append('*stopped,reason="end-stepping-range",thread-id=' + quote(core + 1))
            elif breakpoints:
                after.append('*stopped,reason="breakpoint-hit",bkptno="1",thread-id="1"')
    elif op == '-exec-interrupt':
        after.append('*stopped,reason="signal-received",signal-name="SIGINT",thread-id="1"')
    elif op in ('-break-insert', '-break-watch'):
        if mode == 'no_slots':
            result = '^error,msg="No hardware breakpoint available"'
        else:
            next_id += 1
            kind = 'hw breakpoint' if op == '-break-insert' else 'watchpoint' if mode == 'software_watch' else 'hw watchpoint'
            breakpoints[str(next_id)] = kind
            result += (',bkpt=' if op == '-break-insert' else ',wpt=') + '{number=' + quote(next_id) + ',type=' + quote(kind) + ',exp="counter"}'
    elif op == '-break-info':
        result += ',BreakpointTable={body=[bkpt={number=' + quote(args[1]) + ',type=' + quote(breakpoints[args[1]]) + '}]}'
    elif op == '-break-delete':
        if len(args) == 1:
            breakpoints.clear()
        else:
            for key in args[1:]: breakpoints.pop(key, None)
    elif op == '-target-download':
        send('+download,section=".text",section-sent="64",section-size="64"')
        if mode == 'download_error':
            result = '^error,msg="Flash programming failed"'
    elif op == '-data-write-register-values':
        core = int(args[args.index('--thread') + 1]) - 1
        if int(args[-2]) == 15:
            pc[core] = int(args[-1], 0)
    elif op not in ('-gdb-set', '-interpreter-exec', '-thread-select', '-data-write-memory-bytes', '-target-disconnect'):
        result = '^error,msg=' + quote('Unexpected test command: ' + command)
    send(token + result)
    for event in after:
        send(event)
