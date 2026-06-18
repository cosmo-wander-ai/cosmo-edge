#include "catch_amalgamated.hpp"
/*
 * test_llm_yes_no_judge.cc - LlmYesNoJudge unit tests
 *
 * Tests for StripTrailingJudgePunct, ParseChineseYesNoTrue, ParseJudgeYesNo.
 */
#include "flow/common/LlmYesNoJudge.h"

using namespace cosmo;

// ── StripTrailingJudgePunct ──

TEST_CASE("StripTrailingJudgePunct: Basic punctuation stripping", "[LlmYesNoJudge]") {
    SECTION("Strip ASCII period") {
        std::string s = "是.";
        StripTrailingJudgePunct(s);
        REQUIRE(s == "是");
    }
    SECTION("Strip ASCII exclamation") {
        std::string s = "是!";
        StripTrailingJudgePunct(s);
        REQUIRE(s == "是");
    }
    SECTION("Strip fullwidth CJK period U+3002") {
        std::string s = "\xe6\x98\xaf\xe3\x80\x82";  // 是。
        StripTrailingJudgePunct(s);
        REQUIRE(s == "\xe6\x98\xaf");  // 是
    }
    SECTION("Strip fullwidth exclamation U+FF01") {
        std::string s = "\xe6\x98\xaf\xef\xbc\x81";  // 是！
        StripTrailingJudgePunct(s);
        REQUIRE(s == "\xe6\x98\xaf");  // 是
    }
    SECTION("Strip fullwidth question U+FF1F") {
        std::string s = "\xe6\x98\xaf\xef\xbc\x9f";  // 是？
        StripTrailingJudgePunct(s);
        REQUIRE(s == "\xe6\x98\xaf");  // 是
    }
    SECTION("Multiple trailing punctuation stripped") {
        std::string s = "是..!";
        StripTrailingJudgePunct(s);
        REQUIRE(s == "是");
    }
    SECTION("No punctuation — unchanged") {
        std::string s = "是";
        StripTrailingJudgePunct(s);
        REQUIRE(s == "是");
    }
    SECTION("Empty string") {
        std::string s = "";
        StripTrailingJudgePunct(s);
        REQUIRE(s.empty());
    }
}

// ── ParseChineseYesNoTrue ──

TEST_CASE("ParseChineseYesNoTrue: Basic yes/no", "[LlmYesNoJudge]") {
    SECTION("Simple 是 returns true") {
        REQUIRE(ParseChineseYesNoTrue("是") == true);
    }
    SECTION("Simple 否 returns false") {
        REQUIRE(ParseChineseYesNoTrue("否") == false);
    }
    SECTION("Empty string returns false") {
        REQUIRE(ParseChineseYesNoTrue("") == false);
    }
}

TEST_CASE("ParseChineseYesNoTrue: Negation patterns", "[LlmYesNoJudge]") {
    SECTION("不是 returns false despite containing 是") {
        REQUIRE(ParseChineseYesNoTrue("不是") == false);
    }
    SECTION("不存在 returns false") {
        REQUIRE(ParseChineseYesNoTrue("不存在") == false);
    }
    SECTION("没有 returns false") {
        REQUIRE(ParseChineseYesNoTrue("没有") == false);
    }
    SECTION("是否 (question) returns false") {
        REQUIRE(ParseChineseYesNoTrue("是否") == false);
    }
}

TEST_CASE("ParseChineseYesNoTrue: Ambiguous/mixed returns false", "[LlmYesNoJudge]") {
    SECTION("Both 是 and 否 present") {
        // 是 and 否 both present and neither negation pattern
        REQUIRE(ParseChineseYesNoTrue("是的，但也有否定") == false);
    }
}

TEST_CASE("ParseChineseYesNoTrue: Whitespace and backticks stripped", "[LlmYesNoJudge]") {
    SECTION("Leading/trailing whitespace") {
        REQUIRE(ParseChineseYesNoTrue("  是  ") == true);
    }
    SECTION("Backticks stripped") {
        REQUIRE(ParseChineseYesNoTrue("`是`") == true);
    }
}

TEST_CASE("ParseChineseYesNoTrue: With punctuation", "[LlmYesNoJudge]") {
    SECTION("是。 returns true") {
        REQUIRE(ParseChineseYesNoTrue("是。") == true);
    }
    SECTION("是！ returns true") {
        REQUIRE(ParseChineseYesNoTrue("是！") == true);
    }
    SECTION("否。 returns false") {
        REQUIRE(ParseChineseYesNoTrue("否。") == false);
    }
}

// ── ParseJudgeYesNo ──

TEST_CASE("ParseJudgeYesNo: Plain text", "[LlmYesNoJudge]") {
    SECTION("是 -> Yes") {
        REQUIRE(ParseJudgeYesNo("是") == LlmJudgeYesNo::Yes);
    }
    SECTION("否 -> No") {
        REQUIRE(ParseJudgeYesNo("否") == LlmJudgeYesNo::No);
    }
    SECTION("Unrelated text -> Unknown") {
        REQUIRE(ParseJudgeYesNo("hello world") == LlmJudgeYesNo::Unknown);
    }
    SECTION("Empty -> Unknown") {
        REQUIRE(ParseJudgeYesNo("") == LlmJudgeYesNo::Unknown);
    }
}

TEST_CASE("ParseJudgeYesNo: Markdown fenced code block", "[LlmYesNoJudge]") {
    SECTION("是 inside code block -> Yes") {
        std::string text = "```\n是\n```";
        REQUIRE(ParseJudgeYesNo(text) == LlmJudgeYesNo::Yes);
    }
    SECTION("否 inside code block -> No") {
        std::string text = "```\n否\n```";
        REQUIRE(ParseJudgeYesNo(text) == LlmJudgeYesNo::No);
    }
    SECTION("Code block with language tag") {
        std::string text = "```text\n是\n```";
        REQUIRE(ParseJudgeYesNo(text) == LlmJudgeYesNo::Yes);
    }
}

TEST_CASE("ParseJudgeYesNo: Multiline — only first line matters", "[LlmYesNoJudge]") {
    SECTION("First line 是, second line 否 -> Yes") {
        REQUIRE(ParseJudgeYesNo("是\n否") == LlmJudgeYesNo::Yes);
    }
    SECTION("First line 否, second line 是 -> No") {
        REQUIRE(ParseJudgeYesNo("否\n是") == LlmJudgeYesNo::No);
    }
}

TEST_CASE("ParseJudgeYesNo: Negation pattern -> No", "[LlmYesNoJudge]") {
    SECTION("不是 -> No (contains 是 but negation detected)") {
        REQUIRE(ParseJudgeYesNo("不是") == LlmJudgeYesNo::No);
    }
}

TEST_CASE("ParseJudgeYesNo: Descriptive text is Unknown", "[LlmYesNoJudge]") {
    SECTION("像是 is not a yes answer") {
        REQUIRE(ParseJudgeYesNo("监控摄像头位于一个明亮的室内空间，看起来像是一个办公区或活动区域。") ==
                LlmJudgeYesNo::Unknown);
    }
    SECTION("是否 in a sentence is not a no answer") {
        REQUIRE(ParseJudgeYesNo("判断图片中是否存在目标") == LlmJudgeYesNo::Unknown);
    }
}
