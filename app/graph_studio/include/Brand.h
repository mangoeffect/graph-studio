#pragma once

// 对外站点/仓库链接的唯一来源（AboutDialog、节点手册链接等共用）。
// 改域名时只改这里；节点手册 URL 约定见 docs/research/node-doc-links-design.md：
//   <站点>/blog/<task_type>/
inline constexpr const char* kWebsiteBaseUrl = "https://studio.mangoeffect.net/";
inline constexpr const char* kSourceRepoUrl = "https://github.com/mangoeffect/graph-studio";
