#include <QApplication>
#include <QMainWindow>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QSurfaceFormat>
#include <QTimer>

#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "view/MainWindow.h"
#include "PluginBootstrap.h"
#include "GpuBootstrap.h"
#include "ModelBootstrap.h"
#include "CrashReporter.h"
#ifdef __EMSCRIPTEN__
#include "../wasm/test_hooks.h"
#endif

#include <cstring>
#include <cstdlib>
#include <cstdio>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <fstream>
#include <iterator>
#endif

using namespace graph_studio;

#ifdef __EMSCRIPTEN__
// ?open=<url>[&run=1] 的 C 入口（EM_ASM 闭包里经 MEMFS 交换调用）：
// 图与相对资产已由 JS 预取进 MEMFS 根，这里走与桌面 --open/--run 完全
// 相同的打开路径（OpenGraphAtStartup，含标题更新）。
namespace {
MainWindow* g_urlOpenWindow = nullptr;
constexpr const char* kUrlOpenPath = "/tmp/gs_url_open.txt";
}

extern "C" EMSCRIPTEN_KEEPALIVE int gs_wasm_open_graph()
{
    if (!g_urlOpenWindow) return 0;
    std::ifstream f(kUrlOpenPath, std::ios::binary);
    if (!f) return 0;
    const std::string payload((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    const auto sep = payload.find('\x1f');
    if (sep == std::string::npos || sep + 2 > payload.size()) return 0;
    const QString path = QString::fromUtf8(payload.c_str(), int(sep));
    const bool run = payload[sep + 1] == '1';
    std::remove(kUrlOpenPath);
    return g_urlOpenWindow->OpenGraphAtStartup(path, run) ? 1 : 0;
}
#endif

int main(int argc, char* argv[])
{
#ifndef __EMSCRIPTEN__
    // 启动崩溃上报（Sentry + crashpad）。在最早期调用以覆盖启动期间崩溃；
    // 未构建 sentry（third_party 缺失）或 SENTRY_DSN 未设置时为 no-op。
    InitCrashReporting();

    // --test-crash：人为触发真实崩溃，验证 crashpad minidump 能上报。
    // 用法：SENTRY_DSN=... graph_studio --test-crash
    bool test_crash = false;
    // --open <graph.json> [--run]：启动即打开（可选立即执行）。macOS E2E
    // 的图输入通道——NSOpenPanel 的自动化极不稳定（见 dev-docs/e2e-macos.md），
    // 命令行传图绕开文件选择器；失败只记日志不弹窗。
    QString openPath;
    bool openRun = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test-crash") == 0) {
            test_crash = true;
        } else if (std::strcmp(argv[i], "--open") == 0 && i + 1 < argc) {
            openPath = QString::fromUtf8(argv[++i]);   // macOS argv 恒 UTF-8
        } else if (std::strcmp(argv[i], "--run") == 0) {
            openRun = true;
        }
    }
#endif

    QApplication app(argc, argv);

#ifndef __EMSCRIPTEN__
    if (test_crash)
        TriggerTestCrash();
#endif

    QIcon appIcon;
    appIcon.addFile(":/icons/app_icon_16.png", QSize(16, 16));
    appIcon.addFile(":/icons/app_icon_32.png", QSize(32, 32));
    appIcon.addFile(":/icons/app_icon_48.png", QSize(48, 48));
    appIcon.addFile(":/icons/app_icon_64.png", QSize(64, 64));
    appIcon.addFile(":/icons/app_icon_128.png", QSize(128, 128));
    appIcon.addFile(":/icons/app_icon_256.png", QSize(256, 256));
    appIcon.addFile(":/icons/app_icon_512.png", QSize(512, 512));
    app.setWindowIcon(appIcon);

    GraphModel model;
    GraphViewModel vm(model);

    // 启动期加载内置插件（subnode 构建产物 + env + .app/PlugIns），
    // 让任务库面板/右键菜单能拿到完整可用 task 列表。
    // 放在 ViewModel 之后：此时日志 sink 已注册，插件加载日志能进 log 面板。
    LoadBuiltinPlugins();

    // 初始化 GPU backend（macOS->Metal），让 gpu_* 节点可用。
    // fail-open：init 失败仅记 WARN，非 GPU 节点仍可执行。
    InitGpuBackend();

    // 安装全局 ModelFinder：任务参数里只填模型名，这里从 models 目录
    //（env / 打包布局）解析出文件路径。fail-open：无 models 目录时名称
    // 回退图相对路径。需在 QApplication 之后（依赖 applicationDirPath）。
    InitModelFinder();

    MainWindow window(vm);

#ifdef __EMSCRIPTEN__
    // 浏览器 E2E 测试桥（window.__gsTest），桌面构建为 no-op
    InstallTestHooks(vm, window);

    // ?open=<url>[&run=1]：WASM 侧对齐桌面 --open/--run 的图输入通道
    //（可分享的图链接 + 浏览器 E2E 免文件选择器自动化）。JS 侧 fetch 图
    // 与相对路径资产进 MEMFS 根（与桌面落位同语义：相对引用按图所在目录
    // 解析），再经 MEMFS 交换调 gs_wasm_open_graph。字符串交换而非 ccall
    // 的原因见 test_hooks.cpp 头注释（EXPORTED_RUNTIME_METHODS 不含
    // stringToUTF8）。Module/FS 就绪时机不定，与 test_hooks 同款轮询。
    g_urlOpenWindow = &window;
    // 注意：EM_ASM 的 JS 里不能出现正则字面量——C 预处理器不认识正则，
    // /^\// 或 \. 会被误lex成行注释/杂散token 吞掉后面的右括号，报
    // "unterminated function-like macro invocation"。路径判断用 endsWith。
    EM_ASM({
        var waitForModule = setInterval(function() {
            if (typeof Module === 'undefined' || !Module.FS
                || !Module.FS.writeFile || !Module._gs_wasm_open_graph) return;
            clearInterval(waitForModule);
            var params = new URLSearchParams(location.search);
            var open = params.get('open');
            if (!open) return;
            var run = params.get('run') === '1';
            var clean = open.split('#')[0].split('?')[0];
            var name = clean.split('/').pop() || 'graph.json';
            var dir = clean.slice(0, clean.lastIndexOf('/') + 1);
            fetch(open).then(function(r) {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                return r.text();
            }).then(function(text) {
                Module.FS.writeFile('/' + name, text);
                // 相对路径资产预取（启发式与 scripts/e2e_graph_cases.py /
                // 桌面落位一致：路径形态 + 资产扩展名，写出型任务的
                // file_path/out_path 除外）。单项失败只告警，交给执行期暴露。
                var ASSET_EXT = ['.png', '.jpg', '.jpeg', '.webp', '.bmp',
                                 '.mp4', '.avi', '.mov', '.js', '.json',
                                 '.cube', '.task', '.tflite', '.mnn',
                                 '.onnx', '.metal', '.vert', '.frag', '.wgsl'];
                var refs = [];
                try {
                    var g = JSON.parse(text);
                    (g.tasks || []).forEach(function(t) {
                        var ty = t.type || '';
                        var writer = ty.endsWith('_write')
                                  || ty.endsWith('video_writer');
                        Object.keys(t.params || {}).forEach(function(k) {
                            var v = t.params[k];
                            if (typeof v !== 'string' || !v) return;
                            if (v.charAt(0) === '/' || v.indexOf('://') >= 0) return;
                            var looksPath = v.indexOf('/') >= 0
                                || ASSET_EXT.some(function(ext) {
                                    return v.toLowerCase().endsWith(ext);
                                });
                            if (!looksPath) return;
                            if (writer && (k === 'file_path' || k === 'out_path')) return;
                            if (refs.indexOf(v) < 0) refs.push(v);
                        });
                    });
                } catch (e) { /* 非 JSON：留给 C++ 侧 loadFromFile 报错 */ }
                return Promise.all(refs.map(function(ref) {
                    return fetch(dir + ref).then(function(r) {
                        if (!r.ok) throw new Error('HTTP ' + r.status);
                        return r.arrayBuffer();
                    }).then(function(buf) {
                        var path = '/' + ref;
                        var dirPart = path.slice(0, path.lastIndexOf('/'));
                        if (dirPart) Module.FS.createPath('/',
                            dirPart.slice(1), true, true);
                        Module.FS.writeFile(path, new Uint8Array(buf));
                    }).catch(function(e) {
                        console.warn('[gs] ?open 资产预取失败: ' + ref
                                     + ': ' + e.message);
                    });
                }));
            }).then(function() {
                Module.FS.writeFile('/tmp/gs_url_open.txt',
                                    name + '\x1f' + (run ? '1' : '0'));
                if (Module._gs_wasm_open_graph() !== 1)
                    console.error('[gs] ?open 打开失败: ' + open);
            }).catch(function(e) {
                console.error('[gs] ?open 拉取失败: ' + open + ': ' + e.message);
            });
        }, 100);
    });

    // OS 文件拖入（graph.json + 相对路径资产）：Qt 6.6 wasm 平台对浏览器
    // 文件 drop 的交付不可靠（拖入文件的字节需异步进 MEMFS，Qt 无同步读取
    // 通道），在 document 捕获阶段自行处理——与 ?open 通道同一套 MEMFS
    // 落位/交换语义。dragover 必须 preventDefault 否则浏览器按"打开文件"
    // 导航离开页面；stopImmediatePropagation 阻止 Qt 平台层再转一份
    // QDropEvent（避免与 MainWindow::dropEvent 双开）。同 EM_ASM 约定：
    // JS 里不能出现正则字面量。
    EM_ASM({
        var waitForModule = setInterval(function() {
            if (typeof Module === 'undefined' || !Module.FS
                || !Module.FS.writeFile || !Module._gs_wasm_open_graph) return;
            clearInterval(waitForModule);
            document.addEventListener('dragenter', function(e) {
                e.preventDefault();
            }, true);
            document.addEventListener('dragover', function(e) {
                e.preventDefault();
            }, true);
            document.addEventListener('drop', function(e) {
                e.preventDefault();
                e.stopImmediatePropagation();
                // 事件返回后 DataTransferItemList 会 detach，先同步收集 File
                var files = [];
                for (var i = 0; i < e.dataTransfer.files.length; ++i)
                    files.push(e.dataTransfer.files[i]);
                if (!files.length) return;
                // 第一个 .json 当图打开，其余按文件名落 MEMFS 根（资产，
                // 与 ?open 的预取同语义）；资产先写、图后写（与 ?open 同序）。
                var graphFile = null;
                var writes = [];
                files.forEach(function(f) {
                    if (!graphFile && f.name.toLowerCase().endsWith('.json')) {
                        graphFile = f;
                        return;
                    }
                    writes.push(f.arrayBuffer().then(function(buf) {
                        Module.FS.writeFile('/' + f.name, new Uint8Array(buf));
                    }));
                });
                Promise.all(writes).then(function() {
                    if (!graphFile) return null;
                    return graphFile.text();
                }).then(function(text) {
                    if (text === null || text === undefined) return;
                    var name = graphFile.name;
                    Module.FS.writeFile('/' + name, text);
                    Module.FS.writeFile('/tmp/gs_url_open.txt',
                                        name + '\x1f0');
                    if (Module._gs_wasm_open_graph() !== 1)
                        console.error('[gs] 拖入打开失败: ' + name);
                }).catch(function(err) {
                    console.error('[gs] 拖入文件读取失败: ' + err.message);
                });
            }, true);
        }, 100);
    });
#endif

    window.show();

#ifndef __EMSCRIPTEN__
    if (!openPath.isEmpty()) {
        // 等事件循环起来（场景/日志面板就绪）再打开；--run 时执行紧随其后
        QTimer::singleShot(0, &window, [&window, openPath, openRun]() {
            if (!window.OpenGraphAtStartup(openPath, openRun))
                std::fprintf(stderr, "[gs] --open failed: %s\n",
                             openPath.toUtf8().constData());
        });
    }
#endif

    int ret = app.exec();

    ShutdownModelFinder();
    ShutdownGpuBackend();
#ifndef __EMSCRIPTEN__
    ShutdownCrashReporting();
#endif
    return ret;
}
