#include "jwt.h++"
#include <regex>
#include <openssl/crypto.h>
#include <openssl/hmac.h>

using std::optional, std::runtime_error, std::string, std::string_view;

namespace Ludwig {
  auto make_jwt(JwtPayload payload, JwtSecret secret) -> string {
    string json;
    JsonSerialize<JwtPayload>::to_json(payload, json);
    auto buf = fmt::format("{}.{}", JWT_HEADER, Base64::encode(json, false));
    uint8_t sig[64];
    unsigned sig_len;
    if (!HMAC(
      EVP_sha512(), secret.data(), JWT_SECRET_SIZE,
      reinterpret_cast<const uint8_t*>(buf.data()), buf.length(),
      sig, &sig_len
    )) {
      throw runtime_error("JWT HMAC failed");
    }
    assert(sig_len == 64);
    fmt::format_to(std::back_inserter(buf), ".{}", Base64::encode(sig, 64, false));
    return buf;
  }

  auto make_jwt(uint64_t session_id, Timestamp expiration, JwtSecret secret) -> string {
    const auto iat = now_s();
    return make_jwt({
      .sub = session_id,
      .iat = iat,
      .exp = timestamp_to_uint(expiration)
    }, secret);
  }

  auto parse_jwt(string_view jwt, JwtSecret secret) -> optional<JwtPayload> {
    static thread_local simdjson::ondemand::parser parser;

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
    string payload_str = Base64::decode(payload_b64);
    pad_json_string(payload_str);

    // Check the signature
    uint8_t expected_sig[64], actual_sig[64];
    if (Base64::decode(sig_b64, expected_sig, 64) != 64) return {};
    unsigned sig_len;
    if (!HMAC(
      EVP_sha512(), secret.data(), JWT_SECRET_SIZE,
      reinterpret_cast<const uint8_t*>(to_sign.data()), to_sign.length(),
      actual_sig, &sig_len
    )) {
      spdlog::warn("JWT HMAC failed");
      return {};
    }
    assert(sig_len == 64);
    if (CRYPTO_memcmp(expected_sig, actual_sig, 64)) {
      if (spdlog::get_level() <= spdlog::level::warn) {
        try {
          JsonSerialize<JwtPayload>::from_json(parser.iterate(payload_str));
          spdlog::warn("JWT failed signature validation");
        } catch (const simdjson::simdjson_error& e) {
          spdlog::warn("JWT payload is invalid - {}", e.what());
        }
      }
      return {};
    }

    // Extract the payload
    try {
      const auto payload = JsonSerialize<JwtPayload>::from_json(parser.iterate(payload_str));

      // Check the date
      const auto now = now_s();
      if (now >= payload.exp) {
        spdlog::debug("JWT is expired ({} seconds past expiration)", now - payload.exp);
        return {};
      }

      return payload;
    } catch (const simdjson::simdjson_error& e) {
      spdlog::warn("JWT payload is invalid - {}", e.what());
      return {};
    }
  }
}
