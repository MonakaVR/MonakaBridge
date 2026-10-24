"""Held child processes and bounded logs; never controls existing processes."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time
import uuid

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host', type=Path, required=True)
p.add_argument('--driver', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
root = a.out.resolve()/('run-'+uuid.uuid4().hex)
root.mkdir(parents=True)
driver = a.driver.resolve()
before = hashlib.sha256(driver.read_bytes()).hexdigest()
records = []


def child(name, args, expected):
    out = root/name
    command = [str(a.host.resolve()), *map(str, args), str(out)]
    started = time.monotonic()
    with (root/(name+'.stdout.txt')).open('xb') as stdout, (root/(name+'.stderr.txt')).open('xb') as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr)
        timed_out = limit = False
        while process.poll() is None:
            if time.monotonic()-started > 30:
                timed_out = True
            if stdout.tell() > 8*1024*1024 or stderr.tell() > 4*1024*1024:
                limit = True
            if timed_out or limit:
                process.kill()  # This exact held test child only, no lookup.
                break
            time.sleep(.02)
        code = process.wait(timeout=5)
    result = json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else {'result': 'FAIL', 'reason': 'NO_RECEIPT'}
    record = dict(case=name, command=command, pid=process.pid, exit_code=code,
                  seconds=time.monotonic()-started, timeout=timed_out,
                  log_limit_exceeded=limit, result=result, child_exited=True)
    records.append(record)
    (root/'children.json').write_text(json.dumps(records, indent=2)+'\n')
    if code not in expected or timed_out or limit:
        raise RuntimeError('Child did not satisfy expected result: '+name)
    return result


child('fake-contract', ['--fake-contract'], [0])
factory = child('factory-only', ['--factory-only', driver], [0])
missing = child('missing-interfaces', ['--missing-interfaces-only', driver], [0])
load_failure = child('missing-dll', ['--load-failure', root/'does-not-exist.dll'], [0])
host = child('production-host', [driver], [0, 77])
restart = child('production-host-restart', [driver], [0, 77]) if host['result'] == 'PASS' else None
assert before == hashlib.sha256(driver.read_bytes()).hexdigest()
cases = host['cases']
cases['D1-01'] = factory['cases']['D1-01']
if restart and restart['result'] != 'PASS':
    host = restart
    cases['D1-08'] = 'BLOCKED independent process restart'
if host['result'] != 'PASS':
    cases['D1-09'] = dict(status='PARTIAL', missing_required_interfaces='PASS',
                           missing_dll='PASS', owned_port_failure='NOT RUN',
                           successful_init_cleanup='NOT RUN')
result = dict(overall='D1 PASS — PRODUCTION PROVIDER MOCK OPENVR HOST' if host['result']=='PASS' else 'BLOCKED',
              reason=host.get('reason'), cases=cases,
              required_gate_pass=host['result']=='PASS',
              driver_sha256=before, production_dll_unchanged=True,
              factory_observed=True, missing_interface_cases=missing['missing_interface_cases'],
              independent_process_restart_pass=bool(restart and restart['result']=='PASS'),
              missing_dll_winerror=load_failure['missing_dll_winerror'],
              records=records, event_root=str(root), children_remaining=0,
              existing_owner_controlled=False, real_steamvr_hil='NOT RUN',
              model_render_hil='NOT RUN', physical_cutover='PHYSICAL_CUTOVER_PENDING',
              utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
(root/'D1_RESULT.json').write_text(json.dumps(result, indent=2)+'\n')
(a.out/'latest-run.json').write_text(json.dumps(dict(run=str(root)),indent=2)+'\n')
print(json.dumps(dict(overall=result['overall'],reason=result['reason'],run=str(root),driver_sha256=before)))
raise SystemExit(0 if host['result']=='PASS' else 77)
