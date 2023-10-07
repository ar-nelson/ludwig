#include "base64.h++"

// Base64 code adapted from https://gist.github.com/tomykaira/f0fd86b6c73063283afe550bc5d77594

// Original license:

/**
 * The MIT License (MIT)
 * Copyright (c) 2016 tomykaira
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

namespace Base64 {
  std::string encode(const char* data, uint64_t in_len, bool add_equals) {
    static constexpr char ENCODING_TABLE[64] = {
      'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
      'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
      'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
      'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
      'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
      'w', 'x', 'y', 'z', '0', '1', '2', '3',
      '4', '5', '6', '7', '8', '9', '-', '_'
    };

    size_t out_len = 4 * ((in_len + 2) / 3);
    std::string ret(out_len, '\0');
    size_t i;
    char *p = const_cast<char*>(ret.c_str());
    bool equals_used = false;

    for (i = 0; i < in_len - 2; i += 3) {
      *p++ = ENCODING_TABLE[(data[i] >> 2) & 0x3F];
      *p++ = ENCODING_TABLE[((data[i] & 0x3) << 4) | ((int) (data[i + 1] & 0xF0) >> 4)];
      *p++ = ENCODING_TABLE[((data[i + 1] & 0xF) << 2) | ((int) (data[i + 2] & 0xC0) >> 6)];
      *p++ = ENCODING_TABLE[data[i + 2] & 0x3F];
    }
    if (i < in_len) {
      *p++ = ENCODING_TABLE[(data[i] >> 2) & 0x3F];
      if (i == (in_len - 1)) {
        *p++ = ENCODING_TABLE[((data[i] & 0x3) << 4)];
        *p++ = '=';
        equals_used = true;
      }
      else {
        *p++ = ENCODING_TABLE[((data[i] & 0x3) << 4) | ((int) (data[i + 1] & 0xF0) >> 4)];
        *p++ = ENCODING_TABLE[((data[i + 1] & 0xF) << 2)];
      }
      *p++ = '=';
      equals_used = true;
    }

    if (!add_equals && equals_used) return ret.substr(0, ret.find_first_of('='));
    return ret;
  }

  size_t decode(const std::string_view& input, char* out, size_t out_len) {
    static constexpr unsigned char DECODING_TABLE[256] = {
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 62, 64, 63,
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
      64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
      64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
    };

    size_t in_len = input.size();

    size_t j = 0;
    for (size_t i = 0; i < in_len;) {
      uint32_t a = (i >= in_len || input[i] == '=') ? 0 & i++ : DECODING_TABLE[static_cast<int>(input[i++])];
      uint32_t b = (i >= in_len || input[i] == '=') ? 0 & i++ : DECODING_TABLE[static_cast<int>(input[i++])];
      uint32_t c = (i >= in_len || input[i] == '=') ? 0 & i++ : DECODING_TABLE[static_cast<int>(input[i++])];
      uint32_t d = (i >= in_len || input[i] == '=') ? 0 & i++ : DECODING_TABLE[static_cast<int>(input[i++])];

      uint32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + (d << 0 * 6);

      if (j < out_len) out[j++] = static_cast<char>((triple >> 2 * 8) & 0xFF);
      if (j < out_len) out[j++] = static_cast<char>((triple >> 1 * 8) & 0xFF);
      if (j < out_len) out[j++] = static_cast<char>((triple >> 0 * 8) & 0xFF);
    }

    return j;
  }

  size_t decode(const std::string_view& input, std::string& out) {
    size_t in_len = input.size();

    size_t out_len = in_len / 4 * 3;
    switch (in_len % 4) {
      case 3: out_len += 2; break;
      case 2: out_len += 1; break;
      case 1: return 0;
      case 0:
        if (input[in_len - 1] == '=') out_len--;
        if (input[in_len - 2] == '=') out_len--;
    }

    out.resize(out_len);

    return decode(input, const_cast<char*>(out.c_str()), out_len);
  }

  std::string decode(const std::string_view& input) {
    std::string out;
    decode(input, out);
    return out;
  }
}
