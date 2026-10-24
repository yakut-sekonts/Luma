"""Independent algebraic regressions for the Core ML export adaptation."""
from pathlib import Path
import sys
import unittest

import torch
from torch.nn import functional as F

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from models.drunet import Downsample2d, Upsample2d, LinearDenoise, NoiseConditioned, estimate_noise


class DownsampleTests(unittest.TestCase):
    def test_matches_direct_convolution_and_checkpoint_layout(self):
        generator = torch.Generator().manual_seed(367)
        for channels, bias in ((1, True), (3, True), (32, True), (32, False)):
            layer = Downsample2d(channels, channels * 2, bias=bias).double()
            state = {"weight": torch.randn(channels * 2, channels, 2, 2,
                                          dtype=torch.float64, generator=generator)}
            if bias:
                state["bias"] = torch.randn(channels * 2, dtype=torch.float64, generator=generator)
            layer.load_state_dict(state, strict=True)
            self.assertEqual(set(layer.state_dict()), set(state))
            for height, width in ((2, 2), (8, 10), (9, 11), (10, 9)):
                with self.subTest(channels=channels, height=height, width=width):
                    image = torch.randn(1, channels, height, width,
                                        dtype=torch.float64, generator=generator)
                    original = image.clone()
                    with torch.inference_mode():
                        expected = F.conv2d(image, state["weight"], state.get("bias"), stride=2)
                        actual = layer(image)
                    self.assertEqual(actual.shape, (1, channels * 2, height // 2, width // 2))
                    torch.testing.assert_close(actual, expected, rtol=1e-12, atol=1e-12)
                    self.assertTrue(torch.equal(image, original))
                    for name, value in layer.state_dict().items():
                        self.assertTrue(torch.equal(value, state[name]))

    def test_spatial_and_channel_order(self):
        layer = Downsample2d(2, 8).double()
        with torch.no_grad():
            layer.weight.zero_()
            layer.bias.zero_()
            for channel in range(2):
                for y in range(2):
                    for x in range(2):
                        layer.weight[channel * 4 + y * 2 + x, channel, y, x] = 1
        image = torch.arange(2 * 6 * 8, dtype=torch.float64).reshape(1, 2, 6, 8)
        with torch.inference_mode():
            actual = layer(image)
        for channel in range(2):
            for y in range(2):
                for x in range(2):
                    self.assertTrue(torch.equal(actual[:, channel * 4 + y * 2 + x],
                                                image[:, channel, y::2, x::2]))


class UpsampleTests(unittest.TestCase):
    def test_matches_transpose_convolution_and_checkpoint_layout(self):
        generator = torch.Generator().manual_seed(238)
        for channels in (1, 3, 32):
            layer = Upsample2d(channels * 2, channels).double()
            weight = torch.randn(channels * 2, channels, 2, 2,
                                 dtype=torch.float64, generator=generator)
            layer.load_state_dict({"weight": weight}, strict=True)
            for height, width in ((1, 1), (8, 10), (9, 11)):
                image = torch.randn(1, channels * 2, height, width,
                                    dtype=torch.float64, generator=generator)
                original = image.clone()
                with torch.inference_mode():
                    expected = F.conv_transpose2d(image, weight, stride=2)
                    actual = layer(image)
                torch.testing.assert_close(actual, expected, rtol=1e-12, atol=1e-12)
                self.assertTrue(torch.equal(image, original))
                self.assertEqual(set(layer.state_dict()), {"weight"})
                self.assertTrue(torch.equal(layer.weight, weight))

    def test_channel_and_spatial_order(self):
        layer = Upsample2d(2, 2).double()
        with torch.no_grad():
            layer.weight.zero_()
            layer.weight[0, 1, 0, 1] = 2
            layer.weight[1, 0, 1, 0] = 3
        image = torch.zeros(1, 2, 3, 4, dtype=torch.float64)
        image[0, 0, 1, 2] = 5
        image[0, 1, 0, 1] = 7
        expected = torch.zeros(1, 2, 6, 8, dtype=torch.float64)
        expected[0, 1, 2, 5] = 10
        expected[0, 0, 1, 2] = 21
        with torch.inference_mode():
            self.assertTrue(torch.equal(layer(image), expected))


class NoiseEstimateTests(unittest.TestCase):
    def test_constants_and_planar_signal_do_not_count_as_noise(self):
        axis = torch.arange(32, dtype=torch.float64)
        plane = (axis[:, None] + 3 * axis[None, :]).reshape(1, 1, 32, 32)
        image = torch.cat((plane, plane * 2 + 5, torch.full_like(plane, 7)), dim=1)
        self.assertEqual(estimate_noise(image).item(), 0)

    def test_gaussian_noise_calibration(self):
        generator = torch.Generator().manual_seed(617)
        sigma = .03
        noise = torch.randn(1, 3, 256, 256, dtype=torch.float64, generator=generator) * sigma
        self.assertLess(abs(estimate_noise(noise).item() / sigma - 1), .015)

    def test_estimate_scales_to_dark_inputs_without_a_floor(self):
        generator = torch.Generator().manual_seed(811)
        image = torch.randn(1, 3, 32, 32, dtype=torch.float64, generator=generator)
        for scale in (1e-8, .001, .1, 8):
            torch.testing.assert_close(estimate_noise(image * scale), estimate_noise(image) * scale,
                                       rtol=1e-12, atol=0)

    def test_known_haar_coefficients_use_lower_absolute_median(self):
        for coefficients in ((-7,), (-3, 1), (-8, -3, -1, 0, 2), (-8, -3, -1, 0, 2, 5)):
            with self.subTest(coefficients=coefficients):
                image = torch.zeros(1, 1, 2, len(coefficients) * 2, dtype=torch.float64)
                image[0, 0, 0, ::2] = torch.tensor(coefficients, dtype=torch.float64) * 2
                expected = sorted(abs(value) for value in coefficients)[(len(coefficients) - 1) // 2]
                self.assertAlmostEqual(estimate_noise(image).item(), expected / .6744897501960817)

    def test_sparse_high_frequency_outliers_do_not_dominate(self):
        image = torch.zeros(1, 3, 32, 32, dtype=torch.float64)
        # Less than half of the independent HH coefficients contain texture.
        image[:, :, :8:2, ::2] = 1e8
        self.assertEqual(estimate_noise(image).item(), 0)

    def test_conditions_the_network_with_the_estimated_noise_map(self):
        class Echo(torch.nn.Module):
            def forward(self, image):
                return image
        image = torch.zeros(1, 3, 8, 8, dtype=torch.float64)
        image[:, :, ::2, ::2] = 1
        original = image.clone()
        actual = NoiseConditioned(Echo())(image)
        self.assertEqual(actual.shape, (1, 4, 8, 8))
        self.assertTrue(torch.equal(actual[:, :3], original))
        self.assertTrue(torch.equal(image, original))
        # Each HH coefficient is exactly 1/2 for this known 2x2 pattern.
        expected_sigma = .5 / .6744897501960817
        torch.testing.assert_close(actual[:, 3], torch.full_like(image[:, 0], expected_sigma),
                                   rtol=1e-12, atol=0)


class UniformTileTests(unittest.TestCase):
    class BrightPrediction(torch.nn.Module):
        def forward(self, image):
            return torch.ones_like(image)

    def test_preserves_uniform_colors_without_a_brightness_threshold(self):
        model = LinearDenoise(self.BrightPrediction())
        for color in ((0, 0, 0), (.0001, .0001, .0001), (.18, .18, .18),
                      (.15, .4, .8), (1, 1, 1), (-.1, .5, 1.2)):
            with self.subTest(color=color):
                image = torch.tensor(color, dtype=torch.float32).reshape(1, 3, 1, 1)
                image = image.expand(1, 3, 16, 16).clone()
                self.assertTrue(torch.equal(model(image), image))

    def test_real_variation_still_reaches_prediction(self):
        model = LinearDenoise(self.BrightPrediction())
        image = torch.zeros(1, 3, 16, 16)
        image[0, 0, 5, 7] = 1e-7
        self.assertTrue(torch.equal(model(image), torch.ones_like(image)))

    def test_outside_domain_pixels_keep_all_three_channels(self):
        model = LinearDenoise(self.BrightPrediction())
        image = torch.zeros(1, 3, 16, 16)
        image[0, :, 5, 7] = torch.tensor([-.1, .5, .3])
        image[0, :, 8, 9] = torch.tensor([.2, .4, 1.1])
        actual = model(image)
        self.assertTrue(torch.equal(actual[0, :, 5, 7], image[0, :, 5, 7]))
        self.assertTrue(torch.equal(actual[0, :, 8, 9], image[0, :, 8, 9]))
        self.assertTrue(torch.equal(actual[0, :, 0, 0], torch.ones(3)))


if __name__ == "__main__":
    torch.set_num_threads(2)
    unittest.main()
