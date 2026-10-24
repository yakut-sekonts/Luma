"""Independent controls for conservative Sky region classification."""
from pathlib import Path
import runpy
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
module = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/check-sky-photographs.py"))
quality = module["quality"]
LIMITS = {"sky_min_mean": .9, "sky_min_fraction_over_half": .98,
          "non_sky_max_mean": .1, "non_sky_max_fraction_over_half": .02}


class SkyOracleTests(unittest.TestCase):
    def setUp(self):
        self.fixture = {"size": [100, 100], "regions": [
            {"name": "sky", "sky": True, "rect": [0, 0, 100, 20]},
            {"name": "ground", "sky": False, "rect": [0, 80, 100, 100]}]}
        self.ideal = np.zeros((100, 100), dtype=np.float32)
        self.ideal[:20] = 1

    def test_correct_prediction_and_constant_shortcuts(self):
        self.assertEqual(quality(self.ideal, self.fixture, LIMITS)["status"], "PASS")
        for wrong in (1 - self.ideal, np.zeros_like(self.ideal), np.ones_like(self.ideal)):
            self.assertEqual(quality(wrong, self.fixture, LIMITS)["status"], "FAIL")

    def test_small_false_positive_is_not_hidden_by_mean(self):
        wrong = self.ideal.copy()
        wrong[80:83, :20] = 1  # 3% of the background: mean is still below 0.1.
        result = quality(wrong, self.fixture, LIMITS)
        self.assertLess(result["regions"][1]["mean_probability"], .1)
        self.assertEqual(result["status"], "FAIL")

    def test_weak_confidence_is_not_hidden_by_thresholded_area(self):
        wrong = self.ideal.copy()
        wrong[:20] = .6
        self.assertEqual(quality(wrong, self.fixture, LIMITS)["status"], "FAIL")

    def test_unknown_boundaries_are_not_silently_background(self):
        alternate = self.ideal.copy()
        alternate[20:80] = 1
        self.assertEqual(quality(alternate, self.fixture, LIMITS)["status"], "PASS")

    def test_output_centers_project_into_source_coordinates(self):
        fixture = {"size": [8, 4], "regions": [
            {"name": "right", "sky": True, "rect": [4, 0, 8, 4]},
            {"name": "left", "sky": False, "rect": [0, 0, 4, 4]}]}
        probability = np.array([[0, 0, 1, 1], [0, 0, 1, 1]], dtype=np.float32)
        result = quality(probability, fixture, LIMITS)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual([r["pixels"] for r in result["regions"]], [4, 4])

    def test_invalid_planes_are_rejected(self):
        for wrong in (self.ideal.astype(np.float64), self.ideal[:, :, None],
                      np.full_like(self.ideal, np.nan), self.ideal - .1,
                      np.empty((0, 0), dtype=np.float32)):
            with self.assertRaises(ValueError):
                quality(wrong, self.fixture, LIMITS)

    def test_invalid_or_unrepresented_regions_are_rejected(self):
        fixtures = [{"size": [100, 100], "regions": []},
                    {"size": [0, 100], "regions": self.fixture["regions"]},
                    {"size": [100, 100], "regions": [{"name": "bad", "sky": True, "rect": [5, 5, 5, 10]}]},
                    {"size": [10000, 10000], "regions": [{"name": "empty", "sky": True, "rect": [0, 0, 1, 1]}]}]
        for fixture in fixtures:
            with self.assertRaises(ValueError):
                quality(self.ideal, fixture, LIMITS)


if __name__ == "__main__":
    unittest.main()
