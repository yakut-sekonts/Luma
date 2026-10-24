"""Native gradient export/restart; generated RAW and catalogs only."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'assets'))
from generate_dng import create_dng
with tempfile.TemporaryDirectory(prefix='luma-local-') as temporary:
    root=Path(temporary);source=root/'Local.DNG';create_dng(source)
    checksum=hashlib.sha256(source.read_bytes()).hexdigest()
    catalog=root/'catalog.sqlite'
    for args in (["--test-edit",str(source),"--test-exposure","0.3","--test-local-masks","--test-transform","--test-export",str(root/'first.png'),"--test-screenshot",str(root/'local.png')],
                 ["--test-restore","0.3","--test-local-masks","--test-export",str(root/'second.png')]):
        result=subprocess.run([sys.argv[1],"--catalog",str(catalog),*args],capture_output=True,text=True,timeout=120,env={**os.environ,"QT_QUICK_BACKEND":"software"})
        assert result.returncode==0,result.stdout+result.stderr
        assert not any(token in result.stderr for token in ('ReferenceError','Unable to assign','Binding loop')),result.stderr
    assert (root/'first.png').read_bytes()==(root/'second.png').read_bytes()
    assert (root/'local.png').stat().st_size>10000
    with sqlite3.connect(catalog) as db:
        assert db.execute('PRAGMA user_version').fetchone()==(21,)
        text=db.execute('SELECT document FROM edit_local_masks').fetchone()[0]
        assert text.startswith('LUMA_LOCAL_MASKS 3 2\n')
        assert db.execute('SELECT exposure,transform_rotate FROM edit_documents').fetchone()==(.3,8.)
    for code in ('ru','zh_CN','hy','de','es','it','fr'):
        folder=root/code;folder.mkdir();(folder/'language.json').write_text(json.dumps({'schema':1,'language':code}))
        result=subprocess.run([sys.argv[1],"--catalog",str(folder/'catalog.sqlite'),"--test-edit",str(source),"--test-exposure","0.3","--test-local-masks","--test-window-width","1000","--test-screenshot",str(folder/'mask.png')],capture_output=True,text=True,timeout=120,env={**os.environ,"QT_QUICK_BACKEND":"software"})
        assert result.returncode==0,(code,result.stdout+result.stderr)
        assert not any(token in result.stderr for token in ('ReferenceError','Unable to assign','Binding loop')),(code,result.stderr)
    assert hashlib.sha256(source.read_bytes()).hexdigest()==checksum
    print('Native local gradients: history, transformed overlay, byte-identical export/restart and original integrity passed')
