# Room Code References

These PNGs are unmodified crops of the two screenshots supplied by the user, who confirmed the ambiguous characters are letters I and O.

| Fixture | Original Size | Crop X, Y, Width, Height | Confirmed Text |
| --- | --- | --- | --- |
| AZI973.png | 1479 x 1062 | 1280, 775, 155, 48 | AZI973 |
| DOB453.png | 2560 x 1440 | 1015, 675, 115, 38 | DOB453 |

Run `tests/verify-reference.ps1` after building. It checks the raw, unchanged crop result exactly. The probe also saves diagnostic image variants, but their scores do not select a production result.

Continuous recognition now also exposes a guarded `recognizeCode()` path. It preserves the raw text and score, and only tries line normalization for uncertain results on an isolated six-glyph panel. Both fixed paddings must agree and reach the unchanged confidence threshold; it does not pick the highest-scoring diagnostic variant. These two reference crops retain their original text and confidence.

The first screenshot contains scaled, compressed text. An exact read below the default 0.90 minimum-character threshold is diagnostic evidence, not permission to publish the code. These fixtures do not measure live broadcast accuracy or queue success.

## Native Recording References

The user supplied `ScreenRecording_10-15-2026 09-40-44_1.mp4` (2420 x 1668, 39.309 seconds). The following PNGs retain original pixels from the wide input field at `(647, 762, 591, 78)`. KOG460 was explicitly confirmed by the user; the other labels were checked visually in the recording.

| Fixture | Approximate Source Time | Expected Text |
| --- | --- | --- |
| KOG460-native-wide.png | 4 s | KOG460 |
| ZUJ215-native-wide.png | 12 s | ZUJ215 |
| TEJ065-native-wide.png | 20 s | TEJ065 |
| hidden-native-wide.png | 28 s | No visible code; six dots |
| EHQ220-native-wide.png | 36 s | EHQ220 |

These cases reproduce the former failure with large horizontal margins. OCR checks require exact text, unchanged raw evidence, the 0.90 threshold and independent-frame confirmation; hidden pixels must never generate a code. Detector-core checks also require agreement between two fixed crops, rather than selecting the most confident crop. All valid production codes now require exactly three uppercase ASCII letters and three digits.

The separate `real_video_tests` uses 157 original frames from the entire recording at 4 FPS and checks all eight visible codes. Its JSON report lists the manually read frame ranges and local processing measurements. Repeated stills are used only for negative tests, not counted as independent recording frames.
