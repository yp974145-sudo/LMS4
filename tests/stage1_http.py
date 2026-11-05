"""Real HTTP regression tests; every run uses a disposable data directory."""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import hashlib
import subprocess
import sys
import tempfile
import time
import unittest

EXE = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]

class Stage1(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp_root = Path(os.environ.get('LMS_TEST_TEMP_ROOT',str(Path(__file__).resolve().parents[1]/'.tmp')))
        temp_root.mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix='lms-stage1-',dir=temp_root)
        cls.log = open(Path(cls.temp.name)/'server.log','w',encoding='utf-8')
        with socket.socket() as probe:
            probe.bind(('127.0.0.1',0)); cls.port=probe.getsockname()[1]
        cls.process=subprocess.Popen([str(EXE),'--port',str(cls.port)],cwd=cls.temp.name,
            env={**{k:v for k,v in os.environ.items() if not k.startswith('LMS_')},
                 'LMS_JWT_SECRET':'stage1-test-secret-with-at-least-32-bytes'},
            stdout=cls.log,stderr=cls.log)
        try:
            for _ in range(100):
                try:
                    conn=http.client.HTTPConnection('127.0.0.1',cls.port,timeout=.3)
                    conn.request('GET','/api/health'); response=conn.getresponse(); response.read(); conn.close()
                    if response.status==200: break
                except (OSError,http.client.HTTPException): time.sleep(.05)
            else: raise RuntimeError('Server did not start')
            cls.tokens={}
            for name in ['admin','storage','dispatch','customer']:
                conn=http.client.HTTPConnection('127.0.0.1',cls.port,timeout=3)
                conn.request('POST','/api/auth/login',json.dumps({'username':name,'password':name+'123'}),
                             {'Content-Type':'application/json','Connection':'close'})
                response=conn.getresponse(); result=json.loads(response.read()); conn.close()
                if response.status != 200: raise RuntimeError(f'Cannot login demo account {name}: {result}')
                cls.tokens[name]=result['token']
        except BaseException:
            cls.process.terminate(); cls.process.wait(timeout=5); cls.log.close(); cls.temp.cleanup(); raise

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate(); cls.process.wait(timeout=5); cls.log.close(); cls.temp.cleanup()

    def request(self,method,path,body=None,actor='admin'):
        conn=http.client.HTTPConnection('127.0.0.1',self.port,timeout=5)
        headers={'Content-Type':'application/json','Connection':'close'}
        if actor is not None: headers['Authorization']='Bearer '+self.tokens.get(actor,actor)
        conn.request(method,path,None if body is None else json.dumps(body),headers)
        res=conn.getresponse(); data=res.read(); status=res.status; conn.close()
        return status,json.loads(data)

    def create(self,creator='spoofed-admin'):
        status,data=self.request('POST','/api/orders',{
            'customer_name':'Example','cargo_name':'Box','cargo_type':0,'cargo_quantity':1,'creator':creator},'customer')
        self.assertEqual(status,201,data)
        return data['order_id']

    def raw(self,data):
        with socket.create_connection(('127.0.0.1',self.port),timeout=5) as conn:
            conn.sendall(data); result=b''
            while True:
                part=conn.recv(8192)
                if not part: return result
                result+=part

    def test_current_identity_endpoint_is_authenticated_and_omits_passwords(self):
        self.assertEqual(self.request('GET','/api/auth/me',actor=None)[0],401)
        for role,name in enumerate(['admin','storage','dispatch','customer']):
            status,result=self.request('GET','/api/auth/me',actor=name)
            self.assertEqual(status,200)
            self.assertEqual(result['user']['username'],name)
            self.assertEqual(result['user']['role_id'],role)
            self.assertNotIn('password',result['user'])
            self.assertNotIn('password_hash',result['user'])

    def test_customer_edits_pending_order_fields_with_identity_preserved(self):
        path='/api/orders/'+self.create()
        body={'cargo_quantity':7,'cargo_type':2,'sender_address':'New sender',
              'receiver_address':'New receiver','creator':'spoofed'}
        body['edit_token']=self.request('GET',path)[1]['order']['edit_token']
        self.assertEqual(self.request('PUT',path,body,'customer')[0],200)
        order=self.request('GET',path)[1]['order']
        self.assertEqual(order['cargo_quantity'],7)
        self.assertEqual(order['cargo_type_id'],2)
        self.assertEqual(order['sender_address'],'New sender')
        self.assertEqual(order['receiver_address'],'New receiver')
        self.assertEqual(order['creator'],'customer')
        self.assertEqual(self.request('PUT',path,{'cargo_quantity':0,'edit_token':order['edit_token']},'customer')[0],400)
        self.assertEqual(self.request('PUT',path,body,'storage')[0],403)

    def test_stale_edits_and_concurrent_writers(self):
        path='/api/orders/'+self.create()
        revision=self.request('GET',path)[1]['order']['edit_token']
        self.assertEqual(self.request('PUT',path,{'cargo_quantity':8},'customer')[0],428)
        def write(value):return self.request('PUT',path,{'cargo_quantity':value,'edit_token':revision},'customer')[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results=list(pool.map(write,[8,9]))
        self.assertEqual(sorted(results),[200,409])
        saved=self.request('GET',path)[1]['order']
        self.assertIn(saved['cargo_quantity'],[8,9])
        self.assertEqual(write(10),409)
        self.assertEqual(self.request('GET',path)[1]['order']['cargo_quantity'],saved['cargo_quantity'])
        self.assertEqual(self.request('PUT',path,{'cargo_quantity':saved['cargo_quantity'],'edit_token':saved['edit_token']},'customer')[0],200)
        self.assertEqual(self.request('POST',path+'/approve',{})[0],200)
        self.assertEqual(self.request('PUT',path,{'cargo_quantity':11,'edit_token':saved['edit_token']},'customer')[0],409)

    def test_authentication_lifetime_and_bad_token(self):
        # register_routes has returned long before this handler executes.
        for actor in [None,'not-a-valid-token']:
            self.assertEqual(self.request('GET','/api/orders',actor=actor)[0],401)
        self.assertEqual(self.request('GET','/api/users')[0],200)

    def test_role_permissions_and_live_role_lookup(self):
        body={'username':'forbidden','password':'password123','role':0}
        self.assertEqual(self.request('POST','/api/users',body,'customer')[0],403)
        self.assertEqual(self.request('GET','/api/users',actor='storage')[0],403)
        self.assertEqual(self.request('POST','/api/users',body)[0],201)

    def test_existing_token_obeys_current_role_and_account_status(self):
        body={'username':'role-probe','password':'password123','role':3}
        self.assertEqual(self.request('POST','/api/users',body)[0],201)
        status,result=self.request('POST','/api/auth/login',body,actor=None)
        self.assertEqual(status,200)
        token=result['token']
        self.assertEqual(self.request('GET','/api/users',actor=token)[0],403)
        # Fixture-only mutation: emulate an administrator changing persisted account state.
        # No request is in flight while replacing these two bytes in the disposable file.
        path=Path(self.temp.name)/'user.dat'
        original=path.read_bytes()
        is_snapshot=original.startswith(b'LMSDAT01')
        data=bytearray(original[81:] if is_snapshot else original); position=0; role_offset=None
        while position<len(data):
            position+=4  # serialized user id
            length=struct.unpack_from('@i',data,position)[0]; position+=4
            name=bytes(data[position:position+length]).decode(); position+=length
            length=struct.unpack_from('@i',data,position)[0]; position+=4+length
            if name=='role-probe': role_offset=position; break
            position+=2
        self.assertIsNotNone(role_offset)
        def persist_fixture():
            if is_snapshot:
                path.write_bytes(original[:17]+hashlib.sha256(data).hexdigest().encode()+data)
            else:
                path.write_bytes(data)
        try:
            data[role_offset]=0; persist_fixture()
            self.assertEqual(self.request('GET','/api/users',actor=token)[0],200)
            data[role_offset]=3; persist_fixture()
            self.assertEqual(self.request('GET','/api/users',actor=token)[0],403)
            data[role_offset+1]=1; persist_fixture()
            self.assertEqual(self.request('GET','/api/orders',actor=token)[0],401)
        finally:
            path.write_bytes(original)

    def test_order_lifecycle_and_caller_cannot_spoof_identity(self):
        oid=self.create()
        self.assertEqual(self.request('GET','/api/orders/'+oid)[1]['order']['creator'],'customer')
        path='/api/orders/'+oid
        self.assertEqual(self.request('POST',path+'/approve',{},'customer')[0],403)
        self.assertEqual(self.request('POST',path+'/approve',{'auditor':'spoofed'})[0],200)
        self.assertEqual(self.request('GET',path)[1]['order']['auditor'],'admin')
        self.assertEqual(self.request('POST',path+'/approve',{})[0],400)
        self.assertEqual(self.request('POST',path+'/out-stock',{},'dispatch')[0],403)
        for action,actor in [('out-stock','storage'),('start-transport','dispatch'),
                             ('deliver','dispatch'),('complete','customer')]:
            self.assertEqual(self.request('POST',path+'/'+action,{'operator':'spoofed'},actor)[0],200)
        order=self.request('GET',path)[1]['order']
        self.assertEqual(order['status_id'],6); self.assertEqual(order['auditor'],'customer')

    def test_concurrent_creation_has_no_lost_updates(self):
        before=self.request('GET','/api/orders?page_size=100')[1]['total_count']
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as workers:
            ids=list(workers.map(lambda _:self.create(),range(24)))
        self.assertEqual(len(set(ids)),24)
        after=self.request('GET','/api/orders?page_size=100')[1]['total_count']
        self.assertEqual(after-before,24)

    def test_concurrent_approval_transitions_only_once(self):
        path='/api/orders/'+self.create()+'/approve'
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as workers:
            statuses=list(workers.map(lambda _:self.request('POST',path,{})[0],range(8)))
        self.assertEqual(statuses.count(200),1); self.assertEqual(statuses.count(400),7)

    def test_invalid_pagination_returns_400_and_server_survives(self):
        for path in ['/api/orders','/api/users']:
            for query in ['page=0','page=-1','page=abc','page=2abc','page_size=0',
                          'page_size=101','page=99999999999999999999']:
                with self.subTest(path=path,query=query):
                    self.assertEqual(self.request('GET',path+'?'+query)[0],400)
        self.assertEqual(self.request('GET','/api/health',actor=None)[0],200)

    def test_http_pipelining_keeps_next_request_bytes(self):
        result=self.raw(b'GET /api/health HTTP/1.1\r\nHost: localhost\r\n\r\n'
                        b'GET /api/status HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
        self.assertEqual(result.count(b'HTTP/1.1 200'),2)

    def test_fragmented_request_and_persistent_connection(self):
        conn=http.client.HTTPConnection('127.0.0.1',self.port,timeout=5)
        for _ in range(3):
            conn.request('GET','/api/health'); res=conn.getresponse(); res.read(); self.assertEqual(res.status,200)
        conn.close()
        with socket.create_connection(('127.0.0.1',self.port),timeout=5) as sock:
            for chunk in [b'GET /api/he',b'alth HTTP/1.1\r\nHost: local',b'host\r\nConnection: close\r\n\r\n']:
                sock.sendall(chunk); time.sleep(.01)
            self.assertIn(b'200',sock.recv(1024).split(b'\r\n')[0])

    def test_malformed_http_is_rejected(self):
        for headers in [b'Content-Length: -1',b'Content-Length: abc',b'Content-Length: 1048577',
                        b'Transfer-Encoding: chunked',b'Content-Length: 0\r\nContent-Length: 0']:
            with self.subTest(headers=headers):
                result=self.raw(b'POST /api/auth/login HTTP/1.1\r\nHost: local\r\n'+headers+b'\r\nConnection: close\r\n\r\n')
                self.assertIn(b'400',result.split(b'\r\n')[0])
        self.assertIn(b'400',self.raw(b'GET / HTTP/1.1\r\nX: '+b'a'*17000+b'\r\n\r\n').split(b'\r\n')[0])

    def test_truncated_body_does_not_invoke_login(self):
        with socket.create_connection(('127.0.0.1',self.port),timeout=5) as sock:
            sock.sendall(b'POST /api/auth/login HTTP/1.1\r\nContent-Length: 100\r\n\r\n{}')
            sock.shutdown(socket.SHUT_WR)
            self.assertEqual(sock.recv(1024),b'')
        self.assertEqual(self.request('GET','/api/health',actor=None)[0],200)

    def test_enum_values_checked_before_narrowing(self):
        for value in [-1, 4, 256, 259, 1.5, True, "0", 4294967296]:
            with self.subTest(value=value):
                body={'username':'bad-role','password':'password123','role':value}
                self.assertEqual(self.request('POST','/api/users',body)[0],400)
                body={'customer_name':'Example','cargo_name':'Box','cargo_type':value}
                self.assertEqual(self.request('POST','/api/orders',body,'customer')[0],400)
        for value in ['-1','7','256','2abc','1.5','', '999999999999999999999']:
            self.assertEqual(self.request('GET','/api/orders?status='+value)[0],400)
        self.assertEqual(self.request('GET','/api/orders?status=0')[0],200)

    def test_order_numeric_and_text_boundaries(self):
        base={'customer_name':'Example','cargo_name':'Box','cargo_type':0}
        for field,value in [('cargo_quantity',0),('cargo_quantity',-1),
                            ('cargo_quantity',4294967297),('cargo_quantity',1.5),
                            ('cargo_weight',-1),('cargo_volume',-1),
                            ('customer_name','x'*256)]:
            with self.subTest(field=field,value=value):
                self.assertEqual(self.request('POST','/api/orders',{**base,field:value},'customer')[0],400)
        self.assertEqual(self.request('POST','/api/orders',{**base,'customer_name':'x'*255},'customer')[0],201)

    def test_invalid_transitions_and_rejection(self):
        path='/api/orders/'+self.create()
        for action,actor in [('out-stock','storage'),('start-transport','dispatch'),
                             ('deliver','dispatch'),('complete','customer')]:
            self.assertEqual(self.request('POST',path+'/'+action,{},actor)[0],400)
        for reason in ['', 'x'*256]:
            self.assertEqual(self.request('POST',path+'/reject',{'reason':reason})[0],400)
        self.assertEqual(self.request('POST',path+'/reject',{'reason':'damaged','auditor':'spoofed'})[0],200)
        self.assertEqual(self.request('POST',path+'/approve',{})[0],400)
        self.assertEqual(self.request('PUT',path,{'cargo_name':'changed','edit_token':self.request('GET',path)[1]['order']['edit_token']},'customer')[0],409)
        order=self.request('GET',path)[1]['order']
        self.assertEqual(order['status_id'],1)
        self.assertEqual(order['auditor'],'admin')

    def test_concurrent_approve_and_reject_are_exclusive(self):
        path='/api/orders/'+self.create()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
            approve=workers.submit(self.request,'POST',path+'/approve',{})
            reject=workers.submit(self.request,'POST',path+'/reject',{'reason':'damaged'})
            self.assertEqual(sorted([approve.result()[0],reject.result()[0]]),[200,400])
        self.assertIn(self.request('GET',path)[1]['order']['status_id'],[1,2])

if __name__=='__main__': unittest.main(verbosity=2)
