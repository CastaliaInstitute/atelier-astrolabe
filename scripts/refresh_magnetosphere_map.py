#!/usr/bin/env python3
"""Refresh the SPIFFS magnetosphere map from NASA CCMC realtime imagery.

The firmware consumes a 466x466 RGB565 asset from SPIFFS. This helper tries
to pull the current CCMC SWMF/BATSRUS magnetosphere image, converts it for the
watch face, and leaves the existing asset untouched if the realtime service is
unavailable.
"""

from __future__ import annotations

import argparse
import datetime as dt
import io
import re
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps

SIZE = 466
ROOT = Path(__file__).resolve().parents[1]
OUT_PNG = ROOT / "astrolabe175c" / "storage_seed" / "space" / "magnetosphere_466.png"
OUT_RGB565 = ROOT / "astrolabe175c" / "storage_seed" / "space" / "magnetosphere_466.rgb565"
CCMC_PAGE = "https://ccmc.gsfc.nasa.gov/cgi-bin/SWMFpred.cgi"
CCMC_OVERVIEW_PAGE = "https://ccmc.gsfc.nasa.gov/cgi-bin/display/RT_t.cgi"
SWMF2023_BASE = "https://iswa.ccmc.gsfc.nasa.gov/iswa_data_tree/model/geospace/SWMF2023-RT"
USER_AGENT = "Astrolabe-Faculty175-Magnetosphere-Refresh/1"


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def fetch(url: str, timeout: float = 20.0) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as res:
        return res.read()


def image_urls_from_page(url: str) -> list[str]:
    html = fetch(url).decode("utf-8", "replace")
    urls: list[str] = []
    pattern = re.compile(r"<img\s+[^>]*src=[\"']?([^\"'\s>]+)[^>]*alt=[\"']?([^\"'>]+)", re.I)
    for src, alt in pattern.findall(html):
        if "magnetosphere" not in alt.lower() and "magnetopause" not in alt.lower():
            continue
        full = urllib.parse.urljoin(url, src.replace("&amp;", "&"))
        urls.append(full)
    return urls


def hrefs_from_index(url: str) -> list[str]:
    html = fetch(url).decode("utf-8", "replace")
    return [urllib.parse.urljoin(url, href.replace("&amp;", "&")) for href in re.findall(r'href=["\']([^"\']+)["\']', html, re.I)]


def latest_swmf2023_image_url() -> str | None:
    """Find the newest current geospace plot in the public ISWA data tree."""
    now = dt.datetime.now(dt.UTC)
    years = [str(year) for year in range(now.year, now.year - 3, -1)]
    for product, suffix in (
        ("MagnetopausePosition", "_zcut_mp.gif"),
        ("YCutMagneticFieldLines", "_ycut2.gif"),
    ):
        product_url = f"{SWMF2023_BASE}/{product}/"
        try:
            available_years = set(years)
            available_years.update(href.rstrip("/").rsplit("/", 1)[-1] for href in hrefs_from_index(product_url) if re.search(r"/20\d\d/?$", href))
        except (OSError, urllib.error.URLError):
            available_years = set(years)
        for year in sorted((y for y in available_years if y.isdigit()), reverse=True):
            year_url = urllib.parse.urljoin(product_url, f"{year}/")
            try:
                months = [href.rstrip("/").rsplit("/", 1)[-1] for href in hrefs_from_index(year_url) if re.search(r"/\d\d/?$", href)]
            except (OSError, urllib.error.URLError) as exc:
                print(f"warn: could not read {year_url}: {exc}", file=sys.stderr)
                continue
            for month in sorted(months, reverse=True):
                month_url = urllib.parse.urljoin(year_url, f"{month}/")
                try:
                    images = [href for href in hrefs_from_index(month_url) if href.lower().endswith(suffix)]
                except (OSError, urllib.error.URLError) as exc:
                    print(f"warn: could not read {month_url}: {exc}", file=sys.stderr)
                    continue
                if images:
                    return sorted(images)[-1]
    return None


def candidate_urls() -> list[str]:
    urls: list[str] = []
    latest = latest_swmf2023_image_url()
    if latest is not None:
        urls.append(latest)
    for page in (CCMC_PAGE, CCMC_OVERVIEW_PAGE):
        try:
            urls.extend(image_urls_from_page(page))
        except (OSError, urllib.error.URLError) as exc:
            print(f"warn: could not read {page}: {exc}", file=sys.stderr)
    # Known CCMC SWMF realtime cygnets. These are kept after scraped URLs so
    # future page updates win without code changes.
    for cid in (42, 43, 578):
        urls.append(
            "https://iswa.gsfc.nasa.gov/IswaSystemWebApp/iSWACygnetStreamer?"
            f"timestamp=2038-01-23+00%3A44%3A00&window=-1&cygnetId={cid}"
        )
    seen: set[str] = set()
    deduped: list[str] = []
    for url in urls:
        if url not in seen:
            deduped.append(url)
            seen.add(url)
    return deduped


def convert_source(data: bytes) -> Image.Image:
    im = Image.open(io.BytesIO(data)).convert("RGB")
    bbox = color_plot_bbox(im)
    if bbox is not None:
        im = im.crop(bbox)
    im = ImageOps.contain(im, (SIZE, SIZE), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (SIZE, SIZE), (2, 7, 18))
    canvas.paste(im, ((SIZE - im.width) // 2, (SIZE - im.height) // 2))
    canvas = ImageEnhance.Color(canvas).enhance(1.18)
    canvas = ImageEnhance.Contrast(canvas).enhance(1.12)
    canvas = ImageEnhance.Brightness(canvas).enhance(0.92)
    return canvas


def color_plot_bbox(im: Image.Image) -> tuple[int, int, int, int] | None:
    """Crop NASA plot furniture away, keeping the colorful magnetosphere field."""
    col_counts = [0] * im.width
    row_counts = [0] * im.height
    pixels = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = pixels[x, y]
            bright = max(r, g, b)
            dark = min(r, g, b)
            if bright - dark > 28 and bright > 70:
                col_counts[x] += 1
                row_counts[y] += 1
    xs = [x for x, count in enumerate(col_counts) if count > im.height * 0.30]
    ys = [y for y, count in enumerate(row_counts) if count > im.width * 0.30]
    if not xs or not ys:
        return None
    left = max(0, min(xs))
    top = max(0, min(ys))
    right = min(im.width, max(xs) + 1)
    bottom = min(im.height, max(ys) + 1)
    if right - left < im.width // 3 or bottom - top < im.height // 3:
        return None
    return (left, top, right, bottom)


def write_rgb565(path: Path, im: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    for r, g, b in im.getdata():
        data.extend(struct.pack("<H", rgb565(r, g, b)))
    path.write_bytes(data)


def refresh() -> bool:
    for url in candidate_urls():
        try:
            data = fetch(url)
            im = convert_source(data)
        except (OSError, urllib.error.URLError, Image.UnidentifiedImageError) as exc:
            print(f"warn: skipping {url}: {exc}", file=sys.stderr)
            continue
        OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
        im.save(OUT_PNG)
        write_rgb565(OUT_RGB565, im)
        print(f"wrote {OUT_PNG}")
        print(f"wrote {OUT_RGB565}")
        print(f"source {url}")
        return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fallback-procedural",
        action="store_true",
        help="regenerate the procedural offline map if CCMC imagery cannot be fetched",
    )
    args = parser.parse_args()

    if refresh():
        return 0
    if args.fallback_procedural:
        import gen_magnetosphere_map

        gen_magnetosphere_map.main()
        return 0
    print("error: no CCMC magnetosphere image could be fetched; existing asset left unchanged", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
