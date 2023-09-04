#pragma once
#include <chrono>
#include <optional>
#include <cereal/archives/json.hpp>
#include "base64.h++"

namespace Ludwig {
  static constexpr auto JWT_HEADER_TEXT = Base64::FixedString {
    "{\"alg\":\"HS512\",\"typ\":\"JWT\"}"
  };
  constexpr auto JWT_HEADER = Base64::encode_const<JWT_HEADER_TEXT>();
  constexpr size_t JWT_SECRET_SIZE = 64, JWT_SIGNATURE_SIZE = 87;

  static_assert(JWT_HEADER == Base64::FixedString{"eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCJ9"});

  struct JwtPayload {
    uint64_t sub, iat, exp;

    template <typename Archive> void load(Archive& ar) {
      ar(CEREAL_NVP(sub), CEREAL_NVP(iat), CEREAL_NVP(exp));
    }

    template <typename Archive> void save(Archive& ar) const {
      ar(CEREAL_NVP(sub), CEREAL_NVP(iat), CEREAL_NVP(exp));
    }
  };

  auto make_jwt(const JwtPayload& payload, const uint8_t secret[JWT_SECRET_SIZE]) -> std::string;

  auto make_jwt(uint64_t user_id, uint64_t duration_seconds, const uint8_t secret[JWT_SECRET_SIZE]) -> std::string;

  auto parse_jwt(std::string jwt, const uint8_t secret[JWT_SECRET_SIZE]) -> std::optional<JwtPayload>;

  static inline auto now_s() -> uint64_t {
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count()
    );
  }
}
