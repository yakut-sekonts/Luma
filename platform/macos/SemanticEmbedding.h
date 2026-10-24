#pragma once
#include "core/ai/Providers.h"
#include <memory>
namespace luma::macos {
// Offline English semantic search over Vision's image classifications.
// Explicitly label-based: it is not a CLIP image/text joint encoder.
std::shared_ptr<ai::IEmbeddingProvider> makeSemanticEmbedding();
} // namespace luma::macos
