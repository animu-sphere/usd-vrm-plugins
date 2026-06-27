// SPDX-License-Identifier: Apache-2.0
#include "PathUtil.h"

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// FNV-1a, so the empty/non-ASCII fallback name is stable across runs.
std::uint64_t _Hash(const std::string& value)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string _HashSuffix(const std::string& value)
{
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<std::uint32_t>(_Hash(value) & 0xffffffffu);
    return out.str();
}

bool _IsIdentifierChar(unsigned char c)
{
    return std::isalnum(c) || c == '_';
}

} // namespace

std::string
VrmSanitizeIdentifier(const std::string& value, const std::string& fallbackPrefix)
{
    std::string out;
    out.reserve(value.size());

    bool hasAsciiNameChar = false;
    for (unsigned char c : value) {
        if (c < 128 && _IsIdentifierChar(c)) {
            out.push_back(static_cast<char>(c));
            hasAsciiNameChar = true;
        } else {
            out.push_back('_');
        }
    }

    if (out.empty() || !hasAsciiNameChar) {
        out = fallbackPrefix + "_" + _HashSuffix(value);
    }

    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0]))) {
        out = fallbackPrefix + "_" + out;
    }

    return out;
}

std::vector<std::string>
VrmMakeUniqueNames(const std::vector<std::string>& sourceNames,
                   const std::string& fallbackPrefix)
{
    std::vector<std::string> result;
    result.reserve(sourceNames.size());

    std::map<std::string, int> counts;
    for (const std::string& sourceName : sourceNames) {
        const std::string base = VrmSanitizeIdentifier(sourceName, fallbackPrefix);
        int& count = counts[base];
        ++count;
        result.push_back(count == 1 ? base : base + "_" + std::to_string(count));
    }

    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
