---
title: Image Analysis and Comparison
description: Configure region-free image workflows, compare against face and workwear libraries or a second image, and interpret decisions and errors.
---

# Image Analysis and Comparison

Image analysis executes the models, filters, rules, and output nodes configured in a scenario. Select a scenario and upload images to obtain its business decisions. For integration, see the [Image Detection API Guide](../reference/image-detection-api.md).

## Select a Scenario

On the **Scenario Tasks** page, set the **Data Source Type** query to **Image Analysis** to list image scenarios, or **Video Analysis** to list video scenarios. Select all to remove the type filter. The algorithm selector on the Image Analysis page lists image scenarios only.

| Need | Scenario choice | Input and result |
| --- | --- | --- |
| Detection, classification, OCR, or VLM judgment | The corresponding image scenario | Targets, text, or decisions for one image; batch uploads analyze each image independently |
| Compare with enrolled samples | Image face comparison or image no-workwear detection | One image against selected libraries, with business decisions and matching sample details |
| Compare two images | Two-image Face Comparison or Two-image Workwear Comparison | Image A and image B, producing one similarity score from 0 to 100 |

When creating a scenario, set its data source to **Image Analysis**. Check that its model status is normal before opening orchestration. Image tasks have no detection or exclusion regions, video tracking, duration, tripwire, or post-absence semantics. Target crops use detection boxes and require no drawn regions.

## Single Images and Library Comparison

1. For library comparison, create a group in the corresponding face or workwear library and enroll valid samples. A visible thumbnail alone does not establish that a sample has usable features.
2. Open **Image Analysis**, select the face-library or no-workwear scenario, and expand the analysis parameters.
3. Select one or more groups from the face or workwear group selector. Workwear scenarios require workwear libraries; face scenarios require face libraries.
4. Check detection confidence, minimum target size, and any exposed comparison threshold, upload images, and start analysis.
5. Read each image's business decision. A library match also displays the sample name, library, reference image, and comparison score. Click the reference image to preview it.

Library selectors are also available in scenario parameters and the **Image Library Match** node panel. The library type controls which selector appears; match mode controls whether the business rule accepts a library match or a library non-match. After saving scenario changes, reselect the scenario to load the latest parameters. Parameters entered on the Image Analysis page apply to that analysis and do not save new scenario defaults.

### Business Decisions and Library Matches

A business match means the configured scenario rule is satisfied. For **image no-workwear detection**:

| Evidence | Library result | No-workwear business decision |
| --- | --- | --- |
| Valid samples were compared and a workwear sample met the match condition | Library match | Not matched: the no-workwear rule did not fire; the reference image and score are still displayed |
| Valid samples were compared but the best score did not meet the match condition | Library non-match | Matched: the no-workwear rule fired |
| No valid samples could participate | Insufficient evidence | Unknown; this cannot establish that the person is not wearing workwear |

The `(0)` after the card title is the number of business-matched targets, not a similarity score. API business decisions are in `outputs[]`; library evidence is in `targetList[].matchInfo`. Similarity and detection confidence are different metrics.

### Unknown Results and Execution Failures

| Message | Meaning and action |
| --- | --- |
| No face/workwear library configured | No corresponding group was selected; configure a library and analyze again |
| Unknown: no valid library samples available for comparison | A binding was supplied, but zero samples produced usable comparisons. Check that the group exists, its features are nonempty and compatible with the current feature model, and score configuration is valid |
| Unknown: insufficient evidence | A rule lacks its required input. Enable debug details, analyze again, and inspect upstream outputs, filtering reasons, and rule inputs |
| Not matched | The business rule was not satisfied; interpret this in the context of the selected scenario |
| Analysis failed with a specific error | Model loading, decoding, features, or node execution failed; investigate the reported error instead of treating it as a non-match |

If even the enrolled image produces unknown, check the stored features and compatibility between enrollment and analysis models first. Lowering a threshold cannot repair the absence of valid samples. Re-enroll affected samples after fixing the model or feature extraction issue.

The debug-details switch applies to the next analysis. It exposes node timing, input/output counts, skipped nodes, and filtered targets. Check detection first, then filters, features, and library matching. API clients can also inspect `outputs[].reason` and `matchInfo.setPicCount`.

## Compare Two Images

1. Select **Two-image Face Comparison** (built-in ID `93000002`) or **Two-image Workwear Comparison** (`93000058`).
2. Upload image A and image B in their respective slots. Both are required before analysis can start.
3. Each image must contain exactly one eligible target after detection and filtering: a face for face comparison or a person for workwear comparison.
4. Leave the threshold empty for a score only. With a threshold from 0 to 100, a score greater than or equal to the threshold is matched; a lower score is not matched. Clear the threshold to return to score-only mode.
5. Start analysis and inspect both images, target crops, and the score. Replace images or change parameters to analyze again.

No library enrollment or binding is required. Both images use the same models and parameters to extract features before one comparison. Scores below the threshold, including a valid zero, are still returned.

No eligible target, multiple eligible targets, invalid features, or decoding failures produce a specific error and an error side, without a successful score. The current pair workflow supports one detector-to-feature chain, one **Two-image Feature Comparison** node, and one **Image Result Output** node. Branches, joins, and multi-target pairings are unsupported.

Scores represent calibrated model similarity. Workwear comparison reuses body appearance features, so changes in pose, background, and image quality can affect results. The score is not an identity probability or a probability of wearing the same uniform.

## Adjust Image Orchestration

Typical processing orders:

| Scenario | Node order |
| --- | --- |
| Detection with a rule | Image detection → target filter → optional classification → target rule → image result output |
| Face-library comparison | Face detection → filtering → landmarks → face feature extraction → library match → image result output |
| Workwear-library comparison | Person detection → filtering → optional quality classification/filtering → body features → library match → image result output |
| Two-image comparison | Detection/filtering/feature chain → two-image feature comparison → image result output |

The result output node provides an output name and a choice of matched targets or all unfiltered targets. Single-image business responses can also carry library-match evidence targets; use the output node's target references to identify business-selected targets. Image result output does not automatically create video events or send external notifications.

Image rules, conditional branches, whole-image/target classification, OCR, DINO, SAM, and VLM can be composed according to their input contracts. Single-image workflows support forks but not joins; pair workflows use the single-chain restriction above. Conditional branches run descendants only when matched, and negating an unknown decision does not turn it into a match.

## Built-in Templates and Scenarios

The BM1688, CV186X, and x86 resource directories each contain 13 image templates converted from video templates, with a built-in scenario of the same ID for each:

| ID | Image scenario |
| --- | --- |
| `92000002` | Face comparison |
| `92000006` | Person fall detection |
| `92000009` | Smoke detection |
| `92000010` | Flame detection |
| `92000011` | Smoking detection |
| `92000012` | Phone-call detection |
| `92000015` | No safety helmet |
| `92000022` | Mobile-phone use detection |
| `92000029` | License-plate recognition |
| `92000032` | No reflective vest |
| `92000058` | No workwear |
| `92000065` | Sleeping-on-duty detection |
| `92034707` | Vision-language model analysis |

Two additional pair templates and scenarios use IDs `93000002` and `93000058`. Original video templates are retained. Conversions describe a single-image state; workflows requiring regions, tripwires, post absence, duration, or historical counting are excluded.

These are packaged configurations. Required models must still be installed for the actual chip. After an upgrade, inspect the scenarios actually loaded by the device; existing custom scenarios need their own node and parameter review. See the [template generation instructions](../reference/image-detection-api.md#video-to-image-templates) for regeneration and checks.
