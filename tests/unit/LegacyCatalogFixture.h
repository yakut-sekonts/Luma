#pragma once

namespace fixture {
// Used only on generated test catalogs before reconstructing a schema 1/2
// edit_documents table. Remove later features instead of leaving a mixed schema
// that cannot represent an actual historical catalog.
inline constexpr const char *removePostV11Tables = R"sql(
    DROP TABLE import_item_plans;
    DROP TABLE import_options;
    DROP TRIGGER smart_collection_rename;
    DROP TABLE smart_collections;
    DROP TRIGGER initialize_creative;
    DROP TABLE edit_creative;
    DROP TABLE edit_versions;
    DROP TRIGGER initialize_asset_metadata;
    DROP TRIGGER update_organization_metadata;
    DROP TRIGGER update_color_label_metadata;
    DROP TABLE asset_creators;
    DROP TABLE asset_keywords;
    DROP TABLE asset_metadata;
    DROP TABLE derived_images;
    DROP TABLE semantic_embeddings;
    DROP TRIGGER initialize_retouch;
    DROP TABLE edit_retouch;
)sql";
} // namespace fixture
