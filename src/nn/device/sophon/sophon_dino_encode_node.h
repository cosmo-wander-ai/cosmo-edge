#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nn/node/node.h"

namespace cosmo::nn {

// Simple tokenizer implementation for sophon
class SophonTokenizer {
public:
    bool LoadVocab(const std::string& vocab_path);
    void EncodeText(const std::string& text, std::vector<int64_t>& ids);
    std::map<int64_t, std::string> idx2token;
    std::map<std::string, int64_t> token2idx;

private:
    std::vector<std::string> StringSplit(const std::string& str, char delim);
    void Tokenize(const std::string& token, std::vector<int64_t>& idx);
};

class SophonDinoEncodeNode : public Node {
public:
    SophonDinoEncodeNode();

    ~SophonDinoEncodeNode();

    virtual void LoadParam(Op* op) override;

    virtual size_t GetTopCount() override;
    virtual size_t GetBottomCount() override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bases,
                           std::vector<std::shared_ptr<Blob>>& prompts,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    void CreateTokenizer(std::string path);

    std::vector<int64_t> Encode(const std::string& text);

    std::string GetPromptStr(std::shared_ptr<Blob>& prompt);

    std::string Strip(const std::string& str);

    bool Endswith(const std::string& s, const std::string& suffix);

private:
    const int length = 256;

    std::unique_ptr<SophonTokenizer> tokenizer;

    std::vector<std::string> special_tokens = {"[CLS]", "[SEP]", ".", "?"};
    std::vector<int64_t> special_token_ids{};

    int dst_width;
    int dst_height;
    std::vector<float> mean{};
    std::vector<float> scale{};
    bool is_bgr;
};

}  // namespace cosmo::nn
