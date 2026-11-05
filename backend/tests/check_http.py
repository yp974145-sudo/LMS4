"""Framework landing check, isolated port and disposable working directory."""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

exe = str(Path(sys.argv[1]).resolve())
root = os.environ.get('LMS_TEST_TEMP_ROOT')
if root is None and os.name == 'nt':
    root = r'C:\Users\23974\Desktop\jk\x'
if root:
    Path(root).mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='lms-drogon-test-', dir=root) as folder:
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    with open(Path(folder)/'server.log', 'wb') as log:
        process = subprocess.Popen([exe, '--port', str(port)], cwd=folder,
                                   stdout=log, stderr=log)
        def call(path):
            conn = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
            try:
                conn.request('GET', path)
                response = conn.getresponse()
                return response.status, response.read()
            finally:
                conn.close()
        try:
            for attempt in range(100):
                if process.poll() is not None:
                    raise RuntimeError('Server exited with '+str(process.returncode)+'; '+Path(folder, 'server.log').read_text(errors='replace'))
                try:
                    code, body = call('/api/health')
                    break
                except OSError:
                    time.sleep(.05)
            else:
                raise RuntimeError('Server did not start')
            data = json.loads(body)
            assert code == 200 and data['status'] == 'ok'
            assert data['framework'] == 'drogon'
            assert data['platform'] == ('windows' if os.name == 'nt' else 'linux')
            assert call('/api/does-not-exist')[0] == 404
        finally:
            process.terminate()
            process.wait(timeout=10)
    for value in ['0', '65536', '-1', '123x']:
        result = subprocess.run([exe, '--port', value], cwd=folder,
                                capture_output=True, timeout=10)
        assert result.returncode != 0
print('PASS: real Drogon HTTP health, platform identity, 404 and four invalid ports')
