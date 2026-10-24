"""Ensure quality oracles reject identity, bright, blurred and invalid output."""
from pathlib import Path
import runpy
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
api = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/check-denoise-quality.py"))
evaluate = api["evaluate"]


class QualityOracleTests(unittest.TestCase):
    def setUp(self):
        self.cases = list(api["samples"](32))

    def test_analytical_clean_prediction_passes_all_cases(self):
        for name, noisy, clean, mode in self.cases:
            with self.subTest(name=name):
                self.assertEqual(evaluate(noisy, clean, clean, mode, 4)["status"], "PASS")

    def test_identity_cannot_pass_as_denoising(self):
        for name, noisy, clean, mode in self.cases:
            if mode == "denoise":
                with self.subTest(name=name):
                    result = evaluate(noisy, clean, noisy.copy(), mode, 4)
                    self.assertEqual(result["status"], "FAIL")
                    self.assertFalse(result["regions"]["full"]["criteria"]["noise_error_reduced"])

    def test_bright_output_cannot_pass_even_when_two_backends_agree(self):
        for name, noisy, clean, mode in self.cases:
            with self.subTest(name=name):
                result = evaluate(noisy, clean, np.ones_like(noisy), mode, 4)
                self.assertEqual(result["status"], "FAIL")
                self.assertEqual(result["regions"]["runtime-crop"]["status"], "FAIL")

    def test_blurring_away_ramp_detail_fails(self):
        _, noisy, clean, mode = self.cases[0]
        flattened = np.full_like(clean, .5)
        self.assertEqual(evaluate(noisy, clean, flattened, mode, 4)["status"], "FAIL")

    def test_single_amplified_outlier_fails_even_when_mse_improves(self):
        _, noisy, clean, mode = self.cases[0]
        output = clean.copy()
        output[0, 0, 8, 8] += np.abs(noisy - clean).max() * 2
        result = evaluate(noisy, clean, output, mode, 4)
        self.assertTrue(result["regions"]["full"]["criteria"]["noise_error_reduced"])
        self.assertFalse(result["regions"]["full"]["criteria"]["peak_error_not_amplified"])
        self.assertEqual(result["status"], "FAIL")

    def test_borders_are_not_hidden_by_the_runtime_crop(self):
        _, noisy, clean, mode = self.cases[0]
        output = clean.copy()
        output[:, :, 0, :] = 1
        result = evaluate(noisy, clean, output, mode, 4)
        self.assertEqual(result["regions"]["runtime-crop"]["status"], "PASS")
        self.assertEqual(result["status"], "FAIL")

    def test_invalid_prediction_contract_is_rejected(self):
        _, noisy, clean, mode = self.cases[0]
        invalid = [clean.astype(np.float64), clean[:, :1], np.full_like(clean, np.nan),
                   np.full_like(clean, np.inf)]
        for output in invalid:
            with self.assertRaises(ValueError):
                evaluate(noisy, clean, output, mode, 4)
        for halo in (-1, 16, 4.5):
            with self.assertRaises(ValueError):
                evaluate(noisy, clean, clean, mode, halo)


if __name__ == "__main__":
    unittest.main()
