"""Launch a disposable app and exercise role workspaces in a real browser."""
import http.client,json,os,subprocess,sys,tempfile,time,socket,uuid
from pathlib import Path
from mysql_support import Mysql
root=Path(__file__).resolve().parents[1]
exe=Path(sys.argv[1]).resolve();use_mysql='--mysql' in sys.argv
admin=json.loads((root/'.mysql-learning/admin.json').read_text(encoding='utf-8')) if use_mysql else None
schema='lms_browser_test_'+uuid.uuid4().hex[:12]
database=Mysql(admin) if use_mysql else None
if database:
    assert Path(database.query('SELECT @@datadir')[0][0]).resolve()==(root/'.mysql-learning/data').resolve()
    database.query(f'CREATE DATABASE `{schema}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci')
try:
 with tempfile.TemporaryDirectory(prefix='roles-browser-',dir=root/'.tmp') as temporary:
    fixture=Path(temporary)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    config={'port':port,'thread_count':4,'mysql':{**admin,'enabled':True,'database':schema,'pool_size':1} if use_mysql else {'enabled':False},'data_directory':str(fixture/'data'),'log':{'file':str(fixture/'app.log')}}
    (fixture/'config.json').write_text(json.dumps(config),encoding='utf-8')
    env={k:v for k,v in os.environ.items() if not k.startswith('LMS_')}
    env['PATH']='E:/Program Files/MySQL/MySQL Server 8.0/lib;E:/Program Files/MySQL/MySQL Server 8.0/bin;'+env['PATH']
    env['TEMP']=str(fixture);env['TMP']=str(fixture)
    def call(method,path,body=None,token=None):
        c=http.client.HTTPConnection('127.0.0.1',port,timeout=8)
        headers={'Content-Type':'application/json','Connection':'close'}
        if token:headers['Authorization']='Bearer '+token
        c.request(method,path,None if body is None else json.dumps(body),headers)
        response=c.getresponse();data=json.loads(response.read());c.close();return response.status,data
    with open(fixture/'server.log','wb') as output:
        server=subprocess.Popen([str(exe),'--config',str(fixture/'config.json')],cwd=root,env=env,stdout=output,stderr=output)
        try:
            for _ in range(100):
                if server.poll() is not None:raise RuntimeError('Browser fixture exited')
                try:
                    if call('GET','/api/health')[0]==200:break
                except (OSError,http.client.HTTPException):time.sleep(.05)
            else:raise RuntimeError('Browser fixture server did not start')
            tokens={}
            for name in ['admin','storage','dispatch','customer']:
                status,data=call('POST','/api/auth/login',{'username':name,'password':name+'123'});assert status==200;tokens[name]=data['token']
            steps=[[],[('reject','admin')],[('approve','admin')],[('approve','admin'),('out-stock','storage')],
                   [('approve','admin'),('out-stock','storage'),('start-transport','dispatch')],
                   [('approve','admin'),('out-stock','storage'),('start-transport','dispatch'),('deliver','dispatch')],
                   [('approve','admin'),('out-stock','storage'),('start-transport','dispatch'),('deliver','dispatch'),('complete','customer')]]
            for index,actions in enumerate(steps):
                status,data=call('POST','/api/orders',{'customer_name':'演示客户'+str(index),'cargo_name':'测试货物','cargo_type':0,'cargo_quantity':index+1},tokens['customer']);assert status==201;oid=data['order_id']
                for action,name in actions:assert call('POST','/api/orders/'+oid+'/'+action,{'reason':'测试驳回'},tokens[name])[0]==200
            result=subprocess.run(['node',str(root/'tests/browser_roles.mjs'),f'http://127.0.0.1:{port}',str(fixture/'browser'),str(root/'build/role-previews')],cwd=root,env=env,timeout=180)
            if result.returncode:raise RuntimeError('Browser tests failed')
        finally:
            server.terminate();server.wait(timeout=5)
finally:
 if database:
    assert schema.startswith('lms_browser_test_') and len(schema)==29
    database.query(f'DROP DATABASE `{schema}`');database.close()
