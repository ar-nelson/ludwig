#include "jwt.h++"
#include <monocypher-ed25519.h>

using std::optional, std::string, std::string_view;

namespace Ludwig {
  auto make_jwt(const JwtPayload& payload, const uint8_t secret[JWT_SECRET_SIZE]) -> string {
    std::ostringstream payload_os;
    {
      cereal::JSONOutputArchive ar(payload_os);
      payload.save(ar);
    }
    auto buf = fmt::format("{}.{}", JWT_HEADER, Base64::encode(payload_os.str(), false));
    uint8_t sig[64];
    crypto_sha512_hmac(
      sig, secret, JWT_SECRET_SIZE,
      reinterpret_cast<const uint8_t*>(buf.data()), buf.length()
    );
    fmt::format_to(std::back_inserter(buf), ".{}", Base64::encode(sig, 64, false));
    return buf;
  }

  auto make_jwt(uint64_t user_id, uint64_t duration_seconds, const uint8_t secret[JWT_SECRET_SIZE]) -> string {
    const auto iat = now_s();
    return make_jwt({
      .sub = user_id,
      .iat = iat,
      .exp = iat + duration_seconds
    }, secret);
  }

  static inline auto parse_jwt_payload(const string_view& payload_b64) -> optional<JwtPayload> {
    JwtPayload payload;
    const auto payload_str = Base64::decode(payload_b64);
    try {
      std::istringstream payload_is(payload_str);
      cereal::JSONInputArchive archive(payload_is);
      payload.load(archive);
    } catch (...) {
      spdlog::warn("Cannot parse JWT payload {}", payload_str);
      return {};
    }
    return { payload };
  }

  auto parse_jwt(string_view jwt, const uint8_t secret[JWT_SECRET_SIZE]) -> optional<JwtPayload> {
    const auto len = jwt.length(), header_len = JWT_HEADER.size();
    // Avoid DOS from impossibly huge strings
    if (len > 2048) {
      spdlog::warn("JWT is too large (>2048 characters)");
      return {};
    }
    const auto jwt_sv = string_view(jwt);
    const auto dot_ix = jwt.find_last_of('.');
    if (
      // There must be room for a header and a signature
      len < header_len + 2 + JWT_SIGNATURE_SIZE ||
      // Check the header; it must be exact
      // This isn't conformant, but it's not like we'll use anyone else's JWTs
      jwt[header_len] != '.' ||
      jwt_sv.substr(0, header_len) != string_view(JWT_HEADER) ||
      // Find the '.' separating the payload and signature
      dot_ix == string::npos
    ) {
      spdlog::warn("JWT is invalid (bad format or header)");
      return {};
    }

    // Slice up the string
    const auto payload_b64 = jwt_sv.substr(header_len + 1, dot_ix - (header_len + 1)),
      sig_b64 = jwt_sv.substr(dot_ix + 1),
      to_sign = jwt_sv.substr(0, dot_ix);

    // Check the signature
    uint8_t expected_sig[64], actual_sig[64];
    if (Base64::decode(sig_b64, expected_sig, 64) != 64) return {};
    crypto_sha512_hmac(
      actual_sig, secret, JWT_SECRET_SIZE,
      reinterpret_cast<const uint8_t*>(to_sign.data()), to_sign.length()
    );
    if (crypto_verify64(expected_sig, actual_sig)) {
      if (spdlog::get_level() <= spdlog::level::warn) {
        const auto payload = parse_jwt_payload(payload_b64);
        if (payload) {
          spdlog::warn("JWT for user {:x} failed signature validation", payload->sub);
        }
      }
      return {};
    }

    // Extract the payload
    const auto payload = parse_jwt_payload(payload_b64);
    if (!payload) return payload;

    // Check the date
    const auto now = now_s();
    if (now >= payload->exp) {
      spdlog::debug(
        "JWT for user {:x} is expired ({} seconds past expiration)",
        payload->sub, now - payload->exp
      );
      return {};
    }

    return payload;
  }
}
