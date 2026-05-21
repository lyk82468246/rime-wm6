---
name: 自动运行 build.bat
description: 用户授权我直接调用仓库根目录的 build.bat 验证编译，不需要让用户手动跑
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
可以直接通过 Bash 工具运行 `./build.bat`（位于 pwd / 仓库根目录）来验证 WinCE 端口的编译结果，不要让用户手动跑。

**Why:** 用户在 2026-05-18 明确说"bat脚本就在pwd，你下次大可以自己运行"。在此之前我一直让用户手动跑、贴输出回来，徒增往返。

**How to apply:** 每完成一轮 .h/.cc 移植 + vcproj 更新 + smoke test 扩展后，直接 `Bash` 调用 `./build.bat`。读取输出判断成败：成功标志是 "RimeCore - 0 个错误，0 个警告" / "生成: 成功 1 个"；失败时把错误行（C2xxx/C4xxx/C1083 等）摘出来，分析后再迭代，而不是把整段日志原样回贴。
