Place this directory tree at the root of the P4 microSD card.

Expected runtime path:

```text
/sdcard/astrolabe/tarot/720/00-fool.png
/sdcard/astrolabe/moon/720/fullmoon.png
```

The firmware loads these 720px round tarot PNGs from SD first. If the card or
files are missing, it falls back to a smaller embedded deck.

The Moon face currently embeds its 720px greyscale texture in firmware for
dynamic phase rendering. The SD full-moon PNG is included as the matching
full-resolution source/payload asset for the P4 card.
