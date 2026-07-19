#!/usr/bin/env python3
"""Sync local Codex CLI threads with an Astrolabe Codex face.

The companion talks to the authenticated local `codex app-server` over stdio;
no OpenAI credential or Desktop Remote pairing secret is sent to Astrolabe.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import platform
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


EMOJI_CATALOG = [
    ("COMPUTER", "💻"), ("TOOLS", "🛠️"), ("BUG", "🐛"), ("ROCKET", "🚀"),
    ("EXPERIMENT", "🔬"), ("WRITING", "📝"), ("SEARCH", "🔍"), ("DESIGN", "🎨"),
    ("SECURITY", "🔒"), ("WEB", "🌐"), ("DATABASE", "🗄️"), ("ANALYTICS", "📊"),
    ("ROBOT", "🤖"), ("THINKING", "🧠"), ("MAGIC", "✨"), ("URGENT", "🔥"),
    ("PACKAGE", "📦"), ("MOBILE", "📱"), ("CAMERA", "📷"), ("AUDIO", "🎙️"),
    ("TIME", "⏱️"), ("SUCCESS", "✅"), ("WARNING", "⚠️"), ("WORLD", "🌍"),
    ("BOOKS", "📚"), ("IDEA", "💡"), ("MESSAGE", "💬"), ("FOLDER", "📁"),
    ("CLOUD", "☁️"), ("LINK", "🔗"), ("HEART", "❤️"), ("STAR", "⭐"),
]
EMOJI_FALLBACK = 12


class EmojiAssigner:
    """Ask the authenticated Codex model once, then cache title-to-emoji choices."""

    def __init__(self, codex: str, model: str | None, disabled: bool) -> None:
        self.codex = codex
        self.model = model
        self.disabled = disabled
        self.cache_path = Path.home() / ".cache" / "astrolabe-codex" / "task-emoji.json"
        self.cache: dict[str, int] = {}
        self.failed: set[str] = set()
        try:
            loaded = json.loads(self.cache_path.read_text(encoding="utf-8"))
            self.cache = {
                str(title): int(index) for title, index in loaded.items()
                if isinstance(index, int) and 0 <= index < len(EMOJI_CATALOG)
            }
        except (OSError, ValueError, TypeError):
            pass

    def _save(self) -> None:
        self.cache_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.cache_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.cache, ensure_ascii=False, indent=2), encoding="utf-8")
        temporary.replace(self.cache_path)

    def _ask_codex(self, titles: list[str]) -> dict[str, int]:
        schema = {
            "type": "object",
            "properties": {
                "assignments": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "ordinal": {"type": "integer", "minimum": 0, "maximum": len(titles) - 1},
                            "emojiIndex": {"type": "integer", "minimum": 0, "maximum": len(EMOJI_CATALOG) - 1},
                        },
                        "required": ["ordinal", "emojiIndex"],
                        "additionalProperties": False,
                    },
                }
            },
            "required": ["assignments"],
            "additionalProperties": False,
        }
        catalog = "\n".join(f"{i}: {name} {emoji}" for i, (name, emoji) in enumerate(EMOJI_CATALOG))
        prompt = (
            "Select exactly one semantically apt emoji for each Codex task title. "
            "Return every ordinal exactly once using only the catalog index. Do not use tools.\n\n"
            f"CATALOG:\n{catalog}\n\nTASKS:\n" +
            "\n".join(f"{i}: {title}" for i, title in enumerate(titles))
        )
        with tempfile.TemporaryDirectory(prefix="astrolabe-codex-emoji-") as directory:
            base = Path(directory)
            schema_path = base / "schema.json"
            output_path = base / "result.json"
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            command = [
                self.codex, "exec", "--ephemeral", "--sandbox", "read-only",
                "--skip-git-repo-check", "--ignore-rules", "--cd", directory,
                "--output-schema", str(schema_path), "--output-last-message", str(output_path),
            ]
            if self.model:
                command.extend(["--model", self.model])
            command.append("-")
            result = subprocess.run(
                command, input=prompt, text=True, stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE, timeout=180, check=False,
            )
            if result.returncode != 0:
                raise RuntimeError(result.stderr.strip() or f"Codex emoji selection exited {result.returncode}")
            response = json.loads(output_path.read_text(encoding="utf-8"))
        choices: dict[str, int] = {}
        for assignment in response.get("assignments", []):
            ordinal = assignment.get("ordinal")
            index = assignment.get("emojiIndex")
            if isinstance(ordinal, int) and isinstance(index, int) and 0 <= ordinal < len(titles) and 0 <= index < len(EMOJI_CATALOG):
                choices[titles[ordinal]] = index
        if len(choices) != len(titles):
            raise RuntimeError("Codex returned an incomplete emoji assignment")
        return choices

    def assign(self, titles: list[str]) -> dict[str, int]:
        missing = list(dict.fromkeys(title for title in titles if title not in self.cache and title not in self.failed))
        if missing and not self.disabled:
            try:
                choices = self._ask_codex(missing)
                self.cache.update(choices)
                self._save()
                summary = ", ".join(f"{EMOJI_CATALOG[index][1]} {title}" for title, index in choices.items())
                print(f"AI selected task emoji: {summary}", file=sys.stderr, flush=True)
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
                print(f"Astrolabe emoji selection failed; using robot fallback: {error}", file=sys.stderr)
                self.failed.update(missing)
        return {title: self.cache.get(title, EMOJI_FALLBACK) for title in titles}


class AppServer:
    def __init__(self, codex: str) -> None:
        self.process = subprocess.Popen(
            [codex, "app-server", "--stdio"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self.request_id = 0
        self.approvals: dict[str, dict[str, Any]] = {}
        self.request(
            "initialize",
            {"clientInfo": {"name": "astrolabe", "title": "Astrolabe Codex companion", "version": "0.1.0"}},
        )
        self.notify("initialized", {})

    def send(self, message: dict[str, Any]) -> None:
        if self.process.stdin is None:
            raise RuntimeError("Codex app-server stdin closed")
        self.process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
        self.process.stdin.flush()

    def notify(self, method: str, params: dict[str, Any]) -> None:
        self.send({"method": method, "params": params})

    def request(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        self.request_id += 1
        wanted = self.request_id
        self.send({"id": wanted, "method": method, "params": params})
        if self.process.stdout is None:
            raise RuntimeError("Codex app-server stdout closed")
        for line in self.process.stdout:
            message = json.loads(line)
            if message.get("id") != wanted:
                method_name = message.get("method", "")
                params_value = message.get("params") or {}
                if method_name in {"item/commandExecution/requestApproval", "item/fileChange/requestApproval"}:
                    thread_id = params_value.get("threadId")
                    if thread_id:
                        self.approvals[thread_id] = message
                continue
            if "error" in message:
                raise RuntimeError(f"Codex {method} failed: {message['error']}")
            return message.get("result", {})
        raise RuntimeError("Codex app-server exited")

    def approve(self, thread_id: str) -> bool:
        request = self.approvals.pop(thread_id, None)
        if request is None:
            return False
        self.send({"id": request["id"], "result": {"decision": "accept"}})
        return True

    def close(self) -> None:
        self.process.terminate()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()


def api(base_url: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    data = None if payload is None else json.dumps(payload, separators=(",", ":")).encode()
    request = urllib.request.Request(base_url.rstrip("/") + path, data=data)
    request.add_header("Accept", "application/json")
    if data is not None:
        request.add_header("Content-Type", "application/json")
        request.add_header("X-Astrolabe-Codex", "sync-v1")
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


def task_status(thread: dict[str, Any]) -> str:
    status = thread.get("status") or {}
    kind = status.get("type")
    flags = status.get("activeFlags") or []
    if "waitingOnApproval" in flags:
        return "waiting-approval"
    if "waitingOnUserInput" in flags:
        return "waiting-input"
    if kind == "active":
        return "running"
    if kind == "systemError":
        return "error"
    return "idle"


def task_title(thread: dict[str, Any]) -> str:
    title = thread.get("name") or thread.get("preview")
    if title:
        return " ".join(str(title).split())[:63]
    return Path(thread.get("cwd") or "Codex task").name[:63]


def task_usage(thread: dict[str, Any]) -> dict[str, Any]:
    """Read cumulative token counters and estimate the recent task rate locally."""
    usage: dict[str, Any] = {
        "available": False,
        "inputTokens": 0,
        "cachedInputTokens": 0,
        "outputTokens": 0,
        "reasoningOutputTokens": 0,
        "totalTokens": 0,
        "rateTokensPerMinute": 0,
    }
    path_value = thread.get("path")
    if not path_value:
        return usage
    samples: list[tuple[float, int]] = []
    try:
        with Path(path_value).open(encoding="utf-8", errors="replace") as session:
            for line in session:
                try:
                    event = json.loads(line)
                    payload = event.get("payload") or {}
                    info = payload.get("info") or {}
                    if payload.get("type") != "token_count" or not info:
                        continue
                    totals = info.get("total_token_usage") or {}
                    total = int(totals.get("total_tokens") or 0)
                    if total <= 0:
                        continue
                    usage.update({
                        "available": True,
                        "inputTokens": int(totals.get("input_tokens") or 0),
                        "cachedInputTokens": int(totals.get("cached_input_tokens") or 0),
                        "outputTokens": int(totals.get("output_tokens") or 0),
                        "reasoningOutputTokens": int(totals.get("reasoning_output_tokens") or 0),
                        "totalTokens": total,
                    })
                    timestamp = event.get("timestamp")
                    if timestamp:
                        samples.append((dt.datetime.fromisoformat(str(timestamp).replace("Z", "+00:00")).timestamp(), total))
                except (ValueError, TypeError, json.JSONDecodeError):
                    continue
    except (OSError, ValueError):
        return usage
    if len(samples) >= 2:
        last_time, last_total = samples[-1]
        # A historical burst is not a current rate once the task has gone idle.
        if time.time() - last_time > 120.0:
            return usage
        cutoff = last_time - 120.0
        recent = [(timestamp, total) for timestamp, total in samples if timestamp >= cutoff]
        first_time, first_total = recent[0] if len(recent) >= 2 else samples[-2]
        elapsed = last_time - first_time
        if elapsed > 0 and last_total >= first_total:
            usage["rateTokensPerMinute"] = int((last_total - first_total) * 60.0 / elapsed)
    return usage


def snapshot(
    server: AppServer,
    emoji_assigner: EmojiAssigner,
    host_id: str,
    host_name: str,
    ack: int | None = None,
) -> dict[str, Any]:
    result = server.request(
        "thread/list",
        {"limit": 12, "sortKey": "updated_at", "sortDirection": "desc", "archived": False},
    )
    threads = result.get("data", [])
    titles = [task_title(thread) for thread in threads]
    emoji = emoji_assigner.assign(titles)
    body: dict[str, Any] = {
        "replaceHostId": host_id,
        "hosts": [{"id": host_id, "name": host_name, "online": True}],
        "tasks": [
            {
                "id": thread["id"],
                "hostId": host_id,
                "title": title,
                "status": "waiting-approval" if thread["id"] in server.approvals else task_status(thread),
                "updatedAt": thread.get("updatedAt", 0),
                "emojiIndex": emoji[title],
                "usage": task_usage(thread),
            }
            for thread, title in zip(threads, titles)
        ],
    }
    if ack is not None:
        body["ackActionSequence"] = ack
    return body


def interrupt(server: AppServer, thread_id: str) -> bool:
    thread = server.request("thread/read", {"threadId": thread_id, "includeTurns": True}).get("thread", {})
    for turn in reversed(thread.get("turns") or []):
        if turn.get("status") == "inProgress":
            server.request("turn/interrupt", {"threadId": thread_id, "turnId": turn["id"]})
            return True
    return False


def prompt(server: AppServer, thread_id: str, text: str) -> bool:
    if not text:
        return False
    server.request("thread/resume", {"threadId": thread_id})
    server.request("turn/start", {"threadId": thread_id, "input": [{"type": "text", "text": text}]})
    return True


def run(args: argparse.Namespace) -> int:
    host_name = args.name or socket.gethostname().removesuffix(".local")
    host_id = args.host_id or f"{platform.node().lower().replace('.', '-')[:32]}-codex"
    emoji_assigner = EmojiAssigner(args.codex, args.emoji_model, args.no_ai_emoji)
    server = AppServer(args.codex)
    last_unhandled = -1
    try:
        while True:
            state = {} if args.print_snapshot else api(args.astrolabe, "/api/codex/state")
            pending = state.get("pendingAction") or {}
            sequence = int(pending.get("sequence") or 0)
            action = pending.get("action") or ""
            task_id = pending.get("taskId") or ""
            input_text = pending.get("input") or ""
            task = next((item for item in state.get("tasks", []) if item.get("id") == task_id), None)
            ack = None
            if action and task and task.get("hostId") == host_id:
                if action == "interrupt" and interrupt(server, task_id):
                    ack = sequence
                elif action == "prompt" and prompt(server, task_id, input_text):
                    ack = sequence
                elif action == "approve" and server.approve(task_id):
                    ack = sequence
                elif action == "open":
                    server.request("thread/read", {"threadId": task_id, "includeTurns": False})
                    ack = sequence
                    print(f"Astrolabe selected {task.get('title', task_id)}", flush=True)
                elif sequence != last_unhandled:
                    print(f"Astrolabe action {action!r} requires an interactive Codex client; left queued", file=sys.stderr)
                    last_unhandled = sequence
            body = snapshot(server, emoji_assigner, host_id, host_name, ack)
            if args.print_snapshot:
                print(json.dumps(body, indent=2))
            else:
                api(args.astrolabe, "/api/codex/state", body)
            if args.once:
                return 0
            time.sleep(args.interval)
    except (OSError, urllib.error.URLError, RuntimeError, json.JSONDecodeError) as error:
        print(f"astrolabe_codex_companion: {error}", file=sys.stderr)
        return 1
    finally:
        server.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--astrolabe", required=True, help="Astrolabe origin, e.g. http://astrolabe-1234.local")
    parser.add_argument("--name", help="Machine name shown on Astrolabe")
    parser.add_argument("--host-id", help="Stable host identifier (default: hostname-codex)")
    parser.add_argument("--codex", default="codex", help="Codex CLI executable")
    parser.add_argument("--emoji-model", help="Optional Codex model override for task emoji selection")
    parser.add_argument("--no-ai-emoji", action="store_true", help="Use the robot fallback without AI selection")
    parser.add_argument("--interval", type=float, default=2.0, help="Polling interval in seconds")
    parser.add_argument("--once", action="store_true", help="Sync once, then exit")
    parser.add_argument("--print-snapshot", action="store_true", help="Print the local snapshot instead of posting it")
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
