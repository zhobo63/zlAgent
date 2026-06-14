#pragma once

#include <string>
#include "utf8.h"

namespace agent {

/**
 * Convert a UTF-8 string to a wide (UTF-16) string.
 * Uses utf8_mbtowc from utf8.h - no Windows API dependency.
 */
inline std::wstring utf8_to_wide(const std::string& input) {
    std::wstring output;
    const unsigned char* data = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();

    for (size_t i = 0; i < len;) {
        ucs4_t wc = 0;
        int n = utf8_mbtowc(&wc, &data[i], static_cast<int>(len - i));

        if (n == 0) {
            // Invalid UTF-8 byte - skip one byte
            ++i;
        } else {
            // Convert UCS4 to UTF-16 (handle surrogate pairs for code points > 0xFFFF).
            if (wc <= 0xFFFF) {
                output += static_cast<wchar_t>(wc);
            } else if (wc <= 0x10FFFF) {
                wc -= 0x10000;
                output += static_cast<wchar_t>((wc >> 10) + 0xD800);   // high surrogate
                output += static_cast<wchar_t>((wc & 0x3FF) + 0xDC00); // low surrogate
            }
            i += static_cast<size_t>(n);
        }
    }

    return output;
}

/**
 * Convert a wide (UTF-16) string to a UTF-8 string.
 * Uses utf8_wctomb from utf8.h - no Windows API dependency.
 */
inline std::string wide_to_utf8(const std::wstring& input) {
    std::string output;

    for (size_t i = 0; i < input.size(); ++i) {
        ucs4_t wc = static_cast<ucs4_t>(input[i]);

        // Handle UTF-16 surrogate pairs.
        if (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < input.size()) {
            ucs4_t low = static_cast<ucs4_t>(input[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                wc = ((wc - 0xD800) << 10) | (low - 0xDC00) + 0x10000;
                ++i; // skip the low surrogate
            }
        }

        unsigned char buf[6];
        int n = utf8_wctomb(buf, wc, sizeof(buf));
        if (n > 0) {
            output.append(reinterpret_cast<char*>(buf), n);
        } else {
            // Unconvertible - substitute '?'
            output += '?';
        }
    }

    return output;
}

} // namespace agent
