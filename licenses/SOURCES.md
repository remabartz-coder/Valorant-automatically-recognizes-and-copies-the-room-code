# Dependency Sources and Builds

## Qt Base 6.8.3

- Official source archive: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
- Exact source commit: https://github.com/qt/qtbase/tree/c07c2d5a527a644d36e7853d55132ae38921682f
- `Qt-qtbase-6.8.3.spdx.json` is copied verbatim from the installed official Qt SDK's `sbom/qtbase-6.8.3.spdx.json`.
- `qtbase-6.8.3/LICENSES/` is the complete official `LICENSES/` directory at that commit. `UPSTREAM-FILES.json` records its raw download URLs and SHA-256 values.
- Qt's nested third-party sources and their build configuration must also remain available when distributing its DLLs. SPDX and license texts are provenance records, not substitutes for source.

## FFmpeg LGPL Shared Build

- Version: `n8.1.3` (official source tag).
- Source archive: https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.1.3.tar.gz
- SHA-512: `8c667090bcdc70796caaa8cdc2cf82e57d7c7f6feedcb0ec7251cefd6a0c63b7cad203a86c800d3e2f69ca91255167aa4e631701060b96d576d1a9666ca7aafe`.
- `scripts/build-ffmpeg.ps1` builds the shared libraries with MSVC, disables autodetected external libraries, GPL, nonfree and version-3 components, and enables the Windows Schannel HTTPS path. `FFmpeg-BUILD-CONFIG.txt` in the resulting package records the actual configure flags.

The public Windows package is built from this fixed source archive. The exact
archive and the build script are included in the corresponding-sources Release
asset.

## ONNX Runtime 1.31.0

- Official source: https://github.com/microsoft/onnxruntime/tree/v1.31.0
- Official Python wheel used solely as the container for the native DLL: https://files.pythonhosted.org/packages/37/de/6cab7e39917cc87728d2f00abe97c81fe86b29f9e1f758627864c28f0c21/onnxruntime-1.31.0-cp311-cp311-win_amd64.whl
- Wheel SHA-256: `317608967B03807ED4661113B08293FAC02A1DB6496A6863A07D9F19232936AD`.
- `ONNXRuntime-LICENSE.txt` and `ONNXRuntime-ThirdPartyNotices.txt` are copied verbatim from that wheel's `onnxruntime/` directory. Including its complete notices preserves upstream information; it does not imply every listed optional component is active in this build.

## OCR Models And Dictionary

- PaddleOCR source snapshot: https://github.com/PaddlePaddle/PaddleOCR/tree/8cce9b6fd7ccb50226d0c38f94054d81c29b8184
- Converted-model repository snapshot: https://huggingface.co/SWHL/RapidOCR/tree/1cfba2e90fc938db55889873735088de210cc173
- Recognition model: https://huggingface.co/SWHL/RapidOCR/blob/1cfba2e90fc938db55889873735088de210cc173/PP-OCRv3/en_PP-OCRv3_rec_infer.onnx
- Text detector: https://huggingface.co/SWHL/RapidOCR/blob/1cfba2e90fc938db55889873735088de210cc173/PP-OCRv4/ch_PP-OCRv4_det_infer.onnx
- English dictionary: https://github.com/PaddlePaddle/PaddleOCR/blob/8cce9b6fd7ccb50226d0c38f94054d81c29b8184/ppocr/utils/en_dict.txt
- All three production asset hashes are listed in `models/README.md` and checked by `scripts/install-deps.ps1`.
- `RapidOCR-MODEL-CARD.md` is the unmodified upstream card at the pinned snapshot; it declares `apache-2.0` and describes the PaddleOCR-to-ONNX conversion.

No dependency library or model has been modified by QueuePilot. The application
does not impose restrictions on replacing compatible shared libraries or
debugging modifications to them. See `THIRD_PARTY_NOTICES.md` and the individual
licenses for their terms.
