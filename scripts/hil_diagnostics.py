"""Read an immutable draft outcome; preserve the first exact native failure."""
from pathlib import Path
import argparse,json
def report(draft):
    manifest=json.loads((draft/'manifest.json').read_text(encoding='utf-8'))
    terminal=json.loads((draft/'terminal.json').read_text(encoding='utf-8')) if (draft/'terminal.json').is_file() else {}
    return dict(schema='monaka.hil-draft-report.v1',draft=str(draft),status=manifest['status'],reason=terminal.get('reason'),first_failure=terminal.get('first_failure'),terminal=terminal,hardware='NOT_INFERRED_FROM_DRAFT')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('draft',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();value=json.dumps(report(a.draft),indent=2)
    if a.output:a.output.write_text(value+'\n',encoding='utf-8')
    print(value)
if __name__=='__main__':main()
