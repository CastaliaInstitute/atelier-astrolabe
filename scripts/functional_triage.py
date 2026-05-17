#!/usr/bin/env python3
"""Build per-face crash triage from a functional-test report.json + serial logs."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

CRASH_LINE = re.compile(
    r"Guru Meditation|abort\(\)|Backtrace:|Stack overflow|panic'ed|Brownout|rst:0x",
    re.I,
)
ADDR_LINE = re.compile(r"0x[0-9a-f]{8}", re.I)
FAIL_STEP = re.compile(r"gesture:|button:|after_|set_face|screenshot|qa_status")


def git(*args: str, cwd: Path = REPO) -> str:
    return subprocess.check_output(["git", *args], cwd=str(cwd), text=True, stderr=subprocess.STDOUT).strip()


def merge_base(integration: str, main: str) -> str:
    return git("merge-base", main, integration)


def commits_for_paths(base: str, head: str, paths: list[str]) -> list[dict]:
    if not paths:
        return []
    cmd = [
        "git",
        "log",
        f"{base}..{head}",
        "--format=%H%x09%an%x09%s",
        "--",
        *paths,
    ]
    out = subprocess.check_output(cmd, cwd=str(REPO), text=True, stderr=subprocess.STDOUT)
    rows: list[dict] = []
    for line in out.splitlines():
        if not line.strip():
            continue
        sha, author, subject = line.split("\t", 2)
        rows.append({"sha": sha, "author": author, "subject": subject})
    return rows


def pr_for_commit(sha: str, repo: str) -> str | None:
    try:
        out = subprocess.check_output(
            ["gh", "pr", "list", "--repo", repo, "--state", "merged", "--search", sha, "--json", "number,title", "-q", ".[0]"],
            cwd=str(REPO),
            text=True,
            stderr=subprocess.DEVNULL,
        )
        if out.strip():
            data = json.loads(out)
            return f"#{data['number']} {data['title']}"
    except (subprocess.CalledProcessError, json.JSONDecodeError, KeyError):
        pass
    return None


def extract_crash_excerpt(lines: list[str], max_lines: int = 60) -> list[str]:
    start = 0
    for i, ln in enumerate(lines):
        if CRASH_LINE.search(ln):
            start = max(0, i - 5)
            break
    return lines[start : start + max_lines]


def first_failed_step(steps: list[dict]) -> dict | None:
    for s in steps:
        if not s.get("ok", True):
            return s
    return None


def triage_face(
    face: dict,
    report: dict,
    out_dir: Path,
    *,
    integration_ref: str,
    main_ref: str,
    repo: str,
) -> dict:
    name = face["name"]
    serial_path = out_dir / f"{face['id']:02d}-{name}-serial.log"
    serial_lines: list[str] = []
    if serial_path.is_file():
        serial_lines = serial_path.read_text(encoding="utf-8", errors="replace").splitlines()

    fail = first_failed_step(face.get("steps", []))
    fail_step = fail["name"] if fail else "unknown"
    fail_detail = fail.get("detail", "") if fail else ""

    paths = face.get("source_paths") or [f"sketches/Astrolabe/faces/{name}/"]
    base = merge_base(integration_ref, main_ref)
    head = report.get("git_head") or integration_ref
    commits = commits_for_paths(base, head, paths)

    excerpt = extract_crash_excerpt(serial_lines)
    addrs = []
    for ln in excerpt:
        addrs.extend(ADDR_LINE.findall(ln))

    triage = {
        "face_id": face["id"],
        "face": name,
        "failed_step": fail_step,
        "failed_detail": fail_detail,
        "source_paths": paths,
        "merge_base": base,
        "commits": commits,
        "crash_excerpt": excerpt,
        "addresses": sorted(set(addrs))[:20],
        "screenshot": face.get("screenshot", ""),
        "serial_log": str(serial_path),
    }
    if commits:
        triage["likely_pr"] = pr_for_commit(commits[0]["sha"], repo)

    md_path = out_dir / f"{face['id']:02d}-{name}-triage.md"
    md_lines = [
        f"# Functional test failure: face `{name}` (id {face['id']})",
        "",
        f"- **Failed step:** `{fail_step}`",
        f"- **Detail:** {fail_detail or '(none)'}",
        f"- **Integration head:** `{head[:12]}`",
        f"- **Merge-base with main:** `{base[:12]}`",
        "",
        "## Crash excerpt (serial)",
        "",
        "```",
        *excerpt,
        "```",
        "",
        "## Source paths",
        "",
        *[f"- `{p}`" for p in paths],
        "",
        "## Commits on integration since main (touching this face)",
        "",
    ]
    if not commits:
        md_lines.append("_No commits found in range — check `source_paths` or shared regressions._")
    else:
        for c in commits[:15]:
            md_lines.append(f"- `{c['sha'][:12]}` {c['subject']} ({c['author']})")
        if triage.get("likely_pr"):
            md_lines.extend(["", f"**Likely PR:** {triage['likely_pr']}"])
    md_lines.extend(
        [
            "",
            "## Remediation",
            "",
            "1. Revert these commits off `integration` (isolate):",
            f"   `./scripts/functional_unmerge_face.sh {name}`",
            "2. Fix on a branch and re-merge when `./scripts/functional_test.py --faces "
            f"{name}` passes.",
            "",
        ]
    )
    md_path.write_text("\n".join(md_lines), encoding="utf-8")
    triage["triage_md"] = str(md_path)
    return triage


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--integration-ref", default="HEAD")
    parser.add_argument("--main-ref", default="origin/main")
    parser.add_argument("--repo", default="CastaliaInstitute/astrolabe")
    args = parser.parse_args()

    report = json.loads(args.report.read_text(encoding="utf-8"))
    out_dir = args.report.parent

    failed = [f for f in report.get("faces", []) if not f.get("ok", True)]
    if not failed:
        print("no failed faces in report")
        return 0

    triages: list[dict] = []
    for face in failed:
        print(f"→ triage {face['name']}")
        triages.append(
            triage_face(
                face,
                report,
                out_dir,
                integration_ref=args.integration_ref,
                main_ref=args.main_ref,
                repo=args.repo,
            )
        )

    summary_path = out_dir / "triage-summary.json"
    summary_path.write_text(json.dumps({"faces": triages}, indent=2), encoding="utf-8")

    md = out_dir / "triage-summary.md"
    md.write_text(
        "\n".join(
            [
                "# Functional test triage summary",
                "",
                f"Failed faces: {', '.join(t['face'] for t in triages)}",
                "",
                *[f"- [{t['face']}]({Path(t['triage_md']).name}): `{t['failed_step']}`" for t in triages],
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"→ {summary_path}")
    print(f"→ {md}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
