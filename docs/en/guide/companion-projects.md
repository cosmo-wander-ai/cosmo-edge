# Companion Projects: Training Skill and Connect

The CosmoEdge engine and console handle video access, model execution, visual orchestration, and alarms.
Companion projects assist with model customization and site operations. They are installed, maintained,
and released separately.

| Your task | Start here | Current version |
| --- | --- | --- |
| Deploy video AI applications and configure devices and scenes | [Quick Start](../tutorials/01-quickstart/quickstart.md) | CosmoEdge stable `v1.1.0`; main includes unreleased changes |
| Customize an object detector from data | [CosmoEdge Training Skill](https://github.com/cosmo-wander-ai/cosmoedge-training-skill) | Published `v0.1.0-preview.1`; later revisions are available on that repository's `main` |
| Let an AI assistant query alarms, retrieve images, and request confirmed detection start/stop | [CosmoEdge Connect](https://github.com/cosmo-wander-ai/cosmoedge-connect) | `v0.1.0-alpha.1`, Alpha source preview |

## Start from Data: Training Skill

Give your existing AI development assistant the business goal, full available dataset, and computing
resources. The Skill helps the assistant inspect data and annotations, plan training, resolve execution
problems, compare models, and return models, configuration, false-positive/false-negative examples, and
validation results. Existing labels, models, and training projects can be reused.

You need an assistant that can read files, run programs, and inspect images, plus data and computing
resources for the task. See the [Training Skill README](https://github.com/cosmo-wander-ai/cosmoedge-training-skill)
for installation and task examples. Training, evaluation, and export can be completed independently.
CosmoEdge application validation additionally checks the target platform's model format, configuration,
runtime, and business results. The repository's conversion entry is described in
[Agent-Assisted Development](../development/agent-assisted-development.md).

As of 2026-10-09, published assets remain
[`v0.1.0-preview.1`](https://github.com/cosmo-wander-ai/cosmoedge-training-skill/releases/tag/v0.1.0-preview.1).
The September 28 model-integration revision is on `main`; it adds guidance for deriving configuration
from the candidate model, reading it back after saving, and diagnosing packaging issues. Earlier release
assets have not been replaced. Synthetic scenario checks for this revision do not establish real BMRT,
target-device performance, or business accuracy. See the
[September 28 validation record](https://github.com/cosmo-wander-ai/cosmoedge-training-skill/blob/main/docs/validation/20260928/README.md).

## Use an Existing Device: Connect and Local MCP

Connect runs on your computer and lets an AI assistant access one deployed CosmoEdge device and its
existing camera views through local MCP or WorkBuddy. Start with a concrete task: query retained alarms
for a date, retrieve an image from a selected camera, or confirm a pause and later resumption of existing
detection during maintenance.

The assistant organizes queries, analyzes images, and prepares results. Connect returns execution results,
original reports, and images. Each start or stop requires human confirmation on a local page. Device
connection details and credentials are entered on the local connection page.

- [Installation and building](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/installation.md)
- [MCP integration](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/mcp.md)
- [First connection and operations](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/getting-started.md)
- [Changelog](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/CHANGELOG.md) and [compatibility scope](https://github.com/cosmo-wander-ai/cosmoedge-connect/blob/main/docs/compatibility.md)

The current version is an Alpha source preview. Image review uses on-demand capture and the host's model;
continuous video detection runs in the CosmoEdge engine. Scheduled checks and notifications require
integration and validation in a host that provides those capabilities. Consult Connect's compatibility
record for supported clients and platforms.

## Match Versions and Validation

The three projects have separate releases. Installing the training Skill or Connect does not upgrade
CosmoEdge on the device or mean that v1.1.0 packages include training or MCP services. Check the dependencies
and interface requirements in each project's documentation before use.

See [CosmoEdge Unreleased](https://github.com/cosmo-wander-ai/cosmo-edge/blob/main/CHANGELOG.md#unreleased)
for main-branch additions. Model integration and device validation must match the source, package, and
model actually used; historical benchmarks describe only their recorded conditions.
