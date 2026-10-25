"""Byte-identical actual product, isolated sentinel DLL. No genuine SDK calls."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,queue,threading,uuid
product,sentinel,fixture,output=map(lambda s:Path(s).resolve(),sys.argv[1:])
output=output/uuid.uuid4().hex;output.mkdir(parents=True,exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
env=os.environ.copy();env['MONAKA_CALIBRATION_TEST_ROOT']=str(output/'seed');env['MONAKA_CALIBRATION_TEST_MODE']=''
seed=subprocess.run([str(fixture),'--fixture-seed'],env=env,capture_output=True,timeout=10);assert seed.returncode==0
opening=json.loads(seed.stdout)['test_only_fixture_open'];path=Path(opening['config_path']);config=json.loads(path.read_bytes())
for mapping in config['mappings']:mapping.pop('runtime_contract',None)
original=json.dumps(config).encode();path.write_bytes(original)
isolated=output/'isolated';isolated.mkdir();executable=isolated/product.name;shutil.copyfile(product,executable);shutil.copyfile(sentinel,isolated/'openvr_api.dll');assert sha(product)==sha(executable)
env['PATH']=str(Path(os.environ['SystemRoot'])/'System32');env['MONAKA_B2_VR_SENTINEL']=str(output/'sdk-called.marker');env['LOCALAPPDATA']=str(output/'private-appdata')
records=[]
def run(label,args,payload=None):
 result=subprocess.run([str(executable),*args],input=payload,cwd=isolated,env=env,capture_output=True,timeout=15)
 (output/(label+'.stdout')).write_bytes(result.stdout);(output/(label+'.stderr')).write_bytes(result.stderr)
 records.append({'label':label,'exit_code':result.returncode,'command':[str(executable),*args]});assert result.returncode==0,(label,result.stderr)
 assert not (output/'sdk-called.marker').exists() and not Path(env['LOCALAPPDATA']).exists()
 return [json.loads(line) for line in result.stdout.splitlines()]
version=run('version',['--local-api-version'])[0];assert version['static_supported'] and version['output_approval'];assert {'static_points_policy1','independent_output_review'}<=set(version['features'])
for mode,readonly in [('static',False),('static',True),('output_approval',False),('output_approval',True)]:
 request=dict(opening,op='open',request_id='open',helper_path=str(executable),helper_sha256=sha(executable),readonly=readonly)
 if mode=='static':request['mode']='static'
 else:request['purpose']='output_approval'
 # Ask open first to obtain the true native session/run before discover.
 name=mode+('-readonly' if readonly else '-writable');received=queue.Queue();raw=[]
 with (output/(name+'.stderr')).open('wb') as errors:
  process=subprocess.Popen([str(executable),'--local-api'],cwd=isolated,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=errors)
  def reader():
   for line in process.stdout:
    raw.append(line);received.put(line)
  thread=threading.Thread(target=reader,daemon=True);thread.start()
  def send(value):process.stdin.write(json.dumps(value).encode()+b'\n');process.stdin.flush()
  def reply(id):
   for _ in range(16):
    value=json.loads(received.get(timeout=10))
    if value.get('request_id')==id:return value
   raise RuntimeError('Missing matching response')
  try:
   send(request);opened=reply('open');assert opened['status']=='OPEN'
   send(dict(api_version=1,request_id='discover',op='discover',session_id=opened['session_id'],run_id=opened['run_id']));rejected=reply('discover');assert rejected['reason']=='EXPLICIT_DECLARATION_REQUIRED'
   send(dict(api_version=1,request_id='close',op='close',session_id=opened['session_id'],run_id=opened['run_id']));process.stdin.close();assert process.wait(timeout=10)==0;thread.join(timeout=2);assert not thread.is_alive()
  finally:
   if process.poll() is None:process.kill();process.wait(timeout=5)
   (output/(name+'.stdout')).write_bytes(b''.join(raw))
 assert path.read_bytes()==original and not (output/'sdk-called.marker').exists() and not Path(env['LOCALAPPDATA']).exists()
 records.append({'label':name,'exit_code':process.returncode,'open':'OPEN','discover':'EXPLICIT_DECLARATION_REQUIRED','sdk_calls':0})
(output/'receipt.json').write_text(json.dumps({'product_sha256':sha(product),'isolated_copy_sha256':sha(executable),'sentinel_sha256':sha(sentinel),'records':records,'actual_sdk_execution':'NOT_RUN','actual_socket_execution':'NOT_RUN'},indent=2)+'\n')
print('PASS B4 actual product 5 process cases: explicit capabilities, static/approval writable/readonly OPEN; missing declaration rejected before SDK/socket; no drafts or config writes')
