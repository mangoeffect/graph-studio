# 节点参数"使用说明"超链接 + 官网博文承载方案（调研与设计）

> 2026-09-20。需求：GraphStudio 中每个 node 的参数设置处加一个"使用说明"超链接；使用说明以博文形式实现在官网，博文用单独的标签（taxonomy）分类。

## 1. 现状调研结论

### 1.1 App 侧：参数面板与 task type 数据流

- 属性面板没有独立类，直接内嵌在 `MainWindow`：`CreatePropertyPanel()`（`app/graph_studio/src/view/MainWindow.cpp:640`）构建 "Node Properties" → **"Selected Node"**（静态四行 QFormLayout：id/type/x/y）→ **"Parameters"**（`paramsGroup_` + `paramsLayout_`，动态填充）。
- 参数控件由 `RebuildParamWidgets(nodeId)`（`MainWindow.cpp:1053`）按 `vm_.paramSpecs(data.type)` 驱动重建：int→QSpinBox（可联动滑杆）、float→QDoubleSpinBox、string→QLineEdit（widget_hint=file 加浏览按钮）、bool→QCheckBox、enum→QComboBox。**无参数任务时表单为空**（无占位提示）。
- task type 字符串链路：选中 NodeItem → `vm_.selectNode` → `UpdatePropertyPanel`（`MainWindow.cpp:1014`）→ `NodeData data = vm_.nodeData(nodeId)`，**`data.type` 即 task type**（来源 `dag.task_type(id)`，创建节点时由插件注册名写入）。
- `ParamSpec.description` 字段（`include/task_graph/data_types.hpp:322`，注释"可选，UI tooltip"）已被 `GraphViewModel::paramSpecToVariant`（`GraphViewModel.cpp:38`）铺进 QVariantMap，**但 UI 完全没有消费**——目前零 tooltip。
- **打开外链的仓库惯例**：AboutDialog（`AboutDialog.cpp:65-71`）用 `QLabel + setTextFormat(RichText) + setOpenExternalLinks(true)`；`docs/research/about-dialog-plan.md` 已实测记录 **WASM 上点击外链新开标签页、行为正确，无平台分支**。全 app 无 `QDesktopServices::openUrl`、无 EM_ASM window.open。
- 参数面板无 wasm 平台差异（唯一分支在文件浏览回调 `OnBrowseFile`）。
- UI 测试断言面板控件的现成模式：`findChild<T*>()`（`app/graph_studio/tests/test_gui.cpp:903-919` 的 AboutDialog 用例）；参数面板目前零测试覆盖。

### 1.2 task type 体系

- task type 注册表是 `PluginRegistry`（`available_tasks()` 运行时枚举），约定 `const char* const kXxxType` 常量 + `TG_PLUGIN_AUTOREG`。type 字符串同时是图 JSON 的持久化标识，**天然稳定、改名即 breaking change**——这使它可以直接充当 URL slug。
- app 侧 `GraphViewModel::availableTaskTypes()`（`GraphViewModel.cpp:617`）枚举全部类型；`classifyTask`（`GraphViewModel.cpp:631`）只是硬编码启发式分组，**没有 task 级 description/文档元数据挂点**。
- 当前全量 task type **78 个**（subnode.json 声明 + 核心库 `gpu_compute`）：opencv 45（image_io 2 / filtering 17 / geometry 5 / color 4 / grading 6 / enhance 8 / segmentation 5 / video_io 2）、gpu 17、render 19（含 render_pipeline 编排型 + 每效果一个 type）、blend 1、face_detect 1、matting 1。

### 1.3 网站侧（docs/，Hugo + PaperMod，双语）

- 已有 **blog section**：`content/{zh,en}/blog/<slug>/index.md` page bundle，各 4 篇；`mainSections = ["blog"]`。
- taxonomies 显式配置 `tag/category`（`hugo.toml:79-81`）；无 permalinks 配置，blog URL 即 `/blog/<slug>/`，term 聚合页 `/tags/<term>/` 由主题 `taxonomy.html` + `list.html`（term 回退）开箱渲染（term 页含 RSS 按钮）。
- baseURL `https://studio.mangoeffect.net/`；zh 在根、en 在 `/en/`；**同路径的 zh/en 文件自动配对为 translation pair**（hreflang 由主题输出）。
- 语言自动分流已存在：`docs/layouts/_partials/extend_head.html` 的 auto-detect 会把"落在 zh 页面 + 非 zh 浏览器 + 无显式偏好"的访客 `location.replace` 到对应 en 页面（含爬虫豁免）。**App 只需链接 zh 根路径，英文用户会被网站自动引到 /en/ 版**。
- 菜单只有 header（zh/en 成对、identifier 对齐，现有 weight：download 10 / web 15 / changelog 20 / blog 30 / github 40）。
- 404 页已定制（i18n + 返回按钮）——缺文档时的落点体验可接受。

## 2. 方案总览：约定式 URL（slug = task type）

**App 不维护映射表、不改核心库**。链接 URL 按约定构造：

```
https://studio.mangoeffect.net/blog/<task_type>/
例如 https://studio.mangoeffect.net/blog/blend/
```

网站侧每篇使用说明是 blog section 的一篇博文，**文件目录名 = task type**，打专属标签 `node-guide` 聚合分类。

选择约定式而非其它方案的理由：

| 方案 | 结论 |
|---|---|
| **A. 约定式 URL（本方案）** | app 改动集中一个文件；新增任务零登记自动获得链接；task type 本就是持久化稳定标识 |
| B. ParamSpec/INode 加 `doc_url` | 侵入核心库 + 5 个私有子模块仓库全要改；78 个任务逐个填 URL，收益与 A 相同（A 的 URL 本可由 type 推导） |
| C. app 侧映射表 | 两处维护，新增任务忘加映射即断链，比 A 纯劣 |

A 的代价（文档缺失时 404、type 改名需同步）由 §5 一致性守护兜住。

## 3. App 侧设计

### 3.1 UI 位置与实现

在 **"Selected Node" 表单尾部加固定一行 "Docs"**（不放进动态重建的 `paramsGroup_`）：

- 该行与 task type 相关而非参数相关，放静态表单可让 `RebuildParamWidgets` 零改动（不动 `paramWidgets_` 缓存与 `paramSpecsCache_` 语义，也不影响 wasm E2E 对参数面板的既有约定）。
- 无参数节点同样可见（`paramsGroup_` 为空时用户仍能找到文档）。

```cpp
// CreatePropertyPanel()：Selected Node form 追加一行
docsLinkLabel_ = new QLabel();                     // 成员 QLabel* docsLinkLabel_
docsLinkLabel_->setTextFormat(Qt::RichText);
docsLinkLabel_->setOpenExternalLinks(true);        // 桌面调系统浏览器；wasm 新开标签（AboutDialog 已验证）
nodeForm->addRow("Docs:", docsLinkLabel_);

// UpdatePropertyPanel()（MainWindow.cpp:1028 附近）：
docsLinkLabel_->setText(QString(
    "<a href=\"https://studio.mangoeffect.net/blog/%1/\">使用说明 ↗</a>")
    .arg(data.type));

// ClearPropertyPanel()：显示 "-"（或 hide()）。
```

要点：

- **链接文本固定、href 随选中节点切换**；`setOpenExternalLinks` 是仓库唯一先例路径，wasm 无需 `#ifdef`。
- 官网域名已在 `AboutDialog.cpp:69` 硬编码一次，本改动提取共享常量（如 `constexpr char kWebsiteBase[]`，放 app 公共头）避免两处散落。
- URL 构造不对 task type 做存在性探测（无网络请求、无离线清单）——缺失文档落到网站定制 404 页，靠 §5 的构建期校验把缺失压到零。
- i18n：链接指向 zh 根路径；英文浏览器被网站 auto-detect 自动跳 `/en/blog/<type>/`（translation pair 同路径是前提，§4.1 已保证）。App 自身无需感知语言。

### 3.2 测试

- 桌面 Track 1（`test_gui.cpp`）：仿 `testAboutDialogBuildInfo` 的 `findChild<QLabel*>` 模式——选中节点后断言 `docsLinkLabel_` 的 text 含 `href=".../blog/<type>/"`；取消选中后为 "-"。
- WASM E2E（可选）：约定 URL 是静态可推导的，不必扩 `__gsTest` 桥；如要在 files 场景断言，`paramSpecs` 探针旁加一个只读 `docUrl(type)` 即可（低优先级）。

## 4. 网站侧设计

### 4.1 内容结构

```
docs/content/zh/blog/<task_type>/index.md     # 78 篇（渐进补齐，见 §6）
docs/content/en/blog/<task_type>/index.md     # 与 zh 同路径 → 自动 translation pair → hreflang + auto-detect 跳转生效
```

front matter 模板（沿用现有博客惯例 title/date/tags/summary/showToc）：

```yaml
---
title: "blend — 图层混合"
date: 2026-09-20T10:00:00+08:00
tags: ["node-guide"]          # 专属标签：聚合页 /tags/node-guide/ 的分类依据
categories: ["节点手册"]
summary: "blend 节点的端口、27 种混合模式参数与示例图"
showToc: true
# 可选（若不想 78 篇刷屏首页/归档文章流，需在 pin 的 PaperMod master 上验证支持）：
# hiddenFromHomePage: true
---
```

正文建议结构（每篇统一）：**概述 → 端口表（in/out）→ 参数表（name/type/默认值/说明，与 `param_specs()` 逐项对齐）→ 示例 graph JSON（可直接拖进 GraphStudio 运行）→ 注意事项**。示例图可复用 `tests/graphs/*.json` 素材。

### 4.2 标签与导航

- 专属标签用英文 slug `node-guide`（zh/en 同名）：term 页 URL 干净（`/tags/node-guide/`、`/en/tags/node-guide/`）、避免中文 URL 编码、两语言聚合行为一致。zh 文章里显示的标签词仍可按需在内容中说明。
- term 聚合页由主题现成模板渲染（含 RSS），零模板开发。
- header 菜单加"节点手册"入口（zh/en 成对、identifier 相同、weight 35，介于 blog 30 与 github 40 之间），url `/tags/node-guide/`。
- 博客 section 页 `/blog/` 与节点文档可交叉引用（`relref`），如各模块综述文章（"OpenCV 滤波 17 节点总览"）链接到具体节点篇。

### 4.3 SEO

现有基线全部免费继承：canonical / og / sitemap / lastmod(enableGitInfo)。新增 156 页需注意每篇写独立 `description`/`summary`（PaperMod og:description 回退链吃它）；zh 文章 `hasCJKLanguage` 已开，词数统计正确。

## 5. 一致性守护（防断链）

**`scripts/check_node_docs.py`**（新脚本，可挂 `website.yml` 或本地跑）：

1. 枚举预期 task type：读 `subnode.json` 各 `tasks` 数组 + 手动补充核心库 `gpu_compute`（静态来源；运行时枚举需桌面构建，网站 CI 不具备）。
2. 对比 `docs/content/{zh,en}/blog/` 下实际存在的目录名。
3. 输出三栏报告：双方都有（zh/en 是否配对）/ app 有而文档缺 / 文档有而 app 无（孤儿文章）；`--strict` 时缺失即退出码 1。

配套约定：

- **task type 改名**（breaking change）必须同步迁移文章目录——写进 AGENTS.md 惯例节。
- 文档标"对应最新发布版本"；本地旧版 app 的参数集与线上文档可能有小出入，属可接受语义（type 稳定、参数默认增量演化）。

## 6. 分期落地

| 阶段 | 内容 |
|---|---|
| **P1（MVP，一次可交付）** | App 链接行（§3）；网站 `node-guide` 标签 + 菜单项；先写 6–10 篇高频节点文档（`opencv_image_read`、`blend`、`face_detect`、`matting`、`render_pipeline`、`gpu_compute`、`color_grade_*` 挑代表）；`check_node_docs.py` 以非 strict 模式进 CI（只报告） |
| **P2** | 按模块批量补齐 78×2 篇（opencv filtering/geometry 先行——参数语义最需要文档）；校验切 `--strict`；可选：参数表骨架生成器（桌面 probe 二进制导出 `param_specs()` → 生成 markdown 表格初稿，人工补语义说明） |
| **P3（可选增强）** | ① 参数行 tooltip：`RebuildParamWidgets` 读 `s.value("description")` 对控件 `setToolTip`——数据已铺好（`GraphViewModel.cpp:38`），一行改动，与博文互补（悬浮看短说明、链接看长文档）；② 任务库 tree 节点 tooltip 同样加 type→文档链接；③ AboutDialog 加"节点手册"聚合页链接 |

## 7. 风险与权衡

- **内容量**：78×2=156 篇是主要成本，技术方案本身改动很小（app ~20 行 + 网站 0 模板开发）。分期策略把"链接先上线、文档渐进补齐"的空窗期影响压到定制 404 页 + 校验报告可见。
- **断链**：无运行时探测 → 缺文档即 404。换来的是零映射维护与零网络依赖；由构建期校验 + 菜单聚合页兜住。
- **刷屏**：节点文档默认进首页/归档文章流（`mainSections=["blog"]`）；78 篇上线时会淹没普通博文——P2 起量大时验证并启用 `hiddenFromHomePage`（tag 聚合页仍收录）。
- **分类归属**：放 blog + 专属标签（用户需求指定）而非独立 `/docs/` section。若日后想要更"手册感"的呈现（侧边栏、按 classifyTask 分组的落地页），term 页可用项目级 `layouts/term.html` 覆盖升级，URL 不变、app 零改动。
