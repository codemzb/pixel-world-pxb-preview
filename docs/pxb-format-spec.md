# PXB 文件结构规范（pxb-format-spec）

> 状态：基于样本文件 `未命名.pxb`（MZB.ONE 导出）逆向确认。
> 置信度分级：**已确认**（经 Python 参考解析器端到端验证）、**推测**（单一样本、未交叉验证）。

---

## 1. 容器概述

| 项目 | 值 |
|------|----|
| 传输/容器压缩 | **gzip**（RFC 1952，deflate，FNAME 标志位为 0，OS 字节 = 0x03 Unix） |
| 解压后净载荷 | 结构化二进制 + 内嵌 JSON + 内嵌 PNG |
| 字节序 | **小端（Little-Endian）** 贯穿所有多字节整型 |
| 魔术字 | ASCII `PXB1` |

样本：原始文件 11 702 字节，gzip 解压后为 30 821 字节。

---

## 2. 净载荷头部（Header）

从解压后偏移 0 开始：

| 偏移 | 长度 | 字段 | 类型 | 字节序 | 样本值 | 说明 |
|------|------|------|------|--------|--------|------|
| 0    | 4    | `magic` | char[4] | ASCII | `PXB1` | 固定魔术字 |
| 4    | 2    | `version_major` | uint16 | LE | `0x0002` = 2 | 主版本 |
| 6    | 2    | `version_minor` | uint16 | LE | `0x0002` = 2 | 次版本 |
| 8    | 8    | `json_length` | uint64 | LE | `0x0000021A` = 538 | 紧随其后的 JSON 块字节数 |
| 16   | `json_length` | `metadata` | UTF-8 JSON | — | 见 §3 | 文档元数据 |

头部固定 16 字节；数据区起点 = `16 + json_length`。

> **已确认**：上述字段在样本中被完整解析，JSON 长度字段（538）与实际 JSON 字节数完全一致，且 PNG 预览块偏移全部落在数据区内，自洽。

---

## 3. JSON 元数据（Schema）

`metadata` 为 UTF-8 JSON 对象，键名与类型如下（样本实测）：

```json
{
  "version": "1.0.0",
  "content_type": "application/vnd.mzb.px+binary",
  "metadata": {
    "title": "未命名",
    "generator": "MZB.ONE",
    "created_at": "2026-08-03T11:30:19.170Z",
    "updated_at": "2026-08-03T11:31:43.378Z",
    "palette_id": "pxcolor",
    "palette_version": "0.0.1"
  },
  "scene": {
    "dimension": "2",
    "size": { "width": 104, "height": 104 },
    "color_depth": "8-bit",
    "coordinate": "cartesian_top_left"
  },
  "texture_table": [],
  "preview_table": [
    { "type": "thumbnail", "offset": 0,    "length": 2999 },
    { "type": "frame", "frame_index": 0, "offset": 2999, "length": 4193 }
  ],
  "thumbnail_preview_index": 0
}
```

| 键 | 类型 | 说明 |
|----|------|------|
| `version` | string | 工具/格式版本 |
| `content_type` | string | MIME 风格标识 |
| `metadata.title` | string | 文档标题 |
| `metadata.generator` | string | 生成器（样本为 `MZB.ONE`） |
| `metadata.palette_id` | string | **调色板标识**（`pxcolor` 为外部命名调色板） |
| `scene.size` | {w,h} | 画布尺寸（像素） |
| `scene.color_depth` | string | 如 `8-bit` |
| `scene.coordinate` | string | 坐标系（左上角笛卡尔） |
| `preview_table` | array | 预览块目录（见 §4） |
| `thumbnail_preview_index` | int | 缩略图在 preview_table 中的索引 |

> **判断**：`palette_id = "pxcolor"` 表明源像素按**外部命名调色板**索引存储。这正是预览程序必须以内嵌 PNG 预览块为渲染源的根本原因——只有 PNG 携带最终合成颜色，源数据中的索引需依赖编辑器内置调色板才能还原真实色值。

---

## 4. 预览块（preview_table → 内嵌 PNG）

`preview_table` 中的每一项给出数据区内的**字节偏移**与**长度**：

```
blob = payload[ 16 + json_length + entry.offset  ..  + entry.length ]
```

每个 blob 均为 **PNG**（`89 50 4E 47 0D 0A 1A 0A` 起头）。样本实测：

| type | frame_index | offset(区内) | length | 实际 PNG 尺寸 |
|------|-------------|--------------|--------|---------------|
| `thumbnail` | — | 0 | 2999 | 104×104 RGBA |
| `frame` | 0 | 2999 | 4193 | 104×104 RGBA |

PNG IHDR：宽/高 = `scene.size`，色类型 6（RGBA），位深 8。这些 PNG 是**已合成的成品预览**，可直接解码显示。

多帧文件：每个帧对应一个 `type:"frame"` 条目（含 `frame_index`），按 `frame_index` 排序即得播放序列。

> **帧时长（可选 / 前向兼容，推测）**：每个 `frame` 条目**可**携带一个显示时长字段，
> 建议键名 `duration`（别名 `time`），值为**毫秒**（`int`）。当前 MZB.ONE 导出的样本**不含**此字段；
> 读取器会**机会式地**解析它——存在则用于逐帧播放计时，缺失时回退到全局 `fps`
> （`帧时长 = 1000 / fps`）。预览器在帧条每帧下方显示其实际时长（ms），并汇总"总时长"。
> 该字段属增强项，不写入则不影响既有解析与播放。

> **已确认**：两个 blob 均被独立解码为 104×104 RGBA 图像，与 `scene.size` 一致，且保存后可正常显示。

---

## 5. 数据区尾部（源数据 / Source Region）

位于所有 preview_table blob 之后（样本绝对偏移 7746 起，共 23 075 字节）。该区承载可编辑源数据：**调色板索引像素缓冲、帧/图层结构、命名调色板**。

观测到的事实（**推测**为主）：

- 含以**长度前缀 UTF-8 字符串**标注的 `Frame 1`、`Layer 1` 等名称（`07 "Frame 1" 00`：长度字节 + 名称 + 终止符）。
- 含**命名调色板**色号字符串，形如 `G6`、`H1`、`C18`、`P1` 等（`pxcolor` 的语义色名），而非裸 RGBA，进一步印证源数据依赖外部调色板。
- 存在类 `[u8 计数][RGBA]` 或调色板索引字节流（值范围 0–144），但**未能在缺乏编辑器规范下可靠还原为与 PNG 真值一致的逐层像素**（Python 参考解析多组假设重建匹配率均为 0%）。

结论：源数据区对“预览/缩略图”用途**非必需**；逐层像素隔离需编辑器侧 `pxcolor` 调色板定义，属后续增强项。

---

## 6. 解析流程（供实现参考）

```
file_bytes
  └─ gzip_decompress  ─►  payload[]
        ├─ 校验 magic == "PXB1"
        ├─ 读 version_major/minor(u16 LE)、json_length(u64 LE)
        ├─ JSON = payload[16 : 16+json_length]  → 解析 schema
        ├─ data_start = 16 + json_length
        └─ 对每个 preview_table 条目：
              blob = payload[data_start+offset : +length]
              └─ PNG 解码(stb_image) → RGBA 帧/缩略图
        └─ 扫描尾部字节，抽取 "Frame N"/"Layer N" 名称 → 图层列表
```

---

## 7. 示例：十六进制走查（样本头部）

```
偏移   十六进制                                          可读
0x00   50 58 42 31  02 00 02 00  1A 02 00 00 00 00 00 00   PXB1  v2.2  json_len=538
0x10  7B 22 76 65 72 73 69 6F 6E 22 3A 22 31 ...          { "version":"1" ...
```

- `50 58 42 31` → `PXB1`
- `02 00` → version_major = 2（LE）
- `02 00` → version_minor = 2
- `1A 02 00 00 00 00 00 00` → json_length = 0x21A = 538（LE）
- `7B 22 ...` → JSON 起始 `{ "`

---

## 8. 兼容性建议

- 读取器应**前向兼容**：`version_minor` 忽略，仅校验 `version_major == 2`；`json_length` 越界需保护。
- `preview_table` 缺失时不应崩溃（回退到 thumbnail 或首帧）。
- 解析失败须给出明确错误信息，不应静默返回空图。
