#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::util {

// Append 16-byte MAC after encrypted content
std::string EncAesGcmNoPadding(const std::string& enc_str, const std::string& key, const std::string& iv);

// The last 16 bytes of input are MAC
std::string DecAesGcmNoPadding(const std::string& dec_str, const std::string& key, const std::string& iv);

std::string EncMd5(const std::string& enc_str, bool is_upper = true);

std::string EncFileMd5(const std::string& file_path, bool is_upper = true);

std::string EncBase64(const std::string& enc_str);

std::string EncBase64Ex(const uint8_t* data, size_t size);

std::string DecBase64(const std::string& dec_str);

std::vector<uint8_t> DecBase64Vec(const std::string& dec_str);

std::string Sha1(const std::string& input);

std::string GenerateSignature(const std::string& access_key, const std::string& timestamp,
                              const std::string& noise, const std::string& secret_key);

std::string DecAesEcbPadding(const std::string& dec_str, const std::string& key, const std::string& iv);

}  // namespace cosmo::util
