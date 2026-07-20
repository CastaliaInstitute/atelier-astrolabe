#!/usr/bin/env python3
"""Local mock WebSocket server for Astrolabe rolling voice-stream tests."""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import subprocess
import tempfile
import time
from dataclasses import dataclass, field
from typing import Any

from aiohttp import web


ASTROLABE_BINARY_PCM = 0xA1
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8788
AUDIO_DELTA_CHARS = 12 * 1024


MOCK_MP3: bytes | None = None


@dataclass
class StreamSession:
    face: str = "faculty"
    faculty_slug: str = "mock-faculty"
    faculty_name: str = "Mock Faculty"
    sample_rate_hz: int = 16000
    pending: bytearray = field(default_factory=bytearray)
    current: bytearray = field(default_factory=bytearray)
    commits: int = 0
    frames: int = 0
    bytes_received: int = 0


def mock_mp3() -> bytes:
    global MOCK_MP3
    if MOCK_MP3 is not None:
        return MOCK_MP3
    with tempfile.NamedTemporaryFile(suffix=".mp3") as tmp:
        subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-f",
                "lavfi",
                "-i",
                "sine=frequency=880:duration=0.35",
                "-ar",
                "24000",
                "-ac",
                "1",
                "-b:a",
                "48k",
                "-y",
                tmp.name,
            ],
            check=True,
        )
        tmp.seek(0)
        MOCK_MP3 = tmp.read()
    if len(MOCK_MP3) < 64:
        raise RuntimeError("generated mock MP3 is too small")
    return MOCK_MP3


def log(event: str, **fields: Any) -> None:
    payload = {"event": event, "ts": round(time.time(), 3), **fields}
    print(json.dumps(payload, separators=(",", ":")), flush=True)


def decode_binary_frame(data: bytes) -> tuple[int | None, int | None, bytes]:
    if not data:
        return None, None, b""
    if data[0] != ASTROLABE_BINARY_PCM:
        return None, None, data
    if len(data) <= 9:
        return 0, 0, b""
    seq = int.from_bytes(data[1:5], "little")
    capture_ms = int.from_bytes(data[5:9], "little")
    return seq, capture_ms, data[9:]


async def send_json(ws: web.WebSocketResponse, payload: dict[str, Any]) -> None:
    await ws.send_str(json.dumps(payload, separators=(",", ":")))


async def send_audio_delta(ws: web.WebSocketResponse, turn_id: str, mp3: bytes) -> None:
    audio = base64.b64encode(mp3).decode("ascii")
    for offset in range(0, len(audio), AUDIO_DELTA_CHARS):
        await send_json(
            ws,
            {
                "type": "response.audio.delta",
                "turnId": turn_id,
                "audio": audio[offset : offset + AUDIO_DELTA_CHARS],
                "encoding": "mp3",
            },
        )
        await asyncio.sleep(0)


def apply_session_update(session: StreamSession, update: dict[str, Any]) -> None:
    session.face = str(update.get("face") or session.face)
    session.faculty_slug = str(
        update.get("facultySlug") or update.get("faculty_slug") or session.faculty_slug
    )
    session.faculty_name = str(
        update.get("facultyName") or update.get("faculty_name") or session.faculty_name
    )
    sample_rate = update.get("sampleRateHertz") or update.get("sample_rate_hz")
    if isinstance(sample_rate, int) and sample_rate > 0:
        session.sample_rate_hz = sample_rate


async def commit_turn(
    ws: web.WebSocketResponse,
    session: StreamSession,
    turn_id: str,
    final: bool,
    max_turn_bytes: int,
) -> None:
    if not session.current:
        await send_json(
            ws,
            {
                "type": "error",
                "turnId": turn_id,
                "code": "empty_audio_buffer",
                "message": "No audio to commit",
            },
        )
        return
    current = bytes(session.current)
    session.current.clear()
    session.commits += 1
    await send_json(
        ws,
        {
            "type": "input_audio_buffer.committed",
            "turnId": turn_id,
            "pcmBytes": len(current),
        },
    )
    if not final:
        if len(session.pending) + len(current) > max_turn_bytes:
            session.pending.clear()
            await send_json(
                ws,
                {
                    "type": "error",
                    "turnId": turn_id,
                    "code": "audio_turn_overflow",
                    "message": f"Accumulated mock turn exceeded {max_turn_bytes} bytes",
                },
            )
            return
        session.pending.extend(current)
        log("commit", turnId=turn_id, final=False, bytes=len(current), pending=len(session.pending))
        return

    turn_pcm_len = len(session.pending) + len(current)
    session.pending.clear()
    log("commit", turnId=turn_id, final=True, bytes=len(current), turnBytes=turn_pcm_len)
    transcript = f"mock transcript {turn_pcm_len} bytes from {session.face}"
    reply = f"mock duplex reply for {session.face}"
    await send_json(
        ws,
        {
            "type": "conversation.item.input_audio_transcription.completed",
            "turnId": turn_id,
            "transcript": transcript,
        },
    )
    await send_json(ws, {"type": "response.text.delta", "turnId": turn_id, "delta": reply})
    await send_audio_delta(ws, turn_id, mock_mp3())
    await send_json(
        ws,
        {
            "type": "response.done",
            "turnId": turn_id,
            "facultySlug": session.faculty_slug,
            "facultyName": session.faculty_name,
        },
    )


async def health(_request: web.Request) -> web.Response:
    return web.Response(text="astrolabe voice-stream mock ok\n", content_type="text/plain")


async def voice_stream(request: web.Request) -> web.WebSocketResponse:
    max_turn_bytes = int(request.app["max_turn_bytes"])
    ws = web.WebSocketResponse(heartbeat=30)
    await ws.prepare(request)
    session = StreamSession()
    await send_json(
        ws,
        {
            "type": "session.created",
            "maxBufferBytes": max_turn_bytes,
            "audio": {"input": {"format": "pcm16", "sampleRateHz": 16000, "channels": 1}},
        },
    )
    log("open", peer=request.remote or "-")
    async for msg in ws:
        if msg.type == web.WSMsgType.BINARY:
            seq, capture_ms, pcm = decode_binary_frame(msg.data)
            session.current.extend(pcm)
            session.frames += 1
            session.bytes_received += len(pcm)
            log(
                "binary",
                seq=seq,
                captureMs=capture_ms,
                bytes=len(pcm),
                current=len(session.current),
                total=session.bytes_received,
            )
            continue
        if msg.type != web.WSMsgType.TEXT:
            continue
        try:
            event = json.loads(msg.data)
        except json.JSONDecodeError:
            await send_json(ws, {"type": "error", "code": "invalid_json", "message": "Invalid JSON"})
            continue
        event_type = event.get("type")
        if event_type == "session.update":
            update = event.get("session")
            if isinstance(update, dict):
                apply_session_update(session, update)
            await send_json(
                ws,
                {
                    "type": "session.updated",
                    "session": {
                        "face": session.face,
                        "facultySlug": session.faculty_slug,
                        "facultyName": session.faculty_name,
                        "sampleRateHertz": session.sample_rate_hz,
                    },
                },
            )
            log("session.update", face=session.face, faculty=session.faculty_slug)
            continue
        if event_type == "input_audio_buffer.commit":
            await commit_turn(
                ws,
                session,
                str(event.get("turnId") or f"turn-{session.commits + 1}"),
                event.get("final") is not False,
                max_turn_bytes,
            )
            continue
        if event_type == "input_audio_buffer.clear":
            session.current.clear()
            await send_json(ws, {"type": "input_audio_buffer.cleared"})
            continue
        await send_json(
            ws,
            {
                "type": "error",
                "code": "unknown_event",
                "message": f"Unknown event: {event_type}",
            },
        )
    log(
        "close",
        frames=session.frames,
        bytes=session.bytes_received,
        pending=len(session.pending),
        current=len(session.current),
    )
    return ws


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--max-turn-bytes", type=int, default=128 * 1024 * 1024)
    args = parser.parse_args()
    mock_mp3()
    app = web.Application()
    app["max_turn_bytes"] = args.max_turn_bytes
    app.router.add_get("/", health)
    app.router.add_get("/functions/v1/voice-stream", voice_stream)
    print(
        f"voice-stream-mock: listening on ws://{args.host}:{args.port}/functions/v1/voice-stream "
        f"(max_turn_bytes={args.max_turn_bytes})",
        flush=True,
    )
    web.run_app(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
