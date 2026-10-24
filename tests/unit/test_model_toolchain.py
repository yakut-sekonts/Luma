"""Fail before model downloads when the selected Xcode toolchain is incomplete."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("convert_models", SCRIPTS / "convert-models.py")
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


class ToolchainTests(unittest.TestCase):
    @patch.object(converter.subprocess, "check_output")
    @patch.object(converter.subprocess, "run")
    def test_missing_compiler_stops_before_conversion(self, run, version):
        run.side_effect = subprocess.CalledProcessError(72, ["xcrun"])
        with self.assertRaisesRegex(SystemExit, "Full Xcode with coremlcompiler"):
            converter.require_xcode()
        version.assert_not_called()

    @patch.object(converter.subprocess, "check_output")
    @patch.object(converter.subprocess, "run")
    def test_missing_xcrun_has_actionable_error(self, run, version):
        run.side_effect = FileNotFoundError("xcrun")
        with self.assertRaisesRegex(SystemExit, "DEVELOPER_DIR"):
            converter.require_xcode()
        version.assert_not_called()

    @patch.object(converter.subprocess, "check_output")
    @patch.object(converter.subprocess, "run")
    def test_unavailable_xcode_version_is_not_accepted(self, run, version):
        version.side_effect = subprocess.CalledProcessError(1, ["xcodebuild"])
        with self.assertRaisesRegex(SystemExit, "Command Line Tools alone"):
            converter.require_xcode()

    @patch.object(converter.subprocess, "check_output", return_value="Xcode test\nBuild version test\n")
    @patch.object(converter.subprocess, "run")
    def test_selected_xcode_version_is_recorded(self, run, version):
        self.assertEqual(converter.require_xcode(), "Xcode test\nBuild version test")
        self.assertEqual(run.call_args.args[0], ["xcrun", "--find", "coremlcompiler"])
        self.assertTrue(run.call_args.kwargs["check"])


if __name__ == "__main__":
    unittest.main()
