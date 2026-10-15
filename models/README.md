# Recognition Model

PP-OCRv3 mobile English recognition model, used for single-line Latin room codes. The locator continues to use the PP-OCRv4 Chinese text detector.

- Model: https://huggingface.co/SWHL/RapidOCR/blob/1cfba2e90fc938db55889873735088de210cc173/PP-OCRv3/en_PP-OCRv3_rec_infer.onnx
- Model SHA-256: `EF7ABD8BD3629AE57EA2C28B425C1BD258A871B93FD2FE7C433946ADE9B5D9EA`.
- Locator detector: https://huggingface.co/SWHL/RapidOCR/blob/1cfba2e90fc938db55889873735088de210cc173/PP-OCRv4/ch_PP-OCRv4_det_infer.onnx
- Detector SHA-256: `D2A7720D45A54257208B1E13E36A8479894CB74155A5EFE29462512D42F49DA9`.
- Dictionary: https://github.com/PaddlePaddle/PaddleOCR/blob/8cce9b6fd7ccb50226d0c38f94054d81c29b8184/ppocr/utils/en_dict.txt
- Dictionary SHA-256: `5662DF9D2D03F0E8CA0D3B0649D6ACBAB904B6A14B3D3521463C71C37C668CE3`.
- Preprocessing: BGR, float32 CHW, height 48, aspect ratio preserved, zero padding after normalization to [-1, 1].
- Decode: CTC greedy, blank class 0, repeated symbols collapsed, appended space when required by model class count.
- Confidence: minimum probability among emitted symbols, not a calibrated chance that the room code is correct.

The installed filenames remain `recognizer.onnx`, `keys.txt` and `detector.onnx`. The model and its English dictionary must be replaced together. Lowercase output is normalized to uppercase; visually similar characters such as `I/1` and `O/0` are not substituted. The default room-code rule requires exactly three uppercase ASCII letters and three ASCII digits in any order. The optional letters-first rule additionally requires `^[A-Z]{3}[0-9]{3}$`.

The converted models are distributed by SWHL/RapidOCR, whose pinned model card declares Apache-2.0 and identifies them as conversions of PaddleOCR models. The unmodified card is included in `licenses/RapidOCR-MODEL-CARD.md`; PaddleOCR's license and copyright are preserved in `licenses/PaddleOCR-Apache-2.0.txt`. QueuePilot does not modify the model weights or dictionary. `licenses/SOURCES.md` records provenance; these third-party assets are not relicensed under QueuePilot's MIT license.

Measurements made with the previous Chinese recognizer do not establish this model's accuracy or confidence thresholds. Evaluate real cropped room codes separately from synthetic fixtures.

Continuous recognition detects text lines within the selected area and caches their geometry, then recognizes fresh frame pixels. This supports wider input-field or dialog selections; multiple plausible codes remain ambiguous. On-demand auto-location uses the PP-OCRv4 text detector followed by code recognition and format validation. Auto-location itself does not emit clipboard or keyboard output.
