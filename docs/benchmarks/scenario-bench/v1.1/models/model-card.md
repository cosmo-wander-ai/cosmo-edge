# Model identity

The small-model benchmark uses a 640×640 person detector and a 224×224 helmet classifier. The BM1688 and CV186X artifacts are byte-identical. RK3576 and RV1126B use platform-specific RKNN artifacts with the same public input/output contracts.

| Platform | Public model | Version | Format | SHA-256 | Size |
| --- | --- | --- | --- | --- | ---: |
| BM1688 | YOLOv8n person detector | V1.0.0 | `.nn` | `56b207ef…b6c5c8` | 7,023,600 B |
| BM1688 | Helmet classifier | V1.0.0 | `.nn` | `33b0fb4b…cd7c8a` | 6,001,416 B |
| CV186X | YOLOv8n person detector | V1.0.0 | `.nn` | `56b207ef…b6c5c8` | 7,023,600 B |
| CV186X | Helmet classifier | V1.0.0 | `.nn` | `33b0fb4b…cd7c8a` | 6,001,416 B |
| RK3576 | YOLOv8 person detector | V1.0.0 | `.rknn` | `26ed82e0…541e0` | 6,305,107 B |
| RK3576 | Helmet classifier | V1.0.0 | `.rknn` | `471d1de3…d67ea` | 3,024,125 B |
| RV1126B | YOLOv8 person detector | V1.0.0 | `.rknn` | `db6ddca0…396cf` | see platform JSON |
| RV1126B | Helmet classifier | V1.0.0 | `.rknn` | `9468f6a4…d9eeb` | see platform JSON |

The no-safety-helmet workload is a two-stage detector-plus-classifier pipeline. Both stages receive the requested 24, 10, 7, or 5 FPS setting.

VLM identities remain in the BM1688, CV186X, and RK3576 platform JSON files. The formal validation also freezes each installed model, tokenizer, config, runtime, and candidate-package hash in `results/vlm-observations.json`. The RK3576 RKLLM model SHA-256 was recomputed after candidate installation. RV1126B is outside this VLM validation.

Model binaries are not redistributed by this benchmark. Full hashes and contracts are recorded in `bm1688.json`, `cv186x.json`, `rk3576.json`, and `rv1126b.json`.
