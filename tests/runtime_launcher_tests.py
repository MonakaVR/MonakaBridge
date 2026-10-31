"""Fixed runtime startup checks using inert files, never product processes."""
from pathlib import Path
from unittest.mock import patch
from contextlib import redirect_stdout
import importlib.util,io,json,tempfile,unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runtime_launcher',ROOT/'scripts/launch_runtime.py')
launch=importlib.util.module_from_spec(spec);spec.loader.exec_module(launch)

class RuntimeTests(unittest.TestCase):
    def setUp(self):
        test_root=ROOT/'Build/Tests';test_root.mkdir(parents=True,exist_ok=True)
        self.directory=tempfile.TemporaryDirectory(prefix='Runtime with spaces ',dir=test_root)
        self.root=Path(self.directory.name).resolve()
        assert self.root.resolve().is_relative_to(test_root.resolve())
        self.original=launch.ROOT;launch.ROOT=self.root
    def tearDown(self):
        launch.ROOT=self.original;self.directory.cleanup()
    def ready(self):
        for rel in ('Release/monaka_bridge_service.exe','Release/monaka_bridge_config.exe',
                    'Release/monaka_bridge_calibrator.exe','Release/openvr_api.dll',
                    'control/Release/monaka_bridge_control.exe'):
            path=self.root/'Build/runtime'/rel;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'inert fixture')
        config=self.root/'config/bridge.json';config.parent.mkdir();config.write_text(json.dumps(dict(namespace='monaka.bridge',version=2)))
    def test_missing_dependencies_are_aggregated(self):
        result=launch.check();self.assertEqual(len(result['missing_files']),5);self.assertFalse(result['state_ready'])
    def test_check_does_not_spawn_helper_or_product(self):
        self.ready()
        with patch.object(launch.subprocess,'Popen',side_effect=AssertionError('process forbidden')),patch.object(launch.subprocess,'run',side_effect=AssertionError('process forbidden')),redirect_stdout(io.StringIO()):
            for mode in ('normal','inspect','calibrate'):self.assertEqual(launch.main(['--check','--mode',mode]),0)
    def test_control_modes_and_fixed_helper(self):
        self.ready()
        for mode in ('normal','inspect','calibrate'):
            result=launch.check(mode);args=result['argv']
            self.assertIn(str(self.root/'Build/runtime/Release/monaka_bridge_config.exe'),args)
            self.assertEqual('--service-exe' in args,mode=='normal')
            self.assertEqual('--control-window-only' in args,mode!='normal')
            self.assertEqual('--read-only' in args,mode=='inspect')
    def test_application_failure_is_not_retried(self):
        self.ready()
        with patch.object(launch.subprocess,'Popen') as spawn,redirect_stdout(io.StringIO()):
            spawn.return_value.wait.return_value=37
            self.assertEqual(launch.main([]),37);self.assertEqual(spawn.call_count,1)
    def test_config_failure_is_reported_before_start(self):
        self.ready();(self.root/'config/bridge.json').write_text('{bad')
        with patch.object(launch.subprocess,'Popen',side_effect=AssertionError('process forbidden')),redirect_stdout(io.StringIO()):self.assertEqual(launch.main([]),1)

if __name__=='__main__':unittest.main()
