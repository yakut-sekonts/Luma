#pragma once
#include "core/export/ExportOptions.h"
#include "core/metadata/DescriptiveMetadata.h"
#include "platform/macos/CFHandle.h"
#include <ImageIO/ImageIO.h>
#include <optional>
#include <vector>

namespace luma::macos {
CFHandle<CGMutableImageMetadataRef>
exportMetadata(const std::filesystem::path &source, const ExportOptions &options, int width,
               int height, const std::optional<MetadataOverlay> &overlay = {});
std::vector<std::uint8_t> metadataXMP(CGImageMetadataRef metadata, MetadataPolicy policy);
} // namespace luma::macos
