#pragma once
#include "core/edits/Retouch.h"
struct sqlite3;
namespace luma::detail {
std::vector<RetouchStroke> readRetouch(sqlite3 *db, const std::string &assetID);
}
