# 手动打包 WMRime CAB 安装包

WM6 应用的安装包标准格式是 **CAB**（Microsoft Cabinet）。一个 CAB 里同时打包二进制文件 + 注册表项 + 快捷方式描述，用户点击就完成完整安装。

工具链：**cabwiz.exe**（VS2008 SmartDevices SDK 自带），位置：

```
C:\Program Files (x86)\Microsoft Visual Studio 9.0\SmartDevices\SDK\SDKTools\cabwiz.exe
```

cabwiz 读 `.inf` 描述文件，吐出 `.cab`。

---

## 一、文件清单

打包前先把要进 CAB 的东西放好。建议在 `dist/` 下临时组织：

```
dist/
├── WMRime.inf                              ← 描述文件（你写）
├── RimeCore.dll                            ← 从 rime-wm6\RimeCore\...\Release\ 拷过来
├── WMRimeSIP.dll                           ← 从 rime-wm6\WMRimeSIP\...\Release\ 拷过来
├── luna_pinyin.prism.bin                   ← 从 data\ 拷过来
└── luna_pinyin.table.bin                   ← 从 data\ 拷过来
```

**用 Release 版本打包**，不用 Debug（Debug 大、且依赖调试运行时）。

---

## 二、`WMRime.inf` 模板

下面是个能直接用的模板。**所有 GUID 需要替换成你的实际值**（CLSID_WMRimeSIP 在 `src/wmrime_sip/clsids.h` 里）。

```ini
[Version]
Signature   = "$Windows NT$"
Provider    = "WMRime"
CESignature = "$Windows CE$"

[CEStrings]
AppName    = "WMRime"
InstallDir = %CE1%\%AppName%       ; %CE1% = \Program Files

[Strings]
Manufacturer = "WMRime Project"
ProductName  = "WMRime Input Method"

[CEDevice]
ProcessorType = 2577               ; ARMV4I; 2577 == IMAGE_FILE_MACHINE_ARMV4I
VersionMin    = 5.0                ; WinCE 5.0+, covers WM6.x
VersionMax    = 7.99

[DefaultInstall]
CopyFiles  = Files.Sip, Files.Engine, Files.Data
AddReg     = Reg.WMRime, Reg.SIP, Reg.CLSID, Reg.InprocServer

CESetupDLL = ""

[SourceDisksNames]
1 = , "Common files",, .

[SourceDisksFiles]
RimeCore.dll               = 1
WMRimeSIP.dll              = 1
luna_pinyin.prism.bin      = 1
luna_pinyin.table.bin      = 1

; ---------- DestinationDirs ----------
; %CE1% = \Program Files, %CE2% = \Windows.
; CRITICAL: the engine DLL must go in \Windows\ because WinCE's
; dependent-DLL search path is {loading process cwd, \Windows} --
; NOT the directory of the importing DLL. Putting RimeCore.dll in
; \Program Files\WMRime\ next to WMRimeSIP.dll looks natural but
; makes WMRimeSIP.dll fail to load (silent), and the SIP picker
; will then drop our entry without any visible error.
[DestinationDirs]
Files.Sip       = 0, %InstallDir%       ; \Program Files\WMRime\
Files.Engine    = 0, %CE2%              ; \Windows\
Files.Data      = 0, %InstallDir%\data

; ---------- File copy groups ----------
[Files.Sip]
WMRimeSIP.dll

[Files.Engine]
RimeCore.dll

[Files.Data]
luna_pinyin.prism.bin
luna_pinyin.table.bin

; ---------- Registry: where the engine finds dict files ----------
[Reg.WMRime]
HKLM,"Software\WMRime",,0x00000000,
HKLM,"Software\WMRime","SharedDataDir",0x00000000,"%InstallDir%\data"

; ---------- Registry: SIP enumeration (so the shell shows "WMRime") ----------
; Replace {7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47} with your CLSID_WMRimeSIP.
; IMPORTANT: IsSIPInputMethod must be REG_DWORD (flag 0x00010001), not
; REG_SZ. The shell's SIP enumerator silently skips entries with the
; wrong type, and your input method won't appear in the picker.
[Reg.SIP]
HKLM,"Software\Microsoft\Shell\Keybd\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}","Name",0x00000000,"WMRime"
HKLM,"Software\Microsoft\Shell\Keybd\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}","IsSIPInputMethod",0x00010001,1
HKLM,"Software\Microsoft\Shell\Keybd\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}","PreferredImage",0x00010001,0

; ---------- Registry: COM CLSID -> DLL (mirror under HKLM\Classes + HKCR) ----------
[Reg.CLSID]
HKLM,"Software\Classes\CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}",,0x00000000,"WMRime SIP"
HKCR,"CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}",,0x00000000,"WMRime SIP"

[Reg.InprocServer]
HKLM,"Software\Classes\CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}\InprocServer32",,0x00000000,"%InstallDir%\WMRimeSIP.dll"
HKLM,"Software\Classes\CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}\InprocServer32","ThreadingModel",0x00000000,"Apartment"
HKCR,"CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}\InprocServer32",,0x00000000,"%InstallDir%\WMRimeSIP.dll"
HKCR,"CLSID\{7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}\InprocServer32","ThreadingModel",0x00000000,"Apartment"
```

### 几个 INF 语法的小坑
- **Section 名大小写**：cabwiz 区分大小写。模板里我用 `Files.Bin` / `Reg.WMRime` 这种命名风格，统一即可。
- **REG_SZ 用 0x00000000 (= 0)**；REG_DWORD 用 0x00010001；REG_MULTI_SZ 用 0x00010000；REG_BINARY 用 0x00000001。
- **`%InstallDir%`**：从 `[CEStrings]` 解析，默认会展开成 `\Program Files\WMRime`。
- **`%CE1%`**：CE1 = `\Program Files`，CE2 = `\Windows`，CE11 = `\Windows\Start Menu\Programs`，CE17 = `\Windows\Startup`。
- **ProcessorType**：ARMV4I = 2577（十进制），ARMV5 也用这个值。

---

## 三、调用 cabwiz

打开 VS2008 命令行（`vcvarsall.bat` 那个），cd 到 `dist/`，跑：

```bat
"C:\Program Files (x86)\Microsoft Visual Studio 9.0\SmartDevices\SDK\SDKTools\cabwiz.exe" WMRime.inf /compress /err WMRime.err
```

**参数说明**：
- `WMRime.inf` —— 输入文件
- `/compress` —— 压缩
- `/err WMRime.err` —— 错误日志输出（如果有错的话）
- `/cpu ARMV4I` —— 可选，针对单一 CPU 出包；省略会读 `[CEDevice]` 段

成功后产出 `WMRime.ARMV4I.cab`（或类似名字）。

---

## 四、部署 + 安装

1. ActiveSync / WMDC 把 `WMRime.ARMV4I.cab` 拷到设备的 `\Storage Card\` 或 `\My Documents\`
2. 设备上用文件浏览器（自带的 File Explorer 即可）找到这个 `.cab`，**点击**
3. WM 系统自带的 `wceload.exe` 接管，询问安装位置（建议选 "Device" 即内置存储），点 "Install"
4. 安装完会自动：
   - 拷贝 4 个文件到 `\Program Files\WMRime\`
   - 写完所有注册表项
   - **DLL 的 COM 注册不会自动完成**——这是个坑，下面 §5 处理

---

## 五、CAB 不会自动 regsvr 的解决办法

WM 的 CAB 安装**不会**自动调 `DllRegisterServer`。需要其中一种方式触发：

### 方案 A：手动 regsvrce（开发期）
设备上找到 `\Windows\regsvrce.exe`，传 DLL 路径作为参数跑一次。但 WM 没有 cmd，需要文件管理器（Total Commander CE 之类）的 "Run" 功能。

### 方案 B：写 setup.dll（推荐用户场景）
cabwiz 支持 `CESetupDLL = "Setup.dll"`，安装完成时会自动调里面的 `Install_Init` / `Install_Exit` 回调。在 `Install_Exit` 里 LoadLibrary + GetProcAddress + 调 DllRegisterServer。setup.dll 模板：

```cpp
// setup.cc — 编成 setup.dll，放进 CAB
#include <windows.h>
#include <ce_setup.h>

extern "C" codeINSTALL_INIT Install_Init(HWND, BOOL, BOOL, LPCTSTR) {
    return codeINSTALL_INIT_CONTINUE;
}

extern "C" codeINSTALL_EXIT Install_Exit(HWND, LPCTSTR pszInstallDir,
                                          WORD cFailedDirs, WORD cFailedFiles,
                                          WORD cFailedRegKeys, WORD cFailedRegVals,
                                          WORD cFailedShortcuts) {
    if (cFailedDirs || cFailedFiles || cFailedRegKeys || cFailedRegVals)
        return codeINSTALL_EXIT_UNINSTALL;

    // Register the SIP DLL.
    wchar_t path[MAX_PATH];
    wcscpy(path, pszInstallDir);
    wcscat(path, L"\\WMRimeSIP.dll");

    HMODULE h = LoadLibraryW(path);
    if (h) {
        typedef HRESULT (STDAPICALLTYPE *PFN)(void);
        PFN reg = (PFN)GetProcAddress(h, L"DllRegisterServer");
        if (reg) reg();
        FreeLibrary(h);
    }
    return codeINSTALL_EXIT_DONE;
}

BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID) { return TRUE; }
```

把这个 setup.dll 编成 ARMV4I DLL，跟其他文件一起放进 dist/，INF 里设 `CESetupDLL = "Setup.dll"`，cabwiz 会自动处理。

### 方案 C：让 INF 直接写注册表（最省事）
**我们 INF 模板已经这么做了**——`[Reg.CLSID]` 和 `[Reg.InprocServer]` 段把 COM CLSID 注册表项直接写好，等价于手动 regsvr32 的效果。**但要确保 `DllRegisterServer` 里写的注册表项和 INF 里完全一致**。

我们的 `sip_main.cc::DllRegisterServer` 写：
- `HKLM\Software\Classes\CLSID\{...}\(default) = "WMRime SIP"`
- `HKLM\Software\Classes\CLSID\{...}\InprocServer32\(default) = "<DLL path>"`
- `HKLM\Software\Microsoft\Shell\Keybd\{...}\Name = "WMRime"`
- `HKLM\Software\Microsoft\Shell\Keybd\{...}\IsSIPInputMethod = "1"`

模板 INF 都覆盖了。所以 **方案 C 就够了，不用 setup.dll**。

---

## 六、卸载

WM6 的 Settings → Remove Programs 里会列出 "WMRime"。卸载时系统会：
- 反向执行 `DefaultInstall` 段的 CopyFiles（删除文件）
- 反向执行 `AddReg`（删除注册表项）
- 如果有 `CESetupDLL`，会调里面的 `Uninstall_Init` / `Uninstall_Exit`

**不会**自动调 `DllUnregisterServer`——但如果走 §5 方案 C，反向 AddReg 就把 CLSID 项删干净了，效果等价。

---

## 七、调试 CAB 失败

- **设备上看错误信息**：CAB 安装失败时设备会弹个对话框给个 HRESULT。常见值：
  - `0x80070005` — Access denied（注册表项写不进，可能要 SecurityPolicy 例外）
  - `0x80070002` — File not found（INF 里写了文件但没在 `[SourceDisksFiles]` 列）
  - `0x80004005` — Generic fail（cabwiz 警告级别太低看不出，加 `/dest` 重做一遍）
- **桌面 cabwiz 错误**：`/err WMRime.err` 输出的日志，常见是 INF 语法错（section 名拼错、缺逗号）。
- **安装日志**：设备上看 `\Windows\AppMgr\install.log`（如果存在）。

---

## 八、签名（可选）

WM6 设备如果设了 Privileged/Unsigned policy，未签名的 CAB 会被拒。开发期可以通过 Settings → System → Security → Certificates 加你的开发证书；生产环境用 **Mobile2Market** 流程签名。

签名工具：`signtool.exe`（VS2008 自带）。流程：
1. 用 `MakeCert.exe` 生成自签证书 + `.pvk`/`.cer`
2. `Cert2SPC.exe` 转成 `.spc`
3. `pvk2pfx.exe` 合成 `.pfx`
4. `signtool sign /f WMRime.pfx /p <密码> WMRime.cab`

测试设备一般 Unprivileged，未签名也能装。

---

## 九、附：dist/ 里要放的 Release 文件路径

```
rime-wm6\RimeCore\Windows Mobile 6 Professional SDK (ARMV4I)\Release\RimeCore.dll
rime-wm6\WMRimeSIP\Windows Mobile 6 Professional SDK (ARMV4I)\Release\WMRimeSIP.dll
data\luna_pinyin.prism.bin
data\luna_pinyin.table.bin
```

build Release 版的命令：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\vcvarsall.bat"
devenv rime-wm6\rime-wm6.sln /Build "Release|Windows Mobile 6 Professional SDK (ARMV4I)"
```
