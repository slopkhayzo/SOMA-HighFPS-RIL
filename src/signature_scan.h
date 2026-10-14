#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace soma::signature {

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

struct Pattern {
    std::vector<PatternByte> bytes;
};

enum class ParseStatus {
    valid,
    empty,
    invalidToken,
};

struct ParseResult {
    ParseStatus status = ParseStatus::empty;
    Pattern pattern;
    std::size_t errorOffset = 0;
};

enum class MatchStatus {
    noMatch,
    unique,
    ambiguous,
};

struct MatchResult {
    MatchStatus status = MatchStatus::noMatch;
    std::size_t offset = 0;
    std::size_t count = 0;
};

[[nodiscard]] ParseResult Parse(std::string_view text);
[[nodiscard]] MatchResult FindUnique(std::span<const std::uint8_t> haystack,
                                     const Pattern& pattern) noexcept;

} // namespace soma::signature
