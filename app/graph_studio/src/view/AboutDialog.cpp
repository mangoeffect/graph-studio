#include "view/AboutDialog.h"
#include "Brand.h"

#include <task_graph_api.hpp>

#include <QClipboard>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

// 测试目标等未注入宏时的回退（graph_studio 主目标由 CMake 统一定义）。
#ifndef GRAPH_STUDIO_VERSION
#define GRAPH_STUDIO_VERSION "0.0.0"
#endif
#ifndef GRAPH_STUDIO_GIT_HASH
#define GRAPH_STUDIO_GIT_HASH "unknown"
#endif
#ifndef GRAPH_STUDIO_BUILD_ENV
#define GRAPH_STUDIO_BUILD_ENV "development"
#endif

namespace graph_studio {

QString AboutDialog::buildInfoText() {
    const auto tasks = task_graph::PluginRegistry::instance().available_tasks();
    QString text;
    text += QString("Version: %1 (%2, %3)\n")
                .arg(GRAPH_STUDIO_VERSION, GRAPH_STUDIO_GIT_HASH,
                     GRAPH_STUDIO_BUILD_ENV);
    text += QString("Qt: %1\n").arg(qVersion());
    text += QString("OS: %1 (%2)\n")
                .arg(QSysInfo::prettyProductName(), QSysInfo::buildCpuArchitecture());
    text += QString("Tasks registered: %1\n").arg(tasks.size());
#ifdef GRAPH_STUDIO_HAS_SENTRY
    text += "Crash reporting: enabled\n";
#else
    text += "Crash reporting: disabled\n";
#endif
    return text;
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("About Graph Studio"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* titleLabel = new QLabel(tr("Graph Studio"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    layout->addWidget(new QLabel(tr("A DAG visual editor for task_graph."), this));

    auto* infoLabel = new QLabel(this);
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLabel->setText(buildInfoText());
    layout->addWidget(infoLabel);

    auto* linkLabel = new QLabel(this);
    linkLabel->setTextFormat(Qt::RichText);
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setText(
        tr("Website: <a href=\"%1\">studio.mangoeffect.net</a><br>"
           "Source: <a href=\"%2\">github.com/mangoeffect/graph-studio</a>")
            .arg(kWebsiteBaseUrl, kSourceRepoUrl));
    layout->addWidget(linkLabel);

    auto* copyButton = new QPushButton(tr("Copy build info"), this);
    // 报障时一键贴环境（与 Sentry 崩溃上报互补：用户主动反馈的场景）。
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(buildInfoText());
    });
    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(copyButton);
    buttonRow->addStretch();
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);
}

} // namespace graph_studio
