# 技术架构设计（architecture）

> 跨平台（Windows / Linux / macOS）轻量级 `pxb` 预览程序。
> 目标：读取 gzip 压缩的二进制像素画数据，提供文件管理、多帧浏览、图层列表与显隐、帧动画播放/暂停与帧率控制；三平台文件关联 + 文件管理器内直接预览；单文件/极少文件分发，依赖最小化，开源、架构清晰、代码规范。

---

## 0. 功能特性与硬性要求

本程序必须满足以下两项关键功能要求（均已落地实现并在 Windows 上构建验证）：

1. **PXB GUI 预览功能（实时预览）**：系统必须集成图形界面预览能力，基于 SDL3 + Dear ImGui 提供实时、所见即所得的 `.pxb` 预览——多帧切换、图层列表与显隐、帧动画播放/暂停与 FPS 控制均在同一 GUI 内完成。双击 `.pxb` 默认由本程序打开。
2. **文件管理器内直接预览主图**：在 Windows 资源管理器 / macOS 访达 / Linux 文件管理器中，**无需调用外部工具即可直接查看图片大致内容**（区别于系统默认图标/占位缩略图）。由三平台系统集成（§4）将文件内嵌的合成 PNG 渲染为缩略图/QuickLook，使主图在主管理器内即可浏览。

**环境约定**：SDL3 为唯一外部依赖，CMake 通过 `CMAKE_PREFIX_PATH` 指向其安装目录即可；具体编译器、CMake、SDL3 版本请以本机工具链为准。详细构建命令见 `README.md` / `docs/BUILD.md`。

---

## 1. 渲染与图形技术选型（含 bgfx 评估）

### 1.1 结论（先给答案）

**选用：SDL3 + Dear ImGui + stb_image + miniz（单一跨平台代码库）。**
**拒绝 bgfx、Skia、glad；不采用每平台原生图形后端作为默认方案。**

### 1.2 候选对比

| 方案 | 体积(静态) | 依赖/构建复杂度 | 跨平台一致性 | 与本场景匹配度 | 结论 |
|------|-----------|----------------|--------------|----------------|------|
| **bgfx** | ~1–3 MB | 高：需跨平台着色器、bx/bimg 配套、CMake 定制 | 高 | **低**：本场景只是把一张已解码 RGBA 贴图 blit 到屏，无需 3D/多 Pass/统一后端 | **拒绝** |
| **Skia** | 8–30 MB+ | 极高：Chromium 级依赖、巨大、构建漫长 | 高 | 低：完整 2D 图形引擎，杀鸡用牛刀 | **拒绝** |
| **SDL3 + ImGui** | ~1.6 MB(可执行) | 低：成熟 CMake，单窗口+GL 上下文 | 高（同一份代码） | **高**：ImGui 原生支持纹理贴图、列表、滑块、复选框，正好覆盖全部 UI 需求 | **采用（默认）** |
| **原生 API**（D3D11/Metal/GL） | 最小 | **最高**：需 3 套后端代码（Win32+D3D11 / Cocoa+Metal / X11+GL） | 中 | 中：体积极致但开发/维护 3× | 备选（见 §1.4） |

### 1.3 为什么不是 bgfx（重点）

bgfx 的价值在于**跨平台抽象 3D 渲染后端**。本程序需求是：解码 PNG → 得到一张 104×104 的 RGBA 贴图；以**最近邻（NEAREST）**贴到窗口（像素画保持硬边）；在其上叠加 GUI。本质是 2D 贴图 blit + 立即模式 GUI，**不需要任何着色器或后端抽象**。引入 bgfx 会带来跨平台着色器维护、bx/bimg/bgfx 三件套与自定义构建步骤，与 ImGui 集成成熟度亦逊于 SDL/OpenGL 路径——投入产出比为负，故拒绝。

### 1.4 为什么不引入 glad / 不用原生 API

- **glad**：上游仓库是生成器，不含可直接编译的 `src/gl.c`，FetchContent 路径不可靠。本程序仅需约 10 个 GL 1.x/2.x 函数，改为 `src/app/gl_loader.cpp` 用 `SDL_GL_GetProcAddress` 运行时解析（Windows 下 SDL 自动回退到 `opengl32.dll` 取 GL 1.1 符号）。ImGui 的 OpenGL3 后端自带 `imgui_impl_opengl3_loader.h`，与本加载器分属不同翻译单元、符号名不同，互不冲突。
- **原生 API**：可做到零外部运行时、体积极小，但代价是 3 套独立后端。对本项目"轻量 + 跨平台 + 可维护"的优先级，SDL3 单一代码库综合成本更低。保留此路径作为"极致体积"目标的备选。

---

## 2. 模块划分

```
pxb-preview/
├─ src/
│  ├─ main.cpp                # 入口；--register/--unregister headless 模式（UAC 子进程，纯 Win32 + MessageBox）
│  ├─ core/                   # 与平台/图形无关的纯逻辑层（被主程序与三平台缩略图器复用）
│  │  ├─ pxb_format.h         # 格式常量、结构体定义（RgbaImage/SceneInfo/FrameInfo/PxbDocument）
│  │  ├─ gzip.h/.cpp          # gzip 解压：自解析 gzip 头 → miniz 原始 inflate（保持对构建配置不敏感）
│  │  ├─ gzip_partial.h/.cpp  # 增量/部分解压（流式预览用）
│  │  ├─ image.h/.cpp         # PNG→RGBA32（stb_image）
│  │  ├─ json_min.h           # 极简 JSON 解析（仅本格式子集，零第三方依赖）
│  │  ├─ fileutil.h/.cpp      # 跨平台目录列举/路径工具（含 UTF-8 路径辅助）
│  │  ├─ pxb_meta.h/.cpp      # 元数据懒读取（列表/缩略图路径）
│  │  ├─ pxb_cache.h/.cpp     # 解析结果缓存（内容指纹 key、LRU）
│  │  ├─ pxb_reader.h/.cpp    # 总装配：解压→解析头/JSON→按 preview_table 抽帧/缩略图→扫描图层名
│  │  └─ pxb_reg_contract.h   # ★文件关联契约（CLSID/ProgID/shellex 键）唯一权威
│  └─ app/                    # 表现层（依赖 SDL3/ImGui）
│     ├─ renderer.h/.cpp      # SDL3+OpenGL3+ImGui 后端；DPI 感知；纹理上传（缓存+清理）；主循环帧
│     ├─ gl_loader.h/.cpp     # 运行时 GL 函数解析（替代 glad）
│     ├─ app.h/.cpp           # 应用状态机（load_file / refresh_siblings / tick）
│     ├─ ui.h/.cpp            # ImGui 面板（文件列表/图像视图/胶片条/图层/播放条/菜单/About）
│     ├─ regutil.h/.cpp       # .pxb 文件关联：查询/注册/注销/提权（Win32 专用）
│     └─ i18n.h/.cpp          # ★中英双语：Str 枚举 + zh/en 表 + tr()/trw()/set_lang()
├─ thirdparty/                # vendored 源码（构建离线、可复现）
│  ├─ imgui/ (+backends/)     # Dear ImGui（sdl3 + opengl3 后端 + 内置 GL loader）
│  ├─ stb/                    # stb_image.h / stb_image_write.h
│  └─ miniz/                  # miniz（miniz.c/.h + tdef/tinfl 实现 + export 桩）
├─ assets/                    # icon.png / icon.ico / icon.rc（exe 资源：ICON + IDD_ABOUT 原生对话框模板）
├─ integrations/              # 三平台系统集成（见 §4）
├─ scripts/                   # fetch_sdl3.ps1（SDL3 预编译包拉取）、package_windows.ps1（一键打包）
├─ .github/workflows/         # GitHub Actions CI（Windows 完整构建打包；Linux/macOS 尽力而为）
└─ docs/                      # 本文件 + pxb-format-spec.md + BUILD.md
```

### 2.1 分层原则

- **core 层零 UI/图形依赖**：被主程序、Windows 缩略图处理器、macOS QuickLook、Linux 缩略图器**共同复用**（单一事实来源，避免格式解析分叉）。
- **app 层不含渲染细节**：仅持有状态与业务动作。
- **renderer/ui 层仅做呈现**：读取 App 状态、回调 App 方法。

### 2.2 数据流

```
文件(.pxb)
  → gzip_decompress: 解析 gzip 头 → miniz 原始 inflate
  → pxb_reader::read_pxb
       ├─ 头部校验 / 版本（PXB1 / u16 LE 版本 / u64 LE json_length）
       ├─ JSON 元数据（尺寸、调色板标识、preview_table）—— 数值字段用 as_int/as_number 读取
       ├─ 按 preview_table 抽取 PNG → stb_image → RgbaImage（帧序列 + 缩略图，跳过越界/解码失败的 blob）
       └─ 扫描尾部字节 → 图层名列表
  → App.doc（PxbDocument）
  → UI 渲染：图像视图(当前帧纹理) / 帧列表 / 图层复选框 / 播放控制
  → tick(dt)：playing 时按 fps 推进 current_frame
```

---

## 3. 渲染策略（关键设计决定）

**渲染源 = preview_table 内嵌 PNG，而非源像素缓冲。**

依据（见格式规范 §3、§5）：
- 源数据以 `palette_id="pxcolor"` 的**外部命名调色板**索引方式存储；
- 只有内嵌 PNG 携带**最终合成颜色**；
- 任何"从源缓冲重建颜色"的做法都依赖编辑器侧调色板定义，在预览器内不可靠。

故：帧显示、动画、缩略图全部直接使用 PNG 预览块（像素级正确、与编辑器一致）。三平台缩略图实现均复用 `src/core` 同一解析逻辑，保证缩略图与主程序渲染结果一致。

**图层显隐**：图层名列表由源区扫描得到，复选框状态被记录；由于渲染基于合成 PNG，单图层像素隔离属增强项（需在格式规范的源数据区补充外部调色板后才能实现）。该限制已在 UI 中明确提示，不做虚假承诺。

**关键设计决定（与 AGENTS.md 一致）**：
- 渲染走 OpenGL 2.1 + ImGui OpenGL3 后端，纹理用 `GL_NEAREST` 保持像素硬边。
- **DPI 感知渲染**：窗口创建尺寸与全部手写布局常量按显示内容缩放（`SDL_GetDisplayContentScale` → 窗口尺寸与 `ui_scale()`）；ImGui 样式经 `ScaleAllSizes` 缩放，字体按 `base × scale` 栅格化（微软雅黑等系统 CJK 字体），视口用物理像素——高分屏（125%–200%）下文字原生清晰、布局比例与 100% 一致。
- About 窗口为 Win32 原生 `DialogBox`（`IDD_ABOUT` 资源 + `DialogBoxParamW`），非 Win 平台回退到 ImGui 浮窗。**不用** ImGui popup/modal 弹 About（菜单关闭同帧创建 popup 会被 ImGui 吞掉，表现为"点击无反应"）。
- `imgui.ini` 全局禁用（`io.IniFilename = nullptr`）；所有窗口 `NoSavedSettings`。
- 文件关联注册动作由 Windows 缩略图 DLL 的 `DllRegisterServer/DllUnregisterServer` 执行（等同 `regsvr32`），exe 通过 `LoadLibrary` 调用——不在 exe 里复制键名逻辑。CLSID/ProgID/shellex 键只在 `src/core/pxb_reg_contract.h` 一处定义。

---

## 4. 三平台系统集成

| 平台 | 文件关联（双击打开） | 文件管理器内直接预览主图 |
|------|----------------------|----------------|
| Windows | `.pxb` → ProgID `PxbPreview.pxbfile` → `pxb-preview.exe "%1"`（注册动作由 GUI File 菜单触发，**不**分发独立 install/uninstall 脚本；提权路径：GUI → `ShellExecuteW(runas, --register/--unregister)` → main.cpp headless 分支） | COM `IThumbnailProvider`（`PxbThumbnailHandler.dll`，复用 core 解析 + GDI→HBITMAP；`regsvr32` 注册；DLL 内部 `DllRegisterServer` 拥有注册契约） |
| macOS | App Bundle `Info.plist` `CFBundleDocumentTypes` + `UTExportedTypeDeclarations`（声明 `com.pxb.pxb`） | Quick Look 生成器（`PxbQL.qlgenerator`，复用 core 解析 → CGImage） |
| Linux | `.desktop`（`MimeType=application/x-pxb`）+ `application-x-pxb.xml`，`xdg-mime default` | freedesktop 缩略图器（`pxb.thumbnailer` + `pxb-thumbnailer`，输出 PNG） |

要点：
- **三平台缩略图实现均复用 `src/core` 的同一解析逻辑**，保证缩略图与主程序渲染结果一致。
- Windows 处理器用原生 GDI 输出 HBITMAP，无需额外成像库；macOS 用 ImageIO/CGImage；Linux 用 stb_image_write 输出 PNG。
- **主图直览要求**：上述三平台预览均渲染文件内嵌合成 PNG（即主图本身），在文件管理器主界面即可看到图片内容，无需先打开本程序，满足"文件管理器内直接预览主图"的硬性要求。

---

## 5. 构建与体积优化

- **依赖 vendor 化且可静态链接**：imgui / stb / miniz 全部源码内置于 `thirdparty/`，构建无需联网、可复现；SDL3 为唯一外部运行时依赖。
- **C++ 运行时静态链接**：MinGW 下 `-static-libgcc -static-libstdc++`（winpthread 仅静态库缺失时动态）。发布物运行时依赖仅 `SDL3.dll` + `libwinpthread-1.dll`（单 exe + 2 DLL，符合"极少文件分发"）。
- **core 层零额外依赖**：缩略图器与主程序都直接 include `src/core` 源文件（不拉入 SDL3/ImGui），保持单一可执行单元；GUI 仅依赖 SDL3/ImGui 渲染与系统 shell API。
- **GL 零依赖**：`gl_loader.cpp` 运行时解析，不引入 glad。
- **C++17 + 严格分层 + 单一职责**：符合开源优秀架构与代码规范要求。
- **跨平台可移植性**：Windows 下 UTF-8 路径在 libstdc++ "C" locale 下会破坏（`std::filesystem::path::string()` 把 UTF-8 当 ANSI），所以路径辅助函数手动切分 UTF-8，文件读取/存在检查走 Win32 宽字符 API（`CreateFileW`/`GetFileAttributesW`），文件对话框 filter 用 `L'\0'` 显式追加以避开编译期 `\0` 截断。
- **嵌入图标资源**：`assets/icon.rc` 包含应用 ICON 与 `IDD_ABOUT` 原生对话框模板，链接进 `pxb-preview.exe`。

---

## 6. 已验证状态

- Windows 端到端构建通过：`pxb-preview.exe`（约 4 MB）、`PxbThumbnailHandler.dll`（自包含，正确导出 `DllRegisterServer`/`DllGetClassObject`/`DllCanUnloadNow`）。
- C++ 解析核心用真实样本 `未命名.pxb` 验证：`OK v2.2 frames=1 layers=1 scene=104x104 thumb=104x104 title='未命名'`，与 Python 参考解析器一致。
- 72 帧多帧动画播放、胶片条、175% DPI 缩放渲染已实测（Windows）。
- GUI 渲染需桌面会话；无桌面环境（服务会话）下 `SDL_Init(video)` 失败，属环境限制而非代码缺陷。
- Linux / macOS 集成代码齐备（deb / AppImage / 缩略图器 / QuickLook），CI 有构建作业但尚未实机验证。

---

## 7. 交付物清单

| 类别 | 文件 |
|------|------|
| 可运行程序源码 | `src/**`（core + app + gl_loader） |
| vendored 依赖 | `thirdparty/{imgui,stb,miniz}` |
| 构建 | `CMakeLists.txt`（+ `integrations/windows/PxbThumbnailHandler/CMakeLists.txt`） |
| 打包脚本 | `scripts/{fetch_sdl3.ps1, package_windows.ps1}` |
| CI | `.github/workflows/ci.yml` |
| 文档 | `docs/pxb-format-spec.md`、`docs/architecture.md`、`docs/BUILD.md`、`README.md`、`AGENTS.md` |
| Windows 集成 | `integrations/windows/{package.nsi, PxbThumbnailHandler/*}` |
| macOS 集成 | `integrations/macos/{Info.plist, quicklook/*}` |
| Linux 集成 | `integrations/linux/{pxb-preview.desktop, application-x-pxb.xml, pxb.thumbnailer, pxb_thumbnailer.cpp, install.sh, build_appimage.sh, build_deb.sh}` |
