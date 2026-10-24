#pragma once
#include "platform/macos/ImageExporter.h"

namespace luma::macos {
void writeAdvancedImage(const DecodedRaw &decoded, const EditDocument &edits,
                        const std::filesystem::path &destination, const ExportOptions &options,
                        CancellationToken stop, const std::function<void(double)> &progress,
                        const std::filesystem::path &metadataSource,
                        const ExportInstallGuard &installGuard,
                        const std::optional<MetadataOverlay> &metadataOverlay = {});
} // namespace luma::macos
