// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/payload_checksum.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

void test_xxh64_reference_values()
{
    // Reference values of the xxHash specification (XXH64, seed 0).
    assert(fsim::support::Xxh64::digest("") == UINT64_C(0xef46db3751d8e999));
    assert(fsim::support::Xxh64::digest("abc") == UINT64_C(0x44bc2cf5ad770999));
}

void test_xxh64_streaming_matches_one_shot()
{
    std::string bytes;
    for (std::size_t index = 0U; index < 1000U; ++index) {
        bytes.push_back(static_cast<char>((index * 131U + 7U) & 0xffU));
    }
    for (const std::size_t piece : { 1U, 3U, 31U, 32U, 33U, 100U }) {
        fsim::support::Xxh64 streamed;
        for (std::size_t at = 0U; at < bytes.size(); at += piece) {
            streamed.update(std::string_view { bytes }.substr(at, piece));
        }
        assert(streamed.finish() == fsim::support::Xxh64::digest(bytes));
    }
}

void test_payload_checksum_spellings()
{
    const auto checksum = fsim::support::payload_checksum("abc");
    assert(checksum == "xxh64:44bc2cf5ad770999");
    assert(fsim::support::payload_checksum_spelling(checksum));
    assert(fsim::support::payload_checksum_matches("abc", checksum));
    assert(!fsim::support::payload_checksum_matches("abd", checksum));
    // SHA-256 hex stays a valid recorded checksum.
    const std::string sha256
        = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    assert(fsim::support::payload_checksum_spelling(sha256));
    assert(fsim::support::payload_checksum_matches("abc", sha256));
    assert(!fsim::support::payload_checksum_matches("abd", sha256));
    assert(!fsim::support::payload_checksum_spelling("xxh64:44BC2CF5AD770999"));
    assert(!fsim::support::payload_checksum_spelling("xxh64:44bc"));
    assert(!fsim::support::payload_checksum_spelling(sha256.substr(1U)));
}

} // namespace

int main()
{
    test_xxh64_reference_values();
    test_xxh64_streaming_matches_one_shot();
    test_payload_checksum_spellings();
}
