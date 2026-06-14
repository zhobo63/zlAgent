#pragma once

#include <string>
#include "big5.h"
#include "utf8.h"

namespace agent {

/**
 * Encoding conversion utilities.
 *
 * BIG5 <-> UTF-8 via UCS4 intermediate:
 *   BIG5 -> UCS4  (big5_mbtowc)
 *   UCS4  -> UTF8 (utf8_wctomb)
 *   UTF8  -> UCS4 (utf8_mbtowc)
 *   UCS4  -> BIG5 (big5_wctomb)
 */

/**
 * Convert a BIG5-encoded string to UTF-8.
 * Unconvertible characters are replaced with '?'.
 */
inline std::string big5_to_utf8(const std::string& input) {
    std::string output;

    const unsigned char* data = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();

    for (size_t i = 0; i < len;) {
        ucs4_t wc = 0;
        int n = big5_mbtowc(&wc, &data[i], static_cast<int>(len - i));

        if (n == 0) {
            // Unconvertible - substitute '?' and advance one byte
            output += '?';
            ++i;
        } else {
            // Convert UCS4 to UTF-8
            unsigned char buf[6];
            int utf8_len = utf8_wctomb(buf, wc, sizeof(buf));
            if (utf8_len > 0) {
                output.append(reinterpret_cast<char*>(buf), utf8_len);
            } else {
                output += '?';
            }
            i += static_cast<size_t>(n);
        }
    }

    return output;
}

/**
 * Sanitize a string by replacing invalid UTF-8 byte sequences with '?'.
 * Use this before passing data to json::parse() or other strict parsers.
 */
inline std::string sanitize_utf8(const std::string& input) {
    std::string output;
    const unsigned char* data = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();

    for (size_t i = 0; i < len;) {
        ucs4_t wc = 0;
        int n = utf8_mbtowc(&wc, &data[i], static_cast<int>(len - i));

        if (n == 0) {
            // Invalid UTF-8 byte - substitute '?' and advance one byte
            output += '?';
            ++i;
        } else {
            // Valid UTF-8 - copy through as-is
            output.append(reinterpret_cast<const char*>(&data[i]), n);
            i += static_cast<size_t>(n);
        }
    }

    return output;
}

/**
 * Convert a UTF-8-encoded string to BIG5.
 * Strips the BOM if present. Unconvertible characters are replaced with '?'.
 */
inline std::string utf8_to_big5(const std::string& input) {
    std::string output;

    // Strip UTF-8 BOM (EF BB BF) if present - some LLMs may include it.
    const unsigned char* data = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        len -= 3;
    }

    for (size_t i = 0; i < len;) {
        ucs4_t wc = 0;
        int n = utf8_mbtowc(&wc, &data[i], static_cast<int>(len - i));

        if (n == 0) {
            // Unconvertible - substitute '?' and advance one byte
            output += '?';
            ++i;
        } else {
            // Convert UCS4 to BIG5
            unsigned char buf[2];
            int big5_len = big5_wctomb(buf, wc, sizeof(buf));
            if (big5_len > 0) {
                output.append(reinterpret_cast<char*>(buf), big5_len);
            } else {
                // Character not in BIG5 charset - substitute '?'
                output += '?';
            }
            i += static_cast<size_t>(n);
        }
    }

    return output;
}



} // namespace agent
