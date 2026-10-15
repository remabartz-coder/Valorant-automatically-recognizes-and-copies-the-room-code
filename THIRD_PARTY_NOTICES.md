# Third-party Components

QueuePilot's own source code is available under the MIT license in `LICENSE`.
Third-party libraries, models, data and runtime files retain their own terms.
This document and the included license texts must accompany redistributions.

| Component | Version | License and included material |
| --- | --- | --- |
| Qt Base | 6.8.3 | LGPL-3.0-only option; `licenses/Qt-LGPL-3.0.txt`, `Qt-GPL-3.0.txt`, `Qt-qtbase-6.8.3.spdx.json` and `qtbase-6.8.3/LICENSES/` |
| FFmpeg official source build | `n8.1.3` | LGPL-2.1-or-later; `licenses/FFmpeg-LICENSE.txt` and the build configuration in the package. The public build disables external autodetection and restricted components. |
| ONNX Runtime | 1.31.0 | MIT; `licenses/ONNXRuntime-LICENSE.txt` and the official `ONNXRuntime-ThirdPartyNotices.txt` |
| PaddleOCR recognition model and dictionary | PP-OCRv3 English | Apache-2.0; copyright (c) 2016 PaddlePaddle Authors; `licenses/PaddleOCR-Apache-2.0.txt` |
| PaddleOCR text detector | PP-OCRv4 | Apache-2.0; same PaddleOCR license |
| RapidOCR converted ONNX models | Repository snapshot `1cfba2e90fc938db55889873735088de210cc173` | Apache-2.0 as declared in the archived upstream `licenses/RapidOCR-MODEL-CARD.md` |
| Microsoft Visual C++ runtime and Direct3D compiler redistributables | Selected by the local Windows build toolchain and Qt deployment tool | Microsoft redistribution terms apply; these files are not covered by QueuePilot's MIT license |

Qt is copyright (C) The Qt Company Ltd. and other contributors. Its bundled
third-party component copyright statements, source locations and license
identifiers are recorded in the included official Qt SPDX document. The complete
upstream Qt Base `LICENSES` directory is included without implying that every
listed component is used by QueuePilot. Standard and custom license texts are
kept under their upstream filenames. Portions of this software are copyright
(c) The FreeType Project (https://freetype.org/). All rights reserved. This
software is based in part on the work of the Independent JPEG Group.

QueuePilot dynamically links Qt and FFmpeg. Users may replace compatible DLLs
beside the executable and may modify or reverse engineer those components for
debugging their modifications, subject to their licenses. No Python interpreter
or Python ONNX wrapper is used by the application.

See `licenses/SOURCES.md` for pinned provenance and `models/README.md` for model
hashes. The public package's `sources/` directory contains the corresponding Qt
and FFmpeg archives, build records and Microsoft redistribution evidence.
