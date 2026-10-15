# Microsoft Binary Redistribution Inputs

These are the installed Windows x64 inputs inspected on October 15, 2026.
The project was built with Visual Studio 2022 `17.14.37301.10`, MSVC tools
directory `14.44.35207`, compiler file version `19.44.35227.0`, and Windows SDK
`10.0.26100.0`.

## Visual C++ Runtime

The runtime DLLs come from the Visual Studio directory
`VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT`, not `debug_nonredist`.
Every listed DLL has file version `14.44.35211.0`; the REDIST directory version
and DLL servicing version differ intentionally.

The installed `Licenses/2052/Redist.txt` points to the official current
redistribution list at https://aka.ms/vs/17/redist.txt. Preserve the downloaded
list, verify that it covers the actual files below, and retain the applicable
Visual Studio redistribution terms before publishing a binary package. The
Microsoft runtime is not covered by QueuePilot's MIT license.

The official Microsoft Learn HTML was downloaded with TLS verification and
preserved by the evidence collector. The `Visual C++ Runtime Files` section
explicitly permits unmodified files under `[VisualStudioFolder]\\VC\\redist`
and subdirectories, subject to the Visual Studio license terms. The ten inputs
below are within that directory, outside the listed nonredistributable debug
directories. Canonical source:
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
The captured HTML SHA-256 is
`37BDEDF9831BA710BA4C37AFCC51ADEB193D630F7EC5CB2DFFC350BEC185511E`.

| File | SHA-256 |
| --- | --- |
| concrt140.dll | 2405355F0A58067B258F8DF33C327E3A3D716EAAC5A3A5AEBB757842D85BD376 |
| msvcp140_1.dll | BFAD5AEF4C63A669E3C140655CDFDF395B6C979B400A447BD5DCB65ED8826C3D |
| msvcp140_2.dll | 3EA06F0EE098B4823CB79599DF3780E7F23CCE52C19AAC31D2A0D47EFE33A5E9 |
| msvcp140_atomic_wait.dll | 640B2AEFCED484D0368EEA5BDD06ADDD0658A3A70A49256E560D6923B404A479 |
| msvcp140_codecvt_ids.dll | F2069A52880EC885EE7F0511186100EB7FADA0411A2B4948FAFEA7735B878A18 |
| msvcp140.dll | 0F885B509A685D2BBFA652FED26B5FB31D88FBDAB0A978C641D1C7B8AA460AA9 |
| vccorlib140.dll | 19839407C3FDBC824E5BCE189BF68DDF8097F12EC28B757797FFA0415C144DDD |
| vcruntime140_1.dll | 1F2D41C4AA5DB0BC33EBF7B66D72943A817D7CE6CBE880502A9403823633093F |
| vcruntime140_threads.dll | 219915CF20822F34D5E7C1FDD4E21AE7F3396881096C51036225FB8F84B47AFA |
| vcruntime140.dll | D5E4D9A3E835FA679450145D6A7D94E36573A509317111904D9B3712C30D9066 |

All ten files had valid Microsoft Authenticode signatures at inspection.
The collector generates a machine-readable inventory from the installed files;
packaging must match those inputs rather than relying on this document alone.

## Direct3D Compiler

Use the file from Windows SDK
`Redist/D3D/x64/d3dcompiler_47.dll`, version
`10.0.26100.7705 (WinBuild.160101.0800)`, SHA-256
`A05F99734F7C4822FEFC12B367AF21FD0976ED6608752FB1E1E80B6ECE7ECBBB`.
It has a valid Microsoft Authenticode signature.

The older file automatically deployed in the previous local package is version
`10.0.22621.2506`, SHA-256
`B0E7667CD5EA5C578EE1A73C7CD89521C200A854DA2A4AD57C1877232FC8B6A7`.
The release packager must explicitly use the verified SDK REDIST input instead
of inheriting that system-origin file from an earlier deployment.

The installed Windows SDK license is
`Licenses/10.0.26100.0/sdk_license.rtf`, EULA ID
`WIN10SDK.RTM.AUG_2018_en-US`. Section 2(a) grants distribution rights for
object code on the REDIST.TXT list and specifies requirements and restrictions.
It points to https://go.microsoft.com/fwlink/?LinkId=524842 for the complete
list. Save the official list and confirm `d3dcompiler_47.dll` is covered before
distribution. Preserve copyright notices and the applicable terms for the
Microsoft files. No other DirectX SDK files are required by this inventory.

The captured SDK REDIST HTML (updated October 21, 2024) explicitly lists
`Program Files\\Windows Kits\\10\\Redist\\D3D\\x64\\d3dcompiler_47.dll` for
Classic Windows applications. QueuePilot is a Classic Windows desktop
application. Canonical source:
https://learn.microsoft.com/en-us/legal/windows-sdk/redist
The captured HTML SHA-256 is
`174C0D0FDA995B695B18D4A43499294C0BB7973B2365A6605963156C8DF2D5B1`.
