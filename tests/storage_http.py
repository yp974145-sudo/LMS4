"""Storage safety regressions against a real server; all fixtures live on E in .tmp."""
import ctypes
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

EXE=Path(sys.argv[1]).resolve()
TOOL=Path(sys.argv[2]).resolve()
sys.argv=sys.argv[:1]
ROOT=Path(__file__).resolve().parents[1]/'.tmp'
ROOT.mkdir(exist_ok=True)
MAGIC=b'LMSDAT01'

def payload(data):
    if data.startswith(MAGIC):
        assert len(data)>=81
        body=data[81:]
        assert int.from_bytes(data[9:17],'little')==len(body)
        assert data[17:81]==hashlib.sha256(body).hexdigest().encode()
        return body
    return data

def snapshot(kind,body):
    return MAGIC+kind+len(body).to_bytes(8,'little')+hashlib.sha256(body).hexdigest().encode()+body

class Storage(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='storage-',dir=ROOT)
        self.directory=Path(self.temporary.name)
        self.process=None; self.output=None
        self.env={k:v for k,v in os.environ.items() if not k.startswith('LMS_')}
        self.env['LMS_JWT_SECRET']='storage-test-secret-at-least-32-bytes'
        self.addCleanup(self.cleanup)
        self.tokens={}

    def cleanup(self):
        self.stop()
        self.temporary.cleanup()

    def stop(self):
        if self.process is not None:
            if self.process.poll() is None: self.process.terminate()
            try: self.process.wait(timeout=5)
            except subprocess.TimeoutExpired: self.process.kill(); self.process.wait(timeout=5)
            self.process=None
        if self.output is not None: self.output.close();self.output=None

    @staticmethod
    def port_number():
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0));return sock.getsockname()[1]

    def start(self):
        self.port=self.port_number()
        self.output=open(self.directory/'server.log','ab')
        self.process=subprocess.Popen([str(EXE),'--port',str(self.port)],cwd=self.directory,
                                      env=self.env,stdout=self.output,stderr=self.output)
        for _ in range(100):
            if self.process.poll() is not None:
                self.fail((self.directory/'server.log').read_text(encoding='utf-8',errors='replace'))
            try:
                status,_=self.request('GET','/api/health',actor=None)
                if status==200: break
            except (OSError,http.client.HTTPException): time.sleep(.03)
        else: self.fail('Server startup timed out')
        for name in ['customer','admin']:
            status,result=self.request('POST','/api/auth/login',{'username':name,'password':name+'123'},actor=None)
            self.assertEqual(status,200,result)
            self.tokens[name]=result['token']

    def request(self,method,path,body=None,actor='admin'):
        connection=http.client.HTTPConnection('127.0.0.1',self.port,timeout=4)
        headers={'Content-Type':'application/json','Connection':'close'}
        if actor is not None: headers['Authorization']='Bearer '+self.tokens[actor]
        connection.request(method,path,None if body is None else json.dumps(body),headers)
        response=connection.getresponse();data=json.loads(response.read());connection.close()
        return response.status,data

    def create(self):
        status,result=self.request('POST','/api/orders',{'customer_name':'Example','cargo_name':'Box','cargo_type':0},'customer')
        self.assertEqual(status,201,result);return result['order_id']

    def new_user(self):
        status,result=self.request('POST','/api/users',{'username':'probe','password':'password123','role':3})
        self.assertEqual(status,201,result)

    def tool(self,action,kind,path):
        return subprocess.run([str(TOOL),action,kind,str(path)],cwd=self.directory,env=self.env,
                              capture_output=True,text=True,timeout=5)

    def startup_fails(self):
        result=subprocess.run([str(EXE),'--port',str(self.port_number())],cwd=self.directory,
                              env=self.env,capture_output=True,timeout=5)
        self.assertNotEqual(result.returncode,0)
        return result

    def test_committed_orders_survive_process_restart(self):
        self.start();oid=self.create()
        self.assertEqual(self.request('POST','/api/orders/'+oid+'/approve',{})[0],200)
        before=(self.directory/'order.dat').read_bytes()
        self.stop();self.start()
        self.assertEqual(self.request('GET','/api/orders/'+oid)[1]['order']['status_id'],2)
        self.assertEqual((self.directory/'order.dat').read_bytes(),before)
        self.assertEqual(list(self.directory.glob('*.writing-*')),[])

    def test_legacy_files_read_without_rewrite_then_upgrade_on_commit(self):
        self.start();oid=self.create();self.stop()
        originals={}
        for name in ['user.dat','order.dat']:
            path=self.directory/name;body=payload(path.read_bytes());path.write_bytes(body);originals[name]=body
        self.start()
        self.assertEqual(self.request('GET','/api/orders/'+oid)[0],200)
        for name,body in originals.items(): self.assertEqual((self.directory/name).read_bytes(),body)
        self.new_user();self.create()
        for name,body in originals.items():
            self.assertTrue((self.directory/name).read_bytes().startswith(MAGIC))
            self.assertEqual((self.directory/(name+'.bak')).read_bytes(),body)

    def test_corrupt_user_snapshots_refuse_start_without_resetting_accounts(self):
        self.start();self.new_user();self.stop()
        path=self.directory/'user.dat';good=path.read_bytes();backup=(self.directory/'user.dat.bak').read_bytes()
        raw=payload(good)
        invalid_length=raw[:4]+struct.pack('@i',-1)+raw[8:]
        for broken in [good[:-1],good+b'garbage',good[:-1]+bytes([good[-1]^1]),b'',invalid_length,
                       snapshot(b'U',raw+raw)]:
            with self.subTest(length=len(broken)):
                path.write_bytes(broken);self.startup_fails()
                self.assertEqual(path.read_bytes(),broken)
                self.assertEqual((self.directory/'user.dat.bak').read_bytes(),backup)
        path.write_bytes(good)

    def test_whole_record_truncation_is_detected_by_snapshot_length(self):
        self.start();self.create()
        first_body=payload((self.directory/'order.dat').read_bytes())
        self.create();self.stop()
        path=self.directory/'order.dat';current=path.read_bytes()
        broken=current[:81]+first_body
        path.write_bytes(broken)
        self.startup_fails()
        self.assertEqual(path.read_bytes(),broken)

    def test_corrupt_order_snapshots_refuse_startup(self):
        self.start();self.create();self.stop()
        path=self.directory/'order.dat';good=path.read_bytes();raw=payload(good)
        for broken in [good[:-1],good+b'garbage',b'',struct.pack('@i',256)+raw[4:],snapshot(b'O',raw+raw)]:
            with self.subTest(length=len(broken)):
                path.write_bytes(broken);self.startup_fails();self.assertEqual(path.read_bytes(),broken)
        path.write_bytes(good)

    def test_corruption_during_runtime_returns_500_without_overwriting_evidence(self):
        self.start();self.create()
        path=self.directory/'order.dat';broken=path.read_bytes()[:-1];backup=(self.directory/'order.dat.bak').read_bytes()
        path.write_bytes(broken)
        for method,route,body in [('GET','/api/orders',None),('POST','/api/orders',{'customer_name':'New','cargo_name':'Box','cargo_type':0})]:
            status,result=self.request(method,route,body)
            self.assertEqual(status,500,result)
            self.assertNotIn(str(self.directory),json.dumps(result))
            self.assertEqual(path.read_bytes(),broken)
            self.assertEqual((self.directory/'order.dat.bak').read_bytes(),backup)
        self.assertEqual(self.request('GET','/api/health',actor=None)[0],200)

    def test_login_storage_failure_is_500_instead_of_bad_password(self):
        self.start();path=self.directory/'user.dat';path.write_bytes(path.read_bytes()[:-1])
        status,_=self.request('POST','/api/auth/login',{'username':'admin','password':'admin123'},actor=None)
        self.assertEqual(status,500)
        self.assertEqual(self.request('GET','/api/health',actor=None)[0],200)

    def test_backup_destination_failure_preserves_main_and_allows_retry(self):
        self.start();self.create()
        path=self.directory/'order.dat';before=path.read_bytes();backup=self.directory/'order.dat.bak'
        backup.unlink();backup.mkdir();(backup/'blocker').write_text('owned test fixture')
        body={'customer_name':'New','cargo_name':'Box','cargo_type':0}
        self.assertEqual(self.request('POST','/api/orders',body,'customer')[0],500)
        self.assertEqual(path.read_bytes(),before)
        self.assertEqual(list(self.directory.glob('*.writing-*')),[])
        (backup/'blocker').unlink();backup.rmdir()
        self.assertEqual(self.request('POST','/api/orders',body,'customer')[0],201)
        self.assertEqual(backup.read_bytes(),before)
        self.assertEqual(self.request('GET','/api/orders')[1]['total_count'],2)

    @unittest.skipUnless(os.name=='nt','Windows file-sharing failure test')
    def test_windows_replace_failure_preserves_old_data(self):
        self.start();self.create();path=self.directory/'order.dat';before=path.read_bytes()
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
        kernel.CreateFileW.restype=ctypes.c_void_p
        kernel.CloseHandle.argtypes=[ctypes.c_void_p];kernel.CloseHandle.restype=ctypes.c_int
        handle=kernel.CreateFileW(str(path),0x80000000,3,None,3,0,None)
        self.assertNotEqual(handle,ctypes.c_void_p(-1).value)
        try:
            body={'customer_name':'New','cargo_name':'Box','cargo_type':0}
            self.assertEqual(self.request('POST','/api/orders',body,'customer')[0],500)
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual((self.directory/'order.dat.bak').read_bytes(),before)
        finally: kernel.CloseHandle(handle)
        self.assertEqual(self.request('POST','/api/orders',body,'customer')[0],201)

    def test_explicit_restore_validates_backup_and_archives_damaged_main(self):
        self.start();oid=self.create();self.create()
        self.assertEqual(self.request('POST','/api/orders/'+oid+'/approve',{})[0],200)
        self.stop();path=self.directory/'order.dat';backup=self.directory/'order.dat.bak';previous=backup.read_bytes()
        broken=path.read_bytes()[:-5];path.write_bytes(broken)
        self.assertNotEqual(self.tool('--check','orders',path).returncode,0)
        restored=self.tool('--restore','orders',path)
        self.assertEqual(restored.returncode,0,restored.stderr)
        self.assertEqual(path.read_bytes(),previous);self.assertEqual(backup.read_bytes(),previous)
        archived=list(self.directory.glob('order.dat.damaged-*'))
        self.assertEqual(len(archived),1);self.assertEqual(archived[0].read_bytes(),broken)
        self.start()
        self.assertEqual(self.request('GET','/api/orders/'+oid)[1]['order']['status_id'],0)
        self.assertEqual(self.request('GET','/api/orders')[1]['total_count'],2)

    def test_invalid_backup_never_replaces_damaged_main(self):
        self.start();self.create();self.stop()
        path=self.directory/'order.dat';path.write_bytes(b'broken main')
        backup=self.directory/'order.dat.bak';backup.write_bytes(b'broken backup')
        result=self.tool('--restore','orders',path)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(path.read_bytes(),b'broken main');self.assertEqual(backup.read_bytes(),b'broken backup')
        self.assertEqual(list(self.directory.glob('*.damaged-*')),[])

    def test_directory_lock_blocks_recovery_and_second_instance(self):
        self.start();self.create()
        before=(self.directory/'order.dat').read_bytes()
        result=self.tool('--restore','orders',self.directory/'order.dat')
        self.assertNotEqual(result.returncode,0)
        self.assertRegex(result.stderr.lower(),r'lock|directory is in use')
        self.assertEqual((self.directory/'order.dat').read_bytes(),before)
        self.startup_fails()
        self.assertEqual(self.request('GET','/api/health',actor=None)[0],200)
        self.stop()
        self.assertEqual(self.tool('--check','orders',self.directory/'order.dat').returncode,0)

    def test_missing_user_main_with_backup_requires_explicit_restore(self):
        self.start();self.new_user();self.stop()
        path=self.directory/'user.dat';backup=self.directory/'user.dat.bak';before=backup.read_bytes();path.unlink()
        self.startup_fails();self.assertFalse(path.exists());self.assertEqual(backup.read_bytes(),before)
        self.assertEqual(self.tool('--restore','users',path).returncode,0)
        self.start()

    def test_missing_order_main_with_backup_never_becomes_empty_list(self):
        self.start();self.create();self.create();self.stop()
        path=self.directory/'order.dat';backup=self.directory/'order.dat.bak';before=backup.read_bytes();path.unlink()
        self.startup_fails();self.assertFalse(path.exists());self.assertEqual(backup.read_bytes(),before)
        self.assertEqual(self.tool('--restore','orders',path).returncode,0)
        self.start();self.assertEqual(self.request('GET','/api/orders')[1]['total_count'],1)

    def test_uncommitted_temporary_file_is_never_promoted(self):
        self.start();oid=self.create();self.stop()
        stale=self.directory/'order.dat.writing-interrupted';stale.write_bytes(b'incomplete uncommitted write')
        self.start();self.assertEqual(self.request('GET','/api/orders/'+oid)[0],200)
        self.assertEqual(stale.read_bytes(),b'incomplete uncommitted write')

if __name__=='__main__': unittest.main(verbosity=2)
