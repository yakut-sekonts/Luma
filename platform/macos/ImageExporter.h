#pragma once
#include "core/edits/EditDocument.h"
#include "core/export/ExportOptions.h"
#include "core/metadata/DescriptiveMetadata.h"
#include "core/raw/RawDecoder.h"

namespace luma::macos {
using ExportInstallGuard = std::function<void(const std::function<void()> &)>;
void writeImage(const DecodedRaw &decoded, EditDocument edits,
                const std::filesystem::path &destination, ExportOptions options,
                CancellationToken stop = {}, const std::function<void(double)> &progress = {},
                const std::filesystem::path &metadataSource = {},
                const ExportInstallGuard &installGuard = {},
                const std::optional<MetadataOverlay> &metadataOverlay = {});
} // namespace luma::macos
