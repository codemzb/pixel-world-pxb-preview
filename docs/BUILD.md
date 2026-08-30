# PXB Preview 构建指南

## 环境要求

| 依赖 | 说明 |
|------|------|
| **CMake** 3.16+ | 任意后端（CI 与本文档均以 Ninja 为例） |
| **C++17 编译器** | Windows 推荐 MinGW-w64（GCC 13+，CI 用 MSYS2 mingw64）；Linux/macOS 用系统 GCC/Clang（未实机验证） |
| **SDL3** | 唯一外部二进制依赖，**不入仓库**，见下节 |
| **NSIS 3.0+** | 仅生成 Windows 安装包时需要（CI 由 choco 安装） |

所有其余第三方库（Dear ImGui、stb、miniz）已 vendor 进 `thirdparty/`，构建无需联网、可复现。

---

## 获取 SDL3

**Windows（推荐脚本）：**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch_sdl3.ps1
# 下载官方预编译包 → external/SDL3/（gitignored）
```

**已有本地 SDL3 安装**（如官方 `-mingw` devel 包解压出的 `x86_64-w64-mingw32/`）：无需脚本，直接用 `-DCMAKE_PREFIX_PATH` 指向它。

**Linux / macOS：** 包管理器或源码安装，`find_package(SDL3)` 自动定位：

```bash
# macOS
brew install sdl3
# Linux（Ubuntu 24.04 无 apt 包，源码安装）：
curl -L -o sdl3.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-3.4.14.tar.gz
tar xf sdl3.tar.gz && cmake -S SDL3-3.4.14 -B sdl3-build && sudo cmake --install sdl3-build
```

---

## 编译

### 主程序

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="<SDL3 前缀目录>"
cmake --build build
```

产物：`build/pxb-preview.exe`（动态依赖 `SDL3.dll` 与 MinGW 运行时 `libwinpthread-1.dll`；libstdc++/libgcc 静态链接）。

### 缩略图处理器（Windows，可选）

```bash
cmake -S integrations/windows/PxbThumbnailHandler -B build_thumb -G Ninja
cmake --build build_thumb
```

产物：`build_thumb/PxbThumbnailHandler.dll`。不依赖 SDL3/ImGui，只复用 `src/core` + GDI。

---

## 打包（Windows）

一条命令完成全部打包（本地与 CI 同一路径）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1
```

产物集中在 `dist/windows/`，文件名带版本与架构（版本号自动取自 `src/app/i18n.h` 的 `kAppVersion`）：

```
dist/windows/
  pxb-preview-<版本>-win64/                便携目录（exe + DLL + icon + README）
  pxb-preview-<版本>-win64-setup.exe       NSIS 安装包
  pxb-preview-<版本>-win64-portable.zip    便携 zip（内含同名目录）
```

注意：

- exe 本体名保持 `pxb-preview.exe`——文件关联、DefaultIcon、快捷方式都引用它，不要改名。
- `libwinpthread-1.dll` 是 exe 与缩略图 DLL 的**动态导入**（`objdump -p` 可证），打包脚本会从 MinGW 工具链自动获取；本地手动打包时若探测失败，用 `-MingwBin <工具链 bin 目录>` 显式指定。

Linux / macOS 的打包脚本（deb / AppImage / dmg）同样从 `kAppVersion` 取版本号——POSIX 侧统一走 `scripts/app_version.sh`，macOS .app 的 Info.plist 由 CMake 注入——**任何脚本里都不存在写死的版本号**。

---

## CI 与发版

`.github/workflows/ci.yml`：

- **Windows**：MSYS2 MinGW 构建 + 官方 SDL3 预编译包 + choco NSIS 打包，上传 artifact。
- **Linux / macOS**：尽力而为的构建作业（`continue-on-error`），失败不影响 Windows 产物与发版。
- **发版**：推送 `v*` tag 自动创建 GitHub Release 并附安装包、便携 zip 与 SHA256SUMS。

发版步骤（版本号唯一权威是 `src/app/i18n.h` 的 `kAppVersion`，tag 必须与之一致）：

```bash
# 1. 修改 kAppVersion 后提交
git commit -m "chore: bump version to 0.0.1"
# 2. 打 tag 并推送
git tag v0.0.1 && git push origin main v0.0.1
```

---

## 故障排除

| 问题 | 解决 |
|------|------|
| CMake 找不到 SDL3 | `-DCMAKE_PREFIX_PATH` 指向 SDL3 安装前缀（含 `lib/cmake/SDL3` 的目录）；Windows 可先跑 `scripts\fetch_sdl3.ps1` |
| Ninja 报 "no work to do" 但行为像旧代码 | 文件系统 mtime 粒度问题：`touch` 改动的源文件强制重编；**打包前务必确认 exe 时间戳已更新** |
| 找不到 makensis | 安装 [NSIS](https://nsis.sourceforge.io/)，或让打包脚本自动探测（`C:\Program Files (x86)\NSIS\makensis.exe`） |
| 启动报缺 `SDL3.dll` / `libwinpthread-1.dll` | 产物旁需这两个 DLL；用 `scripts\package_windows.ps1` 组装的 dist 目录即自带 |
| 手动 makensis 报找不到输入文件 | 不要手动调用；脚本以绝对路径 `/D` 注入全部输入。手动兜底时必须显式传 `/DSOURCE_DIR /DASSETS_DIR /DPROJECT_ROOT /DOUT_FILE /DAPP_VERSION` |

---

## 清理

```bash
rm -rf build build_thumb          # 构建目录
rm -f imgui.ini *.log             # 状态/日志（应用已禁用 imgui.ini，正常不会产生）
```
