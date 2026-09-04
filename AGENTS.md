# AGENTS.md — 给 AI 协作者的项目手册

本文件供 AI 代理（及人类贡献者）快速理解 pxb-preview 的架构、决策、构建与坑。修改代码前请先读本文件；遇到"怪问题"先查 §7 已知坑。

## 1. 项目是什么

PXB 像素画预览器（C++17，App 版本 **0.0.1**，pxb 文件格式 v2.2）。
读取 gzip 压缩的二进制像素画文件（`.pxb`，MZB.ONE 生成），在 GUI 中预览多帧动画、管理同级文件、注册 Windows 文件关联与缩略图。

关键事实：
- pxb 文件 = gzip → `PXB1` + u16/u16 版本 + u64 JSON 长度 + JSON 元数据 + 数据区（内嵌合成 PNG）。详见 [docs/pxb-format-spec.md](docs/pxb-format-spec.md)。
- **pxb 只内嵌"合成后的整帧 PNG"**，无逐层像素——图层勾选只能记录状态，不能改画面。
- 帧无显式时长字段（播放回退全局 fps；解析器已前向兼容可选的 `duration`）。

## 2. 目录结构

```
src/
  main.cpp           入口；--register/--unregister headless 模式（UAC 子进程）
  core/              纯逻辑层，无 UI/GL，被 app 与缩略图 DLL 复用
    gzip.cpp         gzip 头自解析 + miniz raw inflate（MZ_NO_FLUSH！）
    gzip_partial.*   GzipInflater 增量（流式）解压：懒读取的基础设施
    pxb_meta.cpp     懒读取：只解压头部/JSON/缩略图（列表与缩略图路径）
    pxb_cache.cpp    同级文件元数据缓存（内容指纹 key、LRU、shared_ptr 出口）
    json_min.h       极简 JSON 解析器（仅覆盖 pxb 元数据子集）
    image.cpp        stb_image 解码 → RgbaImage
    fileutil.cpp     UTF-8 路径辅助 + list_directory + read_file_bytes
    pxb_reader.cpp   pxb 解析（frame 排序、duration 前向兼容）
    pxb_format.h     RgbaImage/SceneInfo/FrameInfo/PxbDocument
    pxb_reg_contract.h  ★文件关联契约（CLSID/ProgID/shellex 键）唯一权威
  app/
    app.h/cpp        App 状态机：load_file/refresh_siblings/tick
    ui.cpp           render_ui：三栏 + 帧面板 + 状态栏 + 菜单 + About（布局常量按 ui_scale() 缩放）
    renderer.*       GL 纹理（GL_NEAREST）+ ImGui 初始化 + DPI 感知（ini 已禁用）
    gl_loader.cpp    SDL_GL_GetProcAddress 运行时解析 GL 函数
    regutil.*        .pxb 文件关联：查询/注册/注销/提权（Win32）
    i18n.h/cpp       ★中英双语：Str 枚举 + zh/en 表 + tr()/trw()/set_lang()
assets/
  icon.png           应用图标（随 dist 分发，About 窗口加载）
  icon-source.png    源图（1920×1920）
  icon.ico           多尺寸 Windows 图标（PIL 生成）
  icon.rc            ★exe 资源：ICON + IDD_ABOUT 原生对话框模板
integrations/
  windows/PxbThumbnailHandler/  COM 缩略图 DLL（DllRegisterServer 拥有注册契约）
  windows/package.nsi           NSIS 安装包脚本（路径/版本由打包脚本以 /D 注入）
  linux/  macos/                其他平台集成（未实机验证）
scripts/              fetch_sdl3.ps1（SDL3 预编译包拉取到 external/）、package_windows.ps1（一键打包）
.github/workflows/    GitHub Actions CI（Windows 构建+打包+tag 自动 Release；Linux/macOS 尽力而为）
thirdparty/           vendored: imgui/stb/miniz（构建离线）
docs/                 文档（BUILD.md、architecture.md、pxb-format-spec.md）
CMakeLists.txt        CMake 构建配置
```

## 3. 技术选型与决策记录（不要推翻，除非有充分理由）

1. **渲染 = SDL3 + Dear ImGui + OpenGL 2.1，拒绝 bgfx**。理由：渲染极简（贴纹理+UI），bgfx 多后端抽象是过度设计。GL 函数运行时解析，无 glad。完整选型论证见 [docs/architecture.md](docs/architecture.md)。
2. **单一事实源**：
   - 注册契约（CLSID/ProgID/shellex）只在 `src/core/pxb_reg_contract.h`；app（regutil）与 DLL 都 include 它。**禁止**在别处硬编码 `{A1B2C3D4-...}` 等（`{E357FCCD-...}` shellex 键也是）。
   - 文件关联注册动作由 DLL 的 `DllRegisterServer/DllUnregisterServer` 执行（等同 regsvr32），exe 通过 `LoadLibrary` 调用——不在 exe 里复制键名逻辑。
3. **文件关联收编进 GUI**（File 菜单注册/取消/实时状态），分发不带 install.bat/uninstall.bat。UAC 提权路径：GUI → `ShellExecuteW(runas, --register/--unregister)` → main.cpp headless 分支（SDL 初始化前，纯 Win32 + MessageBox）。
4. **i18n**：新文案必须加 `Str` 枚举 + zh/en 两表（i18n.cpp，顺序一致，`static_assert` 校验）。格式串注意：
   - 传给 `ImGui::SliderInt` 的 format 要保留 `%%d`（如 `"帧 %%d/%d"`）；
   - 传给 `snprintf`/`ImGui::TextWrapped`/`SetTooltip` 的格式串直接含 `%d/%s` 并在调用点传参。
5. **About = Win32 原生 DialogBox**（`IDD_ABOUT` 资源 + `DialogBoxParamW`），真正独立 OS 窗口。**不要**用 ImGui popup/modal 弹 About（菜单关闭同帧创建 popup 会被 ImGui 吞掉，表现为"点击无反应"）。非 Win 平台回退 ImGui 浮窗（`app.show_about`）。
6. **imgui.ini 全局禁用**（`io.IniFilename = nullptr`）；所有窗口 NoSavedSettings。不要恢复 ini 机制。
7. **DPI 感知**：窗口创建尺寸 = 逻辑尺寸 × 显示缩放（Apple 平台除外）；`ui.cpp` 的一切手写布局常量按 `ui_scale()` 缩放，新增布局代码必须同样乘系数（详见 renderer.cpp/ui.cpp 注释）。
8. 版本号：App 版本在 `src/app/i18n.h` 的 `kAppVersion`（唯一权威）。所有平台消费方从它派生：Windows 打包脚本解析后注入 NSIS 的 `APP_VERSION`；Linux deb/AppImage 与 macOS dmg 走 `scripts/app_version.sh`；macOS .app 的 Info.plist 由 CMake 解析（`PXB_APP_VERSION` → `MACOSX_BUNDLE_*` 占位符）。**不要在任何脚本里写死版本号。**

## 4. 构建

SDL3 不在仓库里（唯一外部二进制依赖，gitignored）。首次构建前：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch_sdl3.ps1   # → external/SDL3
```

configure 时 `-DCMAKE_PREFIX_PATH` 指向 `external/SDL3`（或任何已安装的 SDL3 前缀）。工具链要求：CMake 3.16+、Ninja、C++17 编译器（Windows 推荐 MinGW-w64 GCC 13+）。

### 主程序

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<SDL3 前缀>"
cmake --build build
```
产物：`build/pxb-preview.exe`

### 缩略图处理器（Windows）

```bash
cmake -S integrations/windows/PxbThumbnailHandler -B build_thumb -G Ninja
cmake --build build_thumb
```
产物：`build_thumb/PxbThumbnailHandler.dll`（不依赖 SDL3，只用 src/core + GDI）

⚠️ **Ninja 时间戳坑**：改动源码后偶尔报 "no work to do"（文件系统 mtime 粒度），此时 `touch 源文件` 强制重编——**务必确认 exe 时间戳已更新再打包**。

## 5. 打包 / 分发流程

**唯一标准路径（本地与 CI 相同）：**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1
```

自动完成：组装 `dist/windows/pxb-preview-<版本>-win64/`（exe、缩略图 DLL、SDL3.dll、winpthread、icon、README）→ 生成 `dist/windows/pxb-preview-<版本>-win64-setup.exe` 与 `...-portable.zip`。**所有产物集中在 `dist/<系统>/`**，文件名带版本与架构；exe 本体名保持 `pxb-preview.exe`（文件关联、DefaultIcon、快捷方式引用它，不要改名）。版本号从 `i18n.h` 的 `kAppVersion` 解析并以 `/DAPP_VERSION` 注入 NSIS。所有 makensis 输入都是绝对路径 `/D`——**不要再手动 `cd integrations/windows && makensis`**（相对路径默认值曾漂移出"找不到 icon.ico"事故；手动兜底时必须 /DSOURCE_DIR /DASSETS_DIR /DPROJECT_ROOT /DOUT_FILE /DAPP_VERSION 全部显式传）。

注意：`libwinpthread-1.dll` 是 exe 与缩略图 DLL 的**动态导入**（`objdump -p` 实证），打包脚本会自动从 MinGW 工具链获取；探测失败时用 `-MingwBin` 显式指定。

## 6. 测试方法

- **解析核心**：编译独立测试程序调 `read_pxb_file` 验证帧数/尺寸/错误。
- **UTF-8 路径**：构造中文路径文件测试 `path_exists/read_file_bytes/load_file`。
- **headless 模式**：`./build/pxb-preview.exe --register` 不初始化 SDL，可测 UAC 子进程路径。
- **UI 验证**：启动 exe 验证基本界面功能；高分屏（125%–200%）下检查 DPI 缩放。
- **CI**：推 tag 前先看 GitHub Actions 是否绿——Windows 是发版必经路径。

## 7. 已知坑（按重要性排序——改到相关代码必读）

1. **miniz 增量 inflate 必须用 `MZ_NO_FLUSH`**。`MZ_FINISH` 在输出缓冲（初值 64KB）填满扩容后返回 `MZ_DATA_ERROR` → 解压 >64KB 的 pxb 全部失败（曾导致 72 帧动画打不开）。gzip.cpp 已修复，别改回去。
2. **MinGW "C" locale 会破坏 UTF-8 路径**：`std::filesystem::path::string()` 把 UTF-8 当 ANSI。路径辅助函数必须手动切分 UTF-8（`/`、`\` 是 ASCII 安全），文件读取/存在检查用 Win32 宽字符 API（`CreateFileW`/`GetFileAttributesW`）。
3. **Win32 文件对话框 filter 是双 NUL 分隔列表**：不能用 `L"\0*.pxb\0"` 字面量拼接（编译期被 \0 截断成空串，导致"看不到 .pxb 文件"）。要逐字符 `filter += L"*.pxb"; filter += L'\0';`。
4. **NSIS 脚本含中文必须 UTF-8 BOM**；electron-builder 精简版 NSIS 无 `MUI_PAGE_LANGUAGE`（用 `MUI_LANGDLL_DISPLAY`）；`MUI_ICON` 用 assets/icon.ico。
5. **`RgbaImage` 字段名是 `pixels`**（不是 data），且没有 (w,h) 构造函数——手动 `width/height/pixels.assign`。
6. **`refresh_siblings()` 有目录缓存**（`dir == siblings_dir_` 短路）——`load_file` 后必须 `siblings_dir_.clear()` 强制重扫，否则打开同目录文件列表不刷新。
7. **ImGui popup 在菜单关闭同帧创建会被吞** → 独立窗口用普通 `Begin` + bool 开关（见决策 5）。
8. **缩略图纹理 key**：当前文档帧用 `0..n-1`、文档缩略图用 `-999`；同级缩略图由 `MetaCache` 分配递减负 key（起点 `-1000000`，即 `gl_tex_key`，`-1` = 未上传）。同级列表的 cache miss 不在 UI 线程同步解压：`sibling_meta()` 只做非阻塞 `find()`，miss 入队预取线程异步补（见 app.cpp / pxb_cache）。
9. 文件读取（pxb/png）一律走 UTF-8 → wide → Win32 API；`std::ifstream` 在中文路径下会失败。
10. **SDL3 拖放事件字符串不要 SDL_free**：`SDL_DropEvent.data`（及 `source`）是 SDL3 的 "temporary memory"，SDL 在下一次事件 pump 时自动释放（SDL_FreeTemporaryMemory）。应用只保证在本次同步回调内使用有效；手动 free 会 double free（与 SDL2 约定相反）。
11. **ImGui GL3 后端初始化顺序**：`rebuild_fonts()` 里的字体图集上传调用 `ImGui_ImplOpenGL3_CreateFontsTexture()`，而该后端的 GL 函数指针表由 `ImGui_ImplOpenGL3_Init()` 解析——**Init 必须先于 rebuild_fonts**（renderer.cpp 用 `gl3_backend_ready_` 标志兜底）。顺序颠倒 = 调空函数指针 → 0xc0000005 崩溃（曾发生在"init 阶段急切上传字体"的未验证改动里）。
12. **DPI 缩放要成套改**：新增 UI 元素的手写像素常量必须乘 `ui_scale()`（ui.cpp），窗口尺寸逻辑在 renderer.cpp init()（Apple 平台除外——SDL 那边坐标是逻辑点，乘了会错一倍）。只改字体不改布局，或反之，都会在 175% 屏上出现截断/错位。
13. **改完源码 ≠ 改完产物**：曾发生"源码已修、build 未编、dist 未同步 → 分发版完全不含修复"的事故。发版前检查链条：build exe 时间戳 > 源码 mtime → `scripts/package_windows.ps1` 重打 → `dist/windows/` 下产物时间戳更新。用户实际运行的是 dist/ 或安装版 exe，不是 build/ 里的。
14. **CJK 字形范围别用完整 `0x4E00-0x9FFF`**：那是全套 ~21k 汉字，启动时 stb 栅格化 ~300ms + 图集上传 ~270ms（合计 ~0.6s，曾构成"打开软件白屏 1 秒"的全部根因）。renderer.cpp rebuild_fonts 的正确做法：`GetGlyphRangesChineseSimplifiedCommon()`（2500 常用字）+ `zh_strings()` 全量并入（i18n 文案自动覆盖"帧/剔/瞰"这类漏网字，新文案加进 kZh 即自动生效）。改字体必须同时保留这两步，否则要么慢、要么中文出 '?'。

## 8. 编码规范

- C++17；头文件 `#pragma once`；命名空间 `pxb`；snake_case 函数/变量、CamelCase 类型。
- 平台差异用 `#ifdef _WIN32`（默认 Windows 优先），非 Win 提供桩/回退，保持可编译。
- UI 文案**必须走 i18n**（禁止硬编码中英文字符串到 ui.cpp/app.cpp）。
- 注册相关值必须引用 `pxb_reg_contract.h`。
- 内存：RgbaImage 用 vector，纹理经 Renderer 缓存（clear_textures 在切文件时调用）。

## 9. 跨平台现状

- Windows：✅ 实机验证（构建/运行/注册/缩略图/安装包）。
- Linux：代码 + 打包脚本齐（deb/AppImage/thumbnailer），CI 有构建作业，**未实机验证**。
- macOS：代码 + dmg/QuickLook 齐，CI 有构建作业，**未实机验证**；OpenGL 2.1 已被 Apple 标记 deprecated（唯一可能需要动代码的点）。
- CI：`.github/workflows/ci.yml`——Windows 完整构建+打包（MSYS2 MinGW + 官方 SDL3 预编译包 + choco NSIS），`v*` tag 自动发 Release（仅 Windows 产物）；Linux/macOS 为构建作业且 `continue-on-error`，失败不影响发版。
