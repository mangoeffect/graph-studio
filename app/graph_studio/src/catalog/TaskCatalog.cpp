#include "catalog/TaskCatalog.h"

using namespace graph_studio;

namespace {

// ---- ParamSpec <-> QVariant 桥接（自 GraphViewModel 原匿名命名空间迁入）----

QVariantMap paramSpecToVariant(const task_graph::ParamSpec& s) {
    QVariantMap m;
    m["name"] = QString::fromStdString(s.name);
    m["description"] = QString::fromStdString(s.description);
    switch (s.type) {
        case task_graph::ParamType::Int:    m["type"] = QStringLiteral("int");    break;
        case task_graph::ParamType::Float:  m["type"] = QStringLiteral("float");  break;
        case task_graph::ParamType::String: m["type"] = QStringLiteral("string"); break;
        case task_graph::ParamType::Bool:   m["type"] = QStringLiteral("bool");   break;
        case task_graph::ParamType::Enum:   m["type"] = QStringLiteral("enum");   break;
    }
    if (s.min_value) m["min"] = *s.min_value;
    if (s.max_value) m["max"] = *s.max_value;
    if (s.step)      m["step"] = *s.step;
    if (auto v = s.default_as_int())        m["default"] = *v;
    else if (auto v = s.default_as_float()) m["default"] = *v;
    else if (auto v = s.default_as_bool())  m["default"] = *v;
    else if (auto v = s.default_as_string()) m["default"] = QString::fromStdString(*v);
    if (s.type == task_graph::ParamType::Enum && !s.enum_values.empty()) {
        QVariantList labels, values;
        for (const auto& [label, value] : s.enum_values) {
            labels.append(QString::fromStdString(label));
            values.append(value);
        }
        m["enumLabels"] = labels;
        m["enumValues"] = values;
    }
    if (!s.widget_hint.empty()) m["widget"] = QString::fromStdString(s.widget_hint);
    if (!s.file_filter.empty()) m["fileFilter"] = QString::fromStdString(s.file_filter);
    // 显隐/联动提示（属性面板消费；见 ParamSpec 字段注释）
    if (s.hidden) m["hidden"] = true;
    if (!s.visible_when.empty()) {
        m["visibleWhen"] = QString::fromStdString(s.visible_when);
        QVariantList vals;
        for (int v : s.visible_when_values) vals.append(v);
        m["visibleWhenValues"] = vals;
        if (s.reset_on_visible_when_change) m["resetOnLinkChange"] = true;
    }
    return m;
}

}  // namespace

namespace graph_studio {

const TaskCatalog::TypeInfo* TaskCatalog::info(const QString& taskType) const {
    auto it = cache_.constFind(taskType);
    if (it != cache_.constEnd()) return &it.value();
    const std::string type = taskType.toStdString();
    if (!task_graph::PluginRegistry::instance().has_task(type)) return nullptr;
    it = cache_.insert(taskType, buildInfo(type));
    return &it.value();
}

const TaskCatalog::TypeInfo& TaskCatalog::infoOrEmpty(const QString& taskType) const {
    static const TypeInfo kEmpty;
    if (const TypeInfo* ti = info(taskType)) return *ti;
    return kEmpty;
}

TaskCatalog::TypeInfo TaskCatalog::buildInfo(const std::string& taskType) const {
    TypeInfo ti;
    auto probe = task_graph::PluginRegistry::instance().create_task(taskType);
    if (!probe) return ti;
    ti.specs = probe->param_specs();
    for (const auto& s : ti.specs) {
        QVariantMap vm = paramSpecToVariant(s);
        if (vm.contains("default")) {
            ti.defaultParams[QString::fromStdString(s.name)] = vm["default"];
        }
        ti.paramSpecList.append(vm);
    }
    for (const auto& s : probe->input_specs()) {
        ti.inputPorts.append(QString::fromStdString(s.name));
        if (ti.firstRequiredInputPort.isEmpty() && s.required) {
            ti.firstRequiredInputPort = QString::fromStdString(s.name);
        }
    }
    for (const auto& s : probe->output_specs()) {
        ti.outputPorts.append(QString::fromStdString(s.name));
    }
    return ti;
}

QVariantList TaskCatalog::paramSpecs(const QString& taskType) const {
    const TypeInfo* ti = info(taskType);
    return ti ? ti->paramSpecList : QVariantList{};
}

QVariantMap TaskCatalog::defaultParams(const QString& taskType) const {
    const TypeInfo* ti = info(taskType);
    return ti ? ti->defaultParams : QVariantMap{};
}

QStringList TaskCatalog::inputPorts(const QString& taskType) const {
    const TypeInfo* ti = info(taskType);
    return ti ? ti->inputPorts : QStringList{};
}

QStringList TaskCatalog::outputPorts(const QString& taskType) const {
    const TypeInfo* ti = info(taskType);
    return ti ? ti->outputPorts : QStringList{};
}

QString TaskCatalog::defaultOutputPort(const QString& taskType) const {
    const TypeInfo* ti = info(taskType);
    if (!ti || ti->outputPorts.empty()) return QStringLiteral("out");
    return ti->outputPorts.front();
}

QString TaskCatalog::defaultInputPort(const QString& taskType) const {
    const TypeInfo* ti = info(taskType);
    if (!ti) return QStringLiteral("in");
    // 优先第一个 required 输入口；否则第一个输入口；无则 "in"
    if (!ti->firstRequiredInputPort.isEmpty()) return ti->firstRequiredInputPort;
    if (!ti->inputPorts.empty()) return ti->inputPorts.front();
    return QStringLiteral("in");
}

bool TaskCatalog::setParam(task_graph::TaskParams& params, const QString& key,
                           const QVariant& value, const TypeInfo& ti) {
    const std::string k = key.toStdString();
    for (const auto& s : ti.specs) {
        if (s.name != k) continue;
        switch (s.type) {
            case task_graph::ParamType::Int:
            case task_graph::ParamType::Enum:
                params.set_int(k, value.toInt()); break;
            case task_graph::ParamType::Float:
                params.set_float(k, value.toFloat()); break;
            case task_graph::ParamType::String:
                params.set_string(k, value.toString().toStdString()); break;
            case task_graph::ParamType::Bool:
                params.set_bool(k, value.toBool()); break;
        }
        return true;
    }
    return false;
}

QVariantMap TaskCatalog::paramsToVariant(const task_graph::TaskParams& p,
                                         const TypeInfo& ti) {
    QVariantMap out;
    std::unordered_map<std::string, const task_graph::ParamSpec*> by_name;
    for (const auto& s : ti.specs) by_name[s.name] = &s;

    for (const auto& s : ti.specs) {
        QString k = QString::fromStdString(s.name);
        switch (s.type) {
            case task_graph::ParamType::Int:
            case task_graph::ParamType::Enum:
                if (auto v = p.get_int(s.name)) out[k] = *v;
                break;
            case task_graph::ParamType::Float:
                if (auto v = p.get_float(s.name)) out[k] = *v;
                break;
            case task_graph::ParamType::String:
                if (auto v = p.get_string(s.name)) out[k] = QString::fromStdString(*v);
                break;
            case task_graph::ParamType::Bool:
                if (auto v = p.get_bool(s.name)) out[k] = *v;
                break;
        }
    }
    for (const auto& [key, _] : p.params()) {
        if (by_name.contains(key)) continue;
        QString k = QString::fromStdString(key);
        if (auto v = p.get_int(key))         out[k] = *v;
        else if (auto v = p.get_float(key))  out[k] = *v;
        else if (auto v = p.get_bool(key))   out[k] = *v;
        else if (auto v = p.get_string(key)) out[k] = QString::fromStdString(*v);
    }
    return out;
}

void TaskCatalog::invalidate() {
    cache_.clear();
}

} // namespace graph_studio
