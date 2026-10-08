#include "nn/utils/laya_frontend.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <regex>
#include <stdexcept>

#include "Eigen/Dense"

namespace cosmo::nn::laya {
namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error("Laya: " + message);
    }
    std::string Text(const nlohmann::ordered_json& value, bool empty = false) {
        Require(value.is_string(), "question text must be a string");
        auto text = value.get<std::string>();
        Require(text.size() <= 16384 && (empty || text.find_first_not_of(" \t\r\n") != std::string::npos),
                "invalid question text length");
        Require(text.find("<mask>") == std::string::npos, "reserved marker in question");
        return text;
    }
    struct NpyHeader {
        std::vector<size_t> shape;
        size_t offset = 0, count = 1, bytes = 0;
        std::string dtype;
    };
    NpyHeader ReadHeader(std::ifstream& stream) {
        std::array<unsigned char, 8> magic{};
        stream.read(reinterpret_cast<char*>(magic.data()), magic.size());
        Require(stream.good() && std::memcmp(magic.data(), "\x93NUMPY", 6) == 0 &&
                    (magic[6] == 1 || magic[6] == 2) && magic[7] == 0,
                "invalid npy header");
        std::array<unsigned char, 4> size{};
        const size_t size_bytes = magic[6] == 1 ? 2 : 4;
        stream.read(reinterpret_cast<char*>(size.data()), size_bytes);
        size_t length = 0;
        for (size_t i = 0; i < size_bytes; ++i)
            length |= size_t(size[i]) << (8 * i);
        Require(length > 0 && length <= 65536, "invalid npy metadata size");
        std::string header(length, '\0');
        stream.read(header.data(), length);
        Require(stream.good(), "truncated npy header");
        NpyHeader result;
        std::smatch match;
        Require(std::regex_search(header, match, std::regex("'descr'\\s*:\\s*'([^']+)'")),
                "missing npy dtype");
        result.dtype = match[1];
        Require(result.dtype == "<f4" || result.dtype == "<f2", "expected little endian float npy");
        Require(std::regex_search(header, std::regex("'fortran_order'\\s*:\\s*False")),
                "expected C-order npy");
        Require(std::regex_search(header, match, std::regex("'shape'\\s*:\\s*\\(([^)]*)\\)")),
                "missing npy shape");
        const auto dimensions = match[1].str();
        const std::regex number("[0-9]+");
        for (auto it = std::sregex_iterator(dimensions.begin(), dimensions.end(), number);
             it != std::sregex_iterator(); ++it) {
            size_t value = std::stoull(it->str());
            Require(value > 0 && result.count <= size_t(256000) * 768 / value, "invalid npy dimensions");
            result.shape.push_back(value);
            result.count *= value;
        }
        result.offset = size_t(stream.tellg());
        result.bytes  = result.count * (result.dtype == "<f2" ? 2 : 4);
        stream.seekg(0, std::ios::end);
        Require(stream.tellg() >= 0 && size_t(stream.tellg()) == result.offset + result.bytes,
                "npy payload size mismatch");
        stream.seekg(result.offset);
        return result;
    }
    float Half(uint16_t value) {
        const unsigned exponent = (value >> 10) & 31;
        const unsigned fraction = value & 1023;
        const float sign        = value & 0x8000 ? -1.f : 1.f;
        if (exponent == 0)
            return sign * std::ldexp(float(fraction), -24);
        if (exponent == 31)
            return fraction ? std::numeric_limits<float>::quiet_NaN()
                            : sign * std::numeric_limits<float>::infinity();
        return sign * std::ldexp(float(1024 + fraction), int(exponent) - 25);
    }
    std::vector<float> Linear(const std::vector<float>& input, const Heads& heads, const std::string& name) {
        const auto& weight = heads.at(name + ".weight");
        Require(weight.shape.size() == 2 && weight.shape[1] == input.size(), "linear shape: " + name);
        using Matrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
        Eigen::Map<const Matrix> w(weight.data.data(), weight.shape[0], weight.shape[1]);
        Eigen::Map<const Eigen::VectorXf> x(input.data(), input.size());
        std::vector<float> result(weight.shape[0]);
        Eigen::Map<Eigen::VectorXf> y(result.data(), result.size());
        y.noalias() = w * x;
        auto bias   = heads.find(name + ".bias");
        if (bias != heads.end()) {
            Require(bias->second.data.size() == result.size(), "bias shape: " + name);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] += bias->second.data[i];
        }
        return result;
    }
    void Gelu(std::vector<float>& values) {
        for (auto& x : values)
            x = 0.5f * x * (1.f + std::erf(x / std::sqrt(2.f)));
    }
    std::vector<float> Softmax(const std::vector<float>& logits, float temperature) {
        std::vector<float> result(logits.size());
        float maximum = *std::max_element(logits.begin(), logits.end());
        float sum     = 0;
        for (size_t i = 0; i < logits.size(); ++i)
            sum += result[i] = std::exp((logits[i] - maximum) / temperature);
        Require(std::isfinite(sum) && sum > 0, "nonfinite scores");
        for (auto& value : result)
            value /= sum;
        return result;
    }
}  // namespace

std::string TokenizerJson(nlohmann::json config) {
    config["post_processor"] = nullptr;
    config["padding"]        = nullptr;
    config["truncation"]     = nullptr;
    auto& pre                = config.at("pre_tokenizer");
    Require(pre.at("type") == "Metaspace" && pre.value("split", true), "unsupported Laya pre-tokenizer");
    if (pre.contains("prepend_scheme")) {
        const auto scheme = pre.at("prepend_scheme").get<std::string>();
        Require(scheme == "always" || scheme == "never", "unsupported metaspace prepend scheme");
        pre["add_prefix_space"] = scheme == "always";
        pre.erase("prepend_scheme");
        pre.erase("split");
    }
    auto& model = config.at("model");
    Require(model.at("type") == "BPE" && !model.value("ignore_merges", false), "unsupported Laya BPE policy");
    model.erase("ignore_merges");
    for (auto& merge : model.at("merges")) {
        if (merge.is_array()) {
            Require(merge.size() == 2 && merge[0].is_string() && merge[1].is_string(),
                    "invalid BPE merge pair");
            const auto a = merge[0].get<std::string>(), b = merge[1].get<std::string>();
            Require(a.find(' ') == std::string::npos && b.find(' ') == std::string::npos,
                    "ambiguous BPE merge serialization");
            merge = a + " " + b;
        }
    }
    return config.dump();
}

Question CompileQuestion(const nlohmann::ordered_json& question, const nlohmann::json& config,
                         const Encoder& encode, const std::string& text_state) {
    Require(question.is_object(), "question must be an object");
    const std::string type = question.at("type").get<std::string>();
    Require(type == "choice" || type == "noul", "only choice and noul questions are supported");
    Question result;
    result.type = type == "choice" ? 0 : 2;
    std::vector<std::string> options;
    auto add = [&](const std::string& label, const nlohmann::ordered_json& description) {
        Require(std::find(result.labels.begin(), result.labels.end(), label) == result.labels.end(),
                "duplicate option");
        result.labels.push_back(label);
        const auto text = description.is_null() ? std::string{} : Text(description, true);
        options.push_back(text.empty() ? label : label + ": " + text);
    };
    if (type == "choice") {
        const auto& criteria = question.at("criteria");
        Require(criteria.is_object() || criteria.is_array(), "invalid choice criteria");
        if (criteria.is_array()) {
            for (const auto& option : criteria)
                add(Text(option.at("label")), option.at("description"));
        } else {
            for (auto it = criteria.begin(); it != criteria.end(); ++it)
                add(Text(it.key()), it.value());
        }
    } else {
        const auto criteria = question.value("criteria", nlohmann::ordered_json::object());
        const auto labels =
            question.value("labels", nlohmann::ordered_json{{"false", "false"}, {"true", "true"}});
        const std::array<std::string, 2> defaults = {"no, the statement does not hold",
                                                     "yes, the statement holds"};
        for (size_t i = 0; i < 2; ++i) {
            const std::string label = i == 0 ? "false" : "true";
            const auto description  = criteria.is_null()
                                          ? nlohmann::ordered_json(nullptr)
                                          : criteria.value(label, nlohmann::ordered_json(nullptr));
            const auto text         = description.is_null() ? std::string{} : Text(description, true);
            result.labels.push_back(label);
            options.push_back(Text(labels.at(label)) + ": " + (text.empty() ? defaults[i] : text));
        }
    }
    Require(options.size() >= 2 && options.size() <= 16, "expected 2..16 options");
    result.tokens.push_back(2);
    const auto head = encode(type + " question: " + Text(question.at("instructions")));
    result.tokens.insert(result.tokens.end(), head.begin(), head.end());
    result.tokens.push_back(1);
    size_t head_length = head.size();
    for (const auto& option : options) {
        auto tokens = encode(" " + option);
        Require(tokens.size() <= 48, "option token budget exceeded");
        head_length += tokens.size() + 1;
        result.markers.push_back(int(result.tokens.size()));
        result.tokens.push_back(4);
        result.tokens.insert(result.tokens.end(), tokens.begin(), tokens.end());
    }
    Require(head_length <= config.value("head_max_len", 256), "question head budget exceeded");
    result.tokens.push_back(1);
    result.image_position = result.tokens.size();
    result.tokens.insert(result.tokens.end(), kImageSlots, 0);
    auto state = encode(Text(text_state, true));
    result.tokens.insert(result.tokens.end(), state.begin(), state.end());
    result.tokens.push_back(1);
    Require(result.tokens.size() <= kSequence, "sequence budget exceeded");
    for (auto token : result.tokens)
        Require(token >= 0 && token < kVocabulary, "token outside vocabulary");
    auto count              = options.size();
    const std::string size  = count <= 2 ? "2" : count <= 5 ? "3-5" : count <= 10 ? "6-10" : "11+";
    const auto temperatures = config.value("temperature_by_options", nlohmann::json::object());
    const auto bucket       = "image|" + type + ":" + size;
    std::string source;
    if (temperatures.contains(bucket)) {
        result.temperature = temperatures.at(bucket).get<float>();
        source             = bucket;
    } else if (config.contains("temperature_image") && !config.at("temperature_image").empty()) {
        result.temperature = config.at("temperature_image").at(result.type).get<float>();
        source             = "temperature_image";
    } else if (temperatures.contains(type + ":" + size)) {
        result.temperature = temperatures.at(type + ":" + size).get<float>();
        source             = "text_fallback";
    } else {
        result.temperature = config.at("temperature").at(result.type).get<float>();
        source             = "text_fallback";
    }
    result.temperature = std::isfinite(result.temperature) ? std::clamp(result.temperature, 0.5f, 5.f) : 1.f;
    result.temperature_info = {{"bucket", bucket}, {"source", source}, {"value", result.temperature}};
    return result;
}

void AttentionMasks(size_t valid_length, std::vector<float>& full, std::vector<float>& sliding) {
    Require(valid_length > 0 && valid_length <= kSequence, "invalid sequence length");
    full.resize(kSequence * kSequence);
    sliding.resize(full.size());
    for (int row = 0; row < kSequence; ++row) {
        const bool padded_empty = row - 64 >= int(valid_length);
        for (int column = 0; column < kSequence; ++column) {
            auto index     = size_t(row * kSequence + column);
            bool key       = column < int(valid_length);
            full[index]    = key ? 0.f : -10000.f;
            sliding[index] = key && (padded_empty || std::abs(row - column) <= 64) ? 0.f : -10000.f;
        }
    }
}

std::vector<float> ImagePatches(const uint8_t* bgr, int width, int height, size_t stride) {
    Require(bgr && width > 0 && height > 0 && stride >= size_t(width) * 3, "invalid BGR image");
    const int side = std::max(width, height);
    Require(side >= 3 && size_t(side) * side <= 64000000, "invalid square image size");
    const int left = (side - width) / 2, top = (side - height) / 2;
    struct Coefficients {
        int first;
        std::vector<int32_t> weights;
    };
    std::array<Coefficients, 256> coefficients;
    const double scale = double(side) / 256, support = std::max(1., scale);
    for (int out = 0; out < 256; ++out) {
        double center           = (out + 0.5) * scale;
        int first               = std::max(0, int(center - support + 0.5));
        int end                 = std::min(side, int(center + support + 0.5));
        coefficients[out].first = first;
        std::vector<double> weights;
        double sum = 0;
        for (int i = first; i < end; ++i) {
            double weight = std::max(0., 1. - std::abs((i - center + 0.5) / support));
            weights.push_back(weight);
            sum += weight;
        }
        for (auto weight : weights)
            coefficients[out].weights.push_back(int32_t(weight / sum * (1 << 22) + 0.5));
    }
    std::vector<uint8_t> horizontal(size_t(side) * 256 * 3);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < 256; ++x)
            for (int channel = 0; channel < 3; ++channel) {
                int64_t sum        = 1 << 21;
                const auto& filter = coefficients[x];
                for (size_t k = 0; k < filter.weights.size(); ++k) {
                    int sx = filter.first + int(k) - left, sy = y - top;
                    int value = sx >= 0 && sx < width && sy >= 0 && sy < height
                                    ? bgr[size_t(sy) * stride + sx * 3 + (2 - channel)]
                                    : 255;
                    sum += int64_t(value) * filter.weights[k];
                }
                horizontal[(size_t(y) * 256 + x) * 3 + channel] =
                    uint8_t(std::clamp<int64_t>(sum >> 22, 0, 255));
            }
    std::vector<float> patches(256 * 768);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            for (int channel = 0; channel < 3; ++channel) {
                int64_t sum        = 1 << 21;
                const auto& filter = coefficients[y];
                for (size_t k = 0; k < filter.weights.size(); ++k)
                    sum += int64_t(horizontal[((size_t(filter.first) + k) * 256 + x) * 3 + channel]) *
                           filter.weights[k];
                float pixel  = float(std::clamp<int64_t>(sum >> 22, 0, 255));
                size_t index = size_t((y / 16) * 16 + x / 16) * 768 + ((y % 16) * 16 + x % 16) * 3 + channel;
                patches[index] = (pixel / 255.f - 0.5f) / 0.5f;
            }
    return patches;
}

Tensor ReadFloatNpy(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    auto header = ReadHeader(input);
    Require(header.bytes <= 64 * 1024 * 1024, "small tensor exceeds 64 MiB");
    Tensor result{header.shape, std::vector<float>(header.count)};
    if (header.dtype == "<f4")
        input.read(reinterpret_cast<char*>(result.data.data()), header.bytes);
    else {
        std::vector<uint16_t> halves(header.count);
        input.read(reinterpret_cast<char*>(halves.data()), header.bytes);
        std::transform(halves.begin(), halves.end(), result.data.begin(), Half);
    }
    Require(input.good(), "failed to read npy payload");
    for (auto value : result.data)
        Require(std::isfinite(value), "nonfinite CPU weight");
    return result;
}
EmbeddingTable::EmbeddingTable(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    auto header = ReadHeader(input);
    Require(header.dtype == "<f2" && header.shape == std::vector<size_t>{kVocabulary, kHidden},
            "embedding table shape");
    int fd = open(path.c_str(), O_RDONLY);
    Require(fd >= 0, "cannot open embedding table");
    mapped_bytes_ = header.offset + header.bytes;
    mapping_      = mmap(nullptr, mapped_bytes_, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        throw std::runtime_error("Laya: embedding mmap failed");
    }
    values_ = reinterpret_cast<const uint16_t*>(static_cast<const char*>(mapping_) + header.offset);
}
EmbeddingTable::~EmbeddingTable() {
    if (mapping_)
        munmap(mapping_, mapped_bytes_);
}
std::vector<float> EmbeddingTable::Splice(const Question& question, const std::vector<float>& visual) const {
    Require(visual.size() == kImageSlots * kHidden && question.tokens.size() <= kSequence &&
                question.image_position + kImageSlots < question.tokens.size(),
            "embedding splice shape");
    std::vector<float> result(kSequence * kHidden);
    for (size_t row = 0; row < kSequence; ++row) {
        int token = row < question.tokens.size() ? question.tokens[row] : 0;
        Require(token >= 0 && token < kVocabulary, "embedding token index");
        for (int column = 0; column < kHidden; ++column)
            result[row * kHidden + column] = Half(values_[size_t(token) * kHidden + column]);
    }
    std::copy(visual.begin(), visual.end(), result.begin() + question.image_position * kHidden);
    return result;
}
Heads ReadHeads(const std::string& directory) {
    Heads result;
    for (auto name : {"type_emb.weight", "scorer_norm_eps", "scorer.0.weight", "scorer.0.bias",
                      "scorer.1.weight", "scorer.1.bias", "scorer.3.weight", "scorer.3.bias",
                      "act_head.0.weight", "act_head.0.bias", "act_head.2.weight", "act_head.2.bias"})
        result.emplace(
            name, ReadFloatNpy((std::filesystem::path(directory) / (std::string(name) + ".npy")).string()));
    return result;
}
Scores Score(const std::vector<float>& hidden, const Question& question, const Heads& heads) {
    Require(hidden.size() == kSequence * kHidden && question.markers.size() >= 2, "scoring input shape");
    for (float value : hidden)
        Require(std::isfinite(value), "nonfinite hidden state");
    const auto& weights = heads.at("scorer.0.weight").data;
    const auto& bias    = heads.at("scorer.0.bias").data;
    Require(weights.size() == kHidden && bias.size() == kHidden, "scorer normalization shape");
    float eps = heads.at("scorer_norm_eps").data.at(0);
    Scores result;
    for (int marker : question.markers) {
        Require(marker >= 0 && marker < int(question.tokens.size()), "scorer marker outside sequence");
        const float* row = hidden.data() + marker * kHidden;
        float mean       = std::accumulate(row, row + kHidden, 0.f) / kHidden;
        float variance   = 0;
        for (int i = 0; i < kHidden; ++i)
            variance += (row[i] - mean) * (row[i] - mean);
        float inverse = 1.f / std::sqrt(variance / kHidden + eps);
        std::vector<float> normalized(kHidden);
        for (int i = 0; i < kHidden; ++i)
            normalized[i] = (row[i] - mean) * inverse * weights[i] + bias[i];
        auto middle = Linear(normalized, heads, "scorer.1");
        Gelu(middle);
        auto score = Linear(middle, heads, "scorer.3");
        Require(score.size() == 1, "scorer output shape");
        result.logits.push_back(score[0]);
    }
    auto raw    = Softmax(result.logits, 1.f);
    auto sorted = raw;
    std::sort(sorted.rbegin(), sorted.rend());
    float entropy = 0;
    for (auto value : raw)
        entropy -= value * std::log(std::max(value, 1e-9f));
    entropy /= std::log(float(raw.size()));
    std::vector<float> pooled(hidden.begin(), hidden.begin() + kHidden);
    pooled.insert(pooled.end(), {sorted[0], sorted[0] - sorted[1], entropy, float(raw.size()) / 255.f});
    auto middle = Linear(pooled, heads, "act_head.0");
    Gelu(middle);
    result.action_logits = Linear(middle, heads, "act_head.2");
    result.probabilities = Softmax(result.logits, question.temperature);
    result.top           = size_t(std::max_element(result.probabilities.begin(), result.probabilities.end()) -
                                  result.probabilities.begin());
    sorted               = result.probabilities;
    std::sort(sorted.rbegin(), sorted.rend());
    result.margin = sorted[0] - sorted[1];
    return result;
}
}  // namespace cosmo::nn::laya
