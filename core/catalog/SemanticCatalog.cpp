#include "core/ai/Embedding.h"
#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <bit>
#include <queue>

namespace luma {
void Catalog::migrateSemanticIndex() {
    execute(R"sql(
        CREATE TABLE semantic_embeddings(
            asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
            provider TEXT NOT NULL CHECK(length(provider) BETWEEN 1 AND 256),
            checksum TEXT NOT NULL, dimension INTEGER NOT NULL CHECK(dimension BETWEEN 1 AND 2048),
            embedding BLOB NOT NULL CHECK(typeof(embedding)='blob' AND length(embedding)=4*dimension),
            updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY(provider,asset_id));
        INSERT INTO schema_migrations(version) VALUES(14);
        PRAGMA user_version=14;
    )sql");
}
std::vector<std::string> Catalog::pendingSemanticImages(const std::string &provider,
                                                        const std::string &after, int limit) const {
    if (provider.empty() || provider.size() > 256 || limit < 1 || limit > 128)
        throw std::runtime_error("Invalid semantic index request.");
    detail::Statement query(db_, "SELECT a.id FROM assets a WHERE a.id>?1 AND NOT EXISTS("
                                 "SELECT 1 FROM semantic_embeddings e WHERE e.asset_id=a.id AND "
                                 "e.provider=?2 AND e.checksum=a.checksum)"
                                 " ORDER BY a.id LIMIT ?3");
    query.text(1, after);
    query.text(2, provider);
    query.integer(3, limit);
    std::vector<std::string> result;
    while (query.row())
        result.push_back(query.string(0));
    return result;
}
void Catalog::saveSemanticEmbedding(const std::string &id, const std::string &checksum,
                                    const std::string &provider, std::span<const float> embedding) {
    if (provider.empty() || provider.size() > 256)
        throw std::runtime_error("Invalid semantic provider identity.");
    const auto vector = ai::normalizedEmbedding(embedding);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(vector.size() * 4);
    for (float value : vector) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (int b = 0; b < 4; ++b)
            bytes.push_back(static_cast<std::uint8_t>(bits >> (8 * b)));
    }
    detail::Statement write(
        db_, "INSERT INTO semantic_embeddings(asset_id,provider,checksum,dimension,embedding)"
             " SELECT id,?2,?3,?4,?5 FROM assets WHERE id=?1 AND checksum=?3"
             " ON CONFLICT(provider,asset_id) DO UPDATE SET "
             "checksum=excluded.checksum,dimension=excluded.dimension,"
             "embedding=excluded.embedding,updated_at=CURRENT_TIMESTAMP");
    write.text(1, id);
    write.text(2, provider);
    write.text(3, checksum);
    write.integer(4, static_cast<std::int64_t>(vector.size()));
    write.blob(5, bytes);
    write.row();
    if (sqlite3_changes(db_) != 1)
        throw std::runtime_error("Photo changed while creating its semantic index.");
}
std::vector<ai::SemanticMatch> Catalog::semanticSearch(const std::string &provider,
                                                       std::span<const float> embedding, int limit,
                                                       CancellationToken stop) const {
    if (limit < 1 || limit > 200)
        throw std::runtime_error("Invalid semantic result limit.");
    const auto queryVector = ai::normalizedEmbedding(embedding);
    detail::Statement query(db_, "SELECT e.asset_id,e.embedding FROM semantic_embeddings e"
                                 " JOIN assets a ON a.id=e.asset_id AND a.checksum=e.checksum"
                                 " WHERE e.provider=?1 AND e.dimension=?2 ORDER BY e.asset_id");
    query.text(1, provider);
    query.integer(2, static_cast<std::int64_t>(queryVector.size()));
    const auto better = [](const ai::SemanticMatch &a, const ai::SemanticMatch &b) {
        return a.similarity != b.similarity ? a.similarity > b.similarity : a.id < b.id;
    };
    std::priority_queue<ai::SemanticMatch, std::vector<ai::SemanticMatch>, decltype(better)> best(
        better);
    while (query.row()) {
        checkCancellation(stop);
        const auto bytes = query.bytes(1);
        if (bytes.size() != queryVector.size() * 4)
            throw std::runtime_error("Invalid stored semantic embedding.");
        double dot = 0, norm = 0;
        for (std::size_t i = 0; i < queryVector.size(); ++i) {
            std::uint32_t bits = 0;
            for (int b = 0; b < 4; ++b)
                bits |= static_cast<std::uint32_t>(bytes[i * 4 + b]) << (8 * b);
            const auto value = std::bit_cast<float>(bits);
            if (!std::isfinite(value))
                throw std::runtime_error("Invalid stored semantic value.");
            dot += value * static_cast<double>(queryVector[i]);
            norm += static_cast<double>(value) * value;
        }
        if (norm < 1e-16)
            continue;
        ai::SemanticMatch match{query.string(0), dot / std::sqrt(norm)};
        if (match.similarity < .15)
            continue;
        if (best.size() < static_cast<std::size_t>(limit))
            best.push(match);
        else if (better(match, best.top())) {
            best.pop();
            best.push(match);
        }
    }
    std::vector<ai::SemanticMatch> result;
    while (!best.empty()) {
        result.push_back(best.top());
        best.pop();
    }
    std::sort(result.begin(), result.end(), better);
    return result;
}
void Catalog::clearSemanticIndex() {
    execute("DELETE FROM semantic_embeddings;");
}
} // namespace luma
