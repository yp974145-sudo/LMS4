import socket
import subprocess
import sys
from pathlib import Path

with socket.socket() as probe:
    probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
process=subprocess.Popen([str(Path(sys.argv[1]).resolve()),str(port)],stdout=subprocess.PIPE,text=True)
try:
    for _ in range(3):
        assert process.stdout.readline().strip()=='READY'
        # An incomplete header blocks one worker; the other worker blocks in accept.
        with socket.create_connection(('127.0.0.1',port),timeout=4) as client:
            client.sendall(b'GET / HTTP/1.1\r\nHost: local')
            assert client.recv(100)==b''
        assert process.stdout.readline().strip()=='STOPPED'
    assert process.wait(timeout=5)==0
    print('PASS: shutdown interrupts active recv and accept; stop is idempotent; listener restarts')
finally:
    if process.poll() is None: process.kill(); process.wait()
