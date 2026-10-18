"""Actual service loopback, synthetic C0 JSON, ephemeral ports; never SDK/HIL."""
from pathlib import Path
import argparse,json,socket,subprocess,time,uuid,hashlib

def fixture():
 rigid=lambda t:dict(rotation=[0,0,0,1],translation=t)
 contract=dict(id='explicit-pico-c0-header',provenance='Pico c0f2f92d145688387b2f5b917672024096736139 Header explicitly declares position/orientation/linear_velocity; synthetic test provisioning',input_policy='STRICT',output_policy='STRICT',input_declaration=['position','orientation','linear_velocity'],output_declaration=['position','orientation','linear_velocity'])
 return dict(namespace='monaka.bridge',version=2,bridge_id='synthetic-bridge',mapping_revision=10,policy='disabled',pose_timeout_ms=500,profiles={'synthetic-pico':dict(convention='pico-output-a-v1',position_axes=[1,2,3],quaternion_axes=[1,2,3,4],approved=True,evidence='SYNTHETIC software coordinate profile, no physical approval')},mappings=[dict(source_id='synthetic-pico-source',device_id='HIL-SYNTH-OLD-0000'+str(n),tracker_id='slot-'+str(n),profile='synthetic-pico',input_space='hil-pico-space',input_revision=7,world_space='synthetic-world',world_revision=2,space_approved=True,world=rigid([1,0,0]),mount=rigid([.25,0,0]),runtime_contract=contract,old_physical_evidence='must not transfer to new device') for n in range(1,5)],unknown_global_extension={'preserve':[1,2,3]})

def main():
 p=argparse.ArgumentParser();p.add_argument('service');p.add_argument('fixture');p.add_argument('out');p.add_argument('--missing-profile',action='store_true');a=p.parse_args();out=Path(a.out)/str(uuid.uuid4());out.mkdir(parents=True)
 document=fixture()
 if a.missing_profile:document['profiles']={}
 config=out/'synthetic.json';config.write_text(json.dumps(document),encoding='utf-8');original=config.read_bytes()
 leases=[socket.socket(socket.AF_INET,socket.SOCK_DGRAM) for _ in range(4)]
 for s in leases:s.bind(('127.0.0.1',0))
 ports=[s.getsockname()[1] for s in leases];leases[0].close();sender=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
 cmd=[a.service,str(config),*map(str,ports),'--duration-ms','2200'];start=time.monotonic();child=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  health=Path(str(config)+'.status.json');deadline=time.monotonic()+10
  while not health.exists():
   if child.poll() is not None:raise RuntimeError('service startup failed')
   if time.monotonic()>deadline:raise RuntimeError('startup deadline')
   time.sleep(.01)
  initial=json.loads(health.read_text())['snapshot_unix_ms'];packets=[json.loads(s) for s in Path(a.fixture).read_text().splitlines() if s.strip()]
  # Repetition advances sequence, preserves stable identities/canonical claims.
  for seq in range(4):
   for packet in packets:packet['sequence']=str(seq);sender.sendto(json.dumps(packet).encode(),('127.0.0.1',ports[0]))
   time.sleep(.15)
  stdout,stderr=child.communicate(timeout=12)
 finally:
  if child.poll() is None:child.terminate();child.communicate(timeout=5)
  sender.close()
 (out/'stdout.txt').write_bytes(stdout);(out/'stderr.txt').write_bytes(stderr)
 assert child.returncode==0,(child.returncode,stderr)
 h=json.loads(health.read_text());rows=h['discovery']['observations'];assert len(rows)==5 and len(h['devices'])==4
 assert h['snapshot_unix_ms']>initial,'discovery blocked normal health updates'
 if a.missing_profile:assert all(r['reason']=='CONFIGURED_PROFILE_MISSING' for r in rows if r['configured'])
 unknown=[r for r in rows if not r['configured']];assert len(unknown)==1 and unknown[0]['device']=='HIL-SYNTH-NEW-00005' and unknown[0]['reason']=='UNMAPPED'
 assert h['policy']=='disabled' and config.read_bytes()==original
 for sock in leases[1:3]:
  sock.settimeout(.05)
  try:sock.recvfrom(65536);raise AssertionError('disabled output unexpectedly emitted')
  except socket.timeout:pass
  finally:sock.close()
 mirror=[];leases[3].settimeout(.05)
 try:
  while True:mirror.append(json.loads(leases[3].recvfrom(65536)[0]))
 except socket.timeout:pass
 finally:leases[3].close()
 assert mirror and all(p['device_id'].startswith('HIL-SYNTH-OLD-') for p in mirror),'unknown bypassed authorized mirror path'
 receipt=dict(result='PASS',command=cmd,seconds=time.monotonic()-start,known_registry=4,unknown_diagnostic=1,health_advances=True,missing_profile_case=a.missing_profile,configuration_unchanged=True,mtp_direct_output_packets=0,authorized_known_mirror_packets=len(mirror),unknown_mirror_packets=0,service_sha256=hashlib.sha256(Path(a.service).read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256(Path(a.fixture).read_bytes()).hexdigest(),actual_sdk='NOT_RUN',physical_hil='NOT_RUN')
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS H02/H03 actual service ephemeral-loopback: known4 + diagnostic unknown1, disabled outputs, config unchanged; '+str(out))
if __name__=='__main__':main()
