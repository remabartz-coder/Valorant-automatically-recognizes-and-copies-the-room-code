# QueuePilot minimal FFmpeg build

QueuePilot's public Windows package is intended to use FFmpeg n8.1.3 built from
the official fixed n8.1.3 tag archive. The build does not use the BtbN daily binary. It keeps
the LGPL-compatible FFmpeg components needed for local files, FLV/HLS/MP4,
H.264/HEVC decoding, HTTP/HTTPS (Schannel), and `swscale`; no external codec
libraries are autodetected.

The fixed source archive is:

`https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.1.3.tar.gz`

The archive SHA-512 is recorded in `SOURCE-SHA512.txt` and is checked before
extraction, so a partial or substituted archive cannot enter a release package.

Build with PowerShell:

```powershell
.\scripts\build-ffmpeg.ps1 -SourceArchive .deps\downloads\ffmpeg-n8.1.3.tar.gz
```

The script requires x64 Visual Studio C++ tools, NASM, a POSIX `bash.exe`, and
`make.exe` (`-Bash` and `-Make` accept explicit paths). It verifies the source hash before extraction and writes
`.deps/ffmpeg-minimal/BUILD-MANIFEST.json` containing the exact configure line,
toolchain paths, and hashes of every installed file. A missing source archive or
build tool is an error; the script never falls back to the existing BtbN package.

The installed `build-records/` directory preserves the actual configure/build
logs, generated configuration headers, patched configure script, make settings,
compiler wrapper and `TOOLS.json` with compiler, resource compiler, linker,
NASM and make versions and hashes. The final manifest covers these records too.
For an already installed build whose original build directory still exists,
`-FinalizeOnly -KeepBuild` refreshes these materials without recompiling.

The script applies one build-only compatibility change to FFmpeg's configure
script: vendor detection accepts `Microsoft` anywhere in a localized compiler
banner. It creates an ASCII banner wrapper around the selected MSVC compiler
to let Windows resource compilation work with a Chinese-only toolchain. All
compilation arguments are forwarded unchanged to `cl.exe`. These build controls
are contained in `scripts/build-ffmpeg.ps1` and must accompany the source archive.

The corresponding source and this build configuration must accompany any
public binary distribution. FFmpeg remains under its applicable LGPL terms;
see `licenses/FFmpeg-LICENSE.txt` and the upstream source notices.
