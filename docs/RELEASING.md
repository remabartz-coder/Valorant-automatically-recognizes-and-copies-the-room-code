# 发布准备

## 源码

1. 运行 `scripts/verify.ps1`；无交互桌面时可加 `-SkipDesktopTests`，并另行完成桌面捕获验收。
2. 检查 README、许可证和依赖版本记录。
3. 运行 `scripts/export-source.ps1`，生成允许列表中的源码、正式模型、图标、测试裁剪、依赖声明和 SHA-256 清单。
4. 检查导出目录后再上传 GitHub。导出不会初始化 Git、创建远程仓库或执行推送。

导出排除 `.deps`、构建产物、运行数据、日志、完整录屏及私人测试帧。代码采用 MIT；依赖和模型保留各自的声明。

## Windows 二进制

Windows Release 使用精简 FFmpeg 构建，打包脚本会在材料缺失时停止。

先获取固定来源材料并构建依赖：

```powershell
./scripts/fetch-release-materials.ps1
./scripts/build-ffmpeg.ps1 -SourceArchive .deps/release-sources/ffmpeg/ffmpeg-n8.1.3.tar.gz
./scripts/build.ps1 -FFmpegRoot .deps/ffmpeg-minimal
```

`licenses/qt-release` 保存 Qt SDK 配置、源码校验和、第三方源码审计及微软官方再分发清单。精简 FFmpeg 禁用外部自动探测，仅使用 FFmpeg 自带组件和 Windows Schannel。

生成公开便携包前，需要：

1. 运行 `scripts/verify.ps1 -FFmpegRoot .deps/ffmpeg-minimal -SkipDesktopTests`，并单独完成桌面捕获验收。
2. 运行 `scripts/package.ps1 -ForPublicRelease -FFmpegRoot .deps/ffmpeg-minimal -DestinationDir dist/QueuePilot`。
3. 在干净 Windows 环境启动 `QueuePilot.exe --demo --no-output`，检查 DLL、模型、许可证和 `sources/` 均在包内。
4. 运行 `scripts/export-source.ps1`，检查源码清单后再上传 GitHub。

更多证据见 `licenses/BINARY_DISTRIBUTION.md` 和 `licenses/qt-release`。

## GitHub Release

`.github/workflows/release-windows.yml` 只接受与 `CMakeLists.txt` 项目版本相同的 `vX.Y.Z` 标签。
标签工作流会先构建和验证，再运行干净目录的 `QueuePilot.exe --demo --no-output --screenshot ...` 冒烟检查，最后生成三个资产：

- `QueuePilot-Windows-x64-vX.Y.Z.zip`：便携运行目录；
- `QueuePilot-corresponding-sources-vX.Y.Z.zip`：对应的 Qt/FFmpeg 源码、构建脚本和声明；
- `SHA256SUMS-vX.Y.Z.txt`：前两个资产的 SHA-256。

工作流先创建 Draft Release 并上传全部资产，成功后才取消 Draft。已有同名 Release 会直接失败，不会覆盖或强制移动用户资产。源码归档使用临时 staging 目录生成，不会把 `.deps`、`data`、日志或测试截图混入运行包。
