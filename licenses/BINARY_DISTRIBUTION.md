# Public Binary Release Requirements

QueuePilot's MIT source can be published with its notices and dependency download
instructions. The current local Windows build is not yet ready for unrestricted
public binary redistribution. Adding license texts alone does not clear the
following requirements.

## Corresponding Source

Before publishing a ZIP containing Qt or FFmpeg DLLs, provide the exact
corresponding source in a manner allowed by their licenses. A practical release
layout places verified source archives and build instructions alongside the
binary download, at no additional charge. Keep those sources available for the
period required by the chosen license-distribution mechanism. Do not make an
unsupported written source offer.

- Qt: retain the Qt Base 6.8.3 source, its included third-party sources, and the configuration/build material needed for the shipped DLLs. The upstream source URL and official SDK SPDX are recorded, but a complete corresponding-source archive has not been assembled or verified locally.
- FFmpeg: retain exact commit `46d899d7a02bc63237472cc4897258d5ddf274ac` of FFmpeg, the BtbN build scripts at `63ac3265b0789ea16c7b1f8351cd2ad195a28722`, all modifications/configuration, and the exact sources, patches and build instructions of the libraries statically linked into the DLLs. The configure flags enable numerous external components, including FreeType, HarfBuzz, libxml2, librsvg, libssh and codec libraries. A generic FFmpeg release tarball or a link to a moving branch does not establish this correspondence.
- External-library licenses: collect and include the required copyright, license and NOTICE material for those FFmpeg dependencies. The top-level FFmpeg LGPL text is not their complete notice set.

The BtbN daily release inspected on October 15, 2026 exposes binary archives and
checksums, but no complete corresponding-source asset. Its `download.sh` can
prepare dependency downloads for a build; that script and a cache link alone do
not demonstrate that every exact source revision used by the shipped binary has
been preserved. Build scripts are available at a pinned commit. An audited
source bundle for this binary has not been produced.

## Other Release Material

- Include QueuePilot's `LICENSE`, `THIRD_PARTY_NOTICES.md`, the complete `licenses/` directory and production-model provenance.
- Preserve ONNX Runtime's official MIT license and `ONNXRuntime-ThirdPartyNotices.txt`.
- Preserve the Qt SPDX, bundled-component copyright statements and applicable license texts. FreeType and Independent JPEG Group attribution is included in the top-level notices.
- Verify the selected Microsoft runtime and Direct3D compiler files are redistributable under the terms of the toolchain used to create the release. They are not MIT-licensed project files.
- Keep compatible Qt and FFmpeg DLLs replaceable. Do not impose terms preventing debugging of user modifications to the libraries.
- Produce a clean archive without user settings, window titles, room-code history, recordings or logs.

Packaging for local testing and a completed test suite do not satisfy the source
and redistribution requirements above. A public binary release must remain
blocked until these materials are complete and verified for its exact DLLs.
