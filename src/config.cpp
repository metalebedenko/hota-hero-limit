#include "config.hpp"

#include <fstream>
#include <string>
#include <string_view>

namespace hero_limit {
namespace {
std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == text.npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

bool boolean(std::string_view value, bool& output) {
    if (value == "true" || value == "false") {
        output = value == "true";
        return true;
    }
    return false;
}
}

std::optional<Settings> read_settings(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return error ? std::nullopt : std::optional(Settings{});
    }
    std::ifstream file(path);
    Settings settings;
    bool has_section = false;
    unsigned seen = 0;
    unsigned line_number = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (++line_number == 1 && line.starts_with("\xef\xbb\xbf")) {
            line.erase(0, 3);
        }
        const auto text = trim(std::string_view(line).substr(0, line.find_first_of(";#")));
        if (text.empty()) {
            continue;
        }
        if (text == "[Rules]" && !has_section) {
            has_section = true;
            continue;
        }
        const auto equals = text.find('=');
        if (!has_section || equals == text.npos) {
            return std::nullopt;
        }
        const auto key = trim(text.substr(0, equals));
        const auto value = trim(text.substr(equals + 1));
        unsigned field = 0;
        if (key == "HeroesPerGame" && (value == "1" || value == "2" || value == "3")) {
            field = 1;
            settings.heroes_per_game = static_cast<std::uint8_t>(value.front() - '0');
        } else if (key == "RetreatIsPermanent" && boolean(value, settings.retreat_is_permanent)) {
            field = 2;
        } else if (key == "PrisonsEnabled" && boolean(value, settings.prisons_enabled)) {
            field = 4;
        }
        if (field == 0 || (seen & field) != 0) {
            return std::nullopt;
        }
        seen |= field;
    }
    return file.eof() && has_section && !file.bad() ? std::optional(settings) : std::nullopt;
}

} // namespace hero_limit
