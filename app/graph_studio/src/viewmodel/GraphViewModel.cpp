#include "viewmodel/GraphViewModel.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>
#include <QQueue>
#include <QStack>
#include <algorithm>
#include <task_graph_api.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

using namespace graph_studio;

// ---- ParamSpec 桥接：把 lib 侧 ParamSpec 拆成 QVariantMap（明确类型） ----
namespace {

const int kLogTrace = static_cast<int>(task_graph::LogLevel::TRACE);
const int kLogDebug = static_cast<int>(task_graph::LogLevel::DEBUG);
const int kLogInfo  = static_cast<int>(task_graph::LogLevel::INFO);
const int kLogWarn  = static_cast<int>(task_graph::LogLevel::WARN);
const int kLogError = static_cast<int>(task_graph::LogLevel::ERROR);
const int kLogFatal = static_cast<int>(task_graph::LogLevel::FATAL);

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
    return m;
}

std::vector<task_graph::ParamSpec> queryParamSpecs(const std::string& task_type) {
    if (!task_graph::PluginRegistry::instance().has_task(task_type)) return {};
    auto probe = task_graph::PluginRegistry::instance().create_task(task_type);
    return probe ? probe->param_specs() : std::vector<task_graph::ParamSpec>{};
}

// 端口名查询：input=true 查 input_specs()，否则 output_specs()。未注册类型返回空。
std::vector<std::string> queryPortNames(const std::string& task_type, bool is_input) {
    if (!task_graph::PluginRegistry::instance().has_task(task_type)) return {};
    std::vector<task_graph::PortSpec> specs;
    if (auto probe = task_graph::PluginRegistry::instance().create_task(task_type)) {
        specs = is_input ? probe->input_specs() : probe->output_specs();
    }
    std::vector<std::string> names;
    names.reserve(specs.size());
    for (const auto& s : specs) names.push_back(s.name);
    return names;
}

QStringList toQStringList(const std::vector<std::string>& v) {
    QStringList out;
    out.reserve(static_cast<int>(v.size()));
    for (const auto& s : v) out.append(QString::fromStdString(s));
    return out;
}

// 解析连线时的默认端口：输出取第一个声明的输出口（无则 "out"）。
QString defaultOutputPort(const std::string& task_type) {
    auto names = queryPortNames(task_type, false);
    return names.empty() ? QStringLiteral("out") : QString::fromStdString(names.front());
}

// 输入端口：优先第一个 required 输入口；否则第一个输入口；无则 "in"。
QString defaultInputPort(const std::string& task_type) {
    if (!task_graph::PluginRegistry::instance().has_task(task_type)) return QStringLiteral("in");
    std::vector<task_graph::PortSpec> specs;
    if (auto probe = task_graph::PluginRegistry::instance().create_task(task_type)) {
        specs = probe->input_specs();
    }
    for (const auto& s : specs) {
        if (s.required) return QString::fromStdString(s.name);
    }
    if (!specs.empty()) return QString::fromStdString(specs[0].name);
    return QStringLiteral("in");
}

QVariantMap defaultParamsForType(const std::string& task_type) {
    QVariantMap out;
    for (const auto& s : queryParamSpecs(task_type)) {
        QVariantMap vm = paramSpecToVariant(s);
        if (vm.contains("default")) out[QString::fromStdString(s.name)] = vm["default"];
    }
    return out;
}

QVariantMap taskParamsToVariant(const task_graph::TaskParams& p,
                                 const std::vector<task_graph::ParamSpec>& specs) {
    QVariantMap out;
    std::unordered_map<std::string, const task_graph::ParamSpec*> by_name;
    for (const auto& s : specs) by_name[s.name] = &s;

    for (const auto& s : specs) {
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

void applyVariantToParams(const QString& key, const QVariant& value,
                          const task_graph::ParamSpec& spec,
                          task_graph::TaskParams& out) {
    const std::string k = key.toStdString();
    switch (spec.type) {
        case task_graph::ParamType::Int:
        case task_graph::ParamType::Enum:
            out.set_int(k, value.toInt()); break;
        case task_graph::ParamType::Float:
            out.set_float(k, value.toFloat()); break;
        case task_graph::ParamType::String:
            out.set_string(k, value.toString().toStdString()); break;
        case task_graph::ParamType::Bool:
            out.set_bool(k, value.toBool()); break;
    }
    }
}  // namespace

// ---- 零拷贝图像转换：cv::Mat / task_graph::Image -> QImage ----
// QImage 通过 cleanup function 持有源数据的引用计数，直接共享像素缓冲，
// 析构时回调释放源对象，避免 QImage::copy() 二次拷贝。
namespace {

void matCleanup(void* info) {
    delete static_cast<cv::Mat*>(info);
}

QImage matToQImage(const cv::Mat& src) {
    if (src.empty()) return {};
    cv::Mat mat = src;  // refcount++，共享像素缓冲
    QImage::Format fmt = QImage::Format_Invalid;
    switch (mat.channels()) {
        case 1:  fmt = QImage::Format_Grayscale8; break;   // 灰度(Sobel/Laplacian)
        case 3:  fmt = QImage::Format_BGR888;      break;  // BGR 直接映射，零拷贝
        case 4: {                                          // BGRA：Qt 无 BGRA8888 格式
            cv::Mat t;
            cv::cvtColor(mat, t, cv::COLOR_BGRA2RGBA);
            mat = t;
            fmt = QImage::Format_RGBA8888;
            break;
        }
        default: return {};
    }
    // keep 持 Mat 引用计数；QImage 析构时 delete keep，refcount 归零才释放像素
    auto* keep = new cv::Mat(mat);
    return QImage(keep->data, keep->cols, keep->rows, keep->step,
                  fmt, matCleanup, keep);
}

void imageDataCleanup(void* info) {
    delete static_cast<std::shared_ptr<std::vector<uint8_t>>*>(info);
}

QImage imageToQImage(const task_graph::Image& src) {
    // 拷贝 Image 结构体（浅拷贝，共享 data 的 shared_ptr）；ensure_cpu 可能改状态，隔离之
    task_graph::Image img = src;
    if (!img.ensure_cpu() || !img.data || img.data->empty()) return {};
    QImage::Format fmt = QImage::Format_Invalid;
    switch (img.channels) {
        case 1: fmt = QImage::Format_Grayscale8; break;
        case 3: fmt = (img.pixel_format == task_graph::PixelFormat::BGR)
                          ? QImage::Format_BGR888 : QImage::Format_RGB888; break;
        case 4: fmt = QImage::Format_RGBA8888; break;
        default: return {};
    }
    // keep 持 shared_ptr<vector<uint8_t>> 引用计数，QImage 析构时释放
    auto* keep = new std::shared_ptr<std::vector<uint8_t>>(img.data);
    return QImage(keep->get()->data(), img.width, img.height,
                  img.width * img.channels, fmt, imageDataCleanup, keep);
}

// 从 std::any 提取图像转 QImage。type-check-first：WASM -fno-exceptions 下
// any_cast 失败会 abort，必须先用 type() 比对。
std::optional<QImage> anyToQImage(const std::any& v) {
    if (!v.has_value()) return std::nullopt;
    if (v.type() == typeid(cv::Mat)) {
        return matToQImage(std::any_cast<cv::Mat>(v));
    }
    if (v.type() == typeid(task_graph::Image)) {
        return imageToQImage(std::any_cast<task_graph::Image>(v));
    }
    return std::nullopt;
}

}  // namespace

GraphViewModel::GraphViewModel(GraphModel& model, QObject* parent)
    : QObject(parent), model_(model)
{
    dagSubId_ = model_.dag().subscribe([this](const task_graph::DAGChangeEvent& e) {
        onDagChanged(e);
    });

    // 注册框架日志 sink：把 TG_LOG_* / ctx.log() 等日志转发到 UI 线程。
    // sink 可能在 executor 工作线程触发，用 QueuedConnection 编组到 UI 线程后 emit。
    task_graph::set_log_sink(
        [this](const task_graph::LogEntry& e) {
            // 格式化：[thread_name/thread_id] [file:line] msg
            // 级别由信号参数携带；时间戳暂不显示（避免消息过长）
            QString prefix;
            if (!e.thread_name.empty()) {
                prefix += QStringLiteral("[%1/%2] ")
                    .arg(QString::fromStdString(e.thread_name),
                         QString::fromStdString(e.thread_id));
            } else if (!e.thread_id.empty()) {
                prefix += QStringLiteral("[T/%1] ")
                    .arg(QString::fromStdString(e.thread_id));
            }
            if (!e.filename.empty() && e.line > 0) {
                prefix += QStringLiteral("[%1:%2] ")
                    .arg(QString::fromStdString(e.filename),
                         QString::number(e.line));
            }
            QString qmsg = prefix + QString::fromStdString(e.msg);
            int ilevel = static_cast<int>(e.level);
            QMetaObject::invokeMethod(this, [this, ilevel, qmsg]() {
                emit logMessage(ilevel, qmsg);
            }, Qt::QueuedConnection);
        });
}

GraphViewModel::~GraphViewModel()
{
    task_graph::clear_log_sink();
    model_.dag().unsubscribe(dagSubId_);
    if (executor_) {
        executor_->cancel();
        executor_->wait();
    }
}

int GraphViewModel::taskCount() const { return static_cast<int>(model_.dag().num_tasks()); }
int GraphViewModel::edgeCount() const { return static_cast<int>(model_.dag().num_edges()); }
QString GraphViewModel::selectedNodeId() const { return selectedNodeId_; }
bool GraphViewModel::isExecuting() const { return executing_; }

QString GraphViewModel::generateUniqueId(const QString& taskType) const
{
    int& counter = typeCounter_[taskType];
    QString id;
    do {
        ++counter;
        id = taskType + "_" + QString::number(counter);
    } while (hasNode(id));
    return id;
}

// ====== DAG 事件处理：翻译为 Qt 信号 ======
void GraphViewModel::onDagChanged(const task_graph::DAGChangeEvent& e) {
    using Type = task_graph::DAGChangeEvent::Type;
    switch (e.type) {
    case Type::TaskAdded: {
        NodeData nd;
        nd.id = QString::fromStdString(e.task_id);
        nd.type = QString::fromStdString(e.task_type);
        QPointF pos = positions_.value(nd.id);
        nd.x = pos.x();
        nd.y = pos.y();
        nd.params = defaultParamsForType(e.task_type);
        nd.inputPorts = toQStringList(queryPortNames(e.task_type, true));
        nd.outputPorts = toQStringList(queryPortNames(e.task_type, false));
        emit taskAdded(nd);
        emit taskCountChanged();
        emit logMessage(kLogInfo, "Task added: " + nd.id + " (" + nd.type + ")");
        break;
    }
    case Type::TaskRemoved: {
        QString id = QString::fromStdString(e.task_id);
        positions_.remove(id);
        if (selectedNodeId_ == id) {
            selectedNodeId_.clear();
            emit selectionChanged({});
        }
        emit taskRemoved(id);
        emit taskCountChanged();
        emit logMessage(kLogInfo, "Task removed: " + id);
        break;
    }
    case Type::TaskUpdated:
        emit nodeParamsChanged(QString::fromStdString(e.task_id));
        break;
    case Type::EdgeAdded: {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        emit edgeAdded(ed);
        emit edgeCountChanged();
        emit logMessage(kLogInfo, "Edge added: " + ed.fromId + " -> " + ed.toId);
        break;
    }
    case Type::EdgeRemoved: {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        emit edgeRemoved(ed);
        emit edgeCountChanged();
        emit logMessage(kLogInfo, "Edge removed: " + ed.fromId + " -> " + ed.toId);
        break;
    }
    case Type::GraphReset: {
        emit graphReset();
        const auto& dag = model_.dag();
        for (const auto& id : dag.task_ids()) {
            NodeData nd;
            nd.id = QString::fromStdString(id);
            nd.type = QString::fromStdString(dag.task_type(id));
            QPointF pos = positions_.value(nd.id);
            nd.x = pos.x();
            nd.y = pos.y();
            auto cfg = dag.task_config(id);
            nd.params = taskParamsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                            queryParamSpecs(dag.task_type(id)));
            nd.inputPorts = toQStringList(queryPortNames(dag.task_type(id), true));
            nd.outputPorts = toQStringList(queryPortNames(dag.task_type(id), false));
            emit taskAdded(nd);
        }
        for (const auto& e : dag.edges()) {
            EdgeData ed;
            ed.fromId = QString::fromStdString(e.from);
            ed.toId = QString::fromStdString(e.to);
            ed.fromPort = QString::fromStdString(e.from_port);
            ed.toPort = QString::fromStdString(e.to_port);
            emit edgeAdded(ed);
        }
        emit taskCountChanged();
        emit edgeCountChanged();
        emit selectionChanged({});
        break;
    }
    }
}

// ====== 操作：只写 DAG，事件自动驱动 UI 信号 ======

QString GraphViewModel::addTask(const QString& taskType, qreal x, qreal y, const QString& taskId)
{
    QString id = taskId.isEmpty() ? generateUniqueId(taskType) : taskId;
    if (hasNode(id)) {
        emit logMessage(kLogWarn, "Task already exists: " + id);
        return {};
    }

    positions_[id] = QPointF(x, y);
    if (!model_.add_task(id.toStdString(), taskType.toStdString())) {
        positions_.remove(id);
        emit logMessage(kLogError, "Failed to add task to DAG: " + id);
        return {};
    }
    return id;
}

bool GraphViewModel::removeTask(const QString& taskId)
{
    if (!hasNode(taskId)) return false;
    model_.remove_task(taskId.toStdString());
    return true;
}

bool GraphViewModel::moveNode(const QString& taskId, qreal x, qreal y)
{
    if (!hasNode(taskId)) return false;
    positions_[taskId] = QPointF(x, y);
    emit nodeMoved(taskId, x, y);
    return true;
}

bool GraphViewModel::addEdge(const QString& fromId, const QString& toId)
{
    // 便捷重载：按两侧 task 类型自动解析端口名（保留旧调用语义的默认解析）
    auto fromType = nodeData(fromId).type.toStdString();
    auto toType = nodeData(toId).type.toStdString();
    return addEdge(fromId, defaultOutputPort(fromType), toId, defaultInputPort(toType));
}

bool GraphViewModel::addEdge(const QString& fromId, const QString& fromPort,
                             const QString& toId, const QString& toPort)
{
    if (fromId == toId) {
        emit logMessage(kLogWarn, "Cannot create self-loop: " + fromId);
        return false;
    }
    if (!hasNode(fromId) || !hasNode(toId)) {
        emit logMessage(kLogWarn, "Node not found for edge");
        return false;
    }
    // 端口级幂等：完全相同的四元组视为已存在（同 pair 不同端口可新增）
    if (model_.has_edge(fromId.toStdString(), fromPort.toStdString(),
                        toId.toStdString(), toPort.toStdString())) {
        emit logMessage(kLogWarn, "Edge already exists: " + fromId + ":" + fromPort +
                        " -> " + toId + ":" + toPort);
        return false;
    }
    // 输入端口只允许一个数据源：目标 to_port 已被任何上游占用时拒绝。
    // （core 层仅为 warning/last-write-wins；编辑器层做更严格的保护。）
    if (model_.input_port_filled(toId.toStdString(), toPort.toStdString())) {
        emit logMessage(kLogWarn, "Input port already connected: " + toId + ":" + toPort);
        return false;
    }
    if (canReach(toId, fromId)) {
        emit logMessage(kLogWarn, "Cycle detected, cannot add edge: " + fromId + " -> " + toId);
        return false;
    }
    if (!model_.add_edge(fromId.toStdString(), fromPort.toStdString(),
                         toId.toStdString(), toPort.toStdString())) {
        emit logMessage(kLogError, "Failed to add edge to DAG: " + fromId + " -> " + toId);
        return false;
    }
    return true;
}

bool GraphViewModel::removeEdge(const QString& fromId, const QString& toId)
{
    if (!model_.has_edge(fromId.toStdString(), toId.toStdString())) return false;
    model_.remove_edge(fromId.toStdString(), toId.toStdString());
    return true;
}

bool GraphViewModel::removeEdge(const QString& fromId, const QString& fromPort,
                                const QString& toId, const QString& toPort)
{
    if (!model_.has_edge(fromId.toStdString(), fromPort.toStdString(),
                         toId.toStdString(), toPort.toStdString())) return false;
    model_.remove_edge(fromId.toStdString(), fromPort.toStdString(),
                       toId.toStdString(), toPort.toStdString());
    return true;
}

void GraphViewModel::selectNode(const QString& taskId)
{
    if (selectedNodeId_ == taskId) return;
    selectedNodeId_ = taskId;
    emit selectionChanged(taskId);
}

void GraphViewModel::clearSelection()
{
    if (selectedNodeId_.isEmpty()) return;
    selectedNodeId_.clear();
    emit selectionChanged({});
}

// ====== 查询：从 DAG 读 ======

QList<NodeData> GraphViewModel::nodes() const
{
    QList<NodeData> result;
    const auto& dag = model_.dag();
    for (const auto& id : dag.task_ids()) {
        NodeData nd;
        nd.id = QString::fromStdString(id);
        nd.type = QString::fromStdString(dag.task_type(id));
        QPointF pos = positions_.value(nd.id);
        nd.x = pos.x();
        nd.y = pos.y();
        auto cfg = dag.task_config(id);
        nd.params = taskParamsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                        queryParamSpecs(dag.task_type(id)));
        nd.inputPorts = toQStringList(queryPortNames(dag.task_type(id), true));
        nd.outputPorts = toQStringList(queryPortNames(dag.task_type(id), false));
        result.append(nd);
    }
    return result;
}

QList<EdgeData> GraphViewModel::edges() const
{
    QList<EdgeData> result;
    for (const auto& e : model_.dag().edges()) {
        EdgeData ed;
        ed.fromId = QString::fromStdString(e.from);
        ed.toId = QString::fromStdString(e.to);
        ed.fromPort = QString::fromStdString(e.from_port);
        ed.toPort = QString::fromStdString(e.to_port);
        result.append(ed);
    }
    return result;
}

bool GraphViewModel::hasNode(const QString& taskId) const
{
    return model_.dag().has_task(taskId.toStdString());
}

NodeData GraphViewModel::nodeData(const QString& taskId) const
{
    NodeData nd;
    nd.id = taskId;
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    if (!dag.has_task(id)) return nd;
    nd.type = QString::fromStdString(dag.task_type(id));
    QPointF pos = positions_.value(taskId);
    nd.x = pos.x();
    nd.y = pos.y();
    auto cfg = dag.task_config(id);
    nd.params = taskParamsToVariant(cfg ? cfg->params : task_graph::TaskParams{},
                                    queryParamSpecs(dag.task_type(id)));
    nd.inputPorts = toQStringList(queryPortNames(dag.task_type(id), true));
    nd.outputPorts = toQStringList(queryPortNames(dag.task_type(id), false));
    return nd;
}

QVariantList GraphViewModel::paramSpecs(const QString& taskType) const
{
    QVariantList out;
    for (const auto& s : queryParamSpecs(taskType.toStdString())) {
        out.append(paramSpecToVariant(s));
    }
    return out;
}

QVariantMap GraphViewModel::nodeParams(const QString& taskId) const
{
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    auto cfg = dag.task_config(id);
    if (!cfg) return {};
    return taskParamsToVariant(cfg->params, queryParamSpecs(dag.task_type(id)));
}

bool GraphViewModel::setNodeParam(const QString& taskId, const QString& key, const QVariant& value)
{
    auto id = taskId.toStdString();
    const auto& dag = model_.dag();
    if (!dag.has_task(id)) return false;

    std::string type = dag.task_type(id);
    auto specs = queryParamSpecs(type);
    for (const auto& s : specs) {
        if (s.name == key.toStdString()) {
            task_graph::TaskParams params = model_.task_params(id);
            applyVariantToParams(key, value, s, params);
            model_.update_task_params(id, params);
            emit logMessage(kLogInfo, "Param updated: " + taskId + "." + key);
            return true;
        }
    }
    return false;
}

QStringList GraphViewModel::availableTaskTypes() const
{
    QStringList out;
    for (const auto& t : task_graph::PluginRegistry::instance().available_tasks()) {
        out.append(QString::fromStdString(t));
    }
    return out;
}

bool GraphViewModel::hasTaskType(const QString& type) const
{
    return task_graph::PluginRegistry::instance().has_task(type.toStdString());
}

QString GraphViewModel::classifyTask(const QString& type)
{
    // 优先级从上到下，首个命中即返回。
    // 读写类：按前缀语义先判定，避免 opencv_image_read/write 被 opencv_* 兜底吞掉
    if (type.endsWith("_read") || type.contains("video_capture") || type.contains("video_reader"))
        return QStringLiteral("Input");
    if (type.endsWith("_write") || type.contains("video_writer") ||
        type.contains("display") || type.contains("save"))
        return QStringLiteral("Output");

    // OpenCV 子域（按 task type 前缀细分，替代原先单一的 "OpenCV Filter" 分组）
    if (type.startsWith("opencv_resize") || type.startsWith("opencv_flip") ||
        type.startsWith("opencv_rotate") || type.startsWith("opencv_warp") ||
        type.startsWith("opencv_transpose") || type.startsWith("opencv_pyr_"))
        return QStringLiteral("OpenCV Geometry");
    if (type.startsWith("opencv_cvt_color") || type.startsWith("opencv_threshold") ||
        type.startsWith("opencv_apply_color_map"))
        return QStringLiteral("OpenCV Color");
    if (type.startsWith("opencv_canny") || type.startsWith("opencv_hough") ||
        type.contains("contour"))
        return QStringLiteral("OpenCV Edges");
    if (type.startsWith("opencv_blur") || type.startsWith("opencv_gaussian") ||
        type.startsWith("opencv_median") || type.startsWith("opencv_bilateral") ||
        type.startsWith("opencv_box") || type.startsWith("opencv_sobel") ||
        type.startsWith("opencv_scharr") || type.startsWith("opencv_laplacian") ||
        type.startsWith("opencv_filter_2d") || type.startsWith("opencv_sep_filter") ||
        type.startsWith("opencv_sqr_box") || type.startsWith("opencv_gabor") ||
        type.startsWith("opencv_dilate") || type.startsWith("opencv_erode") ||
        type.startsWith("opencv_morphology"))
        return QStringLiteral("OpenCV Filter");
    if (type.startsWith("opencv_"))
        return QStringLiteral("OpenCV");

    // 其他内置子模块
    if (type.startsWith("color_grade_")) return QStringLiteral("Color Grading");
    if (type.startsWith("gpu_"))   return QStringLiteral("GPU");
    if (type.startsWith("mp_"))    return QStringLiteral("MediaPipe");
    if (type == QStringLiteral("js_script")) return QStringLiteral("Scripting");

    // 宽松启发式兜底
    if (type.contains("input") || type.contains("load"))  return QStringLiteral("Input");
    if (type.contains("output") || type.contains("save") || type.contains("display"))
        return QStringLiteral("Output");
    return QStringLiteral("Process");
}

QStringList GraphViewModel::inputPorts(const QString& taskType) const
{
    return toQStringList(queryPortNames(taskType.toStdString(), true));
}

QStringList GraphViewModel::outputPorts(const QString& taskType) const
{
    return toQStringList(queryPortNames(taskType.toStdString(), false));
}

void GraphViewModel::clear()
{
    positions_.clear();
    typeCounter_.clear();
    selectedNodeId_.clear();
    model_.clear();
    emit selectionChanged({});
    emit logMessage(kLogInfo, "Graph cleared");
}

bool GraphViewModel::saveToFile(const QString& filePath)
{
    nlohmann::json positions;
    for (const auto& id : model_.dag().task_ids()) {
        QPointF pos = positions_.value(QString::fromStdString(id));
        nlohmann::json p;
        p["x"] = pos.x();
        p["y"] = pos.y();
        p["type"] = model_.dag().task_type(id);
        positions[id] = p;
    }
    nlohmann::json metadata;
    if (!positions.empty()) metadata["positions"] = positions;

    std::string json_str = model_.to_json_string(metadata.dump());
    if (json_str.empty()) {
        emit logMessage(kLogError, "Failed to serialize DAG");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage(kLogError, "Cannot open file for writing: " + filePath);
        return false;
    }
    QTextStream stream(&file);
    stream << QString::fromStdString(json_str);
    file.close();

    emit logMessage(kLogInfo, "Graph saved to: " + filePath);
    return true;
}

bool GraphViewModel::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(kLogError, "Cannot open file: " + filePath);
        return false;
    }
    QString json = QTextStream(&file).readAll();
    file.close();

    positions_.clear();
    selectedNodeId_.clear();

    // graph.json 所在目录作为相对路径基准，注入到每个 task 的 _source_dir。
    // 这样 graph 内引用的图片/模型/脚本可以用相对路径，整个 graph 目录
    // 挪到别处仍可执行。
    const QString graphDir = QFileInfo(filePath).absolutePath();
    std::string metadata_str = model_.from_json_string_with_metadata(
        json.toStdString(), graphDir.toStdString());
    if (metadata_str.empty() && !model_.task_count()) {
        emit logMessage(kLogError, "Failed to parse DAG JSON");
        return false;
    }

    nlohmann::json metadata;
    try { metadata = nlohmann::json::parse(metadata_str); } catch (...) {}

    if (metadata.contains("positions")) {
        for (const auto& id : model_.dag().task_ids()) {
            if (metadata["positions"].contains(id)) {
                qreal x = metadata["positions"][id]["x"].get<qreal>();
                qreal y = metadata["positions"][id]["y"].get<qreal>();
                positions_[QString::fromStdString(id)] = QPointF(x, y);
            }
        }
    } else {
        autoLayout();
    }

    // GraphReset 事件已在 reset_from 中触发，onDagChanged 已重建 UI。
    // 如有位置数据，更新已创建的 NodeItem 位置。
    if (metadata.contains("positions")) {
        for (const auto& id : model_.dag().task_ids()) {
            if (metadata["positions"].contains(id)) {
                qreal x = metadata["positions"][id]["x"].get<qreal>();
                qreal y = metadata["positions"][id]["y"].get<qreal>();
                emit nodeMoved(QString::fromStdString(id), x, y);
            }
        }
    }

    emit logMessage(kLogInfo, "Graph loaded from: " + filePath);
    return true;
}

void GraphViewModel::autoLayout()
{
    if (model_.dag().num_tasks() == 0) return;

    QHash<QString, int> layer;
    QHash<QString, QSet<QString>> succ;
    QHash<QString, QSet<QString>> pred;
    QHash<QString, int> inDegree;

    QStringList allIds;
    for (const auto& id : model_.dag().task_ids()) {
        QString qid = QString::fromStdString(id);
        allIds.append(qid);
        layer[qid] = 0;
        inDegree[qid] = 0;
    }
    for (const auto& e : model_.dag().edge_list()) {
        QString from = QString::fromStdString(e.from);
        QString to = QString::fromStdString(e.to);
        succ[from].insert(to);
        pred[to].insert(from);
        inDegree[to]++;
    }

    QQueue<QString> queue;
    for (const auto& qid : allIds) {
        if (inDegree[qid] == 0) queue.enqueue(qid);
    }

    QHash<QString, int> tempInDegree = inDegree;
    while (!queue.isEmpty()) {
        QString cur = queue.dequeue();
        int maxLayer = 0;
        for (const auto& p : pred[cur]) {
            if (layer[p] + 1 > maxLayer) maxLayer = layer[p] + 1;
        }
        layer[cur] = maxLayer;
        for (const auto& s : succ[cur]) {
            if (--tempInDegree[s] == 0) queue.enqueue(s);
        }
    }

    QHash<int, QStringList> layerNodes;
    int maxLayer = 0;
    for (const auto& qid : allIds) {
        int l = layer[qid];
        layerNodes[l].append(qid);
        if (l > maxLayer) maxLayer = l;
    }

    const qreal xSpacing = 220;
    const qreal ySpacing = 120;
    const qreal startX = -maxLayer * xSpacing / 2;

    for (int l = 0; l <= maxLayer; ++l) {
        const auto& nodes = layerNodes[l];
        qreal totalHeight = (nodes.size() - 1) * ySpacing;
        qreal y = -totalHeight / 2;
        for (const auto& id : nodes) {
            qreal x = startX + l * xSpacing;
            positions_[id] = QPointF(x, y);
            emit nodeMoved(id, x, y);
            y += ySpacing;
        }
    }

    emit logMessage(kLogInfo, "Auto layout applied");
}

void GraphViewModel::execute()
{
    if (executing_) {
        emit logMessage(kLogWarn, "Execution already in progress");
        return;
    }
    if (taskCount() == 0) {
        emit logMessage(kLogWarn, "Nothing to execute: graph is empty");
        return;
    }

    task_graph::DAGCompiler compiler;
    const auto issues = compiler.validate(model_.dag());
    bool hasError = false;
    for (const auto& issue : issues) {
        const bool isError = issue.severity == task_graph::ValidationError::Severity::ERROR;
        hasError = hasError || isError;
        emit logMessage(isError ? kLogError : kLogWarn,
                        QStringLiteral("%1%2%3")
                            .arg(issue.task_id.empty() ? QString()
                                                       : QStringLiteral("%1: ").arg(QString::fromStdString(issue.task_id)))
                            .arg(issue.port_name.empty() ? QString()
                                                          : QStringLiteral("[%1] ").arg(QString::fromStdString(issue.port_name)))
                            .arg(QString::fromStdString(issue.message)));
    }
    if (hasError) {
        emit logMessage(kLogError, "Execution aborted due to validation errors");
        return;
    }

    executing_ = true;
    emit executingChanged();
    emit executionStarted();
    emit logMessage(kLogInfo, QStringLiteral("Executing %1 tasks...").arg(taskCount()));

    ensureExecutor();

    try {
        executor_->execute(model_.dag());
    } catch (const std::exception& ex) {
        emit logMessage(kLogError, QStringLiteral("Failed to start execution: %1").arg(ex.what()));
        executing_ = false;
        emit executingChanged();
        return;
    }
}

void GraphViewModel::ensureExecutor()
{
    if (executor_) return;
    task_graph::ExecutorConfig config;
    config.enable_profiling = true;
    config.callback = [this](const task_graph::ExecutionEvent& e) {
        QMetaObject::invokeMethod(this, [this, e]() { onExecutionEvent(e); },
                                  Qt::QueuedConnection);
    };
    executor_ = std::make_unique<task_graph::DAGExecutor>(config);
}

void GraphViewModel::onExecutionEvent(const task_graph::ExecutionEvent& e) {
    using Type = task_graph::ExecutionEvent::Type;
    switch (e.type) {
    case Type::TaskStarted:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::STARTED), 0);
        break;
    case Type::TaskCompleted:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::COMPLETED),
                              std::chrono::duration<double, std::milli>(e.duration).count());
        emit logMessage(kLogInfo, QStringLiteral("%1  (%2 ms)")
                            .arg(QString::fromStdString(e.task_id))
                            .arg(std::chrono::duration<double, std::milli>(e.duration).count(), 0, 'f', 2));
        break;
    case Type::TaskFailed:
        emit nodeStatusChanged(QString::fromStdString(e.task_id),
                              static_cast<int>(task_graph::ProfilePhase::FAILED),
                              std::chrono::duration<double, std::milli>(e.duration).count());
        emit logMessage(kLogError, QStringLiteral("%1%2")
                            .arg(QString::fromStdString(e.task_id))
                            .arg(e.failure_reason.empty() ? QString() : QStringLiteral(": %1").arg(QString::fromStdString(e.failure_reason))));
        break;
    case Type::DagCompleted:
        finishExecution();
        break;
    default:
        break;
    }
}

void GraphViewModel::stop()
{
    if (!executing_ || !executor_) return;
    emit logMessage(kLogWarn, "Cancelling execution...");
    executor_->cancel();
    finishExecution();
}

void GraphViewModel::finishExecution()
{
    if (!executing_) return;

    int completed = 0, failed = 0;
    imageResults_.clear();  // 清空上一轮结果
    QStringList imageKeys;
    if (executor_) {
        const auto results = executor_->get_results();
        for (const auto& [id, result] : results) {
            const bool ok = result.is_success();
            completed += ok ? 1 : 0;
            failed += ok ? 0 : 1;
            const double ms = std::chrono::duration<double, std::milli>(result.duration).count();
            emit logMessage(ok ? kLogInfo : kLogError,
                            QStringLiteral("%1  (%2 ms)")
                                .arg(QString::fromStdString(id))
                                .arg(ms, 0, 'f', 2));

            // 采集图像结果：多输出节点按端口，单输出(value)按 "out"
            if (ok) {
                const QString qid = QString::fromStdString(id);
                if (!result.outputs.empty()) {
                    for (const auto& [port, anyVal] : result.outputs) {
                        if (auto img = anyToQImage(anyVal)) {
                            QString key = qid + ":" + QString::fromStdString(port);
                            imageResults_[key] = std::move(*img);
                            imageKeys.append(key);
                        }
                    }
                } else if (auto img = anyToQImage(result.value)) {
                    QString key = qid + ":out";
                    imageResults_[key] = std::move(*img);
                    imageKeys.append(key);
                }
            }
        }
    }
    emit logMessage(kLogInfo, QStringLiteral("Execution finished: %1 ok, %2 failed")
                        .arg(completed)
                        .arg(failed));

    // 采集性能分析数据（executor 销毁前），存为一帧
    if (executor_) {
        const auto& profiler = executor_->profiler();
        const auto dagStats = profiler.compute_dag_stats();
        const auto taskStats = profiler.compute_task_stats();

        ProfileFrame frame;

        frame.dag.totalMs = std::chrono::duration<double, std::milli>(dagStats.total_duration).count();
        frame.dag.totalTasks = static_cast<int>(dagStats.total_tasks);
        frame.dag.completedTasks = static_cast<int>(dagStats.completed_tasks);
        frame.dag.failedTasks = static_cast<int>(dagStats.failed_tasks);
        frame.dag.skippedTasks = static_cast<int>(dagStats.skipped_tasks);
        frame.dag.criticalPathMs = std::chrono::duration<double, std::milli>(dagStats.critical_path).count();

        for (const auto& ts : taskStats) {
            ProfileTaskInfo info;
            info.taskId = QString::fromStdString(ts.task_id);
            info.taskType = QString::fromStdString(ts.task_type);
            info.waitMs = std::chrono::duration<double, std::milli>(ts.wait_duration).count();
            info.execMs = std::chrono::duration<double, std::milli>(ts.exec_duration).count();
            info.totalMs = std::chrono::duration<double, std::milli>(ts.total_duration).count();

            if (dagStats.has_start && ts.has_start) {
                info.startMs = std::chrono::duration<double, std::milli>(
                    ts.start_time - dagStats.start_time).count();
            }
            if (dagStats.has_start && ts.has_end) {
                info.endMs = std::chrono::duration<double, std::milli>(
                    ts.end_time - dagStats.start_time).count();
            }

            if (ts.final_status == task_graph::TaskStatus::COMPLETED) info.status = 0;
            else if (ts.final_status == task_graph::TaskStatus::FAILED) info.status = 1;
            else if (ts.final_status == task_graph::TaskStatus::SKIPPED) info.status = 2;
            else info.status = 1;

            frame.tasks.append(info);
        }

        frame.traceJson = QString::fromStdString(profiler.to_trace_string(false));
        frame.reportJson = QString::fromStdString(profiler.to_json_string(true));

        profileFrames_.append(frame);
        if (profileFrames_.size() > MAX_PROFILE_FRAMES) {
            profileFrames_.removeFirst();
        }
    }

    executing_ = false;
    emit executingChanged();
    emit executionFinished();

    if (!profileFrames_.isEmpty()) {
        emit profileDataReady(profileFrames_.size() - 1);
    }

    if (!imageKeys.isEmpty()) {
        emit imageResultsReady(imageKeys);
    }
}

bool GraphViewModel::canReach(const QString& from, const QString& to) const
{
    if (from == to) return true;

    QSet<QString> visited;
    QStack<QString> stack;
    stack.push(from);

    while (!stack.isEmpty()) {
        QString cur = stack.pop();
        if (cur == to) return true;
        if (visited.contains(cur)) continue;
        visited.insert(cur);

        for (const auto& e : model_.dag().outgoing_edges(cur.toStdString())) {
            QString next = QString::fromStdString(e.to);
            if (!visited.contains(next))
                stack.push(next);
        }
    }
    return false;
}

QStringList GraphViewModel::imageResultKeys() const
{
    return imageResults_.keys();
}

QImage GraphViewModel::imageResult(const QString& key) const
{
    auto it = imageResults_.constFind(key);
    return it != imageResults_.end() ? it.value() : QImage();
}

QString GraphViewModel::profileTraceJson() const
{
    if (profileFrames_.isEmpty()) return {};
    return profileFrames_.last().traceJson;
}

QString GraphViewModel::profileReportJson() const
{
    if (profileFrames_.isEmpty()) return {};
    return profileFrames_.last().reportJson;
}

const GraphViewModel::ProfileFrame* GraphViewModel::profileFrame(int index) const
{
    if (index < 0 || index >= profileFrames_.size()) return nullptr;
    return &profileFrames_[index];
}

GraphViewModel::ProfileFrame GraphViewModel::profileAverage() const
{
    ProfileFrame avg;
    if (profileFrames_.isEmpty()) return avg;

    int n = profileFrames_.size();

    // Average DAG stats
    double sumTotal = 0, sumCritical = 0;
    int sumTasks = 0, sumCompleted = 0, sumFailed = 0, sumSkipped = 0;
    for (const auto& f : profileFrames_) {
        sumTotal += f.dag.totalMs;
        sumCritical += f.dag.criticalPathMs;
        sumTasks += f.dag.totalTasks;
        sumCompleted += f.dag.completedTasks;
        sumFailed += f.dag.failedTasks;
        sumSkipped += f.dag.skippedTasks;
    }
    avg.dag.totalMs = sumTotal / n;
    avg.dag.criticalPathMs = sumCritical / n;
    avg.dag.totalTasks = sumTasks / n;
    avg.dag.completedTasks = sumCompleted / n;
    avg.dag.failedTasks = sumFailed / n;
    avg.dag.skippedTasks = sumSkipped / n;

    // Average per-task stats: match by taskId across frames
    // Use first frame's task list as template, average matching tasks from all frames
    const auto& templateFrame = profileFrames_.first();
    for (const auto& tmplTask : templateFrame.tasks) {
        ProfileTaskInfo avgTask;
        avgTask.taskId = tmplTask.taskId;
        avgTask.taskType = tmplTask.taskType;
        avgTask.status = tmplTask.status;

        double sumWait = 0, sumExec = 0, sumTotal = 0, sumStart = 0, sumEnd = 0;
        int count = 0;
        for (const auto& f : profileFrames_) {
            for (const auto& t : f.tasks) {
                if (t.taskId == tmplTask.taskId) {
                    sumWait += t.waitMs;
                    sumExec += t.execMs;
                    sumTotal += t.totalMs;
                    sumStart += t.startMs;
                    sumEnd += t.endMs;
                    ++count;
                    break;
                }
            }
        }
        if (count > 0) {
            avgTask.waitMs = sumWait / count;
            avgTask.execMs = sumExec / count;
            avgTask.totalMs = sumTotal / count;
            avgTask.startMs = sumStart / count;
            avgTask.endMs = sumEnd / count;
        }
        avg.tasks.append(avgTask);
    }

    return avg;
}

void GraphViewModel::clearProfileHistory()
{
    profileFrames_.clear();
}
