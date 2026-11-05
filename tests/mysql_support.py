"""MySQL C API test helper. Credentials come from local config, never argv or logs."""
import ctypes
import json
import os
from pathlib import Path

class Mysql:
    def __init__(self,config,mysql_root=None,database=None):
        root=Path(mysql_root or os.environ.get('MYSQL_ROOT','E:/Program Files/MySQL/MySQL Server 8.0'))
        self.dll_dirs=[]
        if os.name=='nt':
            self.dll_dirs=[os.add_dll_directory(str(root/'lib')),os.add_dll_directory(str(root/'bin'))]
            self.lib=ctypes.CDLL(str(root/'lib/libmysql.dll'))
        else:
            from ctypes import util
            self.lib=ctypes.CDLL(util.find_library('mysqlclient') or util.find_library('mariadb'))
        signatures={
            'mysql_init':([ctypes.c_void_p],ctypes.c_void_p),
            'mysql_options':([ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p],ctypes.c_int),
            'mysql_real_connect':([ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint,ctypes.c_char_p,ctypes.c_ulong],ctypes.c_void_p),
            'mysql_query':([ctypes.c_void_p,ctypes.c_char_p],ctypes.c_int),
            'mysql_store_result':([ctypes.c_void_p],ctypes.c_void_p),
            'mysql_num_fields':([ctypes.c_void_p],ctypes.c_uint),
            'mysql_fetch_row':([ctypes.c_void_p],ctypes.POINTER(ctypes.c_void_p)),
            'mysql_fetch_lengths':([ctypes.c_void_p],ctypes.POINTER(ctypes.c_ulong)),
            'mysql_free_result':([ctypes.c_void_p],None),
            'mysql_errno':([ctypes.c_void_p],ctypes.c_uint),
            'mysql_close':([ctypes.c_void_p],None),
        }
        for name,(args,result) in signatures.items():
            function=getattr(self.lib,name);function.argtypes=args;function.restype=result
        self.connection=self.lib.mysql_init(None)
        timeout=ctypes.c_uint(3);self.lib.mysql_options(self.connection,0,ctypes.byref(timeout))
        encode=lambda value:str(value).encode('utf-8')
        result=self.lib.mysql_real_connect(self.connection,encode(config.get('host','127.0.0.1')),encode(config.get('user','root')),encode(config.get('password','')),encode(database) if database else None,int(config.get('port',3306)),None,0)
        if not result:
            error=self.lib.mysql_errno(self.connection);self.close();raise RuntimeError('MySQL connection failed; error '+str(error))
        self.query('SET NAMES utf8mb4')
    def query(self,sql):
        if self.lib.mysql_query(self.connection,sql.encode('utf-8')):
            raise RuntimeError('MySQL command failed; error '+str(self.lib.mysql_errno(self.connection)))
        result=self.lib.mysql_store_result(self.connection)
        if not result:return []
        try:
            columns=self.lib.mysql_num_fields(result);rows=[]
            while True:
                row=self.lib.mysql_fetch_row(result)
                if not row:break
                lengths=self.lib.mysql_fetch_lengths(result)
                rows.append([ctypes.string_at(row[i],lengths[i]).decode('utf-8') if row[i] else None for i in range(columns)])
            return rows
        finally:self.lib.mysql_free_result(result)
    def close(self):
        if self.connection:self.lib.mysql_close(self.connection);self.connection=None
    def __enter__(self):return self
    def __exit__(self,*args):self.close()

def local_config(path='config/config.json'):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))['mysql']
