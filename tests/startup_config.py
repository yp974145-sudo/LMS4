"""Startup regression tests. All state stays under the project's .tmp directory."""
import contextlib
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

EXE = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]
TEMP_ROOT = Path(__file__).resolve().parents[1] / '.tmp'
TEMP_ROOT.mkdir(exist_ok=True)

class Startup(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='startup-', dir=TEMP_ROOT)
        self.addCleanup(self.temp.cleanup)
        self.cwd = Path(self.temp.name)
        self.env = {k:v for k,v in os.environ.items() if not k.startswith('LMS_')}
        self.env['LMS_JWT_SECRET'] = 'startup-test-secret-with-at-least-32-bytes'

    def config(self, **values):
        path = self.cwd/'test.json'
        path.write_text(json.dumps({'mysql':{'enabled':False}, **values}), encoding='utf-8')
        return str(path)

    def fails(self, args, env=None):
        result = subprocess.run([str(EXE),*args], cwd=self.cwd,
                                env={**self.env,**(env or {})}, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse((self.cwd/'user.dat').exists())
        return result.stderr.decode('utf-8',errors='replace')

    @contextlib.contextmanager
    def running(self, args, port, env=None):
        # File logging avoids filling stdout while startup is being polled.
        with open(self.cwd/'process.log','wb') as output:
            process = subprocess.Popen([str(EXE),*args], cwd=self.cwd,
                         env={**self.env,**(env or {})}, stdout=output,stderr=output)
            try:
                for _ in range(100):
                    if process.poll() is not None:
                        self.fail((self.cwd/'process.log').read_text(encoding='utf-8',errors='replace'))
                    try:
                        conn=http.client.HTTPConnection('127.0.0.1',port,timeout=.2)
                        conn.request('GET','/api/status')
                        response=conn.getresponse(); data=json.loads(response.read()); conn.close()
                        if response.status==200: break
                    except (OSError,http.client.HTTPException): time.sleep(.03)
                else: self.fail('Server did not start on selected port')
                yield data
            finally:
                if process.poll() is None: process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)

    @staticmethod
    def free_port():
        with socket.socket() as probe:
            probe.bind(('127.0.0.1',0))
            return probe.getsockname()[1]

    def test_config_selects_port_and_file_backend(self):
        port=self.free_port()
        with self.running(['--config',self.config(port=port)],port) as data:
            self.assertEqual(data['backend'],'File Storage')

    def test_learning_data_directory_preserves_original_files(self):
        original=b'existing-data-must-not-be-opened-or-replaced'
        (self.cwd/'user.dat').write_bytes(original)
        (self.cwd/'order.dat').write_bytes(original)
        port=self.free_port()
        args=['--config',self.config(port=port,data_directory='data/learning')]
        with self.running(args,port):
            self.assertTrue((self.cwd/'data/learning/user.dat').is_file())
            self.assertEqual((self.cwd/'user.dat').read_bytes(),original)
            self.assertEqual((self.cwd/'order.dat').read_bytes(),original)

    def test_environment_overrides_file(self):
        port=self.free_port()
        with self.running(['--config',self.config(port=0)],port,{'LMS_PORT':str(port)}): pass

    def test_command_line_overrides_environment(self):
        port=self.free_port()
        with self.running(['--port',str(port),'--config',self.config(port=0)],port,{'LMS_PORT':'1'}): pass

    def test_invalid_config_and_missing_explicit_path_fail(self):
        self.assertIn('Cannot open',self.fails(['--config','missing.json']))
        path=self.cwd/'broken.json'; path.write_text('{broken',encoding='utf-8')
        self.assertIn('Invalid configuration',self.fails(['--config',str(path)]))
        self.fails(['--config',self.config(port=1.5)])
        self.fails(['--config',self.config(thread_count=0)])
        self.fails(['--config',self.config(bind_address='not-an-address')])

    def test_port_boundaries_and_bad_arguments(self):
        for value in ['0','65536','-1','2abc','999999999999999999999']:
            with self.subTest(value=value): self.fails(['--port',value])
        for args in [['--port'],['--unknown','1'],['--port','12345','--port','12346']]:
            self.fails(args)

    def test_invalid_environment_and_short_secret_fail(self):
        for env in [{'LMS_PORT':'bad'}, {'LMS_MYSQL_ENABLED':'maybe'}, {'LMS_JWT_SECRET':'short'}]:
            self.fails([],env)

    @unittest.skipUnless(os.environ.get('LMS_TEST_FILE_ONLY')=='1','requires a known file-only executable')
    def test_mysql_request_never_silently_falls_back(self):
        error=self.fails(['--config',self.config(mysql={'enabled':True})])
        self.assertIn('MySQL requested but not compiled',error)

if __name__=='__main__': unittest.main(verbosity=2)
