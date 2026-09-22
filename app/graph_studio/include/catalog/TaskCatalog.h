#ifndef TASK_CATALOG_H
#define TASK_CATALOG_H

// TaskCatalog：task 类型内省缓存（应用侧）。
//
// 解决的问题：GraphViewModel 此前的 queryParamSpecs/queryPortNames 每次查询
// 都 PluginRegistry::create_task() 创建一个临时任务实例、取完 spec 即弃。
// nodes()/nodeData()/nodeParams()/addTask 等路径每个节点都会触发——规格信息
// 对同一 task type 是恒定的，探针实例纯属浪费（部分任务构造并不便宜）。
//
// 缓存内容：ParamSpec 原始列表（参数读写用）+ 预转换的 QVariantMap
// （默认参数 / 属性面板）+ 输入/输出端口名。插件动态加载后调用 invalidate()。

#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QString>
#include <QHash>

#include <task_graph_api.hpp>

#include <optional>
#include <string>
#include <vector>

namespace graph_studio {

class TaskCatalog {
public:
    struct TypeInfo {
        std::vector<task_graph::ParamSpec> specs;  // 原始 spec（setParam 用）
        QVariantMap defaultParams;                 // name -> default value
        QVariantList paramSpecList;                // 属性面板用的 spec 列表
        QStringList inputPorts;
        QStringList outputPorts;
        QString firstRequiredInputPort;            // 无则 "in"（连线默认端口用）
    };

    // 未注册类型返回 nullptr；命中走缓存（每类型探针只创建一次）。
    // 查询接口均为 const（缓存可自愈，mutable 存储）。
    const TypeInfo* info(const QString& taskType) const;
    const TypeInfo* info(const std::string& taskType) const {
        return info(QString::fromStdString(taskType));
    }
    // 兜底变体：未注册类型返回空 TypeInfo（避免调用方判空；UI 测试可创建
    // 未注册的占位类型节点）
    const TypeInfo& infoOrEmpty(const QString& taskType) const;
    const TypeInfo& infoOrEmpty(const std::string& taskType) const {
        return infoOrEmpty(QString::fromStdString(taskType));
    }

    // —— 便捷查询（全部走缓存）——
    QVariantList paramSpecs(const QString& taskType) const;
    QVariantMap defaultParams(const QString& taskType) const;
    QStringList inputPorts(const QString& taskType) const;
    QStringList outputPorts(const QString& taskType) const;

    // 连线时的默认端口（与原 GraphViewModel 匿名命名空间逻辑一致）：
    // 输出取第一个声明的输出口（无则 "out"）；输入优先第一个 required，
    // 否则第一个输入口，无则 "in"。
    QString defaultOutputPort(const QString& taskType) const;
    QString defaultInputPort(const QString& taskType) const;

    // spec 类型感知的参数写入；key 不是已声明参数时返回 false
    static bool setParam(task_graph::TaskParams& params, const QString& key,
                         const QVariant& value, const TypeInfo& ti);

    // TaskParams -> QVariantMap（已声明参数按类型取出 + 未声明键尽力转换）
    static QVariantMap paramsToVariant(const task_graph::TaskParams& p,
                                       const TypeInfo& ti);

    void invalidate();

private:
    TypeInfo buildInfo(const std::string& taskType) const;

    mutable QHash<QString, TypeInfo> cache_;
};

} // namespace graph_studio

#endif // TASK_CATALOG_H
