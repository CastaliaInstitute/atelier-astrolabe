# ESP32-P4-WIFI6-Touch-LCD-4C Backshell

Parametric OpenSCAD backshell for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C used by Astrolabe.

The model is based on Waveshare's published dimensions:

- 4 inch 720 x 720 round display
- 126.00 mm glass outer diameter
- 101.52 mm active display diameter
- 15.00 mm published display/board stack depth
- 85.50 x 65.00 mm rear PCB envelope

## Files

- `p4_4c_backshell.scad` - editable parametric CAD source
- `p4_4c_backshell.stl` - generated printable mesh, when exported

## Print

- Material: PETG or PLA+ for first fit, PETG/ASA for a warmer enclosure
- Layer height: 0.20 mm
- Walls: 3 or more
- Top/bottom: 5 or more
- Infill: 20-30%
- Orientation: flat back on bed, open circular lip facing up
- Supports: normally none, depending on slicer bridging around side cutouts

## Fit Notes

This is a first-fit shell. The major dimensions come from Waveshare, but connector cutouts and screw post locations should be tuned after a quick test print or caliper pass.

The 40 mm speaker grille is enabled by default. Set `enable_speaker_grille = false;` in the SCAD file if the speaker will be mounted elsewhere.

The model is configured as a closed shell for Qi power. By default it exposes no USB, side, speaker, or microphone holes.

It also includes internal Qi receiver features:

- shallow 48 mm coil pocket with a thinner plastic coupling window
- small receiver-PCB retaining clips
- no USB opening by default
- no button/tool pinholes by default
- no speaker grille or microphone through-holes by default

Optional cutouts can be enabled in the SCAD file for:

- USB-C power/programming
- second USB-UART connector
- camera connector
- USB-OTG
- microSD
- speaker wire exit
- BOOT/RESET tool pinholes
- speaker grille
- microphone acoustic ports

The backshell uses four mounting posts matching the board's four mounting holes.

Optional microphone acoustic ports can be enabled near the lower PCB edge for MIC1 and MIC2. Their positions are controlled by `mic_locs` in the SCAD file and should be nudged after the first fit if the ports are not centered over the microphones.

For Qi power, use a receiver module that outputs regulated 5 V with enough current for the P4, display, Wi-Fi, and audio. Feed the board's 5 V input/USB-C 5 V rail, not the RTC battery holder.

If the shell touches the glass or display stack too tightly, increase `glass_clearance`. If it feels loose, reduce it in 0.15 mm steps.
