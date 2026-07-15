# Development workflow (GitHub + Cursor)

Astrolabe uses **GitHub Issues** for trackable work, **one branch per issue**, and **[`docs/BACKLOG.md`](BACKLOG.md)** as the product roadmap. **Cursor Cloud** (and local Cursor agents) implement from an issue on a dedicated branch, open a PR into **`integration`**, and may merge when CI is green. Firmware on **`main`** is updated only after **build + on-device flash** (promotion). GitHub Pages is a site deploy and may publish from `integration`.

## Branches

| Branch | Role |
|--------|------|
| **`integration`** | Default merge target for issue PRs; CI build on every PR/push |
| **`main`** | Firmware release line — must always build and have been flashed before firmware promotion |
| `feature/<#>-slug` / `fix/<#>-slug` | One issue per branch; branch **from** `integration` |

```text
fix|feature/<#>-slug  ──PR──►  integration  ──promote──►  main
```

**Promote** (human or operator after hardware QA on `integration`):

```bash
git fetch origin integration
# flash integration firmware on watch, smoke-test
./scripts/promote-integration.sh --flash-ok
```

GitHub: set the repo **default branch for pull requests** to **`integration`** (Settings → General → Pull Requests).

**Branch protection (recommended)** on `integration`:

- Required status checks: **Firmware build** + **Integration sim gate** (`ENABLE_INTEGRATION_SIM_GATE=true`)
- **Integration device gate** runs on m1 after each build but is **not** a merge requirement — it gates promotion to **`main`**
- **Deploy GitHub Pages** publishes the static site from `docs/` on pushes to **`integration`** or **`main`**; this is exempt from the hardware flash gate because it does not change device firmware.

## Roles

| Artifact | Role |
|----------|------|
| [`docs/BACKLOG.md`](BACKLOG.md) | Roadmap: priorities, specs, done history |
| **GitHub Issue** | Actionable unit of work; discussion; links PRs |
| **Branch** | Isolated implementation (`feature/…` or `fix/…`) |
| **Pull request** | Into **`integration`**; CI; `Closes #N` |

When you **start** a backlog item, **open or claim an issue** (do not implement large features only in markdown).

## Starting work

1. Pick an item in [`BACKLOG.md`](BACKLOG.md) (or create an issue first for new ideas).
2. **Create a GitHub issue** (templates: Feature / Bugfix) with acceptance criteria and link to the backlog section.
3. **Branch** from `integration`:
   ```bash
   git fetch origin
   git checkout integration && git pull
   git checkout -b feature/42-short-slug   # or fix/42-short-slug
   ```
   Use the issue number: `feature/<#>-<kebab-slug>`.
4. In **BACKLOG**, move the line to **In progress** and add `Issue: #42`.
5. Implement; **commit often** (one concern per commit). See [`.cursor/rules/git-workflow.mdc`](../.cursor/rules/git-workflow.mdc).

## Cursor Cloud agents from issues

You can run a **Cloud Agent** per GitHub issue (parallel, no local machine required). The agent clones the repo, works on a branch, and opens a PR.

### One-time setup

1. **Paid Cursor plan** with Cloud Agents enabled.
2. **[GitHub integration](https://cursor.com/dashboard/integrations)** — install the Cursor GitHub App on **CastaliaInstitute/astrolabe** with read/write (repo, PRs, issues).
3. **[Cloud Agents dashboard](https://cursor.com/dashboard/cloud-agents)** — spend limit, “post artifacts to GitHub” on PRs, secrets for CI (not `secrets.local.h`).
4. **Base branch = `integration`** (required for ad-hoc Cloud agents):
   - [Cloud Agents → Default settings](https://cursor.com/dashboard/cloud-agents) → **Base branch** → `integration` for **CastaliaInstitute/astrolabe** (or your team default if this repo is the only one).
   - If Base branch is blank, Cursor uses the GitHub repo default (`main` today), which is wrong for issue work.
   - `./scripts/cloud-agent.sh` already sets `startingRef: "integration"` in the API payload; the dashboard setting covers runs started from the UI or issue comments without the script.

Cloud agents read [`AGENTS.md`](../AGENTS.md) and rules in [`.cursor/rules/`](../.cursor/rules/).

**GitHub (repo admin, optional):** Settings → General → Pull Requests → default base branch **`integration`**. You can keep **`main`** as the repository default branch (release line) and only change the PR default; Cursor’s **Base branch** field is the important one for agents.

### Start an agent from the CLI (recommended)

```bash
# One-time: https://cursor.com/dashboard/integrations → API key
cp scripts/cloud-agent.env.example scripts/cloud-agent.env.local
# edit CURSOR_API_KEY=...

./scripts/cloud-agent.sh 2              # issue #2 → cloud agent + PR
./scripts/cloud-agent.sh 2 --watch      # stream run events
./scripts/cloud-agent.sh 2 --dry-run    # preview payload only
./scripts/cloud-agent.sh --agent bc-… --status
```

The script uses `gh` to read the issue title/body, picks `fix/<#>-<slug>` or `feature/<#>-<slug>`, and calls `POST https://api.cursor.com/v1/agents`. It prints the **cursor.com/agents** URL when done.

### Other ways to start

| Method | How |
|--------|-----|
| **GitHub comment** | `@cursor` on the issue (requires GitHub app). |
| **cursor.com/agents** | Paste issue URL manually. |
| **API / SDK** | `@cursor/sdk` — same API the script uses. |

### Prompt template (copy into issue comment or Cloud agent)

```text
Implement GitHub issue #<N> for CastaliaInstitute/astrolabe.

Branch: fix/<N>-<slug>   (or feature/<N>-<slug>)
Read: issue body, docs/BACKLOG.md, docs/WORKFLOW.md, docs/pocketwatch.md (if needed).
Rules: .cursor/rules/backlog.mdc, git-workflow.mdc, github-workflow.mdc, integration-branch.mdc.

Deliverables:
- Code + ./scripts/build.sh passes
- docs/BACKLOG.md updated (In progress → Done with PR link)
- PR against integration with "Closes #<N>"; merge when Firmware build + Integration sim gate are green (single issue only)
- Do not commit secrets; do not merge to main (promotion needs hardware gate)

Hardware: cloud agents cannot flash the watch. Sim gate (QEMU) covers merge; bench flash/screen.bmp is for promotion to main or optional QA — not a merge blocker.
```

### What Cloud Agents can / cannot do here

| Can | Cannot (you do locally) |
|-----|-------------------------|
| Edit firmware, open PR, fix CI | Flash watch, serial monitor, JTAG |
| Run `./scripts/build.sh` in VM | `screen.bmp` unless Wi‑Fi + device on your LAN |
| Attach screenshots/logs to PR | Castalia sign-in on physical device |

Merging to **`integration`** does **not** require the bench watch — **Integration sim gate** (QEMU) is the automated gate. Run **hardware QA** on **`integration`** before **promoting** to **`main`** ([`hardware-qa.mdc`](../.cursor/rules/hardware-qa.mdc)).

GitHub Pages deploys are separate from firmware promotion: docs/site-only changes
publish from **`integration`** via the Pages workflow and do not require a watch
flash.

### CI

| Workflow | Runner | What |
|----------|--------|------|
| [Firmware build](../.github/workflows/firmware-build.yml) | `ubuntu-latest` | `./scripts/build.sh`; uploads `firmware.bin` artifact |
| [**Integration sim gate**](../.github/workflows/integration-sim-gate.yml) | `ubuntu-latest` | QEMU serial tests — **required for merge to `integration`** |
| [**Integration device gate**](../.github/workflows/integration-device-gate.yml) | **`self-hosted` + `astrolabe-watch`** (m1) | Flash → full hardware functional test — **required for `main` promotion**, not merge |
| [Firmware flash](../.github/workflows/firmware-flash.yml) | **`self-hosted` + `astrolabe-watch`** | Manual / legacy `ENABLE_INTEGRATION_FLASH` only |
| [Firmware functional test](../.github/workflows/firmware-functional-test.yml) | **`self-hosted` + `astrolabe-watch`** | Manual dispatch only |
| [Firmware hardware QA](../.github/workflows/firmware-hardware-qa.yml) | **`self-hosted` + `astrolabe-watch`** | Manual: one face screenshot → issue |
| [Firmware voice QA](../.github/workflows/firmware-voice-qa.yml) | **`self-hosted` + `astrolabe-watch`** | Manual: all-face STT/TTS tour → `summary.json` |
| [Deploy GitHub Pages](../.github/workflows/deploy-github-pages.yml) | `ubuntu-latest` | Static site / simulator deploy from `docs/` — exempt from hardware gate |

GitHub **cloud** runners cannot see USB. To flash in CI, register a [self-hosted runner](https://docs.github.com/en/actions/hosting-your-own-runners/managing-self-hosted-runners/adding-self-hosted-runners) on the Mac where the watch is plugged in.

**One-time runner setup**

1. `./scripts/setup-self-hosted-runner.sh` then `./scripts/install-runner-launchagent.sh`
2. Labels: `self-hosted`, `astrolabe-watch` — remove **offline** duplicate runners in GitHub Settings.
3. Environment **astrolabe-watch**: optional reviewers for revert PRs when `ASTROLABE_FT_UNMERGE_PUSH=1`.
4. Secrets: `ASTROLABE_SECRETS_FILE` on the Mac (default `~/GitHub/astrolabe/include/secrets.local.h`) or GitHub Actions secrets.
5. Plug in the watch (**303A:1001**); optional `ASTROLABE_UPLOAD_PORT` in LaunchAgent.

**Integration sim gate (merge to `integration`)**

- Set repo variable **`ENABLE_INTEGRATION_SIM_GATE=true`**
- After each green **Firmware build** on `integration`: QEMU build (`waveshare_s3_175_qemu`) + serial face matrix ([`faces_qemu.json`](../tests/functional/faces_qemu.json)).
- Reports: `artifacts/functional-sim/latest/report.json` with `"gate": "sim"`.

**Integration device gate (promote to `main`)**

- Set repo variable **`ENABLE_INTEGRATION_DEVICE_GATE=true`**
- After each green **Firmware build** on `integration`: m1 flashes and runs the full hardware matrix (informational on `integration`; required before `./scripts/promote-integration.sh`).
- All device workflows share concurrency group **`astrolabe-watch-device`** (no parallel flash + test).

**Legacy / manual**

- **Firmware flash** — `ENABLE_INTEGRATION_FLASH=true` (avoid if device gate is on).
- **Firmware functional test** — workflow_dispatch only.

Local equivalent: `./scripts/ci-flash.sh`

### Hardware QA on this laptop (JTAG + issue screenshot)

Register the Mac as a self-hosted runner (watch on USB, Wi‑Fi secrets on disk):

```bash
./scripts/setup-self-hosted-runner.sh
cd ~/actions-runner-astrolabe && ./run.sh
```

**Actions → Firmware hardware QA** — inputs: `issue_number`, `face` (default `moon`). The job:

1. Builds/uploads **`waveshare_s3_175_debug`**
2. **JTAG** sets the clock face (`./scripts/jtag_set_face.sh`)
3. Captures **`http://<watch-ip>/screen.bmp`**
4. Posts a **PNG** to the issue comment (via public gist)

Local run:

```bash
ASTROLABE_QA_FACE=moon ASTROLABE_QA_ISSUE=2 ./scripts/ci-hardware-qa.sh
```

### Functional test (all faces, crash + screenshot)

Flashes firmware, walks each clock face via serial `face N`, captures `screen.bmp`, injects gestures/buttons with `qa inject`, and fails on Guru Meditation / backtrace in serial.

```bash
./scripts/functional_test.py --flash
./scripts/functional_test.py --faces classic,moon,spotify   # subset
./scripts/ci-functional-test.sh                             # CI wrapper (always --flash)
```

Matrix: [`tests/functional/faces_astrolabe.json`](../tests/functional/faces_astrolabe.json).  
Comprehensive (L/R/U/D swipes + buttons on every face): [`faces_astrolabe_comprehensive.json`](../tests/functional/faces_astrolabe_comprehensive.json).

### Voice QA (all faces, STT + TTS + crash)

Runs on a bench watch with USB serial attached. The harness walks every ported
face reported by `faces list`, triggers `voice stt <ms>`, speaks a known host
prompt into the watch, and waits for `qa: stt done err=ESP_OK`. That marker is
emitted after reply MP3 playback completes, so each passing row covers STT,
reply generation, and TTS playback for that face. The run fails on any timeout,
missing transcript/reply, crash, or reboot marker.

```bash
scripts/stt_tts_face_tour.py --self-test
./scripts/ci-voice-qa.sh --no-flash
mcp/astrolabe-esp/.venv/bin/python scripts/stt_tts_face_tour.py
summary="$(find artifacts/qa -maxdepth 2 -path '*/summary.json' -path '*stt-tts-face-tour-*' -print | sort | tail -1)"
mcp/astrolabe-esp/.venv/bin/python scripts/stt_tts_face_tour.py --verify-summary "$summary"
mcp/astrolabe-esp/.venv/bin/python scripts/stt_tts_face_tour.py --limit 3  # shakedown
```

Artifacts are written under `artifacts/qa/stt-tts-face-tour-*` with
`summary.json` and a timestamped serial log. A full-run summary only verifies
when every planned face completed with zero STT/TTS failures, crashes, or
reboots; limited shakedown summaries require `--allow-limited`.

GitHub Actions → **Firmware voice QA** runs the same tour on the self-hosted
watch runner and uploads `artifacts/qa/`.

**m1 bench automation** (pull `integration` → flash → comprehensive test):

```bash
./scripts/device-bench.sh              # pull, flash, test (~30–45 min)
./scripts/device-bench.sh --no-pull    # already on integration
ASTROLABE_BENCH_VOICE=1 ./scripts/device-bench.sh --no-pull
ASTROLABE_BENCH_VOICE=1 ASTROLABE_VOICE_LIMIT=3 ./scripts/device-bench.sh --no-pull
./scripts/install-device-bench-launchagent.sh   # daily 06:00 on this Mac
./scripts/install-device-bench-launchagent.sh --run-now
```

Logs: `artifacts/bench/`. Uses `ASTROLABE_UHUBCTL_SEARCH=Espressif` for hub power cycle. When
`ASTROLABE_BENCH_VOICE=1`, the bench run also copies
`latest-voice-summary.json` from the all-face STT/TTS tour.

**On failure** (device gate / `--remediate`):

1. **Triage** — `artifacts/functional/<run>/NN-<face>-triage.md`
2. **GitHub issue** — deduped per open `face:<name>` + `functional-test`
3. **Unmerge** — only if `ASTROLABE_FT_UNMERGE_PUSH=1` (default **off**); approve via `astrolabe-watch` environment
4. **Fix agent** — optional `ASTROLABE_FT_DISPATCH_AGENT=1`

**Promotion** — firmware promotion with `./scripts/promote-integration.sh --flash-ok` requires a **hardware** report (`artifacts/functional/latest/report.json`, `"gate": "hardware"`, `failed: 0`) matching `integration` HEAD (`--skip-functional` to override). Docs/site-only Pages deploys are not firmware promotion.

Secrets: `include/secrets.local.h` on the laptop (`ASTROLABE_SECRETS_FILE`) or GitHub Actions secrets `MYNAH_WIFI_*` / `MYNAH_SUPABASE_*`.

Optional repo variables: `ENABLE_INTEGRATION_HW_QA=true`, `ASTROLABE_QA_ISSUE=2`, `ASTROLABE_QA_FACE=moon` for auto QA after build.

### Suggested labels (optional)

- `cloud-agent` — ready for `@cursor` (criteria complete, branch name in issue body)
- `needs-hardware-qa` — do not promote to **`main`** until watch verification on **`integration`**

### Example issues

- [#1](https://github.com/CastaliaInstitute/astrolabe/issues/1) Moon face UX  
- [#2](https://github.com/CastaliaInstitute/astrolabe/issues/2) Circadian hue  
- [#3](https://github.com/CastaliaInstitute/astrolabe/issues/3) Astrology polish  

## Finishing

1. Merge PR into **`integration`** when **Firmware build** + **Integration sim gate** are green (see [integration-branch.mdc](../.cursor/rules/integration-branch.mdc)); face/UI PRs do not need the bench watch to merge.
2. Mark backlog `[x]`, move to **Done** with `YYYY-MM-DD` and PR link.
3. Before firmware **`main`** promotion: **hardware QA** on **`integration`** for new/changed clock faces ([`hardware-qa.mdc`](../.cursor/rules/hardware-qa.mdc)) or green **Integration device gate**; then `./scripts/promote-integration.sh --flash-ok`. Docs/site-only Pages deploys publish from **`integration`** and skip this gate.
5. Delete the issue branch after merge.

## Branch naming

| Type | Pattern | Example |
|------|---------|---------|
| Feature | `feature/<#>-<slug>` | `feature/51-commonplace-journal` |
| Bugfix | `fix/<#>-<slug>` | `fix/52-moon-tap-steals-swipe` |
| Chore | `chore/<#>-<slug>` | `chore/10-mdns-config` |

## Commits

- One **focused** commit per logical step; **one feature per PR**.
- Message: why, not a file list (`Fix Moon face tap stealing vertical swipe`).
- Never commit secrets.

## Issue templates

- **Feature** — `.github/ISSUE_TEMPLATE/feature.yml`
- **Bugfix** — `.github/ISSUE_TEMPLATE/bugfix.yml`

## PR checklist

See [`.github/pull_request_template.md`](../.github/pull_request_template.md).
