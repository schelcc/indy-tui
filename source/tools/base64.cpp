#include "tools/base64.hpp"
#include <cstddef>
#include <cstdlib>

namespace Tools {

std::string b64_decode(std::string_view encoded) {
  std::string output{};
  size_t pad = 0;

  // Increment padding based on last two chars
  pad += (encoded[encoded.length() - 1] == '=');
  pad += (encoded[encoded.length() - 2] == '=');

  size_t bytes = ((encoded.length() * 6) - (pad << 2)) / 8;

  output.resize(bytes + pad, '0');

  auto enc_iter = encoded.begin();
  auto enc_end = encoded.end();
  size_t str_idx = 0;
  size_t line;

  size_t a;
  size_t b;
  size_t c;
  size_t d;

  for (;;) {
    a = B64_ALPH[*enc_iter++];
    b = B64_ALPH[*enc_iter++];
    c = B64_ALPH[*enc_iter++];
    d = B64_ALPH[*enc_iter++];

    line = 0;
    line = ((a << 18) | (b << 12) | (c << 6) | d);

    output[str_idx++] = static_cast<char>((line & 0xFF0000) >> 16);
    output[str_idx++] = static_cast<char>((line & 0xFF00) >> 8);
    output[str_idx++] = static_cast<char>(line & 0xFF);

    if (enc_iter == enc_end) [[unlikely]]
      break;
  }

  // Resize output to cut off garbage at end if padding occurred
  if (pad > 0)
    output.resize(bytes - 1);

  return output;
}

std::string b64_encode(std::string_view input) {
  std::string output{};

  auto in_iter = input.begin();

  char a;
  char b;
  char c;

  size_t line;

  char pad = 0;

  size_t mask_a = 0x3F << 18;
  size_t mask_b = 0x3F << 12;
  size_t mask_c = 0x3F << 6;

  while (in_iter < input.end()) {
    a = *(in_iter++);
    b = ((in_iter != input.end()) ? *in_iter++ : pad++);
    c = ((in_iter != input.end()) ? *in_iter++ : pad++);

    line = 0;
    line = (line | ((a << 16) | (b << 8) | c));

    output += ENC_B64[(line & mask_a) >> 18];
    output += ENC_B64[(line & mask_b) >> 12];
    output += (pad > 1) ? '=' : ENC_B64[(line & mask_c) >> 6];
    output += (pad > 0) ? '=' : ENC_B64[(line & 0x3F)];
  }

  return output;
}

}; // namespace Tools
