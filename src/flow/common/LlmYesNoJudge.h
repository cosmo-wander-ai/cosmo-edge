// LLM "Yes/No" judgment: shared with Qwen3VLWorker / TaskAlarm alarm audit, avoiding single quote comparison
// of UTF-8 multi-byte characters with char.

#pragma once

#include <cctype>
#include <string>

namespace cosmo {

enum class LlmJudgeYesNo { Unknown, Yes, No };

inline void StripTrailingJudgePunct(std::string& s) {
    static const std::string kSuffix[] = {
        "\xe3\x80\x82",  // U+3002 。
        "\xef\xbc\x81",  // U+FF01 ！
        "\xef\xbc\x9f",  // U+FF1F ？
        ".",
        "!",
    };
    for (;;) {
        if (s.empty()) {
            break;
        }
        bool cut = false;
        for (const auto& suf : kSuffix) {
            if (s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0) {
                s.resize(s.size() - suf.size());
                cut = true;
                break;
            }
        }
        if (!cut) {
            break;
        }
    }
}

inline std::string NormalizeJudgeLine(const std::string& line) {
    std::string s;
    s.reserve(line.size());
    for (char ch : line) {
        if (!std::isspace(static_cast<unsigned char>(ch)) && ch != '`')
            s.push_back(ch);
    }
    StripTrailingJudgePunct(s);
    return s;
}

/// Single line Chinese: Yes -> true, No -> false; Returns false when unrecognized
inline bool ParseChineseYesNoTrue(const std::string& line) {
    std::string s = NormalizeJudgeLine(line);

    if (s.empty()) {
        return false;
    }
    if (s == "是" || s == "是的") {
        return true;
    }
    return false;
}

/// Strip markdown ``` and get the first line to parse "Yes/No"
inline LlmJudgeYesNo ParseJudgeYesNo(const std::string& text) {
    std::string toParse = text;
    size_t fenceStart   = text.find("```");
    if (fenceStart != std::string::npos) {
        size_t lineEnd      = text.find('\n', fenceStart);
        size_t contentStart = (lineEnd != std::string::npos) ? lineEnd + 1 : fenceStart + 3;
        size_t fenceEnd     = text.rfind("```");
        if (fenceEnd != std::string::npos && fenceEnd > contentStart) {
            toParse = text.substr(contentStart, fenceEnd - contentStart);
            while (!toParse.empty() && (toParse.front() == ' ' || toParse.front() == '\t' ||
                                        toParse.front() == '\r' || toParse.front() == '\n'))
                toParse.erase(0, 1);
            while (!toParse.empty() && (toParse.back() == ' ' || toParse.back() == '\t' ||
                                        toParse.back() == '\r' || toParse.back() == '\n'))
                toParse.pop_back();
        }
    }

    std::string firstLine = toParse;
    size_t nl             = firstLine.find('\n');
    if (nl != std::string::npos) {
        firstLine = firstLine.substr(0, nl);
    }

    std::string normalized = NormalizeJudgeLine(firstLine);
    if (normalized == "是" || normalized == "是的") {
        return LlmJudgeYesNo::Yes;
    }
    if (normalized == "否" || normalized == "不是" || normalized == "不存在" || normalized == "没有") {
        return LlmJudgeYesNo::No;
    }
    return LlmJudgeYesNo::Unknown;
}

}  // namespace cosmo
