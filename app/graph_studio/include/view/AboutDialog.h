#pragma once

#include <QDialog>

namespace graph_studio {

// Help 菜单 "About Graph Studio" 弹出的构建/环境信息对话框。
// 版本与 git hash 由 CMake 编译宏注入（GRAPH_STUDIO_VERSION 等，
// 见 app/graph_studio/CMakeLists.txt "应用构建元信息"块）；
// 未定义时（如测试目标）回退默认值。
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

    // 汇总构建/运行时信息（版本、hash、环境、Qt、OS、任务数、崩溃上报状态）。
    // 独立于对话框实例，供 "Copy build info" 按钮与 UI 测试复用。
    static QString buildInfoText();
};

} // namespace graph_studio
