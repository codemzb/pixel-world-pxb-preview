# PXB Preview

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![CI](https://github.com/codemzb/pixel-world-pxb-preview/actions/workflows/ci.yml/badge.svg)](https://github.com/codemzb/pixel-world-pxb-preview/actions/workflows/ci.yml)

轻量级 `pxb` 像素画预览器（C++17，App v0.0.1 / pxb 格式 v2.2）。读取 gzip 压缩的二进制像素画文件（`.pxb`），在 GUI 中预览多帧动画、管理同级文件，并注册 Windows 文件关联与资源管理器缩略图。

技术栈：**SDL3 + Dear ImGui + OpenGL 2.1 + stb_image + miniz**。三方源码 vendor 进 `thirdparty/`，构建完全离线、可复现。

- 文档：[构建指南](docs/BUILD.md) · [技术架构](docs/architecture.md) · [PXB 格式规范](docs/pxb-format-spec.md) · 给 AI 协作者：[AGENTS.md](AGENTS.md)

## 打包

### 1. 主程序

SDL3 是唯一的外部二进制依赖，不进仓库。Windows 先拉取官方预编译包（已装好的 SDL3 也可以，configure 时用 `-DCMAKE_PREFIX_PATH` 指向即可）：

```powershell
powershell -File scripts\fetch_sdl3.ps1   # 下载到 external/SDL3（gitignored）
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PWD/external/SDL3"
cmake --build build
```

产物：`build/pxb-preview.exe`（动态依赖 `SDL3.dll` 与 MinGW 运行时 `libwinpthread-1.dll`；libstdc++/libgcc 静态链接）。

### 2. 缩略图处理器 DLL（Windows，可选）

用于资源管理器内 `.pxb` 缩略图预览：

```bash
cmake -S integrations/windows/PxbThumbnailHandler -B build_thumb -G Ninja
cmake --build build_thumb
```

产物：`build_thumb/PxbThumbnailHandler.dll`。

### 3. 一键打包（推荐）

组装 `dist/`、生成 NSIS 安装包与便携 zip；版本号自动取自 `src/app/i18n.h` 的 `kAppVersion` 并注入安装器元数据：

```powershell
powershell -File scripts\package_windows.ps1
```

产物集中在 `dist/windows/`，文件名带版本与架构：

```
dist/windows/
  pxb-preview-<版本>-win64/                便携目录（exe + dll）
  pxb-preview-<版本>-win64-setup.exe       NSIS 安装包
  pxb-preview-<版本>-win64-portable.zip    便携 zip
```

CI（`.github/workflows/ci.yml`）走同一条路径；推送 `v*` tag 会自动创建 GitHub Release 并附产物与 SHA256SUMS。

> ⚠️ Ninja 时间戳坑：改动源码后偶尔报 "no work to do"（文件系统 mtime 粒度），此时 `touch` 源文件强制重编——**务必确认 exe 时间戳已更新再打包**。

## 项目架构

### 分层

| 层 | 位置 | 职责 |
|---|---|---|
| 纯逻辑层 | `src/core/` | 无 UI/GL，被主程序与缩略图 DLL 复用：gzip 解压、图像解码、pxb 解析、路径/文件工具 |
| 应用层 | `src/app/` | App 状态机（load_file / refresh_siblings / tick）、ImGui UI、GL 渲染、文件关联、i18n |
| 平台集成 | `integrations/` | Windows COM 缩略图 DLL、NSIS 安装包、Linux/macOS 打包脚本（未实机验证） |
| 第三方 | `thirdparty/` | vendored imgui / stb / miniz |

### 目录结构

```
src/
  main.cpp           入口；--register/--unregister headless 模式（UAC 子进程）
  core/
    gzip.cpp         gzip 头自解析 + miniz raw inflate
    image.cpp        stb_image 解码 → RgbaImage
    fileutil.cpp     UTF-8 路径辅助 + list_directory + read_file_bytes
    pxb_reader.cpp   pxb 解析（frame 排序、duration 前向兼容）
    pxb_format.h     RgbaImage/SceneInfo/FrameInfo/PxbDocument
    pxb_reg_contract.h  ★文件关联契约（CLSID/ProgID/shellex 键）唯一权威
  app/
    app.h/cpp        App 状态机：load_file/refresh_siblings/tick
    ui.cpp           render_ui：三栏 + 帧面板 + 状态栏 + 菜单 + About
    renderer.*       GL 纹理（GL_NEAREST）+ ImGui 初始化
    gl_loader.cpp    SDL_GL_GetProcAddress 运行时解析 GL 函数
    regutil.*        .pxb 文件关联：查询/注册/注销/提权（Win32）
    i18n.h/cpp       中英双语：Str 枚举 + zh/en 表 + tr()/trw()/set_lang()
assets/              应用图标（源图 / ico / exe 资源脚本）
integrations/
  windows/PxbThumbnailHandler/  COM 缩略图 DLL（DllRegisterServer 拥有注册契约）
  windows/package.nsi           NSIS 安装包脚本
  linux/  macos/                其他平台集成
scripts/             fetch_sdl3.ps1（拉取 SDL3 预编译包）、package_windows.ps1（一键打包）
.github/workflows/   GitHub Actions CI
thirdparty/          vendored imgui/stb/miniz
CMakeLists.txt       构建配置
```

### 关键设计

- **pxb 文件格式**：gzip 解压后 = `PXB1` 魔数 + 版本号 + JSON 元数据 + 数据区。pxb 只内嵌"合成后的整帧 PNG"，无逐层像素——图层显隐仅记录状态；帧无显式时长，播放回退全局 fps。
- **单一事实源**：文件关联的 CLSID/ProgID/shellex 键名只在 `src/core/pxb_reg_contract.h` 定义，主程序（regutil）与缩略图 DLL 共同 include；注册动作由 DLL 的 `DllRegisterServer` 执行，exe 通过 `LoadLibrary` 调用。
- **i18n**：所有 UI 文案走 `Str` 枚举 + zh/en 双表（`static_assert` 校验顺序一致），禁止硬编码文案。
- **版本号**：App 版本在 `src/app/i18n.h` 的 `kAppVersion`，与 NSIS 脚本的 `APP_VERSION` 同步。


## 许可证

本项目以 [MIT](LICENSE) 许可证开源。
