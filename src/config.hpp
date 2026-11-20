#pragma once

#include "rules.hpp"

#include <filesystem>

namespace hero_limit {

// A missing file uses defaults. An unreadable or malformed file is an error.
[[nodiscard]] std::optional<Settings> read_settings(const std::filesystem::path& path);

} // namespace hero_limit
