#!/usr/bin/env python3
"""Owned-process lifecycle test double. No USB device or socket is opened."""
import json
import os
import sys
import time

with open(os.environ['PICODBG_TEST_SERVER_LOG'], 'w', encoding='utf-8') as out:
    json.dump(sys.argv[1:], out)
mode = os.environ.get('PICODBG_TEST_SERVER_MODE', '')
if mode == 'fail':
    print('Error: CMSIS-DAP probe not found', flush=True)
    sys.exit(1)
port = next(arg.split()[1] for arg in sys.argv if arg.startswith('gdb_port '))
if mode != 'no_port':
    print(f'Info : Listening on port {port} for gdb connections', flush=True)
while True:
    time.sleep(1)
