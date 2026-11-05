"""Initialize only the dedicated project instance; never use original database credentials."""
import json
import secrets
import sys
import time
from pathlib import Path
from mysql_support import Mysql

root=Path(__file__).resolve().parents[1]
runtime=root/'.mysql-learning'
admin_path=runtime/'admin.json'
port=int(sys.argv[1])
admin=json.loads(admin_path.read_text(encoding='utf-8')) if admin_path.exists() else {'host':'127.0.0.1','port':port,'user':'root','password':''}
connection=None
for _ in range(100):
    try:
        connection=Mysql(admin);break
    except RuntimeError:
        # Resume an interrupted first-time setup only for our dedicated local instance.
        if admin.get('password'):
            try: connection=Mysql({**admin,'password':''});break
            except RuntimeError: pass
        time.sleep(.1)
if connection is None: raise RuntimeError('Dedicated learning MySQL did not become ready')
with connection as db:
    datadir,tmpdir=db.query('SELECT @@datadir, @@tmpdir')[0]
    expected=(runtime/'data').resolve()
    if Path(datadir).resolve()!=expected or not Path(tmpdir).resolve().is_relative_to(root):
        raise RuntimeError('Refusing setup: server data/temp directories do not belong to this E-drive project')
    if not admin.get('password'): admin['password']=secrets.token_hex(24)
    admin_path.write_text(json.dumps(admin,indent=2)+'\n',encoding='utf-8')
    db.query("ALTER USER 'root'@'localhost' IDENTIFIED BY '"+admin['password']+"'")
    db.query('CREATE DATABASE IF NOT EXISTS lms_learning CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci')
    app_secret=runtime/'app-secret.txt'
    password=app_secret.read_text(encoding='ascii').strip() if app_secret.exists() else secrets.token_hex(24)
    app_secret.write_text(password,encoding='ascii')
    db.query("CREATE USER IF NOT EXISTS 'lms_learning_app'@'localhost' IDENTIFIED BY '"+password+"'")
    db.query("ALTER USER 'lms_learning_app'@'localhost' IDENTIFIED BY '"+password+"'")
    db.query("GRANT SELECT,INSERT,UPDATE,DELETE,CREATE,INDEX ON lms_learning.* TO 'lms_learning_app'@'localhost'")
    config={'port':18080,'bind_address':'127.0.0.1','thread_count':4,
            'mysql':{'enabled':True,'host':'127.0.0.1','port':port,'user':'lms_learning_app','password':password,'database':'lms_learning','pool_size':4},
            'log':{'file':str(runtime/'logs/app.log'),'level':'info'}}
    (root/'config/mysql-learning.json').write_text(json.dumps(config,indent=2)+'\n',encoding='utf-8')
print('READY: isolated MySQL learning instance, E-drive data/temp/logs, dedicated app account; original server unchanged.')
