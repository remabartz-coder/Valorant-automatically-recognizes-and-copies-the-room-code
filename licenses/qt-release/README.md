# Qt And Microsoft Release Evidence

This directory records the exact local dependency inputs and the material needed
to distribute the Windows build. The evidence collector is not a release
approval flag. The actual source and license archives must be present and
verified before the DLLs are published.

## Qt SDK Provenance

The installed official Qt Base SDK is 6.8.3 for MSVC 2022 x64. Its binary archive
SHA-256 is
`41688269FAC0565DB956C66D9EECAE777D16197E0C02CD81B88640C1F5D73D3F`.
The unmodified SDK's SPDX document identifies Qt Base source commit
`c07c2d5a527a644d36e7853d55132ae38921682f`. It records MSVC
`19.39.33520.0`, CMake `3.30.5` and the original configure command in
`packages[name=qtbase].comment`.

The verified official source archive is 48,426,536 bytes with SHA-256
`56001B905601BB9023D399F3BA780D7FA940F3E4861E496A7C490331F49E0B80`. Its
top-level `.tag` is the exact commit above, it contains 25,704 archive entries,
1,564 entries below `src/3rdparty`, and 922 C/C++ source or header entries
there. The archive includes Qt's `LICENSES/` directory, `configure` templates,
`.cmake.conf`, and attribution records.

The collector preserves that SPDX document verbatim, extracts its configure
record, and copies the SDK's CMake packages, qmake specifications, feature
headers and library `.prl` metadata. These establish the build configuration;
they do not replace the source archive.

Required Qt source inputs are the official archive
`qtbase-everywhere-src-6.8.3.tar.xz`, its official
`qtbase-everywhere-src-6.8.3.tar.xz.sha256`, and the
archive's complete nested `src/3rdparty` sources. Obtain both archive and hash
list over certificate-verified HTTPS from:

https://download.qt.io/archive/qt/6.8/6.8.3/submodules/

Confirm the archive hash against the official list before extracting. Confirm
the source version and commit against the installed SDK's SPDX. Keep the full
archive, its original license and copyright files, its nested third-party
sources, and the collected build configuration available alongside the binary
download. The generated configuration alone is not corresponding source.

## Modifying And Rebuilding Qt

QueuePilot links to shared Qt libraries and adds no library replacement or
debugging restriction. Keep replacement DLLs and plugins compatible with Qt
6.8.3, the x64 architecture and the MSVC dynamic runtime ABI. Release builds use
the ordinary DLL names; a debug Qt build has different names and cannot be
dropped into the release directory without rebuilding the application.

Use an x64 MSVC developer command prompt, CMake, Ninja and the Windows SDK. The
original build used the tool versions and configure command preserved in
`original-qtbase-build-record.txt`. Upstream paths referring to Qt's build
machine, the install prefix, compiler launchers, and SBOM helper interpreters
must be replaced with local equivalents or omitted if they only control build
acceleration/auditing. Retain the original feature definitions when rebuilding
the official SDK. Its OpenSSL support is dynamically loaded; QueuePilot's
package currently deploys the Windows Schannel TLS backend rather than
OpenSSL DLLs.

A source build for the application can instead use the complete verified Qt
Base source tree, `configure.bat -release -shared -opensource -confirm-license
-nomake examples -nomake tests -prefix C:\qt-rebuilt`, then
`cmake --build . --parallel` and `cmake --install .` from a separate build
directory. Review the resulting configuration summary and preserve
`CMakeCache.txt`, the summary and actual configure arguments with that build.
For a shipped custom Qt build, distribute those build records with its source
and use its `windeployqt.exe` to deploy matching plugins. This recipe describes
the upstream build interface; it is not a claim that a custom Qt build has
already been completed or tested.

To use a compatible modified SDK, configure QueuePilot with its Qt location
instead of the original SDK, rebuild, and deploy using the modified SDK's
`windeployqt.exe`. Validate the application and HTTPS resolution with the
replacement libraries before distributing that build. Rebuilding need not
produce byte-identical DLLs; source, local configuration and the ability to
modify and rebuild must remain available.

## Microsoft Redistributables

See [MICROSOFT.md](MICROSOFT.md) for the exact installed CRT and Windows SDK
redistributable files. They retain Microsoft's licensing rather than the
project MIT license. Preserve the relevant official redistribution list and
license terms; a valid digital signature alone does not grant redistribution
rights.

## Collecting Local Evidence

Run from PowerShell with the installed dependency paths:

```powershell
./licenses/qt-release/collect-evidence.ps1 -VisualStudio D:/VS -WindowsSdk 'D:/Windows Kits/10'
```

The output is an ignored local dependency-evidence directory under
`.deps/release-sources/qt`, and a ZIP containing the collected SDK configuration.
No DLL is copied by this collector. It records hashes and signature status for
the Microsoft REDIST files and preserves the installed SDK license verbatim.
Visual Studio is discovered using `vswhere` or `VSINSTALLDIR`; the Windows SDK
is discovered using its registered path or `WindowsSdkDir`. The SDK and CRT
versions are selected numerically, or can be specified explicitly. The source
archive and its official checksum must already be present at the documented
paths (or supplied explicitly). Both official Microsoft HTML lists are copied
without alteration from the downloaded local inputs, with their URLs and
hashes recorded in JSON. The complete Qt source archive remains a separate
release asset alongside this smaller build-evidence ZIP.
