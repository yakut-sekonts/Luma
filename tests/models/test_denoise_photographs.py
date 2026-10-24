"""Independent negative controls for photographic denoise quality criteria."""
from pathlib import Path
import runpy
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
module = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/check-denoise-photographs.py"))
quality = module["quality"]


class PhotographicOracleTests(unittest.TestCase):
    def setUp(self):
        self.clean = np.full((32, 512, 3), .2, dtype=np.float32)
        self.noisy = self.clean + np.random.default_rng(174).normal(0, .015, self.clean.shape).astype(np.float32)

    def test_clean_prediction_passes_and_identity_fails(self):
        self.assertEqual(quality(self.noisy, self.clean, self.clean, 384)["status"], "PASS")
        self.assertEqual(quality(self.noisy, self.clean, self.noisy, 384)["status"], "FAIL")

    def test_one_bad_color_channel_is_not_hidden_by_averaging(self):
        prediction = self.clean.copy()
        prediction[:, :, 2] = self.clean[:, :, 2] + 1.1 * (self.noisy[:, :, 2] - self.clean[:, :, 2])
        result = quality(self.noisy, self.clean, prediction, 384)
        self.assertEqual(result["status"], "FAIL")
        self.assertFalse(result["color"]["linear"]["all_channels_improved"])

    def test_small_global_error_does_not_hide_a_tile_seam(self):
        prediction = self.clean.copy()
        prediction[:, 384, :] += .1
        result = quality(self.noisy, self.clean, prediction, 384)
        self.assertTrue(result["color"]["linear"]["all_channels_improved"])
        self.assertFalse(result["edge_errors"]["horizontal-tile-seams"]["improved"])
        self.assertEqual(result["status"], "FAIL")

    def test_detail_destroying_predictor_fails(self):
        clean = self.clean.copy()
        clean[:, ::2] += .1
        noisy = clean + self.noisy - self.clean
        prediction = (clean + np.roll(clean, 1, axis=1)) / 2
        result = quality(noisy.astype(np.float32), clean, prediction, 384)
        self.assertFalse(result["edge_errors"]["horizontal-all"]["improved"])
        self.assertEqual(result["status"], "FAIL")

    def test_rejects_invalid_output_contracts(self):
        for prediction in (self.clean.astype(np.float64), self.clean[:, :-1],
                           np.full_like(self.clean, np.nan)):
            with self.assertRaises(ValueError):
                quality(self.noisy, self.clean, prediction, 384)
        with self.assertRaises(ValueError):
            quality(self.noisy, self.clean, self.clean, 0)

    def test_both_seam_directions_are_covered(self):
        clean = np.transpose(self.clean, (1, 0, 2)).copy()
        noisy = np.transpose(self.noisy, (1, 0, 2)).copy()
        result = quality(noisy, clean, clean, 384)
        self.assertIn("vertical-tile-seams", result["edge_errors"])
        self.assertNotIn("horizontal-tile-seams", result["edge_errors"])


if __name__ == "__main__":
    unittest.main()
