#!/usr/bin/env python3
"""Derive single-image templates; never change a video's temporal/spatial contract.

Run with --check in CI, or without it to regenerate checked-in resources.
The source video templates remain untouched. IDs use the reserved 92000000 range.
"""
import argparse
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("bm1688", "cv186x", "x86")
MAP = {"AA_00001": "PA_00001", "AA_00002": "PA_00002", "AA_00004": "PA_00004",
       "AA_00005": "PA_00005", "AA_00011": "PA_00011", "BA_00002": "PB_00002",
       "BA_90001": "PB_90001", "BA_00004": "PB_00004", "DA_00001": "PDA_00001",
       "DA_00002": "PDA_00002", "DA_00003": "PDA_00003"}
REMOVE = {"BA_00001", "AA_00003", "BA_00003", "BA_10003"}
PARAMS = {"atomicCode", "featureInput", "keywords", "advanced_mode", "generationStyle",
          "doSample", "topK", "topP", "temperature", "param.faceSet", "param.workClothesSet",
          "param.limitScore", "inputType", "match.libraryType", "match.mode", "output.name",
          "output.targets", "preprocess_type"}
NAMES = {"PA_00004": ("图片关键点", "Image Landmarks"), "PA_00011": ("图片文字识别", "Image OCR"),
         "PB_00002": ("图片目标筛选", "Image Target Filter"), "PB_90001": ("图片目标判断", "Image Target Rule"),
         "PB_90003": ("整图判断", "Image Rule"), "PB_90002": ("条件分支", "Conditional Branch"),
         "PB_00004": ("图片结果输出", "Image Result Output"), "PB_00006": ("图片库比对", "Image Library Match")}


def decode(value, fallback):
    return json.loads(value) if isinstance(value, str) and value else (value or fallback)


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def allowed(key):
    return key in PARAMS or (key.startswith("aiParam.") and key.endswith(".confidence")) or (
        key.startswith("filter.") and len(key.split(".")) == 4 and key.split(".")[2] in {"confidence", "side", "size"})


def field(key, name, default="", kind="text", options=None, level="1"):
    result = dict(key=key, name=name, type=kind, defaultValue=default, level=level, regexpr="")
    if options:
        result["options"] = [dict(name=n, value=v) for n, v in options]
    return result


def catalog(actions, locales):
    by_id = {item["id"]: item for item in actions}
    configs = {
        "PA_00004": decode(by_id["AA_00004"]["inputParamConfig"], []),
        "PA_00011": decode(by_id["AA_00011"]["inputParamConfig"], []),
        "PB_00002": decode(by_id["BA_00002"]["inputParamConfig"], []),
        "PB_90001": decode(by_id["BA_90001"]["inputParamConfig"], []),
        "PB_90003": decode(by_id["BA_90001"]["inputParamConfig"], []),
        "PB_90002": decode(by_id["BA_90001"]["inputParamConfig"], []),
        "PB_00004": [field("output.name", "结果名称 / Output name"), field("output.targets", "返回目标 / Targets", "matched", "select", [("命中 / Matched", "matched"), ("全部 / All", "all")])],
        "PB_00006": [field("match.libraryType", "比对库类型 / Library type", "face", "select", [("人脸 / Face", "face"), ("工服 / Workwear", "body")]),
                     field("match.mode", "判断方式 / Match mode", "matched", "select", [("命中 / Matched", "matched"), ("未命中 / Unmatched", "unmatched")]),
                     field("param.faceSet", "人脸库 / Face libraries", kind="faceSet", level="2"),
                     field("param.workClothesSet", "工服库 / Workwear libraries", kind="workClothesSet", level="2"),
                     field("param.limitScore", "比对阈值 0–100 / Score", "80", level="2")]
    }
    for code, names in NAMES.items():
        action = by_id.get(code)
        if action is None:
            action = dict(id=code, actionUsage=2, actionType=1 if code.startswith("PA_") else 2, businessCategory="2")
            actions.append(action)
        action.update(actionName=names[0], remark=names[0], inputParamConfig=compact(configs[code]))
        for prop in ("actionName", "remark"):
            key = f"resource.action.{code.lower()}.{prop.lower()}"
            action[prop + "I18nKey"] = key
            for lang, label in zip(("zh-CN", "en-US"), names):
                locales[lang][key] = label
    classify = by_id["PA_00002"]
    config = [p for p in decode(classify["inputParamConfig"], []) if p.get("key") != "inputType"]
    config.append(field("inputType", "分类输入 / Classification input", "targets", "select", [("目标 / Targets", "targets"), ("整图 / Whole image", "image")]))
    classify["inputParamConfig"] = compact(config)
    vlm = by_id["PDA_00003"]
    config = [p for p in decode(vlm["inputParamConfig"], []) if p.get("key") != "inputType"]
    config.append(field("inputType", "判断输入 / Judgment input", "image", "select", [("整图 / Whole image", "image"), ("目标 / Targets", "targets")]))
    vlm["inputParamConfig"] = compact(config)
    # Extraction and comparison are independent nodes in picture workflows.
    feature = by_id["PA_00005"]
    feature_params = [p for p in decode(feature["inputParamConfig"], []) if p.get("key") in {"atomicCode", "featureInput"}]
    for param in feature_params:
        for option in param.get("options", []):
            if option["value"] in {"0", "3"}:
                option["name"] = "人脸特征提取" if option["value"] == "0" else "人体特征提取"
                key = option["labelI18nKey"]
                locales["zh-CN"][key] = option["name"]
                locales["en-US"][key] = "Face feature extraction" if option["value"] == "0" else "Body feature extraction"
    feature["inputParamConfig"] = compact(feature_params)


def convert(source, locales):
    nodes = decode(source.get("algorithmProcessdata"), [])
    unsupported = sorted({n["actionId"] for n in nodes} - MAP.keys() - REMOVE)
    if unsupported:
        return None, "requires region, temporal history or unsupported action: " + ", ".join(unsupported)
    ids = {n["flowActionId"]: n for n in nodes}
    if len(ids) != len(nodes):
        raise ValueError("duplicate node IDs")
    metadata = decode(source.get("algorithmMetadata"), {})
    meta = [copy.deepcopy(p) for p in metadata.get("params", []) if allowed(p.get("key", ""))]
    defaults = {p["key"]: str(p.get("defaultValue") or p.get("value") or "") for p in meta}
    thresholds = {}
    for node in nodes:
        web = node.get("configObject", {}).get("webConfig", {}) or {}
        for label in web.get("labelList", []):
            values = label.get("threshold", [])
            if values:
                base = "aiParam." + label["class_name"]
                choice = next((p.get("defaultValue", "0,0") for p in metadata.get("params", []) if p.get("key") == base + ".confidenceConfig"), "0,0") or "0,0"
                selected, offset = (choice.split(",") + ["0"])[:2]
                index = min(max(int(selected), 0), len(values) - 1)
                thresholds[base + ".confidence"] = str(max(0, min(1, float(values[index]) + float(offset))))
    for param in meta:
        key = param["key"]
        if not defaults[key] and key in thresholds:
            defaults[key] = thresholds[key]
            param["defaultValue"] = defaults[key]
            param["value"] = defaults[key]
        param.pop("dependsOn", None)
    output = []
    for original in nodes:
        if original["actionId"] in REMOVE:
            continue
        node = copy.deepcopy(original)
        previous = node["preFlowActionId"]
        seen = set()
        while previous != "-1" and ids[previous]["actionId"] in REMOVE:
            if previous in seen:
                raise ValueError("cyclic template")
            seen.add(previous)
            previous = ids[previous]["preFlowActionId"]
        node["preFlowActionId"] = previous
        node["actionId"] = MAP[original["actionId"]]
        node["actionName"] = NAMES.get(node["actionId"], (node["actionName"],))[0]
        if node["actionId"] in NAMES:
            node["actionNameI18nKey"] = f"resource.action.{node['actionId'].lower()}.actionname"
        config = node["configObject"]
        params = {p["key"]: str(p["value"]) for p in config.get("params") or [] if allowed(p["key"])}
        web = config.get("webConfig", {}) or {}
        web["metaDataParams"] = [p for p in web.get("metaDataParams", []) if allowed(p.get("key", ""))]
        if node["actionId"] == "PB_00002":
            for label in web.get("labelFilterList", []):
                key = f"filter.{label['labelCode']}.side.min"
                params[key] = defaults.get(key) or ("60" if label.get("sideMinIsEnable") == "1" else "0")
        if node["actionId"] == "PB_00004":
            params = {"output.targets": "matched"}
            web = {}
        config["params"] = [dict(key=k, value=v) for k, v in params.items()]
        config["webConfig"] = web
        output.append(node)
        if node["actionId"] == "PA_00005":
            body = params.get("featureInput", "0") != "0"
            old_params = {p["key"]: p["value"] for p in original["configObject"].get("params") or []}
            match_id = node["flowActionId"] + "-match"
            output.append(dict(actionId="PB_00006", actionName=NAMES["PB_00006"][0], flowActionId=match_id,
                               preFlowActionId=node["flowActionId"], actionNameI18nKey="resource.action.pb_00006.actionname",
                               configObject=dict(params=[dict(key="match.libraryType", value="body" if body else "face"),
                                                         dict(key="match.mode", value="unmatched" if body and old_params.get("matchFlag", "0") == "0" else "matched")], webConfig={})))
    match_after = {n["preFlowActionId"]: n["flowActionId"] for n in output if n["actionId"] == "PB_00006"}
    for node in output:
        if node["actionId"] != "PB_00006" and node["preFlowActionId"] in match_after:
            node["preFlowActionId"] = match_after[node["preFlowActionId"]]
    if not output:
        return None, "no single-frame analysis"
    ordered, visited = [], {"-1"}
    while len(ordered) < len(output):
        before = len(ordered)
        for node in output:
            if node["flowActionId"] not in visited and node["preFlowActionId"] in visited:
                ordered.append(node)
                visited.add(node["flowActionId"])
        if before == len(ordered):
            raise ValueError("cycle or dangling parent after conversion")
    code = 92000000 + int(source["algorithmCode"])
    result = copy.deepcopy(source)
    for key in ("eventType", "pollingSupport", "extraFormat", "instructionCode"):
        result.pop(key, None)
    name = "图片" + source["algorithmName"]
    result.update(algorithmCode=code, algorithmId=str(code), id=str(code), gafAlgorithmId=str(code),
                  algorithmName=name, gafAlgorithmName=name, algorithmUsage=2, confVersionId=f"default-{code}",
                  algorithmUpdateTime=202609300001, algorithmProcessdata=compact(ordered),
                  algorithmMetadata=compact(dict(params=meta)),
                  remark="单张图片判断，不含跟踪、持续时间或区域条件。", sourceVideoAlgorithmCode=str(source["algorithmCode"]))
    for prop in ("algorithmName", "remark"):
        key = f"resource.algorithm.{code}.{prop.lower()}"
        result[prop + "I18nKey"] = key
        locales["zh-CN"][key] = result[prop]
        original_key = source.get("algorithmNameI18nKey", "")
        locales["en-US"][key] = ("Image: " + locales["en-US"].get(original_key, source["algorithmName"])) if prop == "algorithmName" else "Single-image decision; no tracking, duration or region conditions."
    result["configVersionList"] = [dict(id=result["confVersionId"], name="默认", algorithmCode=str(code),
                                         **{k: result[k] for k in ("algorithmMetadata", "algorithmProcessdata", "atomicList", "algorithmUpdateTime", "remark")})]
    return result, "single-image adaptation; tracking and sensitivity removed"


def generate(check=False):
    changed = []
    def save(path, value):
        encoded = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
        if not path.exists() or path.read_text(encoding="utf-8") != encoded:
            changed.append(str(path.relative_to(ROOT)))
            if not check:
                path.write_text(encoded, encoding="utf-8", newline="\n")

    for platform in PLATFORMS:
        resource = ROOT / "data/resource" / ("aiboxresource_" + platform)
        locales = {lang: json.loads((resource / "i18n" / f"resource.{lang}.json").read_text(encoding="utf-8")) for lang in ("zh-CN", "en-US")}
        actions = json.loads((resource / "layout/actions.json").read_text(encoding="utf-8"))
        catalog(actions, locales)
        report = []
        for path in sorted((resource / "algorithm_template").glob("*.json")):
            source = json.loads(path.read_text(encoding="utf-8"))
            if source.get("algorithmUsage") != 1:
                continue
            result, reason = convert(source, locales)
            report.append(dict(source=str(source["algorithmCode"]), name=source["algorithmName"],
                               picture=str(result["algorithmCode"]) if result else None, reason=reason))
            if result:
                save(resource / "algorithm_template" / f"{result['algorithmCode']}_picture.json", result)
        # Existing picture templates also have no region configuration.
        for path in (resource / "algorithm_template").glob("*.json"):
            document = json.loads(path.read_text(encoding="utf-8"))
            if document.get("algorithmUsage") != 2 or document.get("sourceVideoAlgorithmCode"):
                continue
            for entry in [document] + document.get("configVersionList", []):
                metadata = decode(entry.get("algorithmMetadata"), {})
                entry["algorithmMetadata"] = compact(dict(params=[p for p in metadata.get("params", []) if allowed(p.get("key", ""))]))
                if entry.get("algorithmProcessdata"):
                    nodes = decode(entry["algorithmProcessdata"], [])
                    for node in nodes:
                        web = node.get("configObject", {}).get("webConfig", {}) or {}
                        if "metaDataParams" in web:
                            web["metaDataParams"] = [p for p in web["metaDataParams"] if not p.get("key", "").endswith(".detPostion")]
                    entry["algorithmProcessdata"] = compact(nodes)
            save(path, document)
        save(resource / "layout/actions.json", actions)
        save(resource / "layout/picture-template-conversions.json", report)
        for lang, values in locales.items():
            save(resource / "i18n" / f"resource.{lang}.json", values)
    for lang in ("zh-CN", "en-US"):
        # Platform dictionaries deliberately share these template keys.
        values = locales[lang]
        save(ROOT / "src/web/public/resource-i18n" / f"resource.{lang}.json", values)
    return changed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    differences = generate(args.check)
    print(f"{'Outdated' if args.check else 'Updated'}: {len(differences)} files")
    if args.check and differences:
        print("\n".join(differences))
        raise SystemExit(1)
