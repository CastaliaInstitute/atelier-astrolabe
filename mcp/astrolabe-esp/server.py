#!/usr/bin/env python3
"""MCP server: PlatformIO build/upload, USB serial monitor, OpenOCD/GDB for Astrolabe watch."""
from __future__ import annotations

import asyncio
import os
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Optional

import sys

_MCP_DIR = Path(__file__).resolve().parent
if str(_MCP_DIR) not in sys.path:
    sys.path.insert(0, str(_MCP_DIR))

from mcp.server.fastmcp import FastMCP

from console_analysis import analyze_serial_lines, format_review_report, publish_latest_artifacts

_DEFAULT_ROOT = Path(__file__).resolve().parents[2]
REPO_ROOT = Path(
    os.environ.get("ASTROLABE_ROOT") or os.environ.get("POCKETMYNAH_ROOT") or _DEFAULT_ROOT
)
DEFAULT_ENV = os.environ.get("ASTROLABE_PIO_ENV", "waveshare_s3_175")
DEBUG_ENV = os.environ.get("ASTROLABE_PIO_DEBUG_ENV", "waveshare_s3_175_debug")
DEFAULT_BAUD = int(os.environ.get("ASTROLABE_BAUD", "115200"))
MONITOR_DIR = REPO_ROOT / "artifacts" / "monitor"
OPENOCD = Path.home() / ".platformio/packages/tool-openocd-esp32/bin/openocd"
OPENOCD_SCRIPTS = Path.home() / ".platformio/packages/tool-openocd-esp32/share/openocd/scripts"
GDB = (
    Path.home()
    / ".platformio/packages/toolchain-xtensa-esp32s3@8.4.0+2021r2-patch5/bin/xtensa-esp32s3-elf-gdb"
)

mcp = FastMCP("astrolabe-esp")

# Debug ELF symbols (xtensa-esp32s3-elf-nm on waveshare_s3_175_debug); statics in pm_clock.cpp / Astrolabe.ino.
SYM_CLOCK_FACE = "_ZL12s_clock_face"
SYM_CLOCK_REPAINT = "_ZL23g_clock_repaint_pending"

FACE_BY_NAME = {
    "classic": 0,
    "analog": 0,
    "hue": 0,
    "apocalypso": 1,
    "digital": 2,
    "spotify": 3,
    "astro": 4,
    "astrology": 4,
    "moon": 5,
    "calcifer": 6,
    "schedule": 6,
    "castalia": 7,
    "settings": 8,
    "wifi": 8,
    "syn": 9,
    "synastry": 9,
    "spectrum": 10,
    "fft": 10,
    "audio": 10,
    "sound": 10,
    "chakra": 11,
    "bowl": 12,
    "tibetan": 12,
    "tibetan_bowl": 12,
    "rocket": 13,
    "launch": 13,
    "launchclock": 13,
    "radar": 14,
    "presence": 14,
    "peers": 14,
    "faculty": 15,
    "fac": 15,
    "weather": 16,
    "quotes": 17,
    "quote": 17,
    "qotd": 31,
    "question": 31,
    "questionofday": 31,
    "questionoftheday": 31,
    "transits": 18,
    "livetransits": 18,
    "live": 18,
    "tarot": 19,
    "cards": 19,
    "notes": 20,
    "note": 20,
    "commonplace": 20,
    "ocarina": 21,
    "ocarinaface": 21,
    "flute": 21,
    "bongo": 22,
    "drum": 22,
    "drums": 22,
    "piano": 23,
    "keys": 23,
    "keyboard": 23,
    "level": 24,
    "bubble": 24,
    "bubblelevel": 24,
    "imu": 24,
    "tuning": 25,
    "tuner": 25,
    "staff": 25,
    "pitch": 25,
    "pandrum": 26,
    "pandrumface": 26,
    "pandrom": 26,
    "pandromface": 26,
    "handpan": 26,
    "hang": 26,
    "alethiometer": 27,
    "aleth": 27,
    "compass": 27,
    "goldencompass": 27,
    "runes": 28,
    "rune": 28,
    "futhark": 28,
    "fortune": 28,
    "orientation": 29,
    "orient": 29,
    "heading": 29,
    "relativeheading": 29,
    "luopan": 30,
    "fengshui": 30,
}
FACE_LABELS = (
    "ClassicAnalog",
    "Apocalypso",
    "DigitalLocal",
    "Spotify",
    "Astrology",
    "Moon",
    "CalciferCountdown",
    "Castalia",
    "Settings",
    "Synastry",
    "Spectrum",
    "Chakra",
    "TibetanBowl",
    "LaunchClock",
    "Radar",
    "Faculty",
    "Weather",
    "Quotes",
    "LiveTransits",
    "Tarot",
    "Notes",
    "Ocarina",
    "Bongo",
    "Piano",
    "Level",
    "Tuning",
    "PanDrum",
    "Alethiometer",
    "Runes",
    "Orientation",
    "Luopan",
    "QuestionOfDay",
)


def _run(cmd: list[str], cwd: Path, timeout: Optional[int] = None) -> tuple[int, str, str]:
    try:
        p = subprocess.run(
            cmd,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            timeout=timeout,
            env={**os.environ, "ASTROLABE_ROOT": str(REPO_ROOT)},
        )
        return p.returncode, p.stdout or "", p.stderr or ""
    except subprocess.TimeoutExpired as e:
        partial = (e.stdout or "") + (e.stderr or "")
        return 124, partial, f"timeout after {timeout}s"


def _find_usb_port() -> Optional[str]:
    try:
        import serial.tools.list_ports
    except ImportError:
        return None
    for p in serial.tools.list_ports.comports():
        if p.device and "usbmodem" in p.device:
            if p.vid == 0x303A or (p.description and "JTAG" in p.description):
                return p.device
    for p in serial.tools.list_ports.comports():
        if p.device and "usbmodem" in p.device:
            return p.device
    return None


def _resolve_port(port: str) -> str:
    if port.strip():
        return port.strip()
    found = _find_usb_port()
    if not found:
        raise RuntimeError("No /dev/cu.usbmodem* found — plug in the watch (Espressif 303A:1001).")
    return found


def _default_serial_log_path() -> Path:
    MONITOR_DIR.mkdir(parents=True, exist_ok=True)
    return MONITOR_DIR / f"serial-{int(time.time())}.log"


def _usb_reset_boot(ser) -> None:
    """DTR toggle to reset ESP32-S3 so boot log appears on CDC."""
    try:
        ser.setDTR(False)
        time.sleep(0.08)
        ser.setDTR(True)
        time.sleep(0.08)
        ser.setDTR(False)
        time.sleep(0.35)
    except Exception:
        pass


def _serial_capture(
    dev: str,
    baud: int,
    duration_sec: int,
    send_line: str,
    reset_boot: bool,
    log_path: Path,
    stop_pattern: str = "",
) -> tuple[list[str], str]:
    import serial

    lines: list[str] = []
    stop_pat = stop_pattern.strip()
    ser = serial.Serial(dev, baud, timeout=0.25)
    try:
        ser.reset_input_buffer()
        if reset_boot:
            _usb_reset_boot(ser)
            lines.append(">> reset: DTR boot pulse")
        else:
            time.sleep(0.3)
        if send_line.strip():
            ser.write((send_line.strip() + "\n").encode("utf-8"))
            ser.flush()
            lines.append(f">> sent: {send_line.strip()}")
        t0 = time.time()
        with open(log_path, "w", encoding="utf-8") as logf:
            logf.write(f"# port={dev} baud={baud}\n")
            while time.time() - t0 < duration_sec:
                chunk = ser.read(65536)
                if not chunk:
                    continue
                text = chunk.decode("utf-8", errors="replace")
                logf.write(text)
                logf.flush()
                for ln in text.splitlines():
                    ln = ln.rstrip()
                    if ln:
                        lines.append(ln)
                if stop_pat and stop_pat in text:
                    lines.append(f">> stop: matched {stop_pat!r}")
                    break
    finally:
        ser.close()
    return lines, str(log_path)


async def _capture_and_review(
    *,
    duration_sec: int,
    port: str,
    baud: int,
    reset_boot: bool,
    send_line: str,
    log_file: str,
    stop_pattern: str,
    tail_lines: int,
) -> str:
    try:
        import serial  # noqa: F401
    except ImportError:
        return "pyserial missing in MCP venv — run mcp/astrolabe-esp/setup.sh"

    try:
        dev = _resolve_port(port)
    except RuntimeError as e:
        return str(e)

    duration_sec = max(5, min(int(duration_sec), 600))
    tail_lines = max(20, min(int(tail_lines), 200))
    log_path = Path(log_file.strip()) if log_file.strip() else _default_serial_log_path()

    lines, log_file_str = await asyncio.to_thread(
        _serial_capture,
        dev,
        baud,
        duration_sec,
        send_line,
        reset_boot,
        log_path,
        stop_pattern,
    )
    review = analyze_serial_lines(lines)
    latest_log, latest_json = publish_latest_artifacts(MONITOR_DIR, log_file_str, review)
    report = format_review_report(
        review, port=dev, log_path=log_file_str, tail_lines=lines[-tail_lines:]
    )
    return (
        f"mode=console_review\nlatest_log={latest_log}\nlatest_review={latest_json}\n"
        f"review_json={review}\n---\n{report}"
    )


def _openocd_capture(duration_sec: int, log_path: Path) -> tuple[int, str]:
    if not OPENOCD.is_file():
        raise RuntimeError(f"OpenOCD not found: {OPENOCD}\nRun: pio pkg install -e {DEBUG_ENV}")

    with open(log_path, "w", encoding="utf-8") as logf:
        logf.write(f"# openocd started {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        proc = subprocess.Popen(
            [
                str(OPENOCD),
                "-s",
                str(OPENOCD_SCRIPTS),
                "-f",
                "board/esp32s3-builtin.cfg",
                "-c",
                "adapter speed 5000",
                "-c",
                "set ESP_FLASH_SIZE 16MB",
                "-c",
                "init",
            ],
            stdout=logf,
            stderr=subprocess.STDOUT,
            cwd=str(REPO_ROOT),
        )
        try:
            proc.wait(timeout=max(5, duration_sec))
            code = proc.returncode or 0
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)
            code = 0
        logf.write(f"\n# openocd stopped after {duration_sec}s\n")

    tail = log_path.read_text(encoding="utf-8", errors="replace")
    return code, tail


def _format_log_body(lines: list[str], max_len: int = 24000) -> str:
    body = "\n".join(lines)
    if len(body) > max_len:
        body = body[: max_len // 2] + "\n…\n" + body[-max_len // 2 :]
    return body


@mcp.tool()
async def astrolabe_list_ports() -> str:
    """List serial ports (PlatformIO + pyserial). Use before upload/monitor."""
    code, out, err = _run(["pio", "device", "list"], REPO_ROOT, timeout=30)
    lines = [f"pio exit {code}", out]
    try:
        import serial.tools.list_ports

        lines.append("\npyserial:")
        for p in serial.tools.list_ports.comports():
            lines.append(f"  {p.device}  vid={p.vid:#06x} pid={p.pid:#06x}  {p.description}")
    except ImportError:
        lines.append("\n(pyserial not installed in MCP venv)")
    if err:
        lines.append(f"\nstderr:\n{err}")
    return "\n".join(lines)


@mcp.tool()
async def astrolabe_build(env: str = DEFAULT_ENV) -> str:
    """PlatformIO build for Astrolabe (default waveshare_s3_175)."""
    code, out, err = _run(["pio", "run", "-e", env], REPO_ROOT, timeout=600)
    tail = (out + err)[-12000:]
    return f"exit={code}\n--- tail ---\n{tail}"


@mcp.tool()
async def astrolabe_upload(env: str = DEFAULT_ENV, port: str = "") -> str:
    """Build (if needed) and upload firmware via esptool."""
    try:
        p = _resolve_port(port)
    except RuntimeError as e:
        return str(e)
    code, out, err = _run(
        ["pio", "run", "-e", env, "-t", "upload", "--upload-port", p],
        REPO_ROOT,
        timeout=180,
    )
    tail = (out + err)[-12000:]
    return f"port={p}\nexit={code}\n--- tail ---\n{tail}"


@mcp.tool()
async def astrolabe_serial_monitor(
    duration_sec: int = 90,
    send_line: str = "",
    port: str = "",
    baud: int = DEFAULT_BAUD,
    reset_boot: bool = True,
    log_file: str = "",
) -> str:
    """
    Capture firmware console on USB CDC (115200): Serial.printf, ESP_LOG, boot banners.
    Same cable as upload; this is the primary log path on ESP32-S3.
  """
    try:
        import serial  # noqa: F401
    except ImportError:
        return "pyserial missing in MCP venv — run mcp/astrolabe-esp/setup.sh"

    try:
        dev = _resolve_port(port)
    except RuntimeError as e:
        return str(e)

    duration_sec = max(5, min(int(duration_sec), 600))
    log_path = Path(log_file.strip()) if log_file.strip() else _default_serial_log_path()

    lines, log_file_str = await asyncio.to_thread(
        _serial_capture, dev, baud, duration_sec, send_line, reset_boot, log_path, ""
    )
    body = _format_log_body(lines)
    return (
        f"mode=serial\nport={dev}\nbaud={baud}\nlog={log_file_str}\nlines={len(lines)}\n"
        f"note=JTAG does not replace CDC; use astrolabe_jtag_* for debug halt/GDB.\n"
        f"tip=use astrolabe_console_review for automatic triage.\n---\n{body}"
    )


@mcp.tool()
async def astrolabe_console_review(
    duration_sec: int = 30,
    port: str = "",
    baud: int = DEFAULT_BAUD,
    reset_boot: bool = True,
    send_line: str = "",
    stop_pattern: str = "",
    log_file: str = "",
    tail_lines: int = 80,
) -> str:
    """
    Capture USB serial, triage boot health (panics, reboot loops, WiFi+BT coexistence),
    and write artifacts/monitor/latest.log + latest-review.json.
    Prefer this over raw astrolabe_serial_monitor when debugging crashes or after flash.
    """
    return await _capture_and_review(
        duration_sec=duration_sec,
        port=port,
        baud=baud,
        reset_boot=reset_boot,
        send_line=send_line,
        log_file=log_file,
        stop_pattern=stop_pattern,
        tail_lines=tail_lines,
    )


@mcp.tool()
async def astrolabe_monitor(
    duration_sec: int = 90,
    send_line: str = "",
    port: str = "",
    baud: int = DEFAULT_BAUD,
) -> str:
    """Alias for astrolabe_serial_monitor (USB CDC console)."""
    return await astrolabe_serial_monitor(
        duration_sec=duration_sec,
        send_line=send_line,
        port=port,
        baud=baud,
        reset_boot=True,
        log_file="",
    )


@mcp.tool()
async def astrolabe_jtag_openocd(duration_sec: int = 30) -> str:
    """
    Run OpenOCD (esp32s3-builtin USB-JTAG) and capture adapter log.
    Use for JTAG link bring-up; firmware printf still appears on USB serial (astrolabe_serial_monitor).
    """
    duration_sec = max(5, min(int(duration_sec), 120))
    MONITOR_DIR.mkdir(parents=True, exist_ok=True)
    log_path = MONITOR_DIR / f"openocd-{int(time.time())}.log"
    try:
        code, tail = await asyncio.to_thread(_openocd_capture, duration_sec, log_path)
    except RuntimeError as e:
        return str(e)
    if len(tail) > 20000:
        tail = tail[:10000] + "\n…\n" + tail[-10000:]
    return f"mode=jtag-openocd\nexit={code}\nlog={log_path}\n---\n{tail}"


@mcp.tool()
async def astrolabe_jtag_gdb(
    gdb_commands: str = "monitor reset halt\ninfo registers\nbacktrace 5\ncontinue",
    env: str = DEBUG_ENV,
    openocd_wait_sec: int = 4,
) -> str:
    """
    OpenOCD + GDB batch on debug ELF: halt, inspect, inject state, continue.
    Build first: pio run -e waveshare_s3_175_debug. Does not stream Serial — pair with astrolabe_serial_monitor.
    """
    return await astrolabe_debug_gdb(
        gdb_commands=gdb_commands, env=env, openocd_wait_sec=openocd_wait_sec
    )


@mcp.tool()
async def astrolabe_console_monitor(
    duration_sec: int = 60,
    port: str = "",
    reset_boot: bool = True,
    include_jtag_log: bool = True,
    jtag_log_sec: int = 12,
) -> str:
    """
    Capture serial console and (optionally) a short OpenOCD log in parallel.
    Best for bring-up: serial shows printf; JTAG log confirms debug adapter.
    """
    duration_sec = max(10, min(int(duration_sec), 300))
    jtag_log_sec = max(5, min(int(jtag_log_sec), 60))

    async def _serial() -> str:
        return await astrolabe_serial_monitor(
            duration_sec=duration_sec,
            port=port,
            reset_boot=reset_boot,
            log_file="",
        )

    async def _jtag() -> str:
        return await astrolabe_jtag_openocd(duration_sec=jtag_log_sec)

    if include_jtag_log:
        serial_out, jtag_out = await asyncio.gather(_serial(), _jtag())
        return f"=== serial ===\n{serial_out}\n\n=== jtag (openocd) ===\n{jtag_out}"
    return await _serial()


@mcp.tool()
async def astrolabe_debug_gdb(
    gdb_commands: str = "monitor reset halt\nbreak loop\ncontinue",
    env: str = DEBUG_ENV,
    openocd_wait_sec: int = 4,
) -> str:
    """Run OpenOCD (esp32s3-builtin) + GDB batch on debug ELF. Commands newline-separated."""
    if not GDB.is_file():
        return f"GDB not found: {GDB}\nRun a debug build first."
    elf = REPO_ROOT / ".pio/build" / env / "firmware.elf"
    if not elf.is_file():
        return f"ELF missing: {elf}\nRun: pio run -e {env}"

    if not OPENOCD.is_file():
        return f"OpenOCD not found: {OPENOCD}\nInstall PlatformIO esp32 platform."

    gdb_script = REPO_ROOT / "debug" / "mcp_gdb_batch.gdb"
    gdb_script.parent.mkdir(parents=True, exist_ok=True)
    gdb_script.write_text(
        "set pagination off\n"
        "set remotetimeout 60\n"
        "target extended-remote :3333\n"
        "monitor gdb_memory_map disable\n"
        + gdb_commands.strip()
        + "\nquit\n",
        encoding="utf-8",
    )

    oc_log = MONITOR_DIR / f"openocd-gdb-{int(time.time())}.log"
    MONITOR_DIR.mkdir(parents=True, exist_ok=True)
    oc_proc = subprocess.Popen(
        [
            str(OPENOCD),
            "-s",
            str(OPENOCD_SCRIPTS),
            "-f",
            "board/esp32s3-builtin.cfg",
            "-c",
            "adapter speed 5000",
            "-c",
            "set ESP_FLASH_SIZE 16MB",
            "-c",
            "init",
        ],
        stdout=open(oc_log, "w"),
        stderr=subprocess.STDOUT,
        cwd=str(REPO_ROOT),
    )
    await asyncio.sleep(max(4, min(openocd_wait_sec, 20)))

    code, out, err = await asyncio.to_thread(
        _run,
        [str(GDB), "-batch", "-x", str(gdb_script), str(elf)],
        REPO_ROOT,
        120,
    )
    oc_proc.terminate()
    try:
        oc_proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        oc_proc.kill()

    oc_tail = oc_log.read_text(encoding="utf-8", errors="replace")[-4000:] if oc_log.is_file() else ""
    return (
        f"elf={elf}\ngdb exit={code}\n--- gdb ---\n{out}\n{err}\n"
        f"--- openocd log ({oc_log}) ---\n{oc_tail}"
    )


def _resolve_face_index(face: int | str) -> int:
    if isinstance(face, int):
        idx = face
    else:
        key = str(face).strip().lower().replace("-", "").replace("_", "")
        if key.isdigit():
            idx = int(key)
        elif key not in FACE_BY_NAME:
            raise ValueError(
                f"Unknown face {face!r}. Use 0–{len(FACE_LABELS) - 1} or: {', '.join(sorted(set(FACE_BY_NAME)))}"
            )
        else:
            idx = FACE_BY_NAME[key]
    if idx < 0 or idx >= len(FACE_LABELS):
        raise ValueError(f"face index {idx} out of range 0..{len(FACE_LABELS) - 1}")
    return idx


@mcp.tool()
async def astrolabe_debug_set_face(face: int | str = 4, repaint: bool = True) -> str:
    """
    Halt via JTAG/GDB and set clock face (debug ELF only: waveshare_s3_175_debug).
    face: 0–25 or name (astro, moon, rocket, radar, faculty, weather, notes, ocarina, bongo, piano, level, pandrum, classic, …). Sets s_clock_face + g_clock_repaint_pending.
    """
    try:
        idx = _resolve_face_index(face)
    except ValueError as e:
        return str(e)
    repaint_line = f"set {SYM_CLOCK_REPAINT} = 1\n" if repaint else ""
    cmds = (
        "monitor reset halt\n"
        "break loop\n"
        "continue\n"
        f"set {SYM_CLOCK_FACE} = {idx}\n"
        f"{repaint_line}"
        "continue\n"
        "detach\n"
    )
    label = FACE_LABELS[idx]
    result = await astrolabe_debug_gdb(gdb_commands=cmds)
    return f"face={idx} ({label})\n{result}"


@mcp.tool()
async def astrolabe_debug_inject_astro_boot() -> str:
    """JTAG: switch to Astrology face and request repaint (then use BOOT or serial `astro` for TTS)."""
    return await astrolabe_debug_set_face(face=4, repaint=True)


@mcp.tool()
async def astrolabe_debug_astro_session(
    upload: bool = True,
    monitor_sec: int = 360,
    port: str = "",
) -> str:
    """
    Debugger workflow: optional debug upload, JTAG set Astrology face, serial `astro`, capture log.
    Requires watch on USB (303A:1001).
    """
    parts: list[str] = []
    if upload:
        up = await astrolabe_upload(env=DEBUG_ENV, port=port)
        parts.append(f"=== upload debug ===\n{up}")
    face = await astrolabe_debug_set_face(face=4, repaint=True)
    parts.append(f"=== jtag face ===\n{face}")
    await asyncio.sleep(3)
    mon = await astrolabe_serial_monitor(
        duration_sec=max(60, min(int(monitor_sec), 600)),
        send_line="astro",
        port=port,
        reset_boot=False,
    )
    parts.append(f"=== serial (astro) ===\n{mon}")
    return "\n\n".join(parts)


@mcp.tool()
async def astrolabe_build_upload_monitor(
    env: str = DEFAULT_ENV,
    monitor_sec: int = 120,
    send_line: str = "astro",
    port: str = "",
) -> str:
    """Upload then capture serial console (common TTS debug flow)."""
    up = await astrolabe_upload(env=env, port=port)
    mon = await astrolabe_console_review(
        duration_sec=monitor_sec,
        send_line=send_line,
        port=port,
        reset_boot=True,
        stop_pattern="Mynah Astrolabe ready",
    )
    return f"=== upload ===\n{up}\n\n=== console_review ===\n{mon}"


if __name__ == "__main__":
    mcp.run(transport="stdio")
