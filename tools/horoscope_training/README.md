# Multi-domain reading model training

This pipeline trains the esp-ai PLE TinyLM to verbalize structured facts for
Astrology, Jyotish, BaZi, Human Design, Tarot, Lenormand, and Synastry. It does not calculate
ephemerides, bodygraphs, or card draws. Deterministic chart/card code must pass
those facts as conditioning text. Named `Self`, `Spouse`, and `Children` fields
keep family members distinct.

## Prepare data

```sh
python3 tools/horoscope_training/generate_dataset.py \
  --rows 50000 --out data/horoscopes.jsonl
python3 tools/horoscope_training/prepare_tokens.py \
  --jsonl data/horoscopes.jsonl --out-dir data/horoscope
```

The upstream trainer expects `data/train_v4096.bin` and `data/val_v4096.bin`.
Copy or symlink the generated streams into the upstream esp32-ai checkout:

```sh
ln -sf "$PWD/data/horoscope/train_v4096.bin" /path/to/esp32-ai/data/train_v4096.bin
ln -sf "$PWD/data/horoscope/val_v4096.bin" /path/to/esp32-ai/data/val_v4096.bin
cd /path/to/esp32-ai
python src/train.py --arm ple --vocab 4096 --seq-len 512 \
  --steps 20000 --tag horoscope-v1
python src/export.py horoscope-v1-s0
```

For a useful result, mix these synthetic pairs with reviewed human-written
examples. Synthetic data teaches grounding and safety structure; it should not
be the only source of voice or interpretation.

## Create firmware assets

```sh
python3 tools/horoscope_training/emit_vocab_header.py \
  data/horoscope/bpe4096.json astrolabe185b/main/esp_ai/horoscope_vocab.h
```

The exported `model.bin` must be uploaded to the device at
`/sdcard/models/esp-ai/model.bin` using the Wi-Fi endpoint documented in the
Astrolabe 1.85B README. The model is not placed in SPI flash.

Tarot prompts use a three-card spread with `Situation`, `Challenge`, and
`Guidance` positions. Synastry prompts carry separate `PersonA` and `PersonB`
chart facts plus the optional family context.

Jyotish prompts carry labeled sidereal Lagna, Sun, Moon, and planetary
placements. BaZi prompts carry distinct Year, Month, Day, and Hour pillars plus
the Day Master. Firmware calculation remains authoritative; the model only
verbalizes supplied facts.

## Metal smoke artifact

A 1,000-step Apple-MPS run is stored locally as
`artifacts/esp-ai/family-five-domain-mps-v1-model.bin`. It reached validation
perplexity 1.05 and its exported int4 model passed the upstream C/PyTorch
golden export. This artifact is small enough to stage in the current 8 MB-
PSRAM smoke loader; production-sized models need the SD row-cache loader.
