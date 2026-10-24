#pragma once
#include "core/edits/EditDocument.h"
#include <string>
#include <string_view>

namespace luma {
inline constexpr std::size_t maximumEditSnapshotBytes = 20 * 1024 * 1024;
// Catalog-only, versioned full-document serialization. No image or source path.
std::string encodeEditSnapshot(const EditDocument &edits);
EditDocument decodeEditSnapshot(std::string_view document);
} // namespace luma
