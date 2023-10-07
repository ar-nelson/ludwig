#include "jwt.h++"
#include "models/protocols_parser.h++"
#include "models/protocols.h++"
#include <flatbuffers/minireflect.h>
#include <regex>
#include <monocypher-ed25519.h>

using std::optional, std::string, std::string_view;

namespace Ludwig {
  auto make_jwt(Jwt payload, const uint8_t secret[JWT_SECRET_SIZE]) -> string {
    string json;
    {
      flatbuffers::FlatBufferBuilder fbb;
      JwtPayloadBuilder b(fbb);
      b.add_sub(payload.sub);
      b.add_iat(payload.iat);
      b.add_exp(payload.exp);
      fbb.Finish(b.Finish());
      flatbuffers::ToStringVisitor visitor("", true, "", false);
      flatbuffers::IterateFlatBuffer(fbb.GetBufferPointer(), JwtPayloadTypeTable(), &visitor);
      json = visitor.s;
      // remove spaces, since Flatbuffers inserts them for some reason
      json.erase(remove(json.begin(), json.end(), ' '), json.end());
    }
    auto buf = fmt::format("{}.{}", JWT_HEADER, Base64::encode(json, false));
    uint8_t sig[64];
    crypto_sha512_hmac(
      sig, secret, JWT_SECRET_SIZE,
      reinterpret_cast<const uint8_t*>(buf.data()), buf.length()
    );
    fmt::format_to(std::back_inserter(buf), ".{}", Base64::encode(sig, 64, false));
    return buf;
  }

  auto make_jwt(uint64_t session_id, uint64_t duration_seconds, const uint8_t secret[JWT_SECRET_SIZE]) -> string {
    const auto iat = now_s();
    return make_jwt({
      .sub = session_id,
      .iat = iat,
      .exp = iat + duration_seconds
    }, secret);
  }

  static inline auto parse_jwt_payload(const string_view& payload_b64) -> optional<Jwt> {
    auto& parser = get_protocols_parser();
    parser.SetRootType("JwtPayload");
    if (!parser.ParseJson(Base64::decode(payload_b64).c_str())) {
      spdlog::warn("Failed to parse JWT payload");
      return {};
    }
    auto* payload = flatbuffers::GetRoot<JwtPayload>(parser.builder_.GetBufferPointer());
    Jwt jwt{
      .sub = payload->sub(),
      .iat = payload->iat(),
      .exp = payload->exp()
    };
    parser.builder_.Clear();
    return jwt;
  }

  auto parse_jwt(string_view jwt, const uint8_t secret[JWT_SECRET_SIZE]) -> optional<Jwt> {
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
          spdlog::warn("JWT failed signature validation");
        }
      }
      return {};
    }

    // Extract the payload
    const auto payload = parse_jwt_payload(payload_b64);
    if (!payload) return {};

    // Check the date
    const auto now = now_s();
    if (now >= payload->exp) {
      spdlog::debug("JWT is expired ({} seconds past expiration)", now - payload->exp);
      return {};
    }

    return payload;
  }
}
