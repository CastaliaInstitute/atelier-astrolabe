# Wand face

Tiny Astrolabe voice pendant UI on the **round watch**: full-time STT and a single faculty conversation (`face=wand`).

| Target | Path | Voice `face` |
|--------|------|--------------|
| M5 AtomS3R + Atomic Voice Base | [`facultyatom/`](../../facultyatom/) native ESP-IDF | **`faculty`** |
| Waveshare ESP32-S3 Touch AMOLED 1.8″ | [`faculty18/`](../../faculty18/) native ESP-IDF | **`faculty`** |
| Round watch | `ClockFace::Wand` in this sketch | **`wand`** |

- FacultyAtom contract: [`include/astrolabe_faculty_atom_face.h`](../../include/astrolabe_faculty_atom_face.h)
- Round watch Wand contract: [`include/astrolabe_wand_face.h`](../../include/astrolabe_wand_face.h)
- Waveshare faculty: [`include/astrolabe_faculty_face.h`](../../include/astrolabe_faculty_face.h)
