#include "signature_scan.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

} // namespace

int main() {
    using soma::signature::MatchStatus;
    using soma::signature::ParseStatus;

    const auto parsed = soma::signature::Parse("48 8B ?? 30 E9 ? ? ? ?");
    Expect(parsed.status == ParseStatus::valid, "wildcard signature should parse");
    Expect(parsed.pattern.bytes.size() == 9, "signature byte count");

    constexpr std::array<std::uint8_t, 14> image{
        0x90, 0x48, 0x8B, 0x49, 0x30, 0xE9, 0xE5,
        0xB3, 0x1C, 0x00, 0xCC, 0xCC, 0xCC, 0xCC,
    };
    const auto unique = soma::signature::FindUnique(image, parsed.pattern);
    Expect(unique.status == MatchStatus::unique, "signature should be unique");
    Expect(unique.offset == 1, "unique signature offset");

    const auto absentPattern = soma::signature::Parse("DE AD BE EF");
    const auto absent = soma::signature::FindUnique(image, absentPattern.pattern);
    Expect(absent.status == MatchStatus::noMatch, "missing signature should fail");

    constexpr std::array<std::uint8_t, 4> repeated{0xAA, 0xBB, 0xAA, 0xBB};
    const auto repeatedPattern = soma::signature::Parse("AA BB");
    const auto ambiguous =
        soma::signature::FindUnique(repeated, repeatedPattern.pattern);
    Expect(ambiguous.status == MatchStatus::ambiguous,
           "multiple matches should fail closed");
    Expect(ambiguous.count == 2, "ambiguity should report at least two matches");

    const auto finalPattern = soma::signature::Parse("CC CC CC CC");
    const auto final = soma::signature::FindUnique(image, finalPattern.pattern);
    Expect(final.status == MatchStatus::unique, "final possible offset must be scanned");
    Expect(final.offset == 10, "final possible offset");

    Expect(soma::signature::Parse("").status == ParseStatus::empty,
           "empty signature should be rejected");
    Expect(soma::signature::Parse("4").status == ParseStatus::invalidToken,
           "single hex digit should be rejected");
    Expect(soma::signature::Parse("GG").status == ParseStatus::invalidToken,
           "non-hex token should be rejected");
    Expect(soma::signature::Parse("48?8B").status == ParseStatus::invalidToken,
           "joined tokens should be rejected");

    if (g_failures != 0) {
        std::cerr << g_failures << " signature scanner assertion(s) failed\n";
        return 1;
    }
    std::cout << "signature parsing and unique-match tests passed\n";
    return 0;
}
