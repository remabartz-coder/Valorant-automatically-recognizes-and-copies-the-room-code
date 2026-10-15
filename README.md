<img src="assets/queuepilot.png" width="72" height="72" alt="QueuePilot icon">

# QueuePilot 房间码助手

Windows 桌面程序，从抖音直播、媒体流或窗口画面中识别无畏契约房间码，确认后复制到剪贴板并语音提示。使用 C++20、Qt、FFmpeg 和本地 ONNX 模型，运行时无需 Python。

这是独立开源项目，与抖音、腾讯和 Riot Games 无隶属关系。识别及本机处理耗时不等于直播总延迟，也不保证成功加入车队。

## 使用

支持 Windows 10 1903+ / Windows 11 x64。自行构建后运行 `build/Release/QueuePilot.exe`，或通过打包脚本生成本地便携目录。

1. 在“来源”选择直播链接、媒体流、本地视频或窗口采集。直播链接支持 `https://live.douyin.com/<房间号>` 和带 `live_web_rid` 参数的页面。点击“开始播放”。
2. 在预览中框住房间码所在的输入框，允许包含留白和周围文字；也可点击“自动定位房间码”。选好后可锁定框选，播放和识别会继续。
3. 设置采样频率、置信阈值及确认帧数，点击“开始识别”。默认 30 FPS 采样、两帧确认、最低字符概率 0.90。实际处理频率受视频来源和硬件限制。
4. 有效码确认后立即复制，再异步播报“识别成功”。可关闭语音。后台和最小化时继续处理，但视频来源须仍在输出画面。
5. 可选游戏填入：热键模式使用 F8，自动模式需先在游戏打开输入框，再启用本轮自动填入。自动填入执行一次后解除。Ctrl+Alt+F10 停止播放和识别。

房间码必须恰好由三个 ASCII 大写字母和三个数字组成；默认允许任意排列，可选择限定为 `^[A-Z]{3}[0-9]{3}$`。不自动替换 `I/1` 或 `O/0`。同一码在一轮识别中只输出一次。

地址可随时修改；切源会停止播放并清空旧候选。窗口采集和测试画面不使用地址，因此这两种模式下地址框禁用。低清晰度、压缩、遮挡或歧义会导致拒绝输出；降低阈值不能证明字符读对。

## 来源与输出

- 直播页通过 HTTPS 获取并解析结构化数据，不生成平台签名。页面变化、登录要求或未开播可能导致解析失败，可改用当前有效媒体地址或窗口采集。
- Cookie 仅可用于 HTTPS 抖音直播页面解析，只保留在内存中。媒体解码不接收 Cookie，避免随跨域重定向泄漏。部分需要媒体 Cookie 的来源因此不支持。HTTPS 媒体连接开启证书校验。
- 画质选择依赖页面提供的地址，同档位优先 FLV 和 HTTPS。直连解码不受浏览器遮挡影响；窗口采集是否继续更新取决于目标窗口，最小化或浏览器暂停渲染会影响采集。
- 只保留最新视频帧。选区内文字检测配合缓存位置和原画复核，跨独立帧确认后发布。自动定位有候选和分块预算，遇到多个不同有效码或预算不足时拒绝选择。
- 游戏输入使用普通 Windows API，要求前台进程为 `VALORANT-Win64-Shipping.exe`，不会自动寻找输入框或切换焦点。填入要求当前高置信候选匹配确认码且画面不超过 500 ms。
- “输入事件已发送”仅表示 Windows 接收了事件。国服对粘贴、逐字符输入和 Enter 的实际兼容性仍需实机验证；“填入后发送 Enter”默认关闭。本地视频和测试画面禁止游戏输入。
- 语音使用系统 SAPI，独立线程播放，实际输出取决于已安装语音和音频设备。

## 配置与隐私

程序旁的 `data/settings.ini` 保存框选、识别和输出设置，以及规范化直播房间地址或现存本地文件路径；不会保存 Cookie 或完整媒体 URL。`data/events.jsonl` 记录房间码、事件和本机帧处理耗时，不保存截图。上述数据目录已列入 `.gitignore`，不随源码导出。

房间码和本地文件路径仍可能属于私人信息，提交问题时应检查日志并移除不需要公开的内容。界面“推理”和“帧处理”时间不包含主播推流、平台分发、来源缓冲和游戏响应。

## 构建

需要 Visual Studio 2022 的 C++ 桌面工具、Windows SDK、CMake 3.24+、7-Zip、PowerShell 和网络访问。Qt 的 `.7z` 包通过 7-Zip 解压，避免 Windows 自带 `tar.exe` 缺少 LZMA 支持；安装脚本会从 PATH 或默认安装目录查找，也可用 `-SevenZip <7z.exe路径>` 指定。GitHub Windows 运行器使用预装的 7-Zip。

```powershell
./scripts/install-deps.ps1 -SkipFFmpeg
./scripts/fetch-release-materials.ps1
./scripts/build-ffmpeg.ps1 -SourceArchive .deps/release-sources/ffmpeg/ffmpeg-n8.1.3.tar.gz
./scripts/build.ps1 -FFmpegRoot .deps/ffmpeg-minimal
./scripts/package.ps1 -FFmpegRoot .deps/ffmpeg-minimal
```

依赖安装在 `.deps`，下载均校验固定 SHA-256/SHA-512。Qt 6.8.3、精简 FFmpeg n8.1.3、ONNX Runtime 1.31.0；识别使用 PP-OCRv3 英文模型，定位使用 PP-OCRv4 检测模型。原生 ONNX DLL 从官方 wheel 提取，不安装 Python。精简 FFmpeg 禁用外部自动探测，使用 Windows Schannel 处理 HTTPS。

公开 Windows 包使用固定的官方 Qt/FFmpeg 源码构建。`fetch-release-materials.ps1` 会下载对应源码和微软官方再分发清单，`build-ffmpeg.ps1` 会在哈希不匹配或缺少 make 时停止。详见 [依赖来源](licenses/SOURCES.md) 和 [发布清单](docs/RELEASING.md)。

仓库标签工作流会生成 Draft Windows Release，完成便携包、对应源码包和 SHA-256 清单的上传后才公开。发布标签必须与 `CMakeLists.txt` 中的项目版本一致，例如 `v0.1.0`。

脚本会探测本机工具。安装脚本支持 `-VisualStudio`，构建及验证脚本还支持 `-CMake`、`-WindowsSdk` 指定路径。无交互桌面的环境可用 `./scripts/build.ps1 -SkipDesktopTests`。直接使用 CMake 时可设置 `-DBUILD_TESTING=OFF`。

本地打包前关闭目标目录中运行的程序，或指定 `-DestinationDir dist/QueuePilot-review`。打包保留已有 `data`。公开包使用 `-ForPublicRelease`，会把对应源码和证据放进 `sources/`，并拒绝不完整材料。

## 验证

```powershell
./scripts/verify.ps1
./build/Release/QueuePilot.exe --demo --no-output --screenshot test-output/demo.png
```

自动测试覆盖码型规则、页面解析、输入限制、OCR 原图和宽边距样例、定位歧义及预算、UI 流程、预览尺寸稳定性、语音生命周期和本地视频。`window_capture` 需要正常 Windows 桌面。测试关闭剪贴板、真实播音和游戏按键输出。

可选 `real_video_tests` 接受经 `fps=4` 提取的录屏帧，通过 `QUEUEPILOT_REAL_VIDEO_FRAMES` 指定目录；完整私人录屏及导出帧不随源码分发。已有记录及其局限见 [验证说明](docs/VALIDATION.md)。GitHub Actions 工作流尚需在目标仓库实际运行验证。

## 许可

项目代码及原创图标采用 [MIT](LICENSE)。模型与运行库采用各自的许可证，详见 [第三方声明](THIRD_PARTY_NOTICES.md)。MIT 不替代 Qt、FFmpeg 等依赖的再分发义务。
