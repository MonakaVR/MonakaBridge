"""Generate configure-time provenance; never infer the executing tree at runtime."""
import datetime,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
values={'MB_DIAG_COMMIT':git('rev-parse','HEAD'),'MB_DIAG_REF':git('rev-parse','--abbrev-ref','HEAD'),'MB_DIAG_TIMESTAMP':datetime.datetime.now(datetime.timezone.utc).isoformat().replace('+00:00','Z')}
out=pathlib.Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
out.write_text('#pragma once\n'+''.join(f'#define {k} {json.dumps(v)}\n' for k,v in values.items())+f'#define MB_DIAG_DIRTY {int(bool(git("status","--porcelain")))}\n',encoding='utf-8')
