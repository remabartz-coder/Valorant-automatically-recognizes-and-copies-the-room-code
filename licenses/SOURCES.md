# Dependency Sources and Builds

## Qt Base 6.8.3

- Official source archive: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
- Exact source commit: https://github.com/qt/qtbase/tree/c07c2d5a527a644d36e7853d55132ae38921682f
- `Qt-qtbase-6.8.3.spdx.json` is copied verbatim from the installed official Qt SDK's `sbom/qtbase-6.8.3.spdx.json`.
- `qtbase-6.8.3/LICENSES/` is the complete official `LICENSES/` directory at that commit. `UPSTREAM-FILES.json` records its raw download URLs and SHA-256 values.
- Qt's nested third-party sources and their build configuration must also remain available when distributing its DLLs. SPDX and license texts are provenance records, not substitutes for source.

## FFmpeg LGPL Shared Build

- Installed version: `n8.1.3-18-g46d899d7a0-20261013`, built with GCC 16.2.0.
- Exact FFmpeg source commit: `46d899d7a02bc63237472cc4897258d5ddf274ac`; source: https://github.com/FFmpeg/FFmpeg/tree/46d899d7a02bc63237472cc4897258d5ddf274ac
- Build-script snapshot: https://github.com/BtbN/FFmpeg-Builds/tree/63ac3265b0789ea16c7b1f8351cd2ad195a28722
- Release: https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-13-13-05
- Pinned archive: https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-13-13-05/ffmpeg-n8.1.3-18-g46d899d7a0-win64-lgpl-shared-8.1.zip
- Pinned archive SHA-256: `19D92DFB1481E72E4E8F94844BF609418297A646F33DEB6864C409CA752469C4`.
- Prior `latest` archive SHA-256: `6FBE3233F0B769CE6D906FC200EF0B874B681FA1554520F6880BFB45B4BEFA58`. Its different internal directory name produces a different ZIP hash. Never substitute one archive for the other without checking its expected hash.
- Actual configure flags and library versions are recorded in `FFmpeg-BUILD-CONFIG.txt`. The build enables version 3, uses FFmpeg shared libraries, and statically incorporates external dependencies.

BtbN's published retention policy keeps the most recent 14 daily builds and the
last build of each month for two years. This October 13 daily URL is therefore
not a durable dependency mirror. Keep a hash-verified copy of the pinned archive.
When the upstream asset has expired, obtain that exact archive from your own
verified backup and pass its local path to the installer:

```powershell
./scripts/install-deps.ps1 -FFmpegArchive C:/dependency-cache/ffmpeg-n8.1.3-18-g46d899d7a0-win64-lgpl-shared-8.1.zip
```

The installer still verifies the same SHA-256. This option only replaces the
FFmpeg download; it does not make installation of all other dependencies
offline. Publishing source for QueuePilot does not distribute the FFmpeg DLLs.
For a binary release, retain and publish the exact corresponding FFmpeg and
external-library sources as described in `BINARY_DISTRIBUTION.md`.

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
