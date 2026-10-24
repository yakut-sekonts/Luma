#pragma once
#include "core/metadata/DescriptiveMetadata.h"
#include "platform/macos/CFHandle.h"
#include <ImageIO/ImageIO.h>
#include <span>

namespace luma::macos {
DescriptiveMetadata readDescriptiveMetadata(const std::filesystem::path &source,
                                            std::span<const std::uint8_t> sidecar = {});
// Rewrites only Luma's descriptive properties; other parsed XMP properties are retained.
std::vector<std::uint8_t> writeDescriptiveXMP(const DescriptiveMetadata &values,
                                              std::span<const std::uint8_t> existing = {});
CFHandle<CGMutableImageMetadataRef> applyMetadataOverlay(CGImageMetadataRef source,
                                                         const MetadataOverlay &overlay);
} // namespace luma::macos
