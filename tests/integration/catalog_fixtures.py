"""Reconstruct pre-0.6 schemas from disposable schema-21 test catalogs."""


def remove_post_v11_features(database):
    # This is fixture construction, never a production downgrade path. Keep the
    # schema guard: a later schema must explicitly update its historical fixtures.
    assert database.execute("PRAGMA user_version").fetchone() == (21,)
    for statement in (
        "DROP TABLE import_item_plans", "DROP TABLE import_options",
        "DROP TRIGGER smart_collection_rename", "DROP TABLE smart_collections",
        "DROP TRIGGER initialize_creative", "DROP TABLE edit_creative",
        "DROP TABLE edit_versions", "DROP TRIGGER initialize_asset_metadata",
        "DROP TRIGGER update_organization_metadata", "DROP TRIGGER update_color_label_metadata",
        "DROP TABLE asset_creators", "DROP TABLE asset_keywords", "DROP TABLE asset_metadata",
        "DROP TABLE derived_images", "DROP TABLE semantic_embeddings",
        "DROP TRIGGER initialize_retouch", "DROP TABLE edit_retouch",
    ):
        database.execute(statement)
