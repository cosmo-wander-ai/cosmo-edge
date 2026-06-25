#include "catch_amalgamated.hpp"
/*
 * test_cipher_util.cc — CipherUtil unit tests
 *
 * Tests MD5, Base64, SHA1 encode/decode and round-trip correctness.
 * AES tests are kept basic to avoid key management complexity.
 */
#include "util/CipherUtil.h"

using namespace cosmo::util;

TEST_CASE("CipherUtil: EncMd5", "[cipher]") {
    SECTION("Known MD5 hash") {
        // MD5("") = D41D8CD98F00B204E9800998ECF8427E (upper)
        auto hash = EncMd5("", true);
        REQUIRE(hash == "D41D8CD98F00B204E9800998ECF8427E");
    }

    SECTION("MD5 lowercase") {
        auto hash = EncMd5("", false);
        REQUIRE(hash == "d41d8cd98f00b204e9800998ecf8427e");
    }

    SECTION("Non-empty string") {
        auto hash = EncMd5("hello");
        REQUIRE(hash.size() == 32);
        // MD5("hello") = 5D41402ABC4B2A76B9719D911017C592
        REQUIRE(hash == "5D41402ABC4B2A76B9719D911017C592");
    }
}

TEST_CASE("CipherUtil: Base64 encode/decode round-trip", "[cipher]") {
    SECTION("Normal string") {
        std::string original = "Hello, World!";
        auto encoded         = EncBase64(original);
        REQUIRE(!encoded.empty());
        auto decoded = DecBase64(encoded);
        REQUIRE(decoded == original);
    }

    SECTION("Empty string") {
        auto encoded = EncBase64("");
        auto decoded = DecBase64(encoded);
        REQUIRE(decoded == "");
    }

    SECTION("Binary data via EncBase64Ex") {
        uint8_t data[] = {0x00, 0x01, 0x02, 0xFF};
        auto encoded   = EncBase64Ex(data, sizeof(data));
        REQUIRE(!encoded.empty());
        auto decoded = DecBase64Vec(encoded);
        REQUIRE(decoded.size() == 4);
        REQUIRE(decoded[0] == 0x00);
        REQUIRE(decoded[3] == 0xFF);
    }
}

TEST_CASE("CipherUtil: DecBase64Vec", "[cipher]") {
    SECTION("Known base64 value") {
        // "AQID" = base64 of {1, 2, 3}
        auto vec = DecBase64Vec("AQID");
        REQUIRE(vec.size() == 3);
        REQUIRE(vec[0] == 1);
        REQUIRE(vec[1] == 2);
        REQUIRE(vec[2] == 3);
    }
}

TEST_CASE("CipherUtil: Sha1", "[cipher]") {
    SECTION("Known SHA1 hash") {
        auto hash = Sha1("hello");
        REQUIRE(hash.size() == 40);
    }

    SECTION("Empty string SHA1") {
        auto hash = Sha1("");
        REQUIRE(hash.size() == 40);
    }
}

TEST_CASE("CipherUtil: GenerateSignature", "[cipher]") {
    auto sig = GenerateSignature("key", "1234567890", "noise", "secret");
    REQUIRE(!sig.empty());

    SECTION("Same inputs produce same signature") {
        auto sig2 = GenerateSignature("key", "1234567890", "noise", "secret");
        REQUIRE(sig == sig2);
    }

    SECTION("Different inputs produce different signature") {
        auto sig3 = GenerateSignature("key", "9999999999", "noise", "secret");
        REQUIRE(sig != sig3);
    }
}

TEST_CASE("CipherUtil: AES-GCM round-trip", "[cipher]") {
    // 16-byte key and 12-byte IV for AES-128-GCM
    std::string key       = "0123456789abcdef";
    std::string iv        = "012345678901";
    std::string plaintext = "test data for encryption";

    auto encrypted = EncAesGcmNoPadding(plaintext, key, iv);
    REQUIRE(!encrypted.empty());
    REQUIRE(encrypted != plaintext);

    auto decrypted = DecAesGcmNoPadding(encrypted, key, iv);
    REQUIRE(decrypted == plaintext);
}
