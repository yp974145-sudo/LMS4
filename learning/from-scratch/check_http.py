"""Check only the isolated lesson server; never use the normal project port/data."""
import http.client,json,os,socket,subprocess,tempfile,time
from pathlib import Path
root=Path(__file__).resolve().parent
temporary_root=Path(os.environ.get('LMS_LESSON_TEMP_ROOT',r'C:\Users\23974\Desktop\jk\x'))
temporary_root.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='lms4-lesson-http-',dir=temporary_root) as folder:
    with socket.socket() as probe:
        probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    env={**os.environ,'TEMP':folder,'TMP':folder}
    with open(Path(folder)/'server.log','wb') as log:
        process=subprocess.Popen([str(root/'build/03_http.exe'),str(port)],cwd=folder,env=env,stdout=log,stderr=log)
        def call(method,path,body=None):
            conn=http.client.HTTPConnection('127.0.0.1',port,timeout=3)
            conn.request(method,path,None if body is None else json.dumps(body),{'Content-Type':'application/json','Connection':'close'})
            response=conn.getresponse();result=json.loads(response.read());conn.close()
            return response.status,result
        try:
            for _ in range(60):
                if process.poll() is not None:raise RuntimeError('Lesson process exited')
                try:
                    if call('GET','/api/health')[0]==200:break
                except OSError:time.sleep(.05)
            else:raise RuntimeError('Lesson listener unavailable')
            good={'customer':'Alice','cargo':'Books','quantity':3,'creator':'spoofed'}
            status,order=call('POST','/api/orders',good)
            assert status==201 and order['creator']=='lesson-customer' and order['quantity']==3
            for quantity in [-5,0,1.5,2**40,'3']:
                assert call('POST','/api/orders',{**good,'quantity':quantity})[0]==400
            assert call('POST','/api/orders',{**good,'customer':''})[0]==400
            print('PASS: isolated HTTP creation, server-owned actor, five numeric boundaries, empty customer')
        finally:
            process.terminate();process.wait(timeout=5)
