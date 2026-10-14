#include "signature_scan.h"

#include <cctype>
#include <limits>

namespace soma::signature {
namespace {

int HexDigit(const char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

} // namespace

ParseResult Parse(const std::string_view text) {
    ParseResult result{};
    std::size_t cursor = 0;

    while (cursor < text.size()) {
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        if (cursor == text.size()) {
            break;
        }

        const std::size_t tokenOffset = cursor;
        if (text[cursor] == '?') {
            ++cursor;
            if (cursor < text.size() && text[cursor] == '?') {
                ++cursor;
            }
            if (cursor < text.size() &&
                std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
                result.status = ParseStatus::invalidToken;
                result.errorOffset = tokenOffset;
                return result;
            }
            result.pattern.bytes.push_back(PatternByte{0, true});
            continue;
        }

        if (cursor + 2 > text.size()) {
            result.status = ParseStatus::invalidToken;
            result.errorOffset = tokenOffset;
            return result;
        }
        const int high = HexDigit(text[cursor]);
        const int low = HexDigit(text[cursor + 1]);
        if (high < 0 || low < 0) {
            result.status = ParseStatus::invalidToken;
            result.errorOffset = tokenOffset;
            return result;
        }
        cursor += 2;
        if (cursor < text.size() &&
            std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
            result.status = ParseStatus::invalidToken;
            result.errorOffset = tokenOffset;
            return result;
        }
        result.pattern.bytes.push_back(
            PatternByte{static_cast<std::uint8_t>((high << 4) | low), false});
    }

    result.status = result.pattern.bytes.empty() ? ParseStatus::empty
                                                 : ParseStatus::valid;
    return result;
}

MatchResult FindUnique(const std::span<const std::uint8_t> haystack,
                       const Pattern& pattern) noexcept {
    MatchResult result{};
    if (pattern.bytes.empty() || pattern.bytes.size() > haystack.size()) {
        return result;
    }

    const std::size_t finalStart = haystack.size() - pattern.bytes.size();
    for (std::size_t start = 0; start <= finalStart; ++start) {
        bool matches = true;
        for (std::size_t index = 0; index < pattern.bytes.size(); ++index) {
            const PatternByte expected = pattern.bytes[index];
            if (!expected.wildcard && haystack[start + index] != expected.value) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            continue;
        }

        if (result.count == 0) {
            result.offset = start;
        }
        if (result.count != std::numeric_limits<std::size_t>::max()) {
            ++result.count;
        }
        if (result.count > 1) {
            result.status = MatchStatus::ambiguous;
            return result;
        }
    }

    if (result.count == 1) {
        result.status = MatchStatus::unique;
    }
    return result;
}

} // namespace soma::signature
