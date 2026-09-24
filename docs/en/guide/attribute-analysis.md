# Object attribute analysis

Attribute analysis combines classifier outputs for one tracked object into one event, with details, combined filters, statistics and CSV export. It reuses detection, tracking, classification, result accumulation and event reporting; no new inference model type is required.

## Configure scenes and channels

1. Create a video scene task with the **Attribute Analysis** category.
2. Connect **Decode → Detect → Track → Classifier A → Classifier B → Result accumulation → Event reporting**. Use `AA_00002` classifiers sequentially. Branch fan-in is not an evidence merge and is not supported here.
3. Set the accumulator purpose to **Object attributes**. Use the selectors for object type, classifier source, attribute type (single value, multiple values or yes/no), confidence threshold and required vote share. Model labels come from the selected classifier; internal attribute and value keys are generated automatically. Display names are filled in and can be selected or customized. Use Custom threshold and vote share for other numeric values.
4. For single/multiple-value attributes, map raw model labels to value keys and display names, for example `wearing_hat → yes → Wearing a hat` and `no_hat → no → No hat`. For a behavior represented by a positive score, select **Yes / No**, bind one model label and configure both outcome keys and names, such as Calling / Not calling. Missing model output is still not a negative.
5. Save the scene, bind channels and configure detection areas. Channel tasks can adjust minimum valid observations, observation duration, disappearance timeout and sampling interval. The scene owns attribute definitions.

Install compatible classifier models first. This feature does not bundle general human-attribute models. Each model's labels require their own mappings. One classifier can supply several attributes; separate nodes using the same model retain separate evidence. A scene supports one attribute accumulator.

Defaults are 3 valid samples, 3000 ms observation duration, 1000 ms disappearance timeout and 200 ms sampling interval. Duration runs from first to last observation. Short tracks produce no record; eligible tracks with insufficient attribute evidence produce a partial record.

## One record per trajectory

The accumulator uses the tracker UUID. A task-session identifier contributes to the record ID, which is inserted idempotently. Multiple visited areas belong to the same record. Disappearance starts only once the tracker stops emitting the target.

A single-value attribute chooses the unique highest-scoring mapped label above its threshold in each observation; tied outputs do not count. Multiple-value attributes count each eligible label. Values become valid once the sample minimum and vote-share threshold are met. Unmapped or low-confidence outputs are not valid samples. For multiple-value attributes, the denominator is also the number of valid samples containing mapped labels.

Yes/no attributes require a positive score on each successful inference. Scores at or above the threshold vote positive; lower scores vote negative. Both count as valid samples and contribute to the vote-share denominator. Missing labels, non-finite or out-of-range scores, filtered targets and failed inference do not produce negative votes. An outcome becomes valid when the minimum sample count and required vote share are met; otherwise it remains **Unknown**. This type requires complete scores and is unsuitable for outputs that already discard low-scoring labels.

Missing or insufficient evidence is **Unknown**. With no valid samples and an observed inference failure, the result is **Inference failed**. Neither becomes an implicit negative. Confidence is the mean confidence of winning-label observations, averaged across winning values for multiple-value attributes. For yes/no attributes, positive votes use the original score and negative votes use `1 - score`; this aggregate score is not a calibrated probability.

Failed frames do not prove departure. Stopping/restarting tasks, changing accumulation settings or regions, stream-session changes and long input gaps discard pending records. A target present at the last frame of a file is not automatically flushed at EOF; subsequent healthy frames must prove departure. At most 256 unfinished tracks retain evidence simultaneously; additional new tracks are temporarily excluded.

An object means a trajectory within one task run. Re-entry, tracker ID switches, restarts and different channels may produce new records. There is no identity recognition or cross-channel deduplication.

## Event center

Open **Event Center → Attribute Analysis** and select one scene, any number of channels and a time range. No channel selection means all channels for that scene. Columns follow the attribute definition; details show values, status, sample counts and confidence.

Records contain immutable schema snapshots and version fingerprints. Changes to names, sources, mappings or thresholds create a new version. Historical versions remain selectable and their statistics are kept separate. Attribute filters are combined with AND; a multiple-value filter matches membership of a selected value.

Existing single-value behavior attributes must be changed to **Yes / No** in the scene editor, given negative display names and saved before new records use the new rules. Historical unknown records lack per-frame scores and are not converted to negative values. Negative values support filtering, statistics, details and CSV export like positive values.

Statistics cover all matching records, independent of pagination. Shares use objects with valid results for that attribute as the denominator; unknown and failed results have separate counts. Multiple-value shares may total more than 100%. CSV export uses the last executed query and historical display names.

## API

Attribute events use category `12` and the existing HTTP/MQTT event delivery. `property.attributes` contains `recordId`, `trackId`, `schemaId`, `schema`, `firstSeen`, `lastSeen`, `areaIds` and `attributes`. Each attribute result contains `key`, `status` (`valid`, `unknown`, `failed`), `values`, `confidence` and `samples`.

Yes/no definitions use `type: "binary"` and exactly two `options`. The first is positive and binds the model label; the second is negative and has an empty `label`. Each has a distinct `value` key and a display `name`, for example `[{"label":"calling","value":"yes","name":"Calling"},{"label":"","value":"no","name":"Not calling"}]`.

Event page/export requests accept `channelIds`, `attributeSchemaId` and `attributeFilters`. Each filter contains a `key` and a `value` or `status`. Page requests with `includeAttributeSummary=true` return `resData.attributeSummary.schemas` and `statistics`. Attribute queries require exactly one `algorithmCodes` entry, `categorys:["12"]` and a valid time range; attribute filters also require a schema version.

Update the frontend and device service together. Unit tests and x86 builds do not establish model accuracy or target-device acceptance. Evaluate actual classifiers with representative scene recordings.
