from pathlib import Path
import copy, hashlib, json, sys, time
ROOT=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT/'scripts'))
from capture_common_world_fault import save_fault_pair
base=Path(sys.argv[1]).resolve()/str(time.monotonic_ns()); base.mkdir(parents=True)
s={'schema':'monaka-common-world-runtime-snapshot-2','runtime_instance':'synthetic-instance','control_session':'synthetic-session','activation_generation':1}
r={'schema':'monaka-common-world-control-2','operation_id':'op-1','operation':'source-reconnect','result':'fresh','accepted':True,'before':s,'after':dict(s,activation_generation=2)}
cases=0
def run(name,f):
 global cases
 f(); cases+=1; print('PASS '+name)
def reject(value,path):
 try: save_fault_pair(value,path)
 except (ValueError,FileExistsError): return
 raise AssertionError('invalid pair accepted')
run('pair preserves exact snapshots and operation ID',lambda:save_fault_pair(r,base/'pair'))
run('prior pair cannot overwrite',lambda:reject(r,base/'pair'))
for field,value in [('runtime_instance','other-runtime'),('control_session','other-session'),('schema','unknown'),('activation_generation',0)]:
 x=copy.deepcopy(r); x['after'][field]=value
 run('reject '+field,lambda x=x,field=field:reject(x,base/field))
def hashes():
 manifest=json.loads((base/'pair/pair.json').read_text())
 for row in manifest['snapshots']: assert hashlib.sha256((base/'pair'/row['path']).read_bytes()).hexdigest()==row['sha256']
run('snapshot and operation hashes match',hashes)
print(f'PASS {cases} fault evidence tooling cases')
