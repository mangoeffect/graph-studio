#include "test_hooks.h"

// 本文件整体为 WASM 专属（桌面 no-op）。E2E 驱动契约见 scripts/e2e_wasm/。
#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <cstdio>
#include <fstream>
#include <QString>

#include "viewmodel/GraphViewModel.h"
#include "view/MainWindow.h"

namespace graph_studio {

namespace {

GraphViewModel* g_vm = nullptr;
MainWindow* g_window = nullptr;

// JS<->C++ 字符串经 MEMFS 固定路径交换（Qt wasm 构建的
// EXPORTED_RUNTIME_METHODS 不含 stringToUTF8/UTF8ToString/HEAPU8，但导出
// 了完整 FS 对象；malloc/free 虽有导出，JS 侧仍无堆视图可写 bytes）。
// 调用全部落主线程（Qt wasm 事件循环所在线程），无需加锁。

constexpr const char* kInPath = "/tmp/gs_e2e_in.txt";
constexpr const char* kOutPath = "/tmp/gs_e2e_out.txt";

std::string read_in()
{
    std::ifstream f(kInPath, std::ios::binary);
    return f ? std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>())
             : std::string();
}

void write_out(const std::string& s)
{
    std::ofstream f(kOutPath, std::ios::binary | std::ios::trunc);
    f << s;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int gs_test_ready()
{
    return g_vm && g_window ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int gs_test_task_count()
{
    return g_vm ? g_vm->taskCount() : -1;
}

EMSCRIPTEN_KEEPALIVE int gs_test_edge_count()
{
    return g_vm ? g_vm->edgeCount() : -1;
}

EMSCRIPTEN_KEEPALIVE int gs_test_executing()
{
    return g_vm && g_vm->isExecuting() ? 1 : 0;
}

// out: 逗号分隔的全部节点 id（E2E 调试用）
EMSCRIPTEN_KEEPALIVE int gs_test_list_nodes()
{
    if (!g_vm) return 0;
    QStringList ids;
    for (const auto& n : g_vm->nodes()) ids << n.id;
    write_out(ids.join(",").toStdString());
    return 1;
}

// out: 逗号分隔的 MainWindow 场景侧节点 id（与 VM 侧对照，E2E 调试用）
EMSCRIPTEN_KEEPALIVE int gs_test_list_scene_nodes()
{
    if (!g_window) return 0;
    write_out(g_window->sceneNodeIds().join(",").toStdString());
    return 1;
}

// in: "<nodeId>\x1f<port>"；out: "<x>,<y>,<w>,<h>,<cx>,<cy>"（端口锚点的
// viewport 像素坐标 + 节点尺寸 + 节点中心）或 "error: ..."。返回 1 成功 / 0 失败。
EMSCRIPTEN_KEEPALIVE int gs_test_node_anchor()
{
    if (!g_vm || !g_window) return 0;
    const QString payload = QString::fromUtf8(read_in());
    const QStringList parts = payload.split(QChar('\x1f'));
    if (parts.size() != 2) {
        write_out("error: payload must be <id>\\x1f<port>");
        return 0;
    }
    const auto pos = g_window->nodePortAnchor(parts[0], parts[1]);
    if (pos.x < 0) {
        write_out("error: node/port not found");
        return 0;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
             pos.x, pos.y, pos.w, pos.h, pos.cx, pos.cy);
    write_out(buf);
    return 1;
}

// in: graph JSON（可选 "\x1f<baseDir>" 后缀）。返回 1 成功 / 0 失败。
// 桥接调用不在 Qt 事件循环内；E2E 驱动在轮询状态前会等待事件分派。
EMSCRIPTEN_KEEPALIVE int gs_test_load_graph()
{
    if (!g_vm) return 0;
    QString payload = QString::fromUtf8(read_in());
    QString baseDir;
    const int sep = payload.indexOf(QChar('\x1f'));
    if (sep >= 0) {
        baseDir = payload.mid(sep + 1);
        payload = payload.left(sep);
    }
    return g_vm->loadFromString(payload, baseDir) ? 1 : 0;
}

// in: 动作名。execute / stop / undo / redo / delete。
EMSCRIPTEN_KEEPALIVE int gs_test_action()
{
    if (!g_vm || !g_window) return 0;
    const QString action = QString::fromUtf8(read_in());
    if (action == "execute") { g_vm->execute(); return 1; }
    if (action == "stop") { g_vm->stop(); return 1; }
    if (action == "undo") { return g_window->triggerAction("Undo") ? 1 : 0; }
    if (action == "redo") { return g_window->triggerAction("Redo") ? 1 : 0; }
    if (action == "delete") { return g_window->triggerAction("Delete") ? 1 : 0; }
    write_out("error: unknown action " + action.toStdString());
    return 0;
}

// in: 任务类型名。out: paramSpecs 的 JSON 数组（含 UI 显隐/联动提示字段
// hidden/visibleWhen/visibleWhenValues/resetOnLinkChange——E2E 断言属性面板
// 联动约定用，如 face/matting 的 model_path 隐藏 + backend 联动）。
EMSCRIPTEN_KEEPALIVE int gs_test_param_specs()
{
    if (!g_vm) return 0;
    const QString type = QString::fromUtf8(read_in());
    const QVariantList specs = g_vm->paramSpecs(type);
    // 手拼 JSON（ QVariant 序列化无内置 JSON 路径，字段集小且稳定）
    std::string out = "[";
    bool first = true;
    for (const QVariant& v : specs) {
        const QVariantMap m = v.toMap();
        if (!first) out += ",";
        first = false;
        out += "{\"name\":\"" + m.value("name").toString().toStdString() + "\"";
        out += ",\"type\":\"" + m.value("type").toString().toStdString() + "\"";
        if (m.contains("hidden")) out += ",\"hidden\":true";
        if (m.contains("visibleWhen"))
            out += ",\"visibleWhen\":\""
                   + m.value("visibleWhen").toString().toStdString() + "\"";
        if (m.contains("visibleWhenValues")) {
            out += ",\"visibleWhenValues\":[";
            const QVariantList vals = m.value("visibleWhenValues").toList();
            bool fv = true;
            for (const QVariant& x : vals) {
                if (!fv) out += ",";
                fv = false;
                out += std::to_string(x.toInt());
            }
            out += "]";
        }
        if (m.contains("resetOnLinkChange")) out += ",\"resetOnLinkChange\":true";
        out += "}";
    }
    out += "]";
    write_out(out);
    return 1;
}

} // extern "C"

} // namespace graph_studio

namespace graph_studio {

void InstallTestHooks(GraphViewModel& vm, MainWindow& window)
{
    g_vm = &vm;
    g_window = &window;

    // 把 C 导出包装成 window.__gsTest 门面。EM_ASM 代码内联在 emscripten
    // 胶水里，闭包内直接引用 Module（MODULARIZE 工厂作用域实例）；等 FS
    // 运行时方法就绪后安装。字符串收发走 MEMFS（见上注释）。
    EM_ASM({
        var waitForModule = setInterval(function() {
            if (typeof Module === 'undefined' || !Module.FS
                || !Module.FS.writeFile || !Module.FS.readFile
                || !Module._gs_test_ready) return;
            clearInterval(waitForModule);
            var dec = new TextDecoder('utf-8');
            var enc = new TextEncoder();
            var putIn = function(s) {
                Module.FS.writeFile('/tmp/gs_e2e_in.txt', enc.encode(s));
            };
            var getOut = function() {
                try {
                    return dec.decode(Module.FS.readFile('/tmp/gs_e2e_out.txt'));
                } catch (e) { return ''; }
            };
            window.__gsTest = {
                ready: function() { return Module._gs_test_ready() === 1; },
                taskCount: function() { return Module._gs_test_task_count(); },
                edgeCount: function() { return Module._gs_test_edge_count(); },
                executing: function() { return Module._gs_test_executing() === 1; },
                listNodes: function() {
                    if (Module._gs_test_list_nodes() !== 1) return null;
                    return getOut();
                },
                listSceneNodes: function() {
                    if (Module._gs_test_list_scene_nodes() !== 1) return null;
                    return getOut();
                },
                // E2E 辅助：向 MEMFS 写字节（如执行输入图）。path 需以 / 开头。
                fsWrite: function(path, bytes) {
                    Module.FS.writeFile(path, bytes);
                },
                loadGraph: function(json, baseDir) {
                    putIn(json + (baseDir ? '\x1f' + baseDir : ''));
                    return Module._gs_test_load_graph() === 1;
                },
                // 返回 {x,y,w,h,cx,cy}：画布 viewport 坐标（= canvas CSS 像素）
                nodeAnchor: function(id, port) {
                    putIn(id + '\x1f' + port);
                    if (Module._gs_test_node_anchor() !== 1) return null;
                    var parts = getOut().split(',');
                    return { x: parseFloat(parts[0]), y: parseFloat(parts[1]),
                             w: parseFloat(parts[2]), h: parseFloat(parts[3]),
                             cx: parseFloat(parts[4]), cy: parseFloat(parts[5]) };
                },
                action: function(name) {
                    putIn(name);
                    return Module._gs_test_action() === 1;
                },
                // paramSpecs（含 hidden/visibleWhen 族字段）的 JSON 数组
                paramSpecs: function(type) {
                    putIn(type);
                    if (Module._gs_test_param_specs() !== 1) return null;
                    try { return JSON.parse(getOut()); } catch (e) { return null; }
                },
                lastError: getOut
            };
        }, 100);
    });
}

} // namespace graph_studio

#else // !__EMSCRIPTEN__

namespace graph_studio {
void InstallTestHooks(GraphViewModel&, MainWindow&) {}
} // namespace graph_studio

#endif
