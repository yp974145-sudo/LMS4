"""Restore checksum-pinned official packages into project-owned dependencies."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import urllib.request
import zipfile

project = Path(__file__).resolve().parents[1]
records = json.loads(Path(__file__).with_name('dependencies-windows.lock.json').read_text())
temporary_root = Path(r'C:\Users\23974\Desktop\jk\x')
temporary_root.mkdir(parents=True, exist_ok=True)
destination = project/'dependencies'
destination.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='lms4-drogon-deps-', dir=temporary_root) as folder:
    for record in records:
        archive = Path(folder)/record['file']
        with urllib.request.urlopen(record['url'], timeout=60) as response, archive.open('wb') as output:
            while chunk := response.read(1024*256):
                output.write(chunk)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != record['sha256']:
            raise RuntimeError('Checksum mismatch: '+archive.name)
        if archive.suffix == '.zip':
            with zipfile.ZipFile(archive) as package:
                package.extractall(destination)
        else:
            prefix = destination/'msys2'
            prefix.mkdir(exist_ok=True)
            subprocess.run(['tar', '-xf', str(archive), '-C', str(prefix)], check=True)
        print('Verified and extracted:', archive.name, flush=True)
