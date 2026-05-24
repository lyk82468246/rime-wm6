# SIP 注册踩坑实录

> WMRime 这个自定义 SIP 第一次出现在 WM6 输入法选择器里之前，我（Claude）连续猜错了两次注册表布局。这篇文档把那段过程记录下来，给后来的人省点时间。

---

## 症状回顾

| 阶段 | 现象 | 当时假设 | 结果 |
|------|------|---------|------|
| 0 | CAB 装上后 Settings → Input 看不到 WMRime | — | 失败 |
| 1 | 改成 `REG_DWORD` 后重装 | "WM6 的 SIP picker 严格要 DWORD" | 失败 |
| 2 | 把 RimeCore.dll 移到 `\Windows\` | "DLL 加载失败导致 picker 丢弃" | 失败 |
| 3 | 联网查微软原始资料后改正 | "原来 picker 根本不读那个路径" | **成功** ✓ |

---

## 第一次猜：REG_SZ vs REG_DWORD（错）

第一版 INF 把 `IsSIPInputMethod` 写成 REG_SZ `"1"`：

```ini
HKLM,"Software\Microsoft\Shell\Keybd\{guid}","IsSIPInputMethod",0x00000000,"1"
```

用户装完看不到 WMRime。我**没查文档**，直接推断："Win32 PC 上很多 enum 接口对 REG_SZ vs REG_DWORD 很挑剔，改成 DWORD 试试"。

第二版：

```ini
HKLM,"Software\Microsoft\Shell\Keybd\{guid}","IsSIPInputMethod",0x00010001,1
```

我甚至自信地把这条写进了 agent memory，标题叫 *"WM6 SIP picker needs DWORD IsSIPInputMethod"*。这是错的。

## 第二次猜：DLL 搜索路径（部分对，但不是根因）

用户反馈第二版 CAB 装完重启依然看不到。我又**没回头查文档**，转去怀疑 DLL 加载：WinCE loader 搜 dependent DLL 的目录是 `{loading_process_cwd, \Windows\}` 而非 importing DLL 的目录。我们的 `WMRimeSIP.dll` 静态依赖 `RimeCore.dll`，两者都在 `\Program Files\WMRime\`，那么 SIP 加载时找不到 RimeCore，CoCreateInstance 失败，picker 丢弃我们 — 听起来很有道理。

把 INF 改成：
```
[DestinationDirs]
Files.Sip       = 0, %InstallDir%       ; \Program Files\WMRime\
Files.Engine    = 0, %CE2%              ; \Windows\
```

用户装完依然看不到。

这条规则本身是对的（WinCE loader 确实这么搜路径），但它解决的是"装了之后选中时跳回默认"的问题，不是"装了根本看不到"的问题。**枚举阶段只读注册表，不加载任何 DLL**。

## 第三次：被骂醒后联网查微软原始资料

用户终于忍无可忍：

> 我已经重新安装 cab 并重启，还不行。**务必联网检索，wm 上输入法的机制到底是什么！不要带入普通 pc 的思维！**

被批评得对。查了微软 [Input Panel Registry Settings (Windows CE 5.0), aa452674](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/aa452674(v=msdn.10))，以及 Marcus Perryman（微软 PPC 老员工）2005 年的官方博客 [Custom Soft Input Panel (SIP) for Pocket PC](https://learn.microsoft.com/en-us/archive/blogs/windowsmobile/custom-soft-input-panel-sip-for-pocket-pc)。

微软文档里默认 IM (CLSID `{42429667-ae04-11d0-a4f8-00aa00a749b9}`) 的注册结构是这样的：

```
HKCR\CLSID\{42429667-...}
    @  : REG_SZ : "LOC_KEYBOARD"          (显示名)

HKCR\CLSID\{42429667-...}\InprocServer32
    @  : REG_SZ : "\Windows\msim.dll"

HKCR\CLSID\{42429667-...}\IsSIPInputMethod   ← **这是子键，不是值**
    @  : REG_SZ : "1"                       ← 默认值，是字符串 "1"

HKCR\CLSID\{42429667-...}\DefaultIcon
    @  : REG_SZ : "\Windows\msim.dll,0"
```

也就是说：

- **`HKLM\Software\Microsoft\Shell\Keybd\` 这个路径在微软文档里压根不存在**。我两次猜的注册表布局完全是凭空捏造的。WM 的 SIP picker 是去 `HKCR\CLSID\*` 找带 `IsSIPInputMethod` 子键的 COM 类。
- `IsSIPInputMethod` 是**子键 (subkey)**，不是父键上的值 (value)。
- 默认值是 `REG_SZ "1"`，**不是 DWORD**。

Perryman 博客评论区还有一条关键提示（2007 年一个 CE5 用户的实测）：

> "..seems that my own sip-panel works now well on CE5, by deleting ThreadModel Variable for InProcServer32 for that CLSID in registry."

也就是说：**不要写 `ThreadingModel`**。微软自己的示例也没写。

## 最终正确的注册结构

```ini
[Reg.Clsid]
HKCR,"CLSID\{guid}",,0x00000000,"WMRime"

[Reg.Inproc]
HKCR,"CLSID\{guid}\InprocServer32",,0x00000000,"<DLL 完整路径>"
; 注意：故意不写 ThreadingModel

[Reg.IsSIP]
HKCR,"CLSID\{guid}\IsSIPInputMethod",,0x00000000,"1"
; 注意：路径上 IsSIPInputMethod 是 subkey，逗号后是默认值，REG_SZ "1"

[Reg.Icon]
HKCR,"CLSID\{guid}\DefaultIcon",,0x00000000,"<DLL 路径>,0"
```

我们还顺手在 `HKLM\Software\Classes\CLSID\` 下镜像了一份 — HKCR 在 WinCE 上通常是这个路径的别名，但不同 ROM 实现略有差异，写两份是防御性的做法，代价只是几个 byte 的注册表空间。

## 教训

1. **不要凭"PC Win32 的直觉"猜 WinCE 行为**。两者在 loader、注册表语义、COM 注册路径上都不同。
2. **每改一次先查文档**。我每次都试图省掉这步，每次都为之付出代价（用户的时间）。
3. **错的 agent memory 是有毒的**。我把 "DWORD" 的错论断写进了 memory，下次新对话进来会被错误信息误导。已替换。
4. **对话外网检索成本远低于一次失败的 CAB 试装**。一次 web fetch 5 秒，一次 CAB 重装重启 5 分钟，对比悬殊。

---

## 文件回看

修复的 commit：[`390e906`](https://github.com/lyk82468246/rime-wm6/commit/390e906) — "Fix WM6 SIP registration: HKCR\\CLSID\\{guid}\\IsSIPInputMethod as subkey"

涉及文件：

- [dist/WMRime.inf](../dist/WMRime.inf) — CAB 安装注册表
- [src/wmrime_sip/sip_main.cc](../src/wmrime_sip/sip_main.cc) — `DllRegisterServer` / `DllUnregisterServer` 走同样布局
- [docs/agent-memory/feedback_wm6_sip_picker_dword.md](agent-memory/feedback_wm6_sip_picker_dword.md) — agent memory，避免后续会话再踩坑

---

## 当前状态（写本文档时）

✓ WMRime 出现在 Settings → Personal → Input 列表中
✗ 选中后立即跳回默认输入法 — 下一阶段问题

这个 "选中即跳回" 是另一个独立症状，参见 [sip-load-debugging.md](sip-load-debugging.md)（如果存在）。注册阶段（让它"被看见"）和加载阶段（让它"能被使用"）是两件事。
