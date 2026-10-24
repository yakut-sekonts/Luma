"""Generate a project-owned Bayer DNG. No private photographs are distributed."""
import struct
from pathlib import Path


def create_dng(path: Path, *, maker="Luma", model="Synthetic Bayer Camera", lens=None,
               focal=35, aperture=2.8, width=128, height=96, sample=None, orientation=1, exif_ifd=False,
               shutter=(1, 125)) -> None:
    entries = []

    def tag(identifier, kind, values):
        if kind == 2:
            data = values.encode("ascii") + b"\0"
            count = len(data)
        elif kind in (5, 10):
            count = len(values)
            data = b"".join(struct.pack("<II" if kind == 5 else "<ii", *v) for v in values)
        else:
            count = len(values)
            data = struct.pack("<" + {1: "B", 3: "H", 4: "I"}[kind] * count, *values)
        entries.append((identifier, kind, count, data))

    for identifier, value in [(256, width), (257, height), (273, 0), (278, height), (279, width * height * 2), (50717, 16383)]:
        tag(identifier, 4, [value])
    for identifier, value in [(258, 16), (259, 1), (262, 32803), (274, orientation), (277, 1), (284, 1), (50711, 1), (50778, 21), (34855, 100)]:
        tag(identifier, 3, [value])
    tag(271, 2, maker)
    tag(272, 2, model)
    tag(50708, 2, maker + " " + model)
    if lens:
        tag(42036, 2, lens)
    tag(33421, 3, [2, 2])
    tag(33422, 1, [0, 1, 1, 2])
    tag(50706, 1, [1, 4, 0, 0])
    tag(50707, 1, [1, 1, 0, 0])
    tag(50710, 1, [0, 1, 2])
    tag(50714, 5, [(512, 1)])
    tag(50728, 5, [(1, 1), (1, 1), (1, 1)])
    tag(50721, 10, [(32406, 10000), (-15372, 10000), (-4986, 10000), (-9689, 10000), (18758, 10000), (415, 10000), (557, 10000), (-2040, 10000), (10570, 10000)])
    tag(50719, 4, [0, 0])
    tag(50720, 4, [width, height])
    tag(33434, 5, [shutter])
    tag(33437, 5, [(int(round(aperture * 100)), 100)])
    tag(37386, 5, [(int(round(focal * 100)), 100)])
    exif_entries = []
    if exif_ifd:
        exif_tags = {33434, 33437, 34855, 37386, 42036}
        exif_entries = sorted(entry for entry in entries if entry[0] in exif_tags)
        entries = [entry for entry in entries if entry[0] not in exif_tags]
        tag(34665, 4, [0])
    entries.sort()
    extra = bytearray()
    table = bytearray()
    payload_start = 8 + 2 + len(entries) * 12 + 4
    # Offset of the uncompressed sensor data after the IFD's auxiliary values.
    image_start = payload_start + sum((len(data) + 1) // 2 * 2 for _, _, _, data in entries if len(data) > 4)
    for identifier, kind, count, data in entries:
        if identifier == 273:
            data = struct.pack("<I", image_start)
        elif identifier == 34665:
            data = struct.pack("<I", image_start + width * height * 2)
        if len(data) <= 4:
            inline = data.ljust(4, b"\0")
        else:
            inline = struct.pack("<I", payload_start + len(extra))
            extra.extend(data)
            if len(extra) % 2:
                extra.append(0)
        table.extend(struct.pack("<HHI", identifier, kind, count) + inline)
    samples = bytearray()
    for y in range(height):
        for x in range(width):
            # A smooth luminance ramp plus a slight per-channel tint.
            color = [0, 1, 1, 2][(y & 1) * 2 + (x & 1)]
            brightness = 0.08 + 0.55 * x / (width - 1) if sample is None else sample(x, y)
            value = 512 + int(brightness * (16383 - 512) * [1, 0.95, 0.9][color])
            samples.extend(struct.pack("<H", value))
    encoded = b"II" + struct.pack("<HIH", 42, 8, len(entries)) + table + b"\0" * 4 + extra + samples
    if exif_ifd:
        exif_table, exif_extra = bytearray(), bytearray()
        exif_payload = len(encoded) + 2 + len(exif_entries) * 12 + 4
        for identifier, kind, count, data in exif_entries:
            if len(data) <= 4:
                inline = data.ljust(4, b"\0")
            else:
                inline = struct.pack("<I", exif_payload + len(exif_extra))
                exif_extra.extend(data)
                if len(exif_extra) % 2:
                    exif_extra.append(0)
            exif_table.extend(struct.pack("<HHI", identifier, kind, count) + inline)
        encoded += struct.pack("<H", len(exif_entries)) + exif_table + b"\0" * 4 + exif_extra
    path.write_bytes(encoded)
