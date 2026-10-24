#pragma once
#include "core/edits/CreativeSettings.h"
struct sqlite3;
namespace luma::detail {
CreativeSettings readCreative(sqlite3 *db, const std::string &assetID);
}
