#!/usr/bin/env python3
"""Offline numeric oracle for dynamic inputs; never exports or deploys a model.

The source model and static encoder are compared with identical F32 embeddings.
The deployed F16 embedding lookup is measured separately and saved for device
replay. Reusing one captured image boundary is not a scene-accuracy evaluation.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import sys
import time


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "model", "exporter", "questions", "visual", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    for name in ("common", "exporter", "visual"):
        p.add_argument("--" + name + "-sha256", required=True)
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--threads", type=int, default=2)
    a = p.parse_args()
    if a.output.exists():
        raise FileExistsError("use a fresh evidence directory")
    checked = {"common": a.source / "laya/common.py", "exporter": a.exporter, "visual": a.visual}
    for name, path in checked.items():
        if digest(path) != getattr(a, name + "_sha256"):
            raise ValueError(name + " identity mismatch")
    os.environ["HF_HUB_OFFLINE"] = "1"
    os.environ["TRANSFORMERS_OFFLINE"] = "1"
    sys.path[:0] = [str(a.source.resolve()), str(a.exporter.parent.resolve()),
                    str(Path(__file__).resolve().parents[1] / "tools/laya_reference")]
    import numpy as np
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
    from safetensors import safe_open
    from laya.common import build_model
    from laya_cpu import splice_embeddings, score_and_act
    from dynamic_frontend import DynamicImageFrontend, dynamic_decision_feeds

    torch.set_num_threads(a.threads)
    torch.backends.mha.set_fastpath_enabled(False)
    a.output.mkdir(parents=True, mode=0o700)
    report = {"status": "RUNNING", "scope": "dynamic numeric boundary; one captured image",
              "torch": torch.__version__, "device": "cpu", "threads": a.threads,
              "identity": {name: digest(path) for name, path in checked.items()},
              "cases": [], "business_accuracy": "UNVERIFIED", "device_parity": "UNVERIFIED"}
    report_path = a.output / "report.json"

    def save():
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")

    save()
    try:
        cfg = json.loads((a.model / "rl_agent_config.json").read_text())
        text_cfg = dict(cfg)
        text_cfg.pop("vision", None)
        model = build_model(text_cfg, encoder_dir=str(a.model / "encoder"), pretrained=False).eval()
        with safe_open(str(a.model / "model.safetensors"), framework="pt", device="cpu") as f:
            sd = {k: f.get_tensor(k) for k in f.keys() if not k.startswith("vision.")}
        model.load_state_dict(sd, strict=True)
        del sd
        model.float().eval()
        enc = model.encoder
        enc.config.reference_compile = False
        report["native_attention"] = enc.config._attn_implementation
        report["identity"].update(model=digest(a.model / "model.safetensors"),
                                  cpu_boundary=digest(a.exporter.parent / "laya_cpu.py"),
                                  verifier=digest(__file__))
        L, D = 256, int(enc.config.hidden_size)
        rope = {}
        with torch.no_grad():
            for kind in set(enc.config.layer_types):
                rope[kind] = tuple(v.detach().clone() for v in enc.rotary_emb(
                    torch.zeros(1, L, D), torch.arange(L)[None], kind))
        classes = [n for n in ast.walk(ast.parse(a.exporter.read_text()))
                   if isinstance(n, ast.ClassDef) and n.name == "StaticDecision"]
        if len(classes) != 1:
            raise ValueError("pinned static decision class missing")
        scope = dict(enc=enc, model=model, L=L, D=D, rope=rope, torch=torch, nn=nn, F=F)
        exec(compile(ast.Module(body=classes, type_ignores=[]), str(a.exporter), "exec"), scope)
        static = scope["StaticDecision"]().eval()
        table = enc.get_input_embeddings().weight.detach().numpy()
        half_table = table.astype(np.float16)
        heads = {k: v.detach().numpy().copy() for k, v in model.state_dict().items()
                 if k.startswith(("scorer.", "act_head.", "type_emb."))}
        heads["scorer_norm_eps"] = np.asarray(model.scorer[0].eps, dtype=np.float32)
        with np.load(a.visual, allow_pickle=False) as archive:
            visual = archive["visual_tokens"].astype(np.float32)
        if visual.shape != (1, 130, D) or not np.isfinite(visual).all():
            raise ValueError("visual fixture mismatch")
        expected_bindings = {"tokenizer_sha256": digest(a.model / "tokenizer/tokenizer.json"),
                             "model_config_sha256": digest(a.model / "rl_agent_config.json")}

        def metric(got, expected):
            got, expected = np.asarray(got), np.asarray(expected)
            delta = np.abs(got - expected)
            return {"max_abs": float(delta.max()), "mean_abs": float(delta.mean()),
                    "allclose": bool(np.allclose(got, expected, atol=3e-4, rtol=3e-4)),
                    "finite": bool(np.isfinite(got).all() and np.isfinite(expected).all()),
                    "atol": 3e-4, "rtol": 3e-4}

        def native(pack, padded):
            s = pack["sequence"]
            valid = s["valid_length"]
            length = L if padded else valid
            ids = torch.zeros(1, length, dtype=torch.long)
            ids[0, :valid] = torch.tensor(s["input_ids"])
            att = torch.zeros_like(ids)
            att[0, :valid] = 1
            hidden = []
            hook = model.head.layers[-1].register_forward_hook(
                lambda module, inputs, output: hidden.append(output.detach().numpy().copy()))
            try:
                with torch.inference_mode():
                    logits, acts = model(ids, att, torch.tensor([s["marker_positions"]]),
                        torch.ones(1, len(pack["option_labels"]), dtype=torch.bool),
                        torch.tensor([pack["qtype"]]), vis_tokens=torch.from_numpy(visual),
                        vis_index=torch.tensor([0]), img_pos=torch.tensor([s["image_position"]]))
                return logits.numpy(), acts.numpy(), hidden[0][:, :valid]
            finally:
                hook.remove()

        paths = sorted(a.questions.glob("*.json"))
        if a.limit:
            paths = paths[:a.limit]
        if not paths:
            raise ValueError("no prepared questions")
        for path in paths:
            started = time.monotonic()
            pack = json.loads(path.read_text())
            frontend = DynamicImageFrontend(pack, pack["compiled_sha256"], expected_bindings, cfg)
            s, valid = pack["sequence"], pack["sequence"]["valid_length"]
            prepared = dict(input_ids=np.asarray(s["input_ids"], dtype=np.int64)[None],
                            valid_length=valid, image_position=s["image_position"],
                            qtype=pack["qtype"], **frontend.masks)
            runtime_feeds = dynamic_decision_feeds(prepared, visual, half_table, heads)
            full_feeds = dict(runtime_feeds)
            full_feeds["inputs_embeds"] = splice_embeddings(table, s["input_ids"], visual,
                                                            s["image_position"], L)
            with torch.inference_mode():
                out = static(**{k: torch.from_numpy(v) for k, v in full_feeds.items()}).numpy()
                runtime_out = static(**{k: torch.from_numpy(v) for k, v in runtime_feeds.items()}).numpy()
            positions = np.asarray(s["marker_positions"], dtype=np.int64)[None]
            mask = np.ones_like(positions, dtype=bool)
            logits, acts = score_and_act(out, positions, mask, heads)
            runtime_logits, runtime_acts = score_and_act(runtime_out, positions, mask, heads)
            ref_logits, ref_acts, ref_hidden = native(pack, True)
            short_logits, short_acts, short_hidden = native(pack, False)
            checks = {"valid_hidden": metric(out[:, :valid], ref_hidden),
                      "option_logits": metric(logits, ref_logits),
                      "action_logits": metric(acts, ref_acts),
                      "unpadded_valid_hidden": metric(ref_hidden, short_hidden),
                      "unpadded_option_logits": metric(ref_logits, short_logits),
                      "unpadded_action_logits": metric(ref_acts, short_acts)}
            row = {"id": pack["question_id"], "compiled_sha256": pack["compiled_sha256"],
                   "valid_length": valid, "qtype": pack["qtype"],
                   "checks": checks, "status": "PASS" if all(x["allclose"] and x["finite"]
                       for x in checks.values()) else "FAIL",
                   "f16_embedding_delta_separate_measurement": {
                       "logits": metric(runtime_logits, logits), "acts": metric(runtime_acts, acts)},
                   "seconds": round(time.monotonic() - started, 3)}
            stem = pack["compiled_sha256"]
            np.savez_compressed(a.output / (stem + "-feeds.npz"), **runtime_feeds)
            np.savez_compressed(a.output / (stem + "-reference.npz"),
                hidden_states=runtime_out, option_logits=runtime_logits, action_logits=runtime_acts,
                marker_positions=positions, marker_mask=mask, native_option_logits=ref_logits,
                native_action_logits=ref_acts)
            report["cases"].append(row)
            save()
            print(json.dumps({k: row[k] for k in ("id", "status", "valid_length", "seconds")}), flush=True)
        report["status"] = "PASS" if all(r["status"] == "PASS" for r in report["cases"]) else "FAIL"
        save()
        print(json.dumps({"status": report["status"], "cases": len(report["cases"])}), flush=True)
        return 0 if report["status"] == "PASS" else 1
    except Exception as error:
        report.update(status="ERROR", error=type(error).__name__ + ": " + str(error))
        save()
        raise


if __name__ == "__main__":
    raise SystemExit(main())
