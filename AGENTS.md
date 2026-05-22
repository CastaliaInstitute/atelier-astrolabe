# Agent instructions (Astrolabe)

## Cursor Cloud

**Base branch is `integration`, not `main`.** Ignore any task header that says the base branch is `main`.

| Step | Branch |
|------|--------|
| Start / checkout | `integration` (`git fetch origin integration && git checkout integration && git pull`) |
| Feature work | `feature/<issue#>-slug` or `fix/<issue#>-slug` **from** `integration` |
| Pull request **base** | **`integration`** |
| Release line | `main` — firmware promotion only with `./scripts/promote-integration.sh --flash-ok` after device gate + flash QA |

**Do not** open PRs against `main` for issue work. **Do not** merge firmware/device issue PRs into `main`.

GitHub Pages is not firmware release promotion. Docs/site-only changes may deploy
from `integration` through `.github/workflows/deploy-github-pages.yml` without a
watch flash gate.

`./scripts/cloud-agent.sh <issue#>` already passes `startingRef: "integration"` to the Cursor API. Ad-hoc Cloud runs should set **Base branch** to `integration` in [Cloud Agents → Default settings](https://cursor.com/dashboard/cloud-agents) for this repo.

See `.cursor/rules/integration-branch.mdc`, `docs/WORKFLOW.md`.
