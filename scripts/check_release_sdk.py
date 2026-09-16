"""Verify the exact OpenVR build inputs; does not run or register SteamVR."""
import argparse
from pathlib import Path
import subprocess
from release_v2 import require, save, sha

REVISION = "0924064316de3effbcd1acf1e309182a2deb1c05"
p = argparse.ArgumentParser()
p.add_argument("--sdk", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
def git(*args):
    return subprocess.check_output(["git", "-C", str(a.sdk), *args])
require(git("rev-parse", "HEAD").decode().strip() == REVISION, "OpenVR SDK revision mismatch")
files = {}
for name in ("headers/openvr.h", "headers/openvr_driver.h", "lib/win64/openvr_api.lib", "bin/win64/openvr_api.dll", "LICENSE"):
    actual = (a.sdk / name).read_bytes()
    committed = git("show", REVISION + ":" + name)
    # The SDK marks headers/license as Git text; Windows checkout uses CRLF.
    # Canonicalize only those text inputs. Library/DLL bytes must match exactly.
    text_input = name.endswith(".h") or name == "LICENSE"
    canonical = actual.replace(b"\r\n", b"\n") if text_input else actual
    expected = committed.replace(b"\r\n", b"\n") if text_input else committed
    require(canonical == expected, "Modified OpenVR input: " + name)
    files[name] = sha(actual)
save(a.output, {"result": "PASS", "source_commit": REVISION, "files_sha256": files,
                "steamvr_interactive": "NOT RUN"})
print("PASS exact OpenVR SDK header/library/runtime/license build inputs; runtime NOT RUN")
