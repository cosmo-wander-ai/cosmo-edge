"""Conversion contract checks independent of a model/runtime installation."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("picture_templates", ROOT / "tools/generate_picture_templates.py")
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class PictureTemplatesTest(unittest.TestCase):
    def test_all_platforms_have_reproducible_region_free_graphs(self):
        self.assertEqual(generator.generate(check=True), [])
        for platform in generator.PLATFORMS:
            resource = ROOT / "data/resource" / ("aiboxresource_" + platform)
            report = json.loads((resource / "layout/picture-template-conversions.json").read_text(encoding="utf-8"))
            self.assertEqual(sum(bool(item["picture"]) for item in report), 13)
            for item in report:
                if item["picture"]:
                    filename = f"{item['picture']}_picture.json"
                    self.assertEqual((resource / "algorithm" / filename).read_bytes(),
                                     (resource / "algorithm_template" / filename).read_bytes())
            for path in (resource / "algorithm_template").glob("*.json"):
                document = json.loads(path.read_text(encoding="utf-8"))
                if document.get("algorithmUsage") != 2:
                    continue
                with self.subTest(platform=platform, code=document["algorithmCode"]):
                    metadata = generator.decode(document["algorithmMetadata"], {})
                    self.assertEqual(set(metadata), {"params"})
                    self.assertTrue(all(generator.allowed(p["key"]) for p in metadata["params"]))
                    nodes = generator.decode(document["algorithmProcessdata"], [])
                    known = {"-1"} | {node["flowActionId"] for node in nodes}
                    self.assertEqual(len(known), len(nodes) + 1)
                    for node in nodes:
                        self.assertIn(node["preFlowActionId"], known)
                        self.assertTrue(node["actionId"].startswith("P"))
                    visited = {"-1"}
                    while len(visited) < len(known):
                        ready = {n["flowActionId"] for n in nodes if n["preFlowActionId"] in visited}
                        self.assertTrue(ready - visited, "workflow must be acyclic")
                        visited |= ready

    def test_picture_parameter_labels_have_separate_translations(self):
        fields = {"PB_00004": {"output.name", "output.targets"},
                  "PB_00006": {"match.libraryType", "match.mode", "param.faceSet", "param.workClothesSet", "param.limitScore"},
                  "PA_00002": {"inputType"}, "PDA_00003": {"inputType"}}
        for platform in generator.PLATFORMS:
            resource = ROOT / "data/resource" / ("aiboxresource_" + platform)
            actions = json.loads((resource / "layout/actions.json").read_text(encoding="utf-8"))
            locales = {lang: json.loads((resource / "i18n" / f"resource.{lang}.json").read_text(encoding="utf-8"))
                       for lang in ("zh-CN", "en-US")}
            public = {lang: json.loads((ROOT / "src/web/public/resource-i18n" / f"resource.{lang}.json").read_text(encoding="utf-8"))
                      for lang in locales}
            for action in actions:
                if action["id"] not in fields:
                    continue
                params = [p for p in generator.decode(action["inputParamConfig"], []) if p["key"] in fields[action["id"]]]
                self.assertEqual({p["key"] for p in params}, fields[action["id"]])
                for param in params:
                    labels = [(param["name"], param["nameI18nKey"])]
                    labels += [(o["name"], o["labelI18nKey"]) for o in param.get("options", [])]
                    for fallback, key in labels:
                        with self.subTest(platform=platform, key=key):
                            self.assertEqual(fallback, locales["zh-CN"][key])
                            self.assertNotIn(" / ", fallback)
                            self.assertRegex(fallback, r"[\u4e00-\u9fff]")
                            self.assertNotRegex(locales["en-US"][key], r"[\u4e00-\u9fff]")
                            for lang in locales:
                                self.assertEqual(locales[lang][key], public[lang][key])

    def test_region_or_history_nodes_prevent_conversion(self):
        source = next(p for p in (ROOT / "data/resource/aiboxresource_bm1688/algorithm_template").glob("*.json")
                      if json.loads(p.read_text(encoding="utf-8")).get("algorithmUsage") == 1)
        document = json.loads(source.read_text(encoding="utf-8"))
        for code in ("BA_00005", "unknown-action"):
            with self.subTest(action=code):
                candidate = copy.deepcopy(document)
                nodes = generator.decode(candidate["algorithmProcessdata"], [])
                nodes[0]["actionId"] = code
                candidate["algorithmProcessdata"] = generator.compact(nodes)
                result, reason = generator.convert(candidate, {"zh-CN": {}, "en-US": {}})
                self.assertIsNone(result)
                self.assertIn(code, reason)


if __name__ == "__main__":
    unittest.main()
