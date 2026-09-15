"""Consume the supplied kit's fixtures using its actual C++ and JVM codecs."""
import argparse,copy,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cpp',type=Path,required=True);p.add_argument('--java-home',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];kit=root/'third_party/monaka-protocol';work=root/'build/interop';work.mkdir(parents=True,exist_ok=True)
classpath=os.pathsep.join([str(work),*[str(x) for x in (kit/'jvm/libs').glob('*.jar')]])
suffix='.exe' if os.name=='nt' else ''
subprocess.run([str(a.java_home/'bin'/('javac'+suffix)),'-cp',classpath,'-d',str(work),str(root/'tests/CodecConsumer.java')],check=True)
jvm=[str(a.java_home/'bin'/('java'+suffix)),'-Dfile.encoding=UTF-8','-cp',classpath,'CodecConsumer'];cpp=[str(a.cpp.resolve())]
def run(command,path):return subprocess.check_output(command+[str(path)],timeout=10).decode().strip()
count=cross=0
for item in json.loads((kit/'fixtures/index.json').read_text(encoding='utf-8')):
 path=kit/'fixtures'/item['file'];results=[run(cmd,path) for cmd in [cpp,jvm]]
 if 'error' in item:
  for text in results:assert text=='ERROR:'+item['error']['code'],(item['file'],text)
 else:
  expected=copy.deepcopy(item['decoded']);expected['version']['minor']=0
  for text in results:assert json.loads(text)==expected,item['file']
  for i,cmd in enumerate([jvm,cpp]):
   f=work/(path.stem+str(i)+'.json');f.write_text(results[i],encoding='utf-8');assert json.loads(run(cmd,f))==expected;cross+=1
 count+=1
report=dict(result='PASS',fixtures=count,cross_language_directions=cross,codec='supplied fixed C++ target and JVM JAR; no reconstructed codec')
(root/'build/protocol-interop-results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
