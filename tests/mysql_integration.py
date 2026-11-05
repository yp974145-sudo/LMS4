"""Real MySQL integration: create and delete only a unique test schema in the E-drive instance."""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from mysql_support import Mysql

root=Path(__file__).resolve().parents[1]
exe=Path(sys.argv[1]).resolve()
pool_test=Path(sys.argv[2]).resolve()
admin=json.loads((root/'.mysql-learning/admin.json').read_text(encoding='utf-8'))
schema='lms_roles_test_'+uuid.uuid4().hex[:12]
assert re.fullmatch(r'lms_roles_test_[0-9a-f]{12}',schema)
checks=[]
with Mysql(admin) as database, tempfile.TemporaryDirectory(prefix='mysql-integration-',dir=root/'.tmp') as temporary:
    fixture=Path(temporary)
    datadir,tmpdir,version=database.query('SELECT @@datadir,@@tmpdir,VERSION()')[0]
    assert Path(datadir).resolve()==(root/'.mysql-learning/data').resolve()
    assert Path(tmpdir).resolve().is_relative_to(root)
    database.query(f'CREATE DATABASE `{schema}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci')
    processes=[];logs=[]
    env={k:v for k,v in os.environ.items() if not k.startswith('LMS_')}
    env['PATH']='E:/Program Files/MySQL/MySQL Server 8.0/lib;E:/Program Files/MySQL/MySQL Server 8.0/bin;'+env['PATH']
    env['LMS_JWT_SECRET']='mysql-integration-test-key-at-least-32-bytes'
    env['TEMP']=str(fixture);env['TMP']=str(fixture)
    def call(port,method,path,body=None,token=None):
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=8)
        headers={'Content-Type':'application/json','Connection':'close'}
        if token:headers['Authorization']='Bearer '+token
        c.request(method,path,None if body is None else json.dumps(body),headers)
        response=c.getresponse();data=json.loads(response.read());c.close();return response.status,data
    config={'port':18080,'thread_count':4,'mysql':{**admin,'database':schema,'enabled':True,'pool_size':1},'log':{'file':str(fixture/'app.log')}}
    config_path=fixture/'config.json';config_path.write_text(json.dumps(config),encoding='utf-8')
    def start():
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        log=open(fixture/f'server-{port}.log','wb');logs.append(log)
        process=subprocess.Popen([str(exe),'--config',str(config_path),'--port',str(port)],cwd=root,env=env,stdout=log,stderr=log);processes.append(process)
        for _ in range(100):
            if process.poll() is not None:raise RuntimeError('MySQL-backed app exited; inspect fixture log')
            try:
                if call(port,'GET','/api/health')[0]==200:break
            except (OSError,http.client.HTTPException):time.sleep(.05)
        else:raise RuntimeError('MySQL app did not start')
        tokens={}
        for name in ['admin','storage','dispatch','customer']:
            status,result=call(port,'POST','/api/auth/login',{'username':name,'password':name+'123'})
            assert status==200,(name,status,result);tokens[name]=result['token']
        assert call(port,'GET','/api/status')[1]['backend'].startswith('MySQL')
        return port,tokens,process
    def passed(message):checks.append(message);print('PASS:',message,flush=True)
    try:
        port,tokens,process=start()
        for role,name in enumerate(['admin','storage','dispatch','customer']):
            assert call(port,'GET','/api/auth/me',token=tokens[name])[1]['user']['role_id']==role
        passed('real MySQL backend and four demo-account logins/current identities')
        def create():
            status,data=call(port,'POST','/api/orders',{'customer_name':'Mysql Example','cargo_name':'Box','cargo_type':0,'cargo_weight':2.5,'cargo_quantity':3,'creator':'spoofed'},tokens['customer'])
            assert status==201,data;return data['order_id']
        oid=create();path='/api/orders/'+oid
        assert call(port,'POST',path+'/approve',{},tokens['customer'])[0]==403
        assert call(port,'GET','/api/users',token=tokens['storage'])[0]==403
        for action,name in [('approve','admin'),('out-stock','storage'),('start-transport','dispatch'),('deliver','dispatch'),('complete','customer')]:
            assert call(port,'POST',path+'/'+action,{'auditor':'spoofed'},tokens[name])[0]==200,(action,name)
        row=database.query(f"SELECT status,creator,auditor,cargo_quantity,cargo_weight FROM `{schema}`.orders WHERE order_id='{oid}'")[0]
        assert row==['6','customer','customer','3','2.5'],row
        passed('complete order lifecycle, SQL values and server-owned actor identity')
        payload={'customer_name':'x'*255,'cargo_name':'long value','cargo_type':1,'cargo_quantity':1,'sender_address':'发送地址','receiver_address':'收货地址'}
        status,data=call(port,'POST','/api/orders',payload,tokens['customer']);assert status==201,data
        order=call(port,'GET','/api/orders/'+data['order_id'],token=tokens['admin'])[1]['order']
        assert order['customer_name']=='x'*255 and order['receiver_address']=='收货地址'
        assert call(port,'GET','/api/orders?page=0',token=tokens['admin'])[0]==400
        passed('255-byte fields, UTF-8 result data, pagination rejection')
        user={'username':'mysql_created','password':'password123','role':3}
        assert call(port,'POST','/api/users',user,tokens['admin'])[0]==201
        assert call(port,'POST','/api/users',user,tokens['admin'])[0]==409
        assert call(port,'POST','/api/users/999/reset-password',{'username':'mysql_created'},tokens['admin'])[0]==200
        assert call(port,'POST','/api/auth/login',{'username':'mysql_created','password':'888888'})[0]==200
        passed('user creation, duplicate conflict and password reset via prepared statements')
        oid=create();second,other_tokens,second_process=start()
        edit_id=create();edit_path='/api/orders/'+edit_id
        revision=call(port,'GET',edit_path,token=tokens['customer'])[1]['order']['edit_token']
        assert call(port,'PUT',edit_path,{'cargo_quantity':99},tokens['customer'])[0]==428
        def edit(index):
            return call(port if index%2 else second,'PUT',edit_path,{'cargo_quantity':20+index,'edit_token':revision},tokens['customer'] if index%2 else other_tokens['customer'])[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as workers:edit_results=list(workers.map(edit,range(8)))
        assert edit_results.count(200)==1 and edit_results.count(409)==7,edit_results
        saved=call(port,'GET',edit_path,token=tokens['customer'])[1]['order']
        assert saved['cargo_quantity']==20+edit_results.index(200)
        assert call(port,'PUT',edit_path,{'cargo_quantity':saved['cargo_quantity'],'edit_token':saved['edit_token']},tokens['customer'])[0]==200
        assert edit(0)==409
        for _ in range(5):
            race_path='/api/orders/'+create()
            revision_race=call(port,'GET',race_path,token=tokens['customer'])[1]['order']['edit_token']
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
                change=workers.submit(call,port,'PUT',race_path,{'cargo_quantity':77,'edit_token':revision_race},tokens['customer'])
                approval=workers.submit(call,second,'POST',race_path+'/approve',{},other_tokens['admin'])
                changed=change.result()[0];approved=approval.result()[0]
            assert changed in [200,409] and approved in [200,400],(changed,approved)
            current=call(port,'GET',race_path,token=tokens['customer'])[1]['order']
            if changed==200:assert current['cargo_quantity']==77
        passed('two processes: stale edits rejected, eight competing edits have one winner, approval never overwrites a committed edit')
        def approve(index):return call(port if index%2 else second,'POST','/api/orders/'+oid+'/approve',{},tokens['admin'] if index%2 else other_tokens['admin'])[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as workers:statuses=list(workers.map(approve,range(8)))
        assert statuses.count(200)==1 and statuses.count(400)==7,statuses
        passed('two application processes: locked snapshot check permits exactly one approval')
        process.terminate();process.wait(timeout=5)
        restarted,_,_=start()
        assert call(restarted,'GET',path,token=tokens['admin'])[1]['order']['status_id']==6
        passed('committed orders survive application restart')
        native=subprocess.run([str(pool_test),str(config_path)],cwd=root,env=env,capture_output=True,text=True,timeout=20)
        print(native.stdout,end='',flush=True)
        assert native.returncode==0,native.stderr
        passed('native pool capacity/notification/timeout, binding moves, long/null results, transaction rollback/commit')
        result={'mysql_version':version,'schema':schema,'checks':checks,'original_database_used':False,'data_directory':datadir,'temp_directory':tmpdir}
        (root/'build/mysql-integration-results.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    finally:
        for process in processes:
            if process.poll() is None:process.terminate()
            process.wait(timeout=5)
        for log in logs:log.close()
        # This name was generated here, validated above, and never comes from original config.
        database.query(f'DROP DATABASE `{schema}`')
print('PASS: test-only schema cleaned; original server/database unchanged.')
