# CosmoEdge Agent Guide

This file gives coding agents stable repository facts. Customer- or task-specific
information belongs only in the ignored `output/agent-runs/` directory. These
rules support agent-assisted work without changing the normal contributor
workflow.

## Branch naming

- Do not use the `codex/` prefix for new branches in this project.
- Use purpose-based names such as `feat/gb28181-onvif-access`, `fix/<topic>`,
  or `docs/<topic>`. Follow an explicit user-provided branch name.
- Do not rename unrelated existing branches unless the user requests it.

## Repository map

- `src/` — C++ engine, split by subsystem.
- `src/web/` — Vue 3 frontend.
- `docs/` — VitePress site; Chinese pages are at the root and English mirrors
  are under `docs/en/`. Navigation is in `docs/.vitepress/config.mts`.
- `scripts/` — build, run, validation, and packaging scripts.
  `scripts/agent/` contains environment admission, measured evidence, and
  implemented task-specific executors.
- `tools/` — Python and Node validation utilities.
- `test/` — Catch2 tests and the HTTP/MQTT push test service.
- `data/resource/aiboxresource_bm1688/`, `data/resource/aiboxresource_cv186x/`,
  and `data/resource/aiboxresource_x86/` — platform-scoped Sophon and x86 model
  resources and templates.
- `3rd/` and `prebuild/` — third-party and prebuilt dependencies; do not edit.

## Mandatory agent-assistance trigger

For a task that combines customer/task model material with model conversion,
remote execution, model transfer, or test-device work, activate this repository's
workflow before the first external or environment-changing command:

1. Restate the business outcome and intended deliverables, then run
   `scripts/agent/start.sh` (or `start.ps1`) with the objective and named
   materials. This creates the private task contract and route assessment; the
   customer does not write JSON or select an implementation recipe.
2. Resolve `needsInput`. A user message that supplies a development host and
   credentials and asks the agent to connect/check/develop is already explicit
   task-scoped `remote-execution` authority; when model material is named for
   that host it also grants `model-transfer`. Pass
   `--user-requested-remote-access` to `start` and do not ask the same question
   again. If intent is unclear, ask one natural-language question, then use
   `authorize.sh --confirmed-by-user` and reassess.
3. A `READY` assessment is required before task `doctor`; that admitted doctor
   report is required before conversion; both are rechecked by verification.
   Changing the contract or assessment invalidates downstream admission.

After the private record and explicit remote grant exist, use `connect.sh` (or
`connect.ps1`) to open interactive SSH; OpenSSH receives the password through
its terminal prompt and the repository stores only a sanitized event. Invoke it
in an interactive terminal/PTY and answer the prompt without repeating the
password in commentary or a command. Raw `ssh`,
`scp`, installation, `sudo`, conversion, or deployment before the record/grant is
outside the assisted workflow. This trigger does not alter ordinary local
repository development that needs none of these task-specific capabilities.

## Working order

1. Start from the user's business outcome, available materials, target context,
   and acceptance. Restate the intended deliverables in ordinary language.
   Do not ask the user to choose a chip flag, precision, compiler version,
   container, script, or repository example when those facts can be inspected
   or recommended from the material, target device, repository, and current
   official documentation.
2. Use `./scripts/agent/start.sh --objective ...` to create the private task
   contract under `output/agent-runs/<run-id>/` and perform the first assessment.
   The contract is an agent-owned execution record, not a customer form. Use
   `assess.sh --contract ...` again after task or authority changes. Translate
   `needsInput` into one concise user question; do not expose internal JSON for
   confirmation.
3. Run `./scripts/agent/doctor.sh --baseline` for a local read-only inventory.
   If the user supplied a remote development machine and explicitly requested
   connection, open it through `connect.sh` even when other business input is
   still unresolved, inspect it read-only, and run task admission on the machine
   that will actually execute the work.
4. Select relevant tutorials, examples, templates, tests, and code facts. When
   reusing an example or template, record why it applies and how the task
   differs. A direct code or documentation task may record these sources in its
   evidence instead of creating a separate selection file.
5. Treat an upstream-supported route as eligible to try, not as proof that the
   current machine is ready. Admit by required capabilities and callable tools;
   after admission, freeze the actual Python/package/image/tool identities and
   commands used by this run. Do not require one globally fixed recipe.
6. Execute only inside the granted scope. Infer no authority from credentials
   alone, but treat credentials plus an explicit connect/check/develop request
   as confirmation and record it without another round trip. Otherwise use
   `authorize.sh`, then reassess. Use four coarse gates when applicable:
   `environment-change`, `remote-execution`, `model-transfer`, and
   `device-deployment`. Keep the exact target, read/write scope, impact, and
   recovery plan in the task context without turning every implementation
   detail into a blocking standard.
7. Validate the deliverables requested by this task. Repository examples are
   reusable evidence, not universal target shapes, hashes, chips, or outputs.

## Minimum checks by change type

| Change | Minimum check |
| --- | --- |
| C++ | `bash scripts/build_cpu_test.sh`, then `./build_cpu/cosmo-tests`; before submission run `scripts/format_check.sh` |
| Documentation | `npm run docs:verify` |
| Frontend | `cd src/web && npm ci && npm run build` |
| Sophon model conversion selected by assessment | `./scripts/agent/start.sh --objective ... --material ...`, resolve and reassess any `needsInput`, then on admitted Linux run `doctor.sh --task model-conversion --contract ...`, `convert_model.sh --contract ...`, and `verify.sh --contract ...` |
| Other task | Use its task contract and the closest native test commands; list anything not verified |

## Engineering boundaries

- The inference backend is selected at compile time. x86 uses ONNX Runtime CPU
  and loads `model.onnx` directly. Sophon uses BMRT; a `.bmodel` is packaged
  inside `model.nn` (CENN) and cannot be turned into one by renaming.
- BM1688 and CV186X artifacts are not interchangeable. A chip name, compiler
  argument, runtime identifier, and artifact mapping must come from current
  repository facts or independent measured evidence. Doctor enforces this
  boundary through the `compatibility-matrix` check: an explicit mismatch fails,
  while missing facts remain `UNVERIFIED`.
- ONNX-to-bmodel conversion depends on an external, locally admitted TPU-MLIR
  package. Follow a route supported by current upstream instructions, but do not
  confuse route eligibility with local readiness. An optional `sophgo/tpuc_dev`
  image is only a base environment and is not the compiler. Accept compatible
  official layouts, then freeze the complete resolved package and command
  identity for the run.
  BM1688/F16 is the first preferred measured path. The example promotion
  mechanism is **Beta**. An entry is eligible for official selection only when
  its status is `conversion-verified`, its lifecycle is `active`, two real
  recordings pass the promotion rules, and its verification seal remains valid.
  Revocation changes lifecycle eligibility without deleting historical
  recordings or seals. An empty index is not a success claim and does not make
  unrelated development unsupported.
- `.pt` and `.pth` training artifacts are accepted as task materials, but this
  release does not claim an automatic training-framework-to-ONNX executor.
  Assess that export as a separate stage or request a suitable ONNX; do not
  improvise an export command and call it the supported conversion path.
- x86 or mock success is not Sophon-device or production acceptance. Report
  conclusions by the layer actually tested.
- Preparing an upstream change to `src/nn/`, `src/infer/`, model templates,
  public APIs, new third-party dependencies, or a broad architecture requires
  the project's normal issue and review process. An authorized customer fork
  may continue locally but must identify the divergence.

## Safety and evidence

- Development-machine authority covers only the workspace, commands, and writes
  explicitly granted by the user. It does not imply software installation,
  administrator access, device access, or production access.
- `--confirmed-by-user` and `--user-requested-remote-access` record authority
  already expressed in the current interaction; neither lets the agent invent
  authority. Supplying credentials without an action request is not enough.
- A user may provide an isolated development host, account, and password in the
  current private task interaction after the risk is stated. Use them only for
  the immediate interactive OpenSSH prompt. Never echo them, pass the password
  as a command argument/URL/environment variable, or persist raw connection
  data in the contract, run events, evidence, documentation, or Git. Sanitize
  record text instead of blocking the requested connection.
- Customer materials explicitly put in scope may be copied only into the
  current private, ignored run when the task needs them. Never place private
  streams, customer data, device serial numbers, private models, or new model
  binaries in repository-tracked paths or evidence text.
- Create run directories with private permissions. Redact credential-like
  arguments, environment variables, and URL user information before recording
  commands. Work only in the current run directory; isolate workspaces on
  shared or multi-customer machines.
- Unknown or untested facts remain unverified. Never replace them with guesses
  or promote a local result to a device or production claim.
- Windows can host the agent, repository inspection, material preparation, and
  route assessment. The Sophon TPU-MLIR conversion route executes in an
  isolated Linux x86_64 environment. Guide the user to local/remote Linux or an
  explicitly experimental compatibility layer; do not label CosmoEdge itself
  unsupported merely because this compiler ecosystem is Linux-oriented.
- A development-machine connection does not authorize `sudo`, dependency
  installation, global configuration changes, production access, or test-device
  writes. Ask only when one of those additional actions becomes necessary.
- Preserve prior failed attempts and data-flow status. A later unverified run
  must not erase an earlier failure; a measured pass may supersede it, or the
  evidence must record an explicit user-confirmed waiver.
- A missing named environment is not automatically a customer-machine defect.
  Recheck the repository instruction, its upstream source, and whether it names
  a base environment or a complete tool before requesting an installation. If
  the instruction is wrong, fix it and rerun the same admission and execution
  checks first.
- Normal task evidence only needs the environment summary, deliverables,
  redacted commands, observed results, and unverified boundaries needed for the
  user's acceptance. Official examples and release claims additionally require
  a frozen commit/tree, input and toolchain identities, artifact hashes,
  applicability, and repeat recordings.
- Documentation or pull requests that cite an official example or make a
  compatibility claim must include its verification-seal short code and refer
  to an `active` example. The short code locates a deterministic evidence-chain
  fingerprint; its presence alone does not prove that the seal is still valid.
  Treat revoked, missing, or invalidly sealed claims as unverified. A seal is not
  a cryptographic signature or device-acceptance claim.
