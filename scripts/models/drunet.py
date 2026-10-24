"""Inference-only DRUNet color, adapted for Core ML export.

Copyright (c) 2020 Kai Zhang. MIT; see resources/models/licenses/DRUNet-LICENSE.txt.
Source: DPIR commit 15bca3fcc1f3cc51a1f99ccf027691e278c19354.
Luma: fixed architecture, equivalent resampling, estimated noise, linear RGB.
"""
import torch
from torch import nn


class Downsample2d(nn.Conv2d):
    """Checkpoint-equivalent stride-2 convolution using explicit spatial phases.

    Avoid the native Core ML Float32 CPU 2x2/stride-2 boundary error observed on
    macOS 27. The same coefficients and optional bias are used without retraining.
    """
    def __init__(self, channels, output_channels, bias=True):
        super().__init__(channels, output_channels, 2, stride=2, bias=bias)

    def forward(self, image):
        height, width = image.shape[-2] // 2 * 2, image.shape[-1] // 2 * 2
        patches = torch.cat([image[:, :, y:height:2, x:width:2]
                             for y in range(2) for x in range(2)], dim=1)
        weight = self.weight.permute(0, 2, 3, 1).reshape(self.out_channels, -1, 1, 1)
        return torch.nn.functional.conv2d(patches, weight, self.bias)


class Upsample2d(nn.ConvTranspose2d):
    """Non-overlapping transpose convolution expressed as 1x1 + PixelShuffle."""
    def __init__(self, channels, output_channels):
        super().__init__(channels, output_channels, 2, stride=2, bias=False)

    def forward(self, image):
        weight = self.weight.permute(1, 2, 3, 0).reshape(self.out_channels * 4, self.in_channels, 1, 1)
        return torch.nn.functional.pixel_shuffle(torch.nn.functional.conv2d(image, weight), 2)


class ResBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.res = nn.Sequential(nn.Conv2d(channels, channels, 3, padding=1, bias=False),
                                 nn.ReLU(), nn.Conv2d(channels, channels, 3, padding=1, bias=False))

    def forward(self, image):
        return image + self.res(image)


class DRUNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.m_head = nn.Conv2d(4, 64, 3, padding=1, bias=False)
        for index, channels in enumerate((64, 128, 256), 1):
            down = nn.Sequential(*[ResBlock(channels) for _ in range(4)],
                                 Downsample2d(channels, channels * 2, bias=False))
            up = nn.Sequential(Upsample2d(channels * 2, channels),
                               *[ResBlock(channels) for _ in range(4)])
            setattr(self, "m_down" + str(index), down)
            setattr(self, "m_up" + str(index), up)
        self.m_body = nn.Sequential(*[ResBlock(512) for _ in range(4)])
        self.m_tail = nn.Conv2d(64, 3, 3, padding=1, bias=False)

    def forward(self, image):
        # Fixed 512x512 inputs are divisible by all three resampling stages.
        first = self.m_head(image)
        second = self.m_down1(first)
        third = self.m_down2(second)
        fourth = self.m_down3(third)
        value = self.m_body(fourth)
        value = self.m_up3(value + fourth)
        value = self.m_up2(value + third)
        value = self.m_up1(value + second)
        return self.m_tail(value + first)


def estimate_noise(image):
    """Robust sRGB AWGN sigma from absolute orthonormal Haar HH coefficients.

    Constants and planar ramps cancel. The standard normal absolute median is
    Phi^-1(3/4). Use the lower median, matching torch.median for even counts;
    topk + amax exports to Core ML without an unsupported median operator.
    Sparse texture/edge outliers cannot dominate the estimate as with a mean.
    Dense texture and non-Gaussian sensor noise still need qualification.
    """
    diagonal = (image[:, :, 0::2, 0::2] - image[:, :, 0::2, 1::2]
                - image[:, :, 1::2, 0::2] + image[:, :, 1::2, 1::2]) / 2
    deviations = diagonal.abs().reshape(-1)
    lower = torch.topk(deviations, (deviations.numel() + 1) // 2,
                       largest=False, sorted=False).values
    return lower.amax() / 0.6744897501960817


class NoiseConditioned(nn.Module):
    def __init__(self, network):
        super().__init__()
        self.network = network

    def forward(self, image):
        noise = torch.ones_like(image[:, :1]) * estimate_noise(image)
        return self.network(torch.cat((image, noise), dim=1))


class LinearDenoise(nn.Module):
    def __init__(self, network):
        super().__init__()
        self.network = network

    def forward(self, image):
        # The pretrained denoiser consumes display-referred sRGB, not RAW data.
        bounded = image.clamp(0, 1)
        encoded = torch.where(bounded <= 0.0031308, bounded * 12.92,
                              1.055 * bounded.clamp(min=1e-8).pow(1 / 2.4) - 0.055)
        prediction = self.network(encoded).clamp(0, 1)
        decoded = torch.where(prediction <= 0.04045, prediction / 12.92,
                              ((prediction + 0.055) / 1.055).pow(2.4))
        # Preserve whole RGB pixels with negative/HDR components. Exactly
        # uniform tiles have no spatial variation to remove; keep them exact.
        in_domain = ((image >= 0) & (image <= 1)).all(dim=1, keepdim=True)
        uniform = (image == image[:, :, :1, :1]).all()
        return torch.where(in_domain & ~uniform, decoded, image)
