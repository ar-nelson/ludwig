#include "test_common.h++"
#include "util/jwt.h++"
#include "util/base64.h++"

static const string SECRET_BASE64 = "67GWYhThscMwBm3jItLAxy6vY4fg49K5eYLYAHexxpW0Z3FOOBz_MQ3MfXiJPXmmztAok4iC3jDGkpSbQyDL9Q",
  BYTES32_BASE64 = "2kVD14ALWWbYccEdphAtnGlZslzeBz2FIE9Z1LGqAyQ",
  SAMPLE_JWT = "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEyMzQsImlhdCI6MTUxNjIzOTAyMiwiZXhwIjoxNTE2MjQ5MDIyfQ.0roIyXlCzgkJl1kFgWguYzPA3ouRZF29jDdiLkffXBYi46MgLJJYxJ9X-kdo2btjpiXdeMccC1k38MZo4JhE6Q";

TEST_CASE("roundtrip base64", "[jwt]") {
  REQUIRE(SECRET_BASE64 == Base64::encode(Base64::decode(SECRET_BASE64), false));
  REQUIRE(BYTES32_BASE64 == Base64::encode(Base64::decode(BYTES32_BASE64), false));
}

TEST_CASE("make_jwt matches JWT generated by jwt.io", "[jwt]") {
  uint8_t secret[Ludwig::JWT_SECRET_SIZE];
  const auto len = Base64::decode(SECRET_BASE64, secret, Ludwig::JWT_SECRET_SIZE);
  REQUIRE(len == Ludwig::JWT_SECRET_SIZE);
  const auto encoded = Ludwig::make_jwt({
    .sub = 1234,
    .iat = 1516239022,
    .exp = 1516249022
  }, secret);
  REQUIRE(encoded == SAMPLE_JWT);
}

TEST_CASE("parse_jwt doesn't accept expired JWT generated by jwt.io", "[jwt]") {
  uint8_t secret[Ludwig::JWT_SECRET_SIZE];
  const auto len = Base64::decode(SECRET_BASE64, secret, Ludwig::JWT_SECRET_SIZE);
  REQUIRE(len == Ludwig::JWT_SECRET_SIZE);
  const auto decoded = Ludwig::parse_jwt(SAMPLE_JWT, secret);
  REQUIRE(!decoded);
}

TEST_CASE("make_jwt -> parse_jwt roundtrip", "[jwt]") {
  const uint64_t user = 1234;
  uint8_t secret[Ludwig::JWT_SECRET_SIZE];
  Base64::decode(SECRET_BASE64, secret, Ludwig::JWT_SECRET_SIZE);
  const auto encoded = Ludwig::make_jwt(user, now_t() + 60s, secret);
  REQUIRE(encoded.starts_with(Ludwig::JWT_HEADER));
  const auto decoded = Ludwig::parse_jwt(encoded, secret);
  REQUIRE(!!decoded);
  REQUIRE(decoded->sub == user);
}

TEST_CASE("make_jwt -> parse_jwt roundtrip fails with wrong secret", "[jwt]") {
  const uint64_t user = 1234;
  uint8_t secret[Ludwig::JWT_SECRET_SIZE];
  Base64::decode(SECRET_BASE64, secret, Ludwig::JWT_SECRET_SIZE);
  const auto encoded = Ludwig::make_jwt(user, now_t() + 60s, secret);
  secret[0]++;
  const auto decoded = Ludwig::parse_jwt(encoded, secret);
  REQUIRE(!decoded);
}
