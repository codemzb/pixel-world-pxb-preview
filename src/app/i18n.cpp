// i18n.cpp — zh/en string tables for the UI. Keep Str ordering in sync with
// i18n.h; the static_assert below enforces table/array length agreement.
//
#include "i18n.h"

#include <cstddef>
#include <string>
#include <vector>

namespace pxb {

namespace {
Lang g_lang = Lang::zh;   // default: Simplified Chinese
}

Lang current_lang() { return g_lang; }
void set_lang(Lang l) { g_lang = l; }

// ---- tables (order MUST match enum Str) -------------------------------------
static const char* kZh[] = {
    // menus
    "文件", "打开…", "退出", "帮助", "关于", "语言",
    "简体中文", "English",
    // association
    "已注册：双击 .pxb 打开", "取消注册…", "未注册 .pxb 文件关联",
    "注册 .pxb 文件关联…",
    "检测到旧版关联缓存（UserChoice），执行注册/取消时会一并清除",
    "文件关联仅 Windows 支持",
    "系统级关联仍残留（需管理员取消）",
    "HKLM 中仍有指向本程序的 .pxb 关联。当前用户已干净，但双击 .pxb 仍可能用本程序打开。"
    "请以管理员身份运行后再次取消注册，或手动删除 HKLM\\Software\\Classes\\.pxb 与 PxbPreview.pxbfile。",
    "注册未生效（HKCU 写入失败？请确认未被组策略限制）",
    "注意：资源管理器缩略图需要管理员权限，本次未注册（双击打开已生效）。"
    "如需缩略图，请以管理员身份运行后再注册一次。",
    "取消注册未生效（仍检测到当前用户的 .pxb 关联）",
    "注意：系统级关联已尝试移除，但仍残留。可手动删除 HKLM\\Software\\Classes\\.pxb 与 PxbPreview.pxbfile。",
    "当前用户的关联已取消。但检测到系统级（管理员安装）的 .pxb 关联仍存在，双击 .pxb 仍可能用本程序打开。"
    "如需彻底移除，请以管理员身份运行本程序，再次「取消 .pxb 文件关联」。",
    "注册 .pxb 文件关联", "取消 .pxb 文件关联",
    "注册成功。\n\n双击 .pxb 将用 PXB Preview 打开（仅当前用户）。\n资源管理器缩略图需管理员权限：如未显示，请以管理员身份运行后重新注册一次。",
    "已取消当前用户的注册（双击 .pxb 不再用本程序打开）。\n系统级缩略图若此前由管理员安装，需以管理员身份运行后再取消注册一次。",
    "操作失败：\n",
    "未提升权限，已取消操作。",
    // panels
    "信息内容", "帧", "图层",
    // preview empty states
    "未加载文件。", "双击 .pxb、或将文件拖入窗口，", "或使用 文件 > 打开。滚轮缩放。",
    "此文件无预览图像。",
    "适应",
    // playback
    "播放", "暂停", "停止", "帧 %d",
    "帧 %d\n时长 %dms%s", "（显式）", "（默认）", "无帧。",
    // metadata labels
    "标题", "生成器", "版本", "内容类型", "创建时间", "更新时间",
    "调色板", "调色板版本", "维度", "画布尺寸: %dx%d", "色彩深度", "坐标系",
    "容器格式: v%d.%d", "缩略图索引: %d", "帧数: %d | 图层数: %d",
    // layers
    "（无图层信息）", "（已隐藏）",
    "提示：pxb 只存合成帧，无逐层像素，隐藏/显示仅记录状态。",
    "提示：本文件带逐层预览，取消勾选的图层会从画面中剔除。",
    "该图层无独立预览数据，勾选仅记录状态",
    // file list
    "（当前目录无 .pxb）",
    "搜索 .pxb 文件…",
    // status bar
    "就绪", "不是 .pxb 文件：", "打开失败：",
    "已加载 %s | %d 帧, %d 图层", "未知错误", "缓存 %zu/%zu ~%zuMB",
    // about
    "关于", "PXB Preview", "版本 %s", "官网", "打开官网",
    "版权", "作者", "许可证", "MIT",
    "SDL3 + Dear ImGui + OpenGL 2.1",
    "构建于 %s", "关闭",
    // dialog
    "PXB 像素文件 (*.pxb)", "所有文件 (*.*)",
    // view
    "视图", "文件列表", "信息与图层", "鸟瞰地图", "鸟瞰",
    // theme
    "主题", "跟随系统", "深色", "亮色",
};

// Every Simplified-Chinese UI string. The font loader merges these into its
// glyph set so every string the UI can produce has atlas glyphs — new strings
// added to kZh are picked up automatically (i18n is the single source of UI
// text, see AGENTS §8).
std::vector<std::string> zh_strings() {
    std::vector<std::string> out;
    out.reserve(std::size(kZh));
    for (const char* s : kZh) out.emplace_back(s);
    return out;
}

static const char* kEn[] = {
    // menus
    "File", "Open...", "Quit", "Help", "About", "Language",
    "简体中文", "English",
    // association
    "Registered: double-click opens .pxb", "Unregister...",
    ".pxb file association not registered", "Register .pxb association...",
    "Stale UserChoice override detected; register/unregister will clear it",
    "File association is Windows-only",
    "System-wide association still present (needs admin to remove)",
    "An .pxb association pointing at this app still exists under HKLM. "
    "The current user is clean, but double-clicking .pxb may still open this app. "
    "Run as administrator and unregister again, or manually delete "
    "HKLM\\Software\\Classes\\.pxb and PxbPreview.pxbfile.",
    "Registration did not take effect (HKCU write failed? Check for group policy restrictions).",
    "Note: Explorer thumbnails require administrator rights and were not registered "
    "this time (double-click open is active). To get thumbnails, run as administrator "
    "and register again.",
    "Unregister did not take effect (the current user's .pxb association is still present).",
    "Note: the system-wide association was removed, but remnants remain. "
    "You can manually delete HKLM\\Software\\Classes\\.pxb and PxbPreview.pxbfile.",
    "The current user's association was removed. However, a system-wide "
    "(admin-installed) .pxb association still exists; double-clicking .pxb may still "
    "open this app. To remove it completely, run this app as administrator and "
    "unregister again.",
    "Register .pxb Association", "Unregister .pxb Association",
    "Registered.\n\nDouble-clicking .pxb opens PXB Preview (current user only).\nExplorer thumbnails need admin: if missing, run as administrator and re-register.",
    "Removed the current-user association (double-click no longer opens this app).\nIf a machine-wide thumbnail was installed by an admin, run as administrator and unregister again.",
    "Operation failed:\n",
    "Elevation was declined; cancelled.",
    // panels
    "Info", "Frames", "Layers",
    // preview empty states
    "No file loaded.", "Double-click a .pxb, or drag & drop one here,",
    "or use File > Open. Wheel = zoom.", "This file has no preview image.",
    "Fit",
    // playback
    "Play", "Pause", "Stop", "Frame %d",
    "Frame %d\nDuration %dms%s", " (explicit)", " (default)", "No frames.",
    // metadata labels
    "Title", "Generator", "Version", "Content Type", "Created", "Updated",
    "Palette", "Palette Version", "Dimension", "Canvas: %dx%d", "Color Depth",
    "Coordinates", "Container: v%d.%d", "Thumbnail #%d", "Frames: %d | Layers: %d",
    // layers
    "(no layer info)", "(hidden)",
    "Note: pxb embeds only composited frames (no per-layer pixels); show/hide records state only.",
    "Note: this file carries per-layer previews — unchecked layers are excluded from the picture.",
    "No isolated preview for this layer; the checkbox records state only",
    // file list
    "(no .pxb in this folder)",
    "Search .pxb files…",
    // status bar
    "Ready", "Not a .pxb file: ", "Failed to open: ",
    "Loaded %s | %d frame(s), %d layer(s)", "unknown error",
    "Cache %zu/%zu ~%zuMB",
    // about
    "About", "PXB Preview", "Version %s", "Homepage", "Open website",
    "Copyright", "Author", "License", "MIT",
    "SDL3 + Dear ImGui + OpenGL 2.1",
    "Built %s", "Close",
    // dialog
    "PXB Pixel Files (*.pxb)", "All Files (*.*)",
    // view
    "View", "File List", "Info & Layers", "Minimap", "Overview",
    // theme
    "Theme", "Follow system", "Dark", "Light",
};

static_assert(sizeof(kZh) / sizeof(kZh[0]) == (size_t)Str::Count_,
              "kZh table length != Str::Count_");
static_assert(sizeof(kEn) / sizeof(kEn[0]) == (size_t)Str::Count_,
              "kEn table length != Str::Count_");

const char* tr(Str s) {
    const auto i = (size_t)s;
    if (i >= (size_t)Str::Count_) return "";
    return g_lang == Lang::en ? kEn[i] : kZh[i];
}

// Wide table: only the two dialog filter strings are needed in wide form.
static const wchar_t* kZhW[] = { L"PXB 像素文件 (*.pxb)", L"所有文件 (*.*)" };
static const wchar_t* kEnW[] = { L"PXB Pixel Files (*.pxb)", L"All Files (*.*)" };

const wchar_t* trw(Str s) {
    if (s == Str::DlgPxbFiles)  return g_lang == Lang::en ? kEnW[0] : kZhW[0];
    if (s == Str::DlgAllFiles)  return g_lang == Lang::en ? kEnW[1] : kZhW[1];
    return L"";
}

} // namespace pxb
