# Repository Guidelines

## Project Structure & Module Organization
- 核心协议栈在 `xnet_tiny/src/xnet_tiny`（IP/ICMP 等）并构建为 `libxnet_tiny.a`。
- 应用与平台适配在 `xnet_tiny/src/xnet_app`，`app.c` 提供交互菜单；驱动适配层在 `lib/xnet`，Npcap 头/库在 `lib/npcap`。
- 产物输出到 `build/`，包含中间文件与 `xnet.exe`；静态演示资源在 `htdocs/`。

## Build, Test, and Development Commands
- `cd build && cmake ..` 配置工程（默认 x64 Npcap 路径，可按需调整）。
- `cd build && cmake --build .` 或 `make` 编译并链接 `xnet.exe`（依赖 `wpcap.lib` 与 `Ws2_32`）。
- `cd build && .\xnet.exe` 运行交互 CLI（Ping/Traceroute/带宽/抖动），需管理员权限以打开网卡。
- `cmake --build . --target clean` 清理生成物，切换工具链前使用。

## Coding Style & Naming Conventions
- C 代码使用 4 空格缩进，偏 C89/C99；保持行长可读（约 100 字符），使用显式 `uint*_t` 类型。
- 函数前缀沿用：栈功能 `xnet_`，ICMP 辅助 `xicmp_`；局部变量用下划线命名，宏/常量全大写。
- 头文件顺序：标准库 → 平台（Windows）→ 项目头；保持 `#pragma pack(1)` 结构体对齐一致。
- 不提交 `build/`、`xnet_tiny/out/` 等生成文件，仅提交 `src` 与 `lib` 源变更。

## Testing Guidelines
- 无自动化测试，手动运行 `xnet.exe` 覆盖各模式。
- Ping/Traceroute：检查 RTT 与路径输出是否合理，可用 Wireshark/Npcap 抓包排查。
- 带宽/抖动：多次测量记录异常；更改时长逻辑时建议附上日志/截图。

## Commit & Pull Request Guidelines
- 历史提交使用简短祈使句摘要（如 `finish`、`fix traceroute ttl`），保持单一主题。
- PR 需描述改动与原因，并标注验证步骤/命令；涉及网络路径改动时附控制台输出或抓包说明。
- 如修改依赖（Npcap 版本/路径），在 PR 中明确，并提示所需工具链（VS/CMake generator）。
- PR 尽量小步提交，避免将重构与功能混合。

## Security & Configuration Tips
- 抓包需管理员权限，选择正确接口，勿提交捕获文件。
- 调整 SDK/库路径时同步更新 `LINK_DIRECTORIES`，并说明配置要求。
