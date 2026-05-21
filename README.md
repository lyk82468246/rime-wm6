# rime-wm6 — RIME 输入法引擎到 Windows Mobile 6.5 / WinCE 6.0 的移植

> 把 [RIME 输入法引擎](https://github.com/rime/librime) 移植到 Windows Mobile 6.5 Professional / WinCE 6.0，目标设备是 ARMV4I/ARMV5 架构的功能机/PDA（HTC TyTN II、Touch Pro 2、HD2 之类的"老古董"）。
>
> 这份 README 是项目交接文档，也是给后续 LLM 接力的 prompt。如果对话中断，新会话开始时请先读这份 + `.claude/projects/c--Users-Joe-Documents-C--rime-wm6/memory/MEMORY.md`（自动记忆索引）。

---

## 1. 项目目标与硬约束

**目标**：在 WM6.5 设备上跑起 RIME 拼音输入法，作为 SIP（Soft Input Panel）供任意应用使用。

**硬约束**（不可妥协，所有设计决策都受其制约）：
- **编译器**：Visual Studio 2008 = MSVC 9.0。**C++03 only**：没有 auto/lambda/nullptr/=default/=delete/NSDMI/brace-init/std::move/variadic templates/template aliases（`using X = Y;`）。
- **架构**：ARMV4I（兼容 ARMV5），WinCE 6.0 / Windows Mobile 6 Professional SDK。
- **运行时**：coredll（WinCE C 运行时），无 std::thread、无 std::filesystem、无 std::regex、无 boost、无 yaml-cpp。
- **源码编码**：所有 WinCE-port 源文件必须 **ASCII-only**（CN-locale 上 MSVC9 把无 BOM 的 UTF-8 当成 cp936 读，会爆 C4819 + 一堆 C2065 级联错）。注释里的中文也不行。

**最终交付**：
- `RimeCore.dll` — 移植后的 RIME 引擎核心
- `WMRimeSIP.dll` — 实现 IInputMethod2 的 SIP COM 服务器，包装 RimeCore
- `luna_pinyin.prism.bin` + `luna_pinyin.table.bin` — 字典数据
- `.cab` 安装包（**未完成**——见第 7 节）

---

## 2. 当前状态（截至 2026-05-20）

### ✅ 完成且 build 通过（0 错 0 警告）

| 项 | 位置 |
|---|---|
| `wince_compat` 兼容层 | `src/wince_compat/` |
| RIME 核心引擎移植 | `src/librime_wince/` |
| Python 字典构建工具链 | `tools/` |
| `rime_api` C 导出层 | `src/librime_wince/src/rime_api.{h,cc}` |
| 手写 YAML parser | `src/librime_wince/src/rime/config/yaml_parser.{h,cc}` |
| MVP `PinyinTranslator` | `src/librime_wince/src/rime/gear/pinyin_translator.{h,cc}` |
| `WMRimeSIP` COM/SIP shell | `src/wmrime_sip/` |

### ⚠️ "能编不能跑" 的部分

- **从未在真机上跑过**。dllmain 里的 smoke test 只构造/析构对象，没真正 query。
- `MiniSession` 绕过 Engine（见 `memory/project_rime_api_mvp.md`），未验证是否会触发 Engine/Context 路径上的隐藏依赖。
- SIP 面板的 UI 只有最小化的方块绘制 + 软键盘命中——没在真机上看过实际渲染。

### ❌ 未做

- **marisa-trie**：当前用自写的 `StringTable` stub（`src/librime_wince/src/rime/dict/string_table.cc`，"RST1" 格式）。这意味着我们生成的 `.table.bin` **与上游 RIME 的二进制格式不兼容**。要兼容需 vendor 上游 marisa-trie（C++03 模板重活，~3-5k 行）。
- **gear/ 真模块**：speller / abc_segmentor / ascii_composer / selector / express_editor 等都没有，Engine 的处理器/分段器/翻译器/过滤器链都是空的。当前用 MiniSession 在 rime_api 层绕过。
- **用户字典 / 学习**：UserDictionary 整个剪掉了（依赖 LevelDB）。
- **YAML emitter**：parser 有了，emitter 还是 stub。
- **CAB 安装包**：第 7 节有手动制作指引。
- **on-device 实测**。

---

## 3. 目录结构

```
rime-wm6/
├── README.md                      ← 本文档
├── build.bat                      ← 调用 devenv build 整个 sln（Debug + ARMV4I）
│
├── data/                          ← 构建好的字典（tools/build_dict.py 输出）
│   ├── luna_pinyin.prism.bin      ← flat-mode prism，~3 KB，运行时在内存里 build Darts trie
│   └── luna_pinyin.table.bin      ← 4 MB（含 essay.txt preset），134662 entries
│
├── src/
│   ├── librime/                   ← 上游 rime/librime 的 pristine clone（**不要改这里**）
│   │                                依赖：darts.h（src/librime/include/）+ data/minimal/*.yaml
│   │
│   ├── librime_wince/             ← WinCE 端口主体，镜像上游 src/ 树
│   │   └── src/
│   │       ├── rime/              ← 镜像 librime/src/rime/
│   │       │   ├── algo/          ← syllabifier, algebra, calculus, encoder, spelling, strings
│   │       │   ├── config/        ← config_data/types/component, builtin_schemas, yaml_parser
│   │       │   ├── dict/          ← mapped_file, prism, vocabulary, table, dictionary, string_table, corrector
│   │       │   ├── gear/          ← 目前只有 pinyin_translator
│   │       │   ├── candidate, composition, context, engine, key_event/table, menu, ...
│   │       │   └── service, session（在 service.{h,cc}）
│   │       ├── rime_api.h/.cc     ← C 导出面（暴露给 WMRimeSIP）
│   │       └── build_config.h
│   │
│   ├── wince_compat/              ← 替代 C++11/boost/std::filesystem 的最小兼容层
│   │   ├── shared_ptr.h, function.h, signal.h, mutex.h, regex.h, path.h, time.cc
│   │   ├── stdint.h               ← WinCE 没有，自己定义
│   │   ├── utf.h, utf8.h          ← UTF 转换
│   │   ├── wince_compat.h         ← 总入口
│   │   └── X11/keysym.h           ← rime 的按键 keysym 定义（来自 X11）
│   │
│   └── wmrime_sip/                ← SIP DLL 源码（COM in-proc server）
│       ├── sip_main.cc            ← DllGetClassObject / DllCanUnloadNow / DllRegister/Unregister
│       ├── class_factory.{h,cc}   ← IClassFactory
│       ├── rime_input_method.{h,cc} ← IInputMethod2 + subclass SIP hwnd 的 WNDPROC
│       ├── sip_window.{h,cc}      ← PanelState、RecomputeLayout、PaintPanel、HandleTap
│       ├── utf_conv.{h,cc}        ← UTF-8 <-> UTF-16
│       ├── sip_globals.{h}        ← g_object_count / g_lock_count / EnsureRimeInitialized
│       ├── clsids.h               ← CLSID_WMRimeSIP（生成的 GUID，发布后别动）
│       └── WMRimeSIP.def          ← 四个 COM 函数的导出列表（替代 dllexport）
│
├── rime-wm6/                      ← VS2008 .sln 和 .vcproj
│   ├── rime-wm6.sln               ← 包含 RimeCore + WMRimeSIP，WMRimeSIP 依赖 RimeCore
│   ├── RimeCore/RimeCore.vcproj   ← 输出 RimeCore.dll + RimeCore.lib（import lib）
│   └── WMRimeSIP/WMRimeSIP.vcproj ← 输出 WMRimeSIP.dll，链接 RimeCore.lib + ole32 + oleaut32
│
├── tools/                         ← 桌面端字典构建工具（Python 3）
│   ├── build_dict.py              ← 把 luna_pinyin.dict.yaml + essay.txt -> .prism.bin + .table.bin
│   ├── inspect_bin.py             ← dump .bin 文件 header + 抽样内容
│   └── verify_dict.py             ← 模拟 C++ Table::Query 的 Python 端到端 lookup
│
└── docs/
    ├── cab-packaging.md           ← 手动打 CAB 包的完整流程（INF 模板 + cabwiz 调用）
    └── agent-memory/              ← 上一个 LLM agent 的持久化记忆（22 篇 markdown）
                                     新会话先读 MEMORY.md 索引
```

---

## 4. 构建

### 依赖
1. **Visual Studio 2008 Standard / Professional**（不是 Express，Express 没有设备项目）
2. **Windows Mobile 6 Professional SDK**（装到默认路径 `C:\Program Files (x86)\Windows Mobile 6 SDK\`）
3. **Python 3.x**（用于 `tools/build_dict.py`，桌面运行）
4. **上游 rime/librime**：`git submodule update --init` 拉到 `src/librime/`

### 构建命令
```bash
# 整个 sln（RimeCore + WMRimeSIP），Debug + ARMV4I
./build.bat
```

`build.bat` 内容就是：
```bat
call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\vcvarsall.bat"
devenv rime-wm6\rime-wm6.sln /Build "Debug|Windows Mobile 6 Professional SDK (ARMV4I)"
```

如果想要 Release：把 `Debug` 改成 `Release`。

### 构建字典
```bash
# 只用 luna_pinyin.dict.yaml（~480 KB .table.bin）
python tools/build_dict.py src/librime/data/minimal/luna_pinyin.dict.yaml data/luna_pinyin

# 加 essay.txt 常用词预置（推荐，~4 MB .table.bin）
python tools/build_dict.py src/librime/data/minimal/luna_pinyin.dict.yaml data/luna_pinyin \
    --preset src/librime/data/minimal/essay.txt --preset-min-weight 500
```

### 桌面端 sanity check
```bash
# 检查 .bin 头部和样本内容
python tools/inspect_bin.py data/luna_pinyin.prism.bin
python tools/inspect_bin.py data/luna_pinyin.table.bin

# 端到端 lookup（模拟 C++ Table::Query）
python tools/verify_dict.py data/luna_pinyin ni hao        # → 你好
python tools/verify_dict.py data/luna_pinyin zhong guo     # → 中國
python tools/verify_dict.py data/luna_pinyin ai qing gu shi  # → 愛情故事（4 syllable 走 tail-index）
```

---

## 5. 在设备上部署

> **注意**：本节是设计意图，**未在真机上验证过**。

### 文件布局（建议）
```
\Program Files\WMRime\
  ├── RimeCore.dll
  ├── WMRimeSIP.dll
  ├── setup.exe                    （可选：自己写的小 EXE 调 DllRegisterServer）
  └── data\
      ├── luna_pinyin.prism.bin
      └── luna_pinyin.table.bin
```

### 必需的注册表项
```
HKLM\Software\WMRime
    SharedDataDir = REG_SZ "\Program Files\WMRime\data"
```
`RimeInitialize` 会从这里读取 `shared_data_dir`，传给 `PinyinTranslator::LoadDictionary` 拼出字典文件路径。

`WMRimeSIP.dll` 的 `DllRegisterServer` 会额外写：
```
HKLM\Software\Classes\CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}
    (default) = "WMRime SIP"
HKLM\Software\Classes\CLSID\{...}\InprocServer32
    (default) = "<DLL 全路径>"
HKLM\Software\Microsoft\Shell\Keybd\{...}
    Name = "WMRime"
    IsSIPInputMethod = "1"
```

### 注册 SIP
WM6 上没有 `cmd.exe`，但有 `\Windows\regsvrce.exe`（等价于 desktop 的 regsvr32）。三条路：
- **A**. 装第三方文件管理器（Total Commander CE / Resco Explorer），用其 "Run" 功能执行 `regsvrce.exe <DLL路径>`
- **B**. 写个 50 行 setup.exe：`LoadLibrary` + `GetProcAddress("DllRegisterServer")` + 调用
- **C**. 打 `.cab` 安装包，用户拷过去点一下自动注册（见第 7 节）

注册成功后：开始 → 设置 → 输入 → SIP 列表里应该出现 "WMRime"，选它就生效。

---

## 6. 技术路线（已做 + 待办）

### 已经做了

**Phase A：兼容层 + 引擎核心**（结束于约 30 个文件）
1. `wince_compat`：用 wince::shared_ptr/function/signal/mutex 替代 std::C++11；regex.h 是手写 Thompson NFA 引擎；path.h 包 Win32 file API。
2. `librime/include/darts.h` 原样可用（header-only C++03）。
3. RIME 的 algo/config/dict/engine/service/translator 等模块按"镜像目录"的方式逐文件移植到 `src/librime_wince/`。源 .cc 文件做 C++03 backport（auto → 显式类型，lambda → 命名 functor，range-for → 迭代器循环，brace-init → ctor，nullptr → NULL 或 default `an<T>()`，`>>` → `> >`）。

**Phase B：字典管线**（在没有 marisa-trie 的情况下走通）
1. `Dictionary::Lookup` 完整移植；`Table` / `Prism` 重写读路径，写路径 stub。
2. `StringTable` 用 vector<string> + "RST1" 自定格式实现（保留与 marisa 同名 API）。
3. `Prism` 增加 flat-mode：`.prism.bin` 只存有序 syllable 列表 + checksum，加载时用 `Darts::DoubleArray::build()` 在内存里建 trie。
4. `tools/build_dict.py` Python 桌面工具：解析 luna_pinyin.dict.yaml + essay.txt → 输出 `.prism.bin` + `.table.bin`。
5. `tools/verify_dict.py` 模拟 C++ `Table::Query` 端到端走索引树，校验 ni hao → 你好 等。

**Phase C：Translator + 配置 + API**
1. `gear/pinyin_translator`（120 行，**意图为 MVP**）：跳过 Memory/Poet/UserDict/Corrector，直接 syllabifier → Dictionary::Lookup → FifoTranslation。**不是**上游 script_translator 的端口。
2. `config/yaml_parser`（600 行手写）：覆盖 block/flow/scalar/literal `|`，不支持 anchors/aliases/merge/folded/tags。`ConfigData::LoadFromStream/File` 接上。
3. `rime_api`（C 头 ~150 行 + impl ~450 行）：暴露给 SIP 的接口。**用 MiniSession 绕过 Engine 主循环**——因为 Engine 需要的 processor/segmentor/translator 链条在 gear/ 里没移植。

**Phase D：SIP shell**
1. `WMRimeSIP.dll`：COM in-proc server，IClassFactory + IInputMethod2。
2. 注册到系统 SIP 列表，subclass SIP framework 给的 hwnd。
3. UI：preedit 条 + 候选条 + 3 行软 QWERTY + 空格键。tap 派发到 `RimeProcessKey`，commit 通过 `IIMCallback::SendString` 推回前台 app。

### 还没做（按优先级）

**P0 — 解锁后续验证**
- [ ] **on-device 实测**：把 RimeCore.dll + WMRimeSIP.dll + .bin 文件部署到 WM6 设备，验证基本流程。这一步**强烈建议下一步就做**——后面所有功能开发都建立在"现有架构能跑"的假设上，越早验证越好。

**P1 — 架构对齐上游**
- [ ] **gear/ 真模块**移植：speller (processor)、abc_segmentor、ascii_composer、selector、express_editor。完成后可以删 `MiniSession`，让 rime_api 真正走 Engine 主循环。会暴露当前 Engine 端口的隐藏 bug。
- [ ] **SIP UI 抛光**：Shift 键、数字行、符号、中英切换、翻页按钮、视觉风格。
- [ ] **写 setup.exe** 或打 .cab 安装包（第 7 节）。

**P2 — 数据兼容性**
- [ ] **marisa-trie vendor**：把上游 `src/librime/deps/marisa-trie` 拉满，把模板 C++ 代码 backport 到 C++03 + MSVC9。完成后：
  - `StringTable` 切到 marisa-backed 实现
  - 我们的 `.table.bin` 与上游字节兼容
  - 可以直接用 `librime-deploy` 在桌面生成 `.bin` 部署到设备
- [ ] **YAML emitter**：`ConfigData::SaveToStream/File` 当前是 stub。
- [ ] **UserDictionary**（学习功能）：需要先解决 LevelDB 替代（SQLite？还是简单文件 KV？）

**P3 — Nice to have**
- [ ] OpenCC 简繁转换（依赖 simplifier filter + opencc 库）
- [ ] 多 schema 切换（switcher）
- [ ] 反查 / 倉頡 / 双拼方案

---

## 7. 手动制作 CAB 包指南

WM6 安装包用 **CAB** 格式。WM SDK 自带 `cabwiz.exe` 可以打包，但需要写 `.inf` 描述文件。

详细步骤见 [docs/cab-packaging.md](docs/cab-packaging.md)。

简要流程：
1. build Release 版的 RimeCore.dll + WMRimeSIP.dll
2. 把 4 个文件（2 个 DLL + 2 个 .bin）放进 `dist/`
3. 写 `WMRime.inf`（cab-packaging.md 里有完整模板）
4. 跑 `cabwiz.exe WMRime.inf /compress` → 产出 `WMRime.ARMV4I.cab`
5. ActiveSync 推到设备，点击安装
6. INF 里直接写 COM 注册表项，**不需要** setup.dll / regsvrce.exe

---

## 8. 给后续 LLM 接力的提示

### 优先读这些（按顺序）
1. **本文档**（README.md）
2. [`docs/agent-memory/MEMORY.md`](docs/agent-memory/MEMORY.md) — 上一个 agent 的持久化记忆索引，列了所有积累下来的"踩坑记录"和"决策记录"
3. 跟当前任务相关的 [`docs/agent-memory/*.md`](docs/agent-memory/) 单条记忆

> 注：`docs/agent-memory/` 是项目随 git 一起 ship 的快照。如果你使用 Claude Code 等 agent 系统，可以把它们拷到本地 agent memory 路径（如 `~/.claude/projects/<ws>/memory/`）让 agent 主动加载；否则当成普通文档读即可。

### 关键约定（严格遵守，违反必踩坑）

**约定 1：ASCII-only 源文件**
WinCE port 的所有 `.h`/`.cc` 文件**不能包含非 ASCII 字符**，注释也不行。MSVC9 在 cp936 中文 Windows 上会把无 BOM 的 UTF-8 误读成 cp936，C4819 警告 + 后续大量 C2065 编译失败。
- 表达中文意思的注释，用拼音或英文：`// preedit: ni hao` 而不是 `// 预编辑：你好`
- 字符串字面量里的中文也不行；测试 UTF-8 时用 `"\xe4\xbd\xa0\xe5\xa5\xbd"` 这种转义形式

**约定 2：C++03 only**
没有 `auto`/`nullptr`/`=default`/`=delete`/lambda/range-for/brace-init/NSDMI/std::move/variadic templates/template aliases。
- `vector<an<T>>` → `vector<an<T> >`（避免 `>>` 解析）
- `nullptr` → `NULL` 或 `an<T>()`（空智能指针）
- `auto x = ...` → 显式类型
- `[](T x) { ... }` → 命名 functor struct
- `Type x = {1, 2}` → `Type x(1, 2)` 或者加显式 ctor

**约定 3：智能指针包装**
项目用 `an<T>` / `the<T>` / `of<T>` / `weak<T>` 包装 `wince::shared_ptr<T>`。坑：
- 这些包装类**没有从 raw pointer 的 ctor**（被 explicit 拒绝）。要 `the<T> x; x.reset(new T(...))`，不要 `the<T> x(new T(...))`。
- `of<T>` 继承自 `an<T>`（不是兄弟），所以 `vector<of<T> >` 的元素可以 bind 到 `const an<T>&`。这是关键架构修复，参见 `memory/feedback_alias_pointer_gotchas.md`。

**约定 4：不写自定义 DllMain**
WinCE/MSVC9 任何 DllMain 写法都会爆 C2731。改用 static initializer（CRT$XCU section）或 `RIME_MODULE_INITIALIZER` 宏。见 `memory/feedback_dllmain_msvc9.md`。

**约定 5：YAML 是 rime 的灵魂**
当前 `builtin_schemas` 里的 hardcoded ConfigData 是 MVP 临时脚手架，真 yaml parser 已经写好（`config/yaml_parser`）但还没有完全替换 builtin_schemas。新功能尽量走真 yaml 加载，不要在 builtin_schemas 里添 hardcode。

### 常踩的坑（按出现频率）

| 错误码 | 原因 | 修复 |
|---|---|---|
| C4819 + 一堆 C2065 | 源文件含非 ASCII 字符 | 把中文翻成 ASCII（拼音/英文） |
| C2731 on DllMain | WinCE/MSVC9 不允许 | 用 static init / .CRT$XCU |
| C2027 inline virtual body | param 类型未完整定义 | 把默认空 body 移到 .cc |
| C2471/C1083 on vc80.pdb | PDB 锁住 | 删掉 .pdb 重 build |
| C2375 on Dll* | 重复声明 + 不同 linkage | 用 .def 文件导出，不要 dllexport |
| unresolved external CLSID_* / IID_* | DEFINE_GUID 没有 INITGUID | sip_main.cc 加 `#define INITGUID` + `#include <initguid.h>` |
| unresolved external `time` | WinCE coredll 没有 time() | `src/wince_compat/time.cc` 已 wrap GetTickCount，确认链接 |
| C3861 SetPropW/GetPropW | WinCE 不存在 | 用 `std::map<HWND, T*>` + CRITICAL_SECTION |

### 构建工作流（**用户已授权 LLM 主动调用**）
- 写完代码就跑 `./build.bat`（用 PowerShell 包一下绕过路径里的 `#`）：
  ```powershell
  $p = "C:\Users\Joe\Documents\C#\rime-wm6"; Push-Location $p; cmd /c ".\build.bat" 2>&1 | Select-Object -Last 80; Pop-Location
  ```
- 目标：**0 errors 0 warnings**。Warning 也要修，因为 cp936 的 C4819 会顺势引发 C2065。

### 当前 session 没干完但有"应该接力"的方向

按现状的优先级建议：
1. **on-device 实测**（P0，必须先做这个，验证一切是否真的能跑）
2. **gear/ 真模块**（P1，让 Engine 主循环活起来，删 MiniSession）
3. **marisa-trie 移植**（P2，binary 兼容上游）

记得更新本 README 第 2 节、第 6 节"已做/待办"，以及 `memory/MEMORY.md` 的 index。

---

## 9. 关于 license

- 上游 `src/librime/` 是 BSD-3-Clause（rime/librime 的 LICENSE）
- 本项目（WinCE 移植部分）：暂未定，建议继承 BSD-3-Clause 保持兼容

---

## 10. 联系 / 历史

项目当前状态：开发中，**未发布**，未在真机验证。

如果你（无论人还是 LLM）接手了这份项目并想往前推，请：
1. 读完本 README 和 [`docs/agent-memory/MEMORY.md`](docs/agent-memory/MEMORY.md)
2. 用 `./build.bat` 验证当前能 0 错 0 警告编过
3. 选定一个 P0/P1 任务，在 memory 里加一条"开工"记录
4. 完成后更新 README 第 2 节状态表 + 第 6 节进度
