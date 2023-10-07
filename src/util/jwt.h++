#pragma once
#include "util/common.h++"
#include "util/base64.h++"

namespace Ludwig {
  static constexpr auto JWT_HEADER_TEXT = Base64::FixedString {
    "{\"alg\":\"HS512\",\"typ\":\"JWT\"}"
  };
  constexpr auto JWT_HEADER = Base64::encode_const<JWT_HEADER_TEXT>();
  constexpr size_t JWT_SECRET_SIZE = 64, JWT_SIGNATURE_SIZE = 87;

  struct Jwt { uint64_t sub, iat, exp; };

  static_assert(JWT_HEADER == Base64::FixedString{"eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCJ9"});

  auto make_jwt(Jwt payload, const uint8_t secret[JWT_SECRET_SIZE]) -> std::string;

  auto make_jwt(uint64_t session_id, uint64_t duration_seconds, const uint8_t secret[JWT_SECRET_SIZE]) -> std::string;

  auto parse_jwt(std::string_view jwt, const uint8_t secret[JWT_SECRET_SIZE]) -> std::optional<Jwt>;
}
