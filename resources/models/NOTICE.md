# Luma bundled offline models

The installer and Sparkle update archive carry the same compiled Core ML models
under `Luma.app/Contents/Resources/Models`. Runtime processing stays on the Mac;
Luma does not download weights or send photographs to a model service.
Python, PyTorch and conversion tools are build dependencies and are not shipped.

`sources.json` pins the source revision, exact weight bytes and upstream licenses.
The generated `manifest.json` records the conversion recipe, toolchain and every
artifact checksum. It records provenance, not a successful quality qualification.

## Denoise: DRUNet color

Copyright (c) 2020 Kai Zhang. The MIT license is in `licenses/DRUNet-LICENSE.txt`.
Source: https://github.com/cszn/DPIR/tree/15bca3fcc1f3cc51a1f99ccf027691e278c19354
The checkpoint is the author's `drunet_color.pth`, distributed through his KAIR
v1.0 release and linked by DPIR's model download script. Its exact URL, size and
SHA-256 are recorded in `sources.json`; the original weight bytes are unchanged.

Luma modifications: an inference-only architecture preserving checkpoint keys,
fixed 512×512 input, Float32 Core ML conversion,
linear-sRGB to sRGB input conversion and the inverse output conversion. The
native adapter uses reflected padding and a 64-pixel halo. RGB pixels with any
input channel outside [0,1] pass through unchanged to preserve negative samples
and HDR highlights; Amount blends the prediction with the original.

Downsampling uses four spatial slices followed by a 1×1 convolution. This
avoids incorrect boundary values observed in the native Core ML Float32 CPU
2×2 convolution on macOS 27. The non-overlapping transpose convolution is
expressed as a 1×1 convolution and PixelShuffle. Both preserve checkpoint
coefficients exactly; no training is performed.

DRUNet's fourth input channel is a noise-level map. Luma estimates a single sRGB
sigma per tile from the lower median of absolute orthonormal Haar HH coefficients,
divided by the standard normal absolute median, 0.6744897501960817. The lower
median is implemented with topk and amax for Core ML export. Sparse texture and
edge outliers cannot dominate it as with the previous mean-based estimator.
Constants and planar ramps cancel; there is no brightness cutoff or minimum
sigma. Exactly uniform RGB tiles pass through unchanged. Dense texture can still
increase the estimate, and neighboring tiles can yield different estimates;
these cases require photographic qualification. The wavelet MAD principle is
described at https://scikit-image.org/docs/stable/api/skimage.restoration.html#skimage.restoration.estimate_sigma
(Donoho and Johnstone, 1994). No scikit-image code is included.

The pretrained model targets Gaussian noise in display-referred images, not RAW
sensor data or a calibrated camera noise distribution. This is not a claim of
camera-independent RAW denoise accuracy. Fine detail, color, highlights, tile
boundaries and real noise behavior require photographic qualification. No
training dataset or upstream training code is included in the app.

## Sky: U²-Net Sky

Copyright (c) 2021 xiongzhu666. MIT license is reproduced in
`licenses/SkySeg-LICENSE.txt`.
Source: https://github.com/xiongzhu666/Sky-Segmentation-and-Post-processing/tree/1f7811b32b64ddc957269defff84bc87a3f0b74f
The ONNX weights are distributed by the attributed MIT model mirror:
https://huggingface.co/JianyuanWang/skyseg/tree/3ba8c6df1d9ba9ff26f637c7ba9568ac11a9aa7f

Luma modifications: use the first prediction output, wrap the author's ImageNet
RGB mean/std normalization, fixed 320×320 Float32 input, ONNX-to-Core-ML
conversion and finite probability coverage in the native mask pipeline. Input
is top-left sRGB with bilinear stretch resizing. Output is clamped to [0,1];
unlike the author's demonstration, Luma does not stretch each image's prediction
minimum/maximum to 0/1, which could amplify a weak no-sky response.

Sky boundaries, reflections, indoor scenes, thin foreground objects and images
without sky require qualification. Model confidence is not a guarantee of a
correct selection; the resulting mask remains editable.
