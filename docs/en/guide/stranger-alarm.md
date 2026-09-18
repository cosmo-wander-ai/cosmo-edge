# Stranger alarms based on pedestrian tracks

The BM1688, CV186X and x86 resources provide a separate scenario, **66: Stranger alarm**. RK3576 and RV1126B packages inherit the x86 scenario templates, action metadata and bilingual locales before applying their target-specific model overlays. Scenario 2 retains its existing face match alarm output mode.

Each platform's `algorithm/` directory includes the installed scenario 66 configuration, while `algorithm_template/` retains its editing template. Both use the same workflow and defaults. After deployment, select Stranger alarm and bind the task's face groups and detection regions.

## Decision

A successful eligible face match permanently saves that pedestrian track from a stranger alarm. After the tracker stops emitting the track and the grace period expires, an alarm requires enough valid unmatched comparisons and sufficient observation time, with no successful match anywhere in the track.

Missing, small, poor-quality, excluded side faces, out-of-region targets and ambiguous face associations never count as failed matches. Missing or unsearchable selected face libraries, inference errors and long input interruptions suppress the affected track. A person with no observed face is not classified as a stranger.

Evidence is scoped to the tracker UUID. Short occlusions do not end observation while the tracker retains the track. ID switches begin fresh evidence; this scenario does not merge identities across tracks or cameras.

## Pipeline

Pedestrian detection → pedestrian tracking → face association (`AA_00006`) → existing face quality classifier → face landmarks → face recognition → result accumulation (`BA_20003`) → event reporting.

`AA_00006` uses the existing detector pool to detect faces in the full frame and associate them unambiguously with pedestrian upper bodies. It preserves pedestrian identities, regions and observations without a face. Overlapping people and multiple faces inside a body box are skipped conservatively.

`AA_00005` uses `outputMode=observations` to preserve complete recognition observations. Result accumulation (`BA_20003`) collects comparisons for each pedestrian track: any successful match suppresses the stranger alarm; otherwise, valid unmatched comparisons and observation duration determine the outcome after the track ends. The internal mode is `inputMode=recognition`; the editor no longer offers a mode selector. Historical behavior mode is retained for configuration compatibility and is not used by the current preset scenarios.

The template reuses model identifiers `1001003`, `1000001`, `1000012`, `1000016` and `1000005` for pedestrian detection, face detection, quality classification, landmarks and feature extraction. Install the matching models on the target before running the scenario. Shared identifiers do not make model binaries interchangeable: x86 uses ONNX, while Sophon and Rockchip require artifacts for their respective targets. The bundled public benchmark models do not include the complete face model chain. Synchronizing templates does not establish runtime availability, completed model conversion or device acceptance.

## Configuration

Select Stranger alarm and bind a camera, regions and face groups containing valid enrolled features.

| Parameter | Default | Meaning |
| --- | --- | --- |
| `param.minFaceSize` | 60 | Minimum face width and height, pixels |
| `param.faceDetectionConfidence` | 0.66 | Face detection threshold |
| `param.minFaceQuality` | 60 | Existing face quality score threshold, 0–100 |
| `param.frontFaceOnly` | 1 | Use frontal faces only |
| `param.limitScore` | 0 | 0 uses library thresholds; otherwise a score threshold in 0–100 |
| `param.minValidFaceCount` | 3 | Minimum valid unmatched comparisons |
| `param.posSenDurationTime` | 3000 | Minimum track observation duration, milliseconds |
| `param.trackLostTimeoutMs` | 1000 | Additional grace period after the tracker stops emitting the track |
| `param.faceSampleIntervalMs` | 200 | Minimum interval between negative counts; success always counts |

Enter the minimum observation duration directly in milliseconds: 3000 means 3 seconds. New configurations use an internal duration multiplier (`param.posSenDurationTimeType`) of 1, with no multiplier input in the editor; editing existing configurations preserves their internal values. Pedestrian detection runs at 5 FPS and downstream actions forward every observation. Area alarm throttling and position suppression are disabled so simultaneous departures can generate separate reports. Each track reports at most once per matched region, with its highest-quality valid unmatched frame.

Task restarts, video session changes, selected group changes and recognition quality or match threshold changes discard unfinished evidence. Track completion alarms have inherent latency and do not promise an immediate alert while a person remains visible.

## Validation

On a configured Linux CPU development environment:

```bash
bash scripts/build_cpu_test.sh
./build_cpu/cosmo-tests "[stranger]"
```

`test_stranger_alarm.cc` covers association, quality and evidence rules. `test_stranger_alarm_flow.cc` covers action and queue integration, separate snapshots, successful matches, failed input and session resets. CPU tests do not establish target-device acceptance. Check known people, strangers, side faces, occlusion, overlapping people, empty frames and stream interruptions before deployment, measuring false alarms, misses and latency.
