#include "catch_amalgamated.hpp"
/*
 * test_msg_dynamic_element.cc — MsgDynamicElement JSON serialization tests
 *
 * Tests to_json/from_json for MsgDynamicKeyValue and MsgDynamicElement,
 * covering various element types and conditional serialization.
 */
#include "nlohmann/json.hpp"
#include "util/MsgDynamicElement.h"

using json = nlohmann::json;

TEST_CASE("MsgDynamicKeyValue: JSON roundtrip", "[msg-dynamic-element]") {
    cosmo::MsgDynamicKeyValue kv;
    kv.key    = "test_key";
    kv.value  = "test_value";
    kv.keys   = {"k1", "k2"};
    kv.values = {"v1", "v2"};

    SECTION("Serialize to JSON") {
        json j;
        to_json(j, kv);
        REQUIRE(j.contains("key"));
        REQUIRE(j["key"] == "test_key");
    }

    SECTION("Roundtrip") {
        json j;
        to_json(j, kv);

        cosmo::MsgDynamicKeyValue kv2;
        from_json(j, kv2);
        REQUIRE(kv2.key.ToString() == "test_key");
        REQUIRE(kv2.value.ToString() == "test_value");
    }
}

TEST_CASE("MsgDynamicElement: JSON serialization basic fields", "[msg-dynamic-element]") {
    cosmo::MsgDynamicElement elem;
    elem.key          = "brightness";
    elem.value        = "50";
    elem.name         = "Brightness";
    elem.defaultValue = "50";
    elem.description  = "Adjust brightness";
    elem.type         = "slider";
    elem.min          = 0.0f;
    elem.max          = 100.0f;
    elem.step         = 1.0f;

    json j;
    to_json(j, elem);

    SECTION("Contains name field") {
        REQUIRE(j.contains("name"));
        REQUIRE(j["name"] == "Brightness");
    }

    SECTION("Contains type field") {
        REQUIRE(j.contains("type"));
        REQUIRE(j["type"] == "slider");
    }
}

TEST_CASE("MsgDynamicElement: from_json deserialization", "[msg-dynamic-element]") {
    json j = {{"key", "contrast"},
              {"value", "70"},
              {"name", "Contrast"},
              {"defaultValue", "50"},
              {"description", "Adjust contrast"},
              {"type", "slider"},
              {"min", 0.0f},
              {"max", 100.0f},
              {"step", 1.0f}};

    cosmo::MsgDynamicElement elem;
    from_json(j, elem);

    REQUIRE(elem.key.ToString() == "contrast");
    REQUIRE(elem.name == "Contrast");
    REQUIRE(elem.min == Catch::Approx(0.0f));
    REQUIRE(elem.max == Catch::Approx(100.0f));
}

TEST_CASE("MsgDynamicElement: Option serialization", "[msg-dynamic-element]") {
    cosmo::MsgDynamicElement::Option opt;
    opt.name  = "High";
    opt.value = "3";

    json j;
    to_json(j, opt);
    REQUIRE(j["name"] == "High");
    REQUIRE(j["value"] == "3");

    cosmo::MsgDynamicElement::Option opt2;
    from_json(j, opt2);
    REQUIRE(opt2.name == "High");
    REQUIRE(opt2.value == "3");
}

TEST_CASE("MsgDynamicElement: DependsOn serialization", "[msg-dynamic-element]") {
    cosmo::MsgDynamicElement::DependsOn dep;
    dep.key   = "mode";
    dep.value = "advanced";

    json j;
    to_json(j, dep);
    REQUIRE(j["key"] == "mode");
    REQUIRE(j["value"] == "advanced");

    cosmo::MsgDynamicElement::DependsOn dep2;
    from_json(j, dep2);
    REQUIRE(dep2.key == "mode");
    REQUIRE(dep2.value == "advanced");
}

TEST_CASE("MsgDynamicElement: element with options", "[msg-dynamic-element]") {
    cosmo::MsgDynamicElement elem;
    elem.key   = "quality";
    elem.value = "2";
    elem.name  = "Quality";
    elem.type  = "select";

    cosmo::MsgDynamicElement::Option o1, o2;
    o1.name      = "Low";
    o1.value     = "1";
    o2.name      = "High";
    o2.value     = "2";
    elem.options = {o1, o2};

    json j;
    to_json(j, elem);

    cosmo::MsgDynamicElement elem2;
    from_json(j, elem2);
    REQUIRE(elem2.options.size() == 2);
    REQUIRE(elem2.options[0].name == "Low");
    REQUIRE(elem2.options[1].name == "High");
}

TEST_CASE("MsgDynamicElement: channel ownership JSON roundtrip", "[msg-dynamic-element][task-parameters]") {
    cosmo::MsgDynamicElement elem;
    elem.key                   = "param.sceneOwned";
    elem.value                 = "11";
    elem.type                  = "text";
    elem.senior                = 2;
    elem.channelEditable       = false;
    elem.legacyChannelEditable = true;

    json j;
    to_json(j, elem);

    REQUIRE(j["senior"] == 2);
    REQUIRE(j["channelEditable"] == false);
    REQUIRE(j["legacyChannelEditable"] == true);

    cosmo::MsgDynamicElement restored;
    from_json(j, restored);
    REQUIRE(restored.senior == 2);
    REQUIRE(restored.channelEditable == false);
    REQUIRE(restored.legacyChannelEditable == true);
    REQUIRE_FALSE(restored.IsChannelEditable());
}

TEST_CASE("MsgDynamicElement: channel ownership compatibility rules",
          "[msg-dynamic-element][task-parameters][compatibility]") {
    SECTION("only the canonical unchecked selection is hidden") {
        cosmo::MsgDynamicElement visible;
        visible.key             = "param.visible";
        visible.senior          = 2;
        visible.channelEditable = true;
        REQUIRE(visible.IsChannelEditable());

        cosmo::MsgDynamicElement managed_special;
        managed_special.key             = "param.videoRepeatCount";
        managed_special.senior          = 2;
        managed_special.channelEditable = false;
        REQUIRE_FALSE(managed_special.IsChannelEditable());

        cosmo::MsgDynamicElement legacy_false;
        legacy_false.key             = "param.legacyFalse";
        legacy_false.senior          = 1;
        legacy_false.channelEditable = false;
        REQUIRE_FALSE(legacy_false.IsChannelEditable());
    }

    SECTION("special legacy parameters remain channel editable") {
        cosmo::MsgDynamicElement repeat;
        repeat.key    = "param.videoRepeatCount";
        repeat.senior = 2;
        REQUIRE(repeat.IsChannelEditable());

        cosmo::MsgDynamicElement retro;
        retro.key    = "param.retroDirect";
        retro.type   = "retroDirect";
        retro.senior = 1;
        REQUIRE(retro.IsChannelEditable());
    }

    SECTION("legacy senior values follow the visible default") {
        cosmo::MsgDynamicElement hidden_client;
        hidden_client.key    = "param.hiddenClient";
        hidden_client.senior = 1;
        REQUIRE(hidden_client.IsChannelEditable());

        cosmo::MsgDynamicElement hidden_everywhere;
        hidden_everywhere.key    = "param.hiddenEverywhere";
        hidden_everywhere.senior = 2;
        REQUIRE(hidden_everywhere.IsChannelEditable());

        cosmo::MsgDynamicElement visible;
        visible.key    = "param.visible";
        visible.senior = 0;
        REQUIRE(visible.IsChannelEditable());
    }

    SECTION("metadata without ownership fields keeps legacy editable behavior") {
        json legacy = {{"key", "param.legacy"}, {"value", "5"}, {"type", "text"}};
        cosmo::MsgDynamicElement elem;
        from_json(legacy, elem);

        REQUIRE_FALSE(elem.channelEditable.has_value());
        REQUIRE_FALSE(elem.senior.has_value());
        REQUIRE(elem.IsChannelEditable());

        json serialized;
        to_json(serialized, elem);
        REQUIRE_FALSE(serialized.contains("channelEditable"));
        REQUIRE_FALSE(serialized.contains("senior"));
    }

    SECTION("null ownership fields reset a reused element") {
        cosmo::MsgDynamicElement elem;
        elem.channelEditable       = false;
        elem.legacyChannelEditable = true;
        elem.senior                = 2;
        json legacy_null           = {{"key", "param.legacy"},
                                      {"type", "text"},
                                      {"channelEditable", nullptr},
                                      {"legacyChannelEditable", nullptr},
                                      {"senior", nullptr}};

        from_json(legacy_null, elem);
        REQUIRE_FALSE(elem.channelEditable.has_value());
        REQUIRE_FALSE(elem.legacyChannelEditable.has_value());
        REQUIRE_FALSE(elem.senior.has_value());
        REQUIRE(elem.IsChannelEditable());
    }

    SECTION("legacy string and numeric ownership fields remain readable") {
        const std::vector<std::pair<json, bool>> editableCases{{true, true}, {false, false}, {1, true},
                                                               {0, false},   {"true", true}, {"false", false},
                                                               {"1", true},  {"0", false}};
        for (const auto& [encoded, expected] : editableCases) {
            cosmo::MsgDynamicElement elem;
            from_json(json{{"key", "param.compat"},
                           {"channelEditable", encoded},
                           {"legacyChannelEditable", encoded}},
                      elem);
            REQUIRE(elem.channelEditable == expected);
            REQUIRE(elem.legacyChannelEditable == expected);
        }

        cosmo::MsgDynamicElement numericSenior;
        from_json(json{{"key", "param.numericSenior"}, {"senior", 2}}, numericSenior);
        REQUIRE(numericSenior.senior == 2);

        cosmo::MsgDynamicElement stringSenior;
        from_json(json{{"key", "param.stringSenior"}, {"senior", "-1"}}, stringSenior);
        REQUIRE(stringSenior.senior == -1);
    }

    SECTION("invalid ownership encodings are rejected") {
        cosmo::MsgDynamicElement elem;
        REQUIRE_THROWS(from_json(json{{"key", "param.badBool"}, {"channelEditable", 2}}, elem));
        REQUIRE_THROWS(from_json(json{{"key", "param.badLegacyBool"}, {"legacyChannelEditable", 2}}, elem));
        REQUIRE_THROWS(from_json(json{{"key", "param.badBool"}, {"channelEditable", "yes"}}, elem));
        REQUIRE_THROWS(from_json(json{{"key", "param.badSenior"}, {"senior", "2tail"}}, elem));
        REQUIRE_THROWS(from_json(json{{"key", "param.badSenior"}, {"senior", 1.5}}, elem));
    }
}

TEST_CASE("MsgDynamicElement: normalizes legacy channel ownership across metadata",
          "[msg-dynamic-element][task-parameters][ownership]") {
    const auto makeElement = [](const std::string& key, const std::string& type = "text") {
        cosmo::MsgDynamicElement element;
        element.key  = key;
        element.type = type;
        return element;
    };

    SECTION("orphaned and scene-managed dependencies are scene managed") {
        auto orphan          = makeElement("orphan");
        orphan.dependsOn.key = "missing";

        auto parent               = makeElement("parent", "switch");
        parent.senior             = 2;
        parent.channelEditable    = false;
        auto child          = makeElement("child");
        child.dependsOn.key = "parent";

        std::vector<cosmo::MsgDynamicElement> elements{orphan, parent, child};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK_FALSE(elements[0].IsChannelEditable());
        CHECK_FALSE(elements[1].IsChannelEditable());
        CHECK_FALSE(elements[2].IsChannelEditable());
    }

    SECTION("dependency cycles are scene managed") {
        auto first           = makeElement("first");
        first.dependsOn.key  = "second";
        auto second          = makeElement("second");
        second.dependsOn.key = "first";

        std::vector<cosmo::MsgDynamicElement> elements{first, second};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK_FALSE(elements[0].IsChannelEditable());
        CHECK_FALSE(elements[1].IsChannelEditable());
    }

    SECTION("explicit ownership cannot bypass an invalid dependency") {
        auto explicitEditable            = makeElement("FaceCheck", "switch");
        explicitEditable.channelEditable = true;

        auto explicitOrphan            = makeElement("orphan");
        explicitOrphan.dependsOn.key   = "missing";
        explicitOrphan.channelEditable = true;

        auto explicitManaged            = makeElement("param.videoRepeatCount");
        explicitManaged.senior           = 2;
        explicitManaged.channelEditable = false;

        std::vector<cosmo::MsgDynamicElement> elements{explicitEditable, explicitOrphan, explicitManaged};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK(elements[0].IsChannelEditable());
        CHECK_FALSE(elements[1].IsChannelEditable());
        CHECK_FALSE(elements[2].IsChannelEditable());
    }

    SECTION("legacy scene-only controls follow the visible default") {
        std::vector<cosmo::MsgDynamicElement> elements{
            makeElement("FaceCheck", "switch"), makeElement("catchView", "radio"),
            makeElement("minFaceWidth", "text"), makeElement("quality", "number")};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK(elements[0].IsChannelEditable());
        CHECK(elements[1].IsChannelEditable());
        CHECK(elements[2].IsChannelEditable());
        CHECK(elements[3].IsChannelEditable());
    }

    SECTION("legacy special controls remain channel editable") {
        auto repeat   = makeElement("param.videoRepeatCount");
        repeat.senior = 2;
        auto retro    = makeElement("retro", "retroDirect");
        retro.senior  = 1;

        std::vector<cosmo::MsgDynamicElement> elements{repeat, retro};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK(elements[0].IsChannelEditable());
        CHECK(elements[1].IsChannelEditable());
    }

    SECTION("legacy snapshot classification preserves old visibility defaults") {
        auto hiddenClient   = makeElement("param.hiddenClient");
        hiddenClient.senior = 1;
        auto visible        = makeElement("param.visible");
        visible.senior      = 0;

        std::vector<cosmo::MsgDynamicElement> elements{hiddenClient, visible};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements, true);

        CHECK_FALSE(elements[0].IsChannelEditable());
        CHECK(elements[1].IsChannelEditable());
    }

    SECTION("dependencies deeper than the channel form can render are scene managed") {
        auto root = makeElement("root", "switch");

        auto child          = makeElement("child");
        child.dependsOn.key = "root";

        auto explicitGrandchild            = makeElement("explicit-grandchild");
        explicitGrandchild.dependsOn.key   = "child";
        explicitGrandchild.channelEditable = true;

        auto forcedGrandchild          = makeElement("forced-grandchild", "retroDirect");
        forcedGrandchild.dependsOn.key = "child";

        auto explicitCycleRoot                  = makeElement("explicit-cycle-root", "switch");
        explicitCycleRoot.dependsOn.key         = "explicit-cycle-child";
        explicitCycleRoot.channelEditable       = true;
        auto explicitCycleChild                 = makeElement("explicit-cycle-child");
        explicitCycleChild.dependsOn.key        = "explicit-cycle-root";
        auto explicitCycleDescendant            = makeElement("explicit-cycle-descendant");
        explicitCycleDescendant.dependsOn.key   = "explicit-cycle-child";
        explicitCycleDescendant.channelEditable = true;

        std::vector<cosmo::MsgDynamicElement> elements{root,
                                                       child,
                                                       explicitGrandchild,
                                                       forcedGrandchild,
                                                       explicitCycleRoot,
                                                       explicitCycleChild,
                                                       explicitCycleDescendant};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK(elements[0].IsChannelEditable());
        CHECK(elements[1].IsChannelEditable());
        CHECK_FALSE(elements[2].IsChannelEditable());
        CHECK_FALSE(elements[3].IsChannelEditable());
        CHECK_FALSE(elements[4].IsChannelEditable());
        CHECK_FALSE(elements[5].IsChannelEditable());
        CHECK_FALSE(elements[6].IsChannelEditable());
        REQUIRE(elements[2].channelEditable.has_value());
        REQUIRE(elements[3].channelEditable.has_value());
        CHECK_FALSE(*elements[2].channelEditable);
        CHECK_FALSE(*elements[3].channelEditable);
        CHECK_FALSE(*elements[6].channelEditable);
    }

    SECTION("controls unsupported by their channel render context are scene managed") {
        auto root = makeElement("root", "switch");

        auto unknownRoot            = makeElement("unknown-root", "customWidget");
        unknownRoot.channelEditable = true;

        auto visibleChild          = makeElement("visible-child", "text");
        visibleChild.dependsOn.key = "root";

        auto numberChild            = makeElement("number-child", "number");
        numberChild.channelEditable = true;
        numberChild.dependsOn.key   = "root";

        auto retroChild          = makeElement("retro-child", "retroDirect");
        retroChild.dependsOn.key = "root";

        auto repeatChild          = makeElement("param.videoRepeatCount", "customWidget");
        repeatChild.dependsOn.key = "root";

        auto textParent                 = makeElement("text-parent", "text");
        auto hiddenTextChild            = makeElement("hidden-text-child", "text");
        hiddenTextChild.dependsOn.key   = "text-parent";
        hiddenTextChild.channelEditable = true;

        std::vector<cosmo::MsgDynamicElement> elements{root,        unknownRoot,    visibleChild,
                                                       numberChild, retroChild,     repeatChild,
                                                       textParent,  hiddenTextChild};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK(elements[0].IsChannelEditable());
        CHECK_FALSE(elements[1].IsChannelEditable());
        CHECK(elements[2].IsChannelEditable());
        CHECK_FALSE(elements[3].IsChannelEditable());
        CHECK_FALSE(elements[4].IsChannelEditable());
        CHECK(elements[5].IsChannelEditable());
        CHECK(elements[6].IsChannelEditable());
        CHECK_FALSE(elements[7].IsChannelEditable());
    }

    SECTION("legacy root exclusions do not hide renderable children with the same key and type") {
        auto root = makeElement("root", "switch");

        auto faceChild           = makeElement("FaceCheck", "switch");
        faceChild.dependsOn.key  = "root";
        auto catchChild          = makeElement("catchView", "radio");
        catchChild.dependsOn.key = "root";
        auto widthChild          = makeElement("minFaceWidth", "text");
        widthChild.dependsOn.key = "root";

        std::vector<cosmo::MsgDynamicElement> elements{root, faceChild, catchChild, widthChild};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK(elements[0].IsChannelEditable());
        CHECK(elements[1].IsChannelEditable());
        CHECK(elements[2].IsChannelEditable());
        CHECK(elements[3].IsChannelEditable());
    }

    SECTION("an explicit child cannot override a scene-managed parent") {
        auto parent            = makeElement("parent", "switch");
        parent.senior          = 2;
        parent.channelEditable = false;
        auto child             = makeElement("child", "text");
        child.dependsOn.key    = "parent";
        child.channelEditable  = true;

        std::vector<cosmo::MsgDynamicElement> elements{parent, child};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK_FALSE(elements[0].IsChannelEditable());
        CHECK_FALSE(elements[1].IsChannelEditable());
    }

    SECTION("only the last complete point pair is channel editable") {
        auto firstInitial = makeElement("first-initial", "initialPoint");
        auto firstEnd     = makeElement("first-end", "endPoint");
        auto lastInitial  = makeElement("last-initial", "initialPoint");
        auto lastEnd      = makeElement("last-end", "endPoint");

        std::vector<cosmo::MsgDynamicElement> elements{firstInitial, firstEnd, lastInitial, lastEnd};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);

        CHECK_FALSE(elements[0].IsChannelEditable());
        CHECK_FALSE(elements[1].IsChannelEditable());
        CHECK(elements[2].IsChannelEditable());
        CHECK(elements[3].IsChannelEditable());

        auto orphanInitial            = makeElement("orphan-initial", "initialPoint");
        orphanInitial.channelEditable = true;
        std::vector<cosmo::MsgDynamicElement> orphan{orphanInitial};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(orphan);
        CHECK_FALSE(orphan.front().IsChannelEditable());

        auto managedInitial            = makeElement("managed-initial", "initialPoint");
        auto managedEnd                = makeElement("managed-end", "endPoint");
        managedEnd.senior              = 2;
        managedEnd.channelEditable     = false;
        managedInitial.channelEditable = true;
        std::vector<cosmo::MsgDynamicElement> managedPair{managedInitial, managedEnd};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(managedPair);
        CHECK_FALSE(managedPair[0].IsChannelEditable());
        CHECK_FALSE(managedPair[1].IsChannelEditable());
    }

    SECTION("malformed dependencies always fail closed") {
        const std::vector<json> malformedDependencies{json::object(), json{{"key", nullptr}}, json::array(),
                                                      json("parent"), json{{"key", 7}}};
        for (const auto& dependency : malformedDependencies) {
            json encoded = {{"key", "malformed"}, {"type", "text"}, {"dependsOn", dependency}};
            auto element = encoded.get<cosmo::MsgDynamicElement>();
            REQUIRE(element.malformedDependsOn);

            std::vector<cosmo::MsgDynamicElement> elements{element};
            cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(elements);
            CHECK_FALSE(elements.front().IsChannelEditable());
        }

        json explicitEncoded = {
            {"key", "explicit"}, {"type", "text"}, {"channelEditable", true}, {"dependsOn", json::object()}};
        auto explicitElement = explicitEncoded.get<cosmo::MsgDynamicElement>();
        std::vector<cosmo::MsgDynamicElement> explicitElements{explicitElement};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(explicitElements);
        CHECK_FALSE(explicitElements.front().IsChannelEditable());

        json forcedEncoded = {
            {"key", "param.videoRepeatCount"}, {"type", "text"}, {"dependsOn", json::object()}};
        auto forcedElement = forcedEncoded.get<cosmo::MsgDynamicElement>();
        std::vector<cosmo::MsgDynamicElement> forcedElements{forcedElement};
        cosmo::MsgDynamicElement::NormalizeLegacyChannelOwnership(forcedElements);
        CHECK_FALSE(forcedElements.front().IsChannelEditable());
    }
}
