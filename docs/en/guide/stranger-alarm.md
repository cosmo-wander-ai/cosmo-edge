# Stranger alarms based on pedestrian tracks

The BM1688, CV186X and x86 resources provide a separate scenario, **66: Stranger alarm**. RK3576 and RV1126B packages inherit the x86 scenario templates, action metadata and bilingual locales before applying their target-specific model overlays. Scenario 2 retains its existing face match alarm output mode.

Each platform's `algorithm/` directory includes the installed scenario 66 configuration, while `algorithm_template/` retains its editing template. Both use the same workflow and defaults. After deployment, select Stranger alarm and bind the task's face groups and detection regions.

## Decision

A successful eligible face match permanently saves that pedestrian track from a stranger alarm. After the tracker stops emitting the track and the grace period expires, an alarm requires enough valid unmatched comparisons and sufficient observation time, with no successful match anywhere in the track.

Missing, small, poor-quality, excluded side faces, out-of-region targets and ambiguous face associations never count as failed matches. Missing or unsearchable selected face libraries, inference errors and long input interruptions suppress the affected track. A person with no observed face is not classified as a stranger.

Evidence is scoped to the tracker UUID. Short occlusions do not end observation while the tracker retains the track. ID switches begin fresh evidence; this scenario does not merge identities across tracks or cameras.

## Pipeline

Pedestrian detection → pedestrian tracking → child target detection and association (`AA_00006`) → existing face quality classifier → face landmarks → face recognition → result accumulation (`BA_20003`) → event reporting.

Child target detection and association (`AA_00006`) uses the existing detector pool to detect the selected labels in the full frame and associate them with a unique parent. The stranger preset selects the face model and `face` label, upper-half region and 0.9 minimum containment. It preserves pedestrian identities, regions and observations without a face. Overlapping people and multiple faces inside a body box are skipped conservatively.

`AA_00005` uses `outputMode=observations` to preserve complete recognition observations. Result accumulation (`BA_20003`) collects comparisons for each pedestrian track: any successful match suppresses the stranger alarm; otherwise, valid unmatched comparisons and observation duration determine the outcome after the track ends. The internal mode is `inputMode=recognition`; the editor no longer offers a mode selector. Historical behavior mode is retained for configuration compatibility and is not used by the current preset scenarios.

The template reuses model identifiers `1001003`, `1000001`, `1000012`, `1000016` and `1000005` for pedestrian detection, face detection, quality classification, landmarks and feature extraction. Install the matching models on the target before running the scenario. Shared identifiers do not make model binaries interchangeable: x86 uses ONNX, while Sophon and Rockchip require artifacts for their respective targets. The bundled public benchmark models do not include the complete face model chain. Synchronizing templates does not establish runtime availability, completed model conversion or device acceptance.

## Configuration

Select Stranger alarm and bind a camera, regions and face groups containing valid enrolled features.

| Parameter | Default | Meaning |
| --- | --- | --- |
| `param.minTargetSize` | 60 | Minimum face width and height, pixels |
| `param.detectionConfidence` | 0.66 | Face detection threshold |
| `param.minFaceQuality` | 60 | Existing face quality score threshold, 0–100 |
| `param.frontFaceOnly` | 1 | Use frontal faces only |
| `param.limitScore` | 0 | 0 uses library thresholds; otherwise a score threshold in 0–100 |
| `param.minValidFaceCount` | 3 | Minimum valid unmatched comparisons |
| `param.posSenDurationTime` | 3000 | Minimum track observation duration, milliseconds |
| `param.trackLostTimeoutMs` | 1000 | Additional grace period after the tracker stops emitting the track |
| `param.faceSampleIntervalMs` | 200 | Minimum interval between negative counts; success always counts |

Enter the minimum observation duration directly in milliseconds: 3000 means 3 seconds. New configurations use an internal duration multiplier (`param.posSenDurationTimeType`) of 1, with no multiplier input in the editor; editing existing configurations preserves their internal values. Pedestrian detection runs at 5 FPS and downstream actions forward every observation. Area alarm throttling and position suppression are disabled so simultaneous departures can generate separate reports. Each track reports at most once per matched region, with its highest-quality valid unmatched frame.

Task restarts, video session changes, selected group changes and recognition quality or match threshold changes discard unfinished evidence. Track completion alarms have inherent latency and do not promise an immediate alert while a person remains visible.

## Child target detection and association

Use parent detection → parent tracking → child target detection and association → downstream classification, landmarks or judgment. Select parent labels in the upstream detector, then select the related-target detector and labels in Child Target Detection and Association. Child targets inherit the parent track identity instead of being tracked separately.

| Setting | Meaning |
| --- | --- |
| Related-target detector and labels | Only checked labels participate; an empty selection produces no usable association |
| Association region | The child center must lie in the whole, upper half or lower half of the parent; new nodes default to whole |
| Minimum containment | Fraction of child area inside the parent box, default 0.9; this is not intersection-over-union |
| Minimum related-target size and detection confidence | Task parameters, default 60 pixels and 0.66; adjust for the model and image |

For example, a helmet detector and upper-half rule can attach helmet detections to pedestrian tracks; a plate detector can attach plates to vehicle tracks. Selected labels must exist in the model, and downstream nodes need matching models. The scope is same-frame, spatial, unambiguous one-to-one association. It does not include cross-camera or cross-track identity merging, one-to-many association or behavior inference. Missing associations do not directly generate absence alarms such as missing helmets.

A child matching multiple parents, or multiple candidates matching one parent, is skipped. Internal results distinguish matched, missing, ambiguous, undersized, upstream-filtered and failed observations. Missing faces never count as failed recognition. Downstream classification, landmarks and judgment consume usable associated targets while parent tracks remain available for accumulation and alarms.

The action ID remains `AA_00006`. Legacy `param.minFaceSize` and `param.faceDetectionConfidence` are accepted. Old configurations without association rules retain `face`, upper-half and 0.9 defaults. Editing a legacy node preserves its thresholds and saves the generic parameter names. The stranger preset retains its other recognition and alarm rules.

## Validation

On a configured Linux CPU development environment:

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests "[stranger],[association]"
```

`test_target_association.cc` covers generic pairing, regions, containment, label selection, failure states and legacy parameters. `test_stranger_alarm.cc` covers association, quality and evidence rules. `test_stranger_alarm_flow.cc` covers action and queue integration, separate snapshots, successful matches, failed input and session resets. CPU tests do not establish target-device acceptance. Check known people, strangers, side faces, occlusion, overlapping people, empty frames and stream interruptions before deployment, measuring false alarms, misses and latency.
