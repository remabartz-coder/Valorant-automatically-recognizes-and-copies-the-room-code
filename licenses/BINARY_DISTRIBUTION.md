# Public Binary Distribution

QueuePilot's MIT source and Windows package are distributed with the dependency
sources, build records and notices needed to rebuild and replace the shared
libraries. The package gate remains fail-closed: a build made with the older
BtbN daily FFmpeg archive cannot be marked as a public release.

## Corresponding sources

- Qt Base 6.8.3 is downloaded from the official archive by `scripts/fetch-release-materials.ps1`. The archive SHA-256, official checksum, SDK SPDX/configuration and nested `src/3rdparty` audit are preserved under `licenses/qt-release` and the release evidence archive.
- FFmpeg n8.1.3 is built from the fixed official tag by `scripts/build-ffmpeg.ps1`. External autodetection is disabled; the build records the exact configure command and hashes of every installed file.
- The FFmpeg source archive, its fixed SHA-512 and build instructions accompany the binary. The build does not use the BtbN daily package or external codec libraries.

The BtbN daily release remains documented only as a historical local-build input;
it must not be used by `-ForPublicRelease`.

## Other Release Material

- Include `LICENSE`, `THIRD_PARTY_NOTICES.md`, the complete `licenses/` directory, model provenance, Qt source/evidence and the FFmpeg source archive.
- Preserve ONNX Runtime's official MIT license and `ONNXRuntime-ThirdPartyNotices.txt`.
- Preserve the Qt SPDX, bundled-component copyright statements and applicable license texts. FreeType and Independent JPEG Group attribution is included in the top-level notices.
- Preserve the official Visual Studio redistribution list and Windows SDK redistribution list. The package copies CRT files from the Visual Studio `VC/Redist` directory and `d3dcompiler_47.dll` from the Windows SDK `Redist/D3D/x64` directory; these files retain Microsoft's terms.
- Keep compatible Qt and FFmpeg DLLs replaceable. Do not impose terms preventing debugging of user modifications to the libraries.
- Produce a clean archive without user settings, window titles, room-code history, recordings or logs.

`scripts/package.ps1 -ForPublicRelease` checks the source archives, FFmpeg build
manifest and official Microsoft lists before copying the evidence beside the
executable. A clean-package launch smoke test and the full CTest suite are still
required before uploading a Release asset.
