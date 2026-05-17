# Development workflow (GitHub + Cursor)

Astrolabe uses **GitHub Issues** for trackable work, **one branch per issue**, and **[`docs/BACKLOG.md`](BACKLOG.md)** as the product roadmap. **Cursor Cloud** (and local Cursor agents) implement from an issue on a dedicated branch, open a PR into **`integration`**, and may merge when CI is green. **`main`** is updated only after **build + on-device flash** (promotion).

## Branches

| Branch | Role |
|--------|------|
| **`integration`** | Default merge target for issue PRs; CI build on every PR/push |
| **`main`** | Release line — must always build and have been flashed before promotion |
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
3. Optional: **[Cloud Agents dashboard](https://cursor.com/dashboard/cloud-agents)** — spend limit, “post artifacts to GitHub” on PRs, secrets for CI (not `secrets.local.h`).

Cloud agents use repo rules from [`.cursor/rules/`](../.cursor/rules/) and hooks from [`.cursor/hooks.json`](../.cursor/hooks.json) when present.

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
Rules: .cursor/rules/backlog.mdc, git-workflow.mdc, github-workflow.mdc; hardware-qa.mdc for clock faces.

Deliverables:
- Code + ./scripts/build.sh passes
- docs/BACKLOG.md updated (In progress → Done with PR link)
- PR against integration with "Closes #<N>"; merge when CI green if confident (single issue only)
- Do not commit secrets; do not merge to main (promotion is separate)

Hardware: this repo targets ESP32 watch firmware. You cannot flash hardware in cloud; note in PR if on-device QA is required.
```

### What Cloud Agents can / cannot do here

| Can | Cannot (you do locally) |
|-----|-------------------------|
| Edit firmware, open PR, fix CI | Flash watch, serial monitor, JTAG |
| Run `./scripts/build.sh` in VM | `screen.bmp` unless Wi‑Fi + device on your LAN |
| Attach screenshots/logs to PR | Castalia sign-in on physical device |

After the PR merges to **`integration`**, run **hardware QA** on **`integration`** for face/UI changes ([`hardware-qa.mdc`](../.cursor/rules/hardware-qa.mdc)). When a batch is verified, **promote** to **`main`**.

### CI

| Workflow | Runner | What |
|----------|--------|------|
| [Firmware build](../.github/workflows/firmware-build.yml) | `ubuntu-latest` | `./scripts/build.sh`; uploads `firmware.bin` artifact |
| [Firmware flash](../.github/workflows/firmware-flash.yml) | **`self-hosted` + `astrolabe-watch`** | `./scripts/ci-flash.sh` to the USB watch |

GitHub **cloud** runners cannot see USB. To flash in CI, register a [self-hosted runner](https://docs.github.com/en/actions/hosting-your-own-runners/managing-self-hosted-runners/adding-self-hosted-runners) on the Mac where the watch is plugged in.

**One-time runner setup**

1. Repo → **Settings → Actions → Runners → New self-hosted runner** (macOS).
2. Install and start the runner on that Mac; add labels: `self-hosted`, `astrolabe-watch`.
3. Create environment **astrolabe-watch** (Settings → Environments) if you want approval gates before flash.
4. Plug in the watch (Espressif **303A:1001**); optional fixed port: set runner env `ASTROLABE_UPLOAD_PORT=/dev/cu.usbmodem1101`.

**Run a flash**

- **Actions → Firmware flash → Run workflow** (pick branch, default `integration`).
- **Auto after build:** set repo variable `ENABLE_INTEGRATION_FLASH` = `true` to flash on every successful [Firmware build](https://github.com/CastaliaInstitute/astrolabe/actions/workflows/firmware-build.yml) on `integration`.

Local equivalent: `./scripts/ci-flash.sh`

### Suggested labels (optional)

- `cloud-agent` — ready for `@cursor` (criteria complete, branch name in issue body)
- `needs-hardware-qa` — do not promote to **`main`** until watch verification on **`integration`**

### Example issues

- [#1](https://github.com/CastaliaInstitute/astrolabe/issues/1) Moon face UX  
- [#2](https://github.com/CastaliaInstitute/astrolabe/issues/2) Circadian hue  
- [#3](https://github.com/CastaliaInstitute/astrolabe/issues/3) Astrology polish  

## Finishing

1. Merge PR into **`integration`** (CI green; agents may merge when confident — see [integration-branch.mdc](../.cursor/rules/integration-branch.mdc)).
2. **Hardware QA** on **`integration`** for new/changed clock faces ([`hardware-qa.mdc`](../.cursor/rules/hardware-qa.mdc)): flash → `http://<watch-ip>/screen.bmp` → evaluate.
3. Mark backlog `[x]`, move to **Done** with `YYYY-MM-DD` and PR link.
4. When ready to release: `./scripts/promote-integration.sh --flash-ok` → **`main`**.
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
