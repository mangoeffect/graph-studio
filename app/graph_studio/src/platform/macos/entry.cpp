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
    // 可选第三段（drop 通道写入）：拖入时缺失的资产清单，\n 分隔——打进
    // 日志面板（JS 的 console.warn 用户平时不看，缺资产要在 app 内可见）。
    QString missingAssets;
    const auto sep2 = payload.find('\x1f', sep + 2);
    if (sep2 != std::string::npos)
        missingAssets = QString::fromUtf8(payload.c_str() + sep2 + 1,
                                          int(payload.size() - sep2 - 1));
    std::remove(kUrlOpenPath);
    const bool ok = g_urlOpenWindow->OpenGraphAtStartup(path, run);
    if (!missingAssets.isEmpty())
        g_urlOpenWindow->PostLog(
            int(task_graph::LogLevel::WARN),
            "Graph references files not dropped (drop the graph together "
            "with its assets, or drop the containing folder): "
                + missingAssets.replace(QLatin1Char('\n'), QStringLiteral(", ")));
    return ok ? 1 : 0;
}

// .tgp 工程包打开通道（drop/URL 两通道共用；工具栏同进程直调
// OpenProjectFile）：包字节从 /tmp/gs_project_open.bin 读，run 标志复用
// /tmp/gs_url_open.txt 交换（"\x1f0|1"，path 段为空；文件缺失视为 run=0），
// 展示名可选经 /tmp/gs_project_display.txt 传入（交换文件名不该露给用户）。
// 解包进会话临时目录并加载。返回 1 成功 / 0 失败。
extern "C" EMSCRIPTEN_KEEPALIVE int gs_wasm_open_project()
{
    if (!g_urlOpenWindow) return 0;
    bool run = false;
    std::ifstream flag(kUrlOpenPath, std::ios::binary);
    if (flag) {
        const std::string payload((std::istreambuf_iterator<char>(flag)),
                                  std::istreambuf_iterator<char>());
        const auto sep = payload.find('\x1f');
        if (sep != std::string::npos && sep + 1 < payload.size())
            run = payload[sep + 1] == '1';
        std::remove(kUrlOpenPath);
    }
    QString displayName;
    std::ifstream nameFile("/tmp/gs_project_display.txt", std::ios::binary);
    if (nameFile) {
        const std::string name((std::istreambuf_iterator<char>(nameFile)),
                               std::istreambuf_iterator<char>());
        displayName = QString::fromStdString(name);
        std::remove("/tmp/gs_project_display.txt");
    }
    const bool ok = g_urlOpenWindow->OpenProjectFile("/tmp/gs_project_open.bin",
                                                     run, displayName);
    std::remove("/tmp/gs_project_open.bin");
    return ok ? 1 : 0;
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

    // 包内默认模型后台预取：fetch models/manifest.json → 逐个拉取写入
    // MEMFS /models（ModelBootstrap 在 wasm 下把该目录注册进 ModelFinder，
    // face/matting 等任务按默认模型名命中）。与 UI 启动并行、不阻塞；
    // 失败仅告警（--skip-models 包 / dev 未 staging 时任务侧报可读错误）。
    // 就绪 promise 挂在 window.__gsModelsReady，?open 立即执行流等它落盘。
    EM_ASM({
        var waitForModels = setInterval(function() {
            if (typeof Module === 'undefined' || !Module.FS
                || !Module.FS.writeFile) return;
            clearInterval(waitForModels);
            window.__gsModelsReady = fetch('models/manifest.json')
                .then(function(r) {
                    if (!r.ok) throw new Error('HTTP ' + r.status);
                    return r.json();
                }).then(function(list) {
                    try { Module.FS.mkdir('/models'); } catch (e) {}
                    var jobs = (list || []).map(function(m) {
                        return fetch('models/' + m.name).then(function(r) {
                            if (!r.ok) throw new Error('HTTP ' + r.status);
                            return r.arrayBuffer();
                        }).then(function(buf) {
                            Module.FS.writeFile('/models/' + m.name,
                                                new Uint8Array(buf));
                        }).catch(function(e) {
                            console.warn('[gs] 模型拉取失败: ' + m.name
                                         + ': ' + e.message);
                        });
                    });
                    return Promise.all(jobs);
                }).catch(function(e) {
                    console.warn('[gs] models/manifest.json 不可用'
                                 + '（--skip-models 包或 dev 未 staging）: '
                                 + e.message);
                });
        }, 50);
    });

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
                return r.arrayBuffer();
            }).then(function(ab) {
                var u8 = new Uint8Array(ab);
                // .tgp 工程包（.tgp 扩展名或 PK 魔数）：单包自带全部依赖，
                // 无需相对资产预取，直接走工程打开通道。
                var isProj = name.toLowerCase().endsWith('.tgp')
                    || (u8.length >= 4 && u8[0] === 0x50 && u8[1] === 0x4b
                        && ((u8[2] === 3 && u8[3] === 4)
                            || (u8[2] === 5 && u8[3] === 6)
                            || (u8[2] === 7 && u8[3] === 8)));
                if (isProj) {
                    Module.FS.writeFile('/tmp/gs_project_open.bin', u8);
                    Module.FS.writeFile('/tmp/gs_url_open.txt',
                                        '\x1f' + (run ? '1' : '0'));
                    Module.FS.writeFile('/tmp/gs_project_display.txt', name);
                    if (Module._gs_wasm_open_project() !== 1)
                        console.error('[gs] ?open 工程包打开失败: ' + open);
                    return;
                }
                var text = new TextDecoder().decode(u8);
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
                // run=1 立即执行前等默认模型落盘（face/matting 参数留空时
                // 依赖 MEMFS /models）；拉取本身失败不阻塞打开。
                var pre = window.__gsModelsReady || Promise.resolve();
                return pre.then(function() {
                    Module.FS.writeFile('/tmp/gs_url_open.txt',
                                        name + '\x1f' + (run ? '1' : '0'));
                    if (Module._gs_wasm_open_graph() !== 1)
                        console.error('[gs] ?open 打开失败: ' + open);
                });
            }).catch(function(e) {
                console.error('[gs] ?open 拉取失败: ' + open + ': ' + e.message);
            });
        }, 100);
    });

    // OS 文件拖入（graph.json + 资产 / 图所在文件夹）：Qt 6.6 wasm 平台对
    // 浏览器文件 drop 的交付不可靠（拖入文件的字节需异步进 MEMFS，Qt 无同步
    // 读取通道），在 document 捕获阶段自行处理——与 ?open 通道同一套 MEMFS
    // 交换语义。dragover 必须 preventDefault 否则浏览器按"打开文件"导航离开
    // 页面；stopImmediatePropagation 阻止 Qt 平台层再转一份 QDropEvent（避免
    // 与 MainWindow::dropEvent 双开）。同 EM_ASM 约定：JS 里不能出现正则
    // 字面量。
    //
    // 资产落位（"拖图后图片路径读不出"的根因是平铺进 MEMFS 根，而图里引用
    // 的是相对路径 data/test.png、甚至 configure_file 烘焙的宿主机绝对路径）：
    //   - 文件夹拖入：dataTransfer.files 不含目录，走 items[].webkitGetAsEntry
    //     递归遍历，整树按原相对结构落 MEMFS——树内图 _source_dir 即其所在
    //     目录，resolve_asset_path 的"图目录+两级祖先"探测天然命中（与源码
    //     树直接拖 json 进桌面 Studio 同构）。树内首个（按名排序）json 当图。
    //   - 松散多文件：第一个 .json 当图（显式意图优先于树内图），其余资产按
    //     basename 与图内引用配对写到引用所指路径（相对 → /+引用；绝对 →
    //     原绝对路径，MEMFS 可承载），未配对才落根。
    // 缺失的引用清单经交换文件第三段带回 C++ 打进日志面板（console.warn
    // 用户平时不看）。遍历/配对算法暴露在 window.__gsDrop 供 E2E 复测——
    // 合成 DragEvent 造不出 FileSystemEntry，算法只能这样单测。
    EM_ASM({
        var waitForModule = setInterval(function() {
            if (typeof Module === 'undefined' || !Module.FS
                || !Module.FS.writeFile || !Module._gs_wasm_open_graph) return;
            clearInterval(waitForModule);
            var ASSET_EXT = ['.png', '.jpg', '.jpeg', '.webp', '.bmp',
                             '.mp4', '.avi', '.mov', '.js', '.json',
                             '.cube', '.task', '.tflite', '.mnn',
                             '.onnx', '.metal', '.vert', '.frag', '.wgsl'];
            // 与 ?open 预取同源的启发式（scripts/e2e_graph_cases.py 对齐），
            // 额外收绝对路径（拖入的本地文件可原样落进 MEMFS）；排除
            // writer 任务的出参（file_path/out_path）与 URL。
            function collectRefs(text) {
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
                            if (v.indexOf('://') >= 0) return;
                            var looksPath = v.charAt(0) === '/'
                                || v.indexOf('/') >= 0
                                || ASSET_EXT.some(function(ext) {
                                    return v.toLowerCase().endsWith(ext);
                                });
                            if (!looksPath) return;
                            if (writer && (k === 'file_path'
                                           || k === 'out_path')) return;
                            if (refs.indexOf(v) < 0) refs.push(v);
                        });
                    });
                } catch (err) { /* 非 JSON：无引用可配对 */ }
                return refs;
            }
            function fsExists(p) {
                try { return Module.FS.analyzePath(p).exists; }
                catch (err) { return false; }
            }
            function parentOf(p) {
                if (p === '/' || p === '') return p;
                var q = p.slice(0, p.lastIndexOf('/'));
                return q === '' ? '/' : q;
            }
            // 镜像 C++ resolve_asset_path 的探测序（图目录+两级祖先，
            // 绝对引用原样探测），判定引用在本次拖入后是否仍缺失。
            function refMissing(graphDir, ref) {
                if (ref.charAt(0) === '/') return !fsExists(ref);
                var base = graphDir;
                for (var up = 0; up < 3 && base; ++up) {
                    if (fsExists((base === '/' ? '' : base) + '/' + ref))
                        return false;
                    var parent = parentOf(base);
                    if (parent === base) break;
                    base = parent;
                }
                return true;
            }
            // readEntries 每批最多 100 项，须读到空批为止
            function readAllEntries(reader) {
                return new Promise(function(res, rej) {
                    var all = [];
                    var step = function() {
                        reader.readEntries(function(batch) {
                            if (!batch.length) { res(all); return; }
                            for (var i = 0; i < batch.length; ++i)
                                all.push(batch[i]);
                            step();
                        }, rej);
                    };
                    step();
                });
            }
            // 目录树递归 -> [{name, path(根下相对路径), file}]；
            // 每层按名排序保证"首个 json"确定性。
            function walkEntry(entry, prefix) {
                if (entry.isFile) {
                    return new Promise(function(res, rej) {
                        entry.file(function(f) {
                            res([{name: entry.name,
                                  path: prefix + entry.name, file: f}]);
                        }, rej);
                    });
                }
                return readAllEntries(entry.createReader()).then(function(kids) {
                    kids.sort(function(a, b) {
                        return a.name < b.name ? -1
                             : (a.name > b.name ? 1 : 0);
                    });
                    return Promise.all(kids.map(function(k) {
                        return walkEntry(k, prefix + entry.name + '/');
                    })).then(function(lists) {
                        var out = [];
                        lists.forEach(function(l) { out = out.concat(l); });
                        return out;
                    });
                });
            }
            function memfsPath(ref) {
                return ref.charAt(0) === '/' ? ref : '/' + ref;
            }
            function ensureDir(path) {
                var dir = path.slice(0, path.lastIndexOf('/'));
                if (dir.length > 1) {
                    try {
                        Module.FS.createPath('/', dir.slice(1), true, true);
                    } catch (err) { /* 已存在 */ }
                }
            }
            function writeBytes(path, data) {
                // FS.writeFile 只收 Uint8Array/string，ArrayBuffer 须包一层
                //（否则抛 "Unsupported data type"）
                var bytes = (typeof data === 'string' || data instanceof Uint8Array)
                          ? data : new Uint8Array(data);
                ensureDir(path);
                Module.FS.writeFile(path, bytes);
            }
            window.__gsDrop = {
                walk: walkEntry, collectRefs: collectRefs,
                refMissing: refMissing
            };
            document.addEventListener('dragenter', function(e) {
                e.preventDefault();
            }, true);
            document.addEventListener('dragover', function(e) {
                e.preventDefault();
            }, true);
            document.addEventListener('drop', function(e) {
                e.preventDefault();
                e.stopImmediatePropagation();
                // 事件返回后 DataTransferItemList 会 detach，目录项与
                // File 都须同步收集；files[] 不含目录（文件夹拖入全靠
                // entries），file entry 与 files[] 一一对应、只从 files[] 读
                //（避免双写）。
                var loose = [];
                var dirRoots = [];
                for (var i = 0; i < e.dataTransfer.files.length; ++i)
                    loose.push(e.dataTransfer.files[i]);
                var items = e.dataTransfer.items;
                if (items) {
                    for (var j = 0; j < items.length; ++j) {
                        var en = null;
                        try {
                            en = items[j].webkitGetAsEntry
                              && items[j].webkitGetAsEntry();
                        } catch (err0) { en = null; }
                        if (en && en.isDirectory) dirRoots.push(en);
                    }
                }
                if (!loose.length && !dirRoots.length) return;
                // 松散 .tgp 工程包：自带全部依赖，直接走工程打开通道并短路
                // （不参与 json 选择与资产配对）。
                var looseProject = null;
                for (var pi = 0; pi < loose.length; ++pi) {
                    if (loose[pi].name.toLowerCase().endsWith('.tgp')) {
                        looseProject = loose[pi];
                        break;
                    }
                }
                if (looseProject) {
                    looseProject.arrayBuffer().then(function(buf) {
                        // FS.writeFile 只收 Uint8Array/string（ArrayBuffer 抛
                        // "Unsupported data type"）
                        Module.FS.writeFile('/tmp/gs_project_open.bin',
                                            new Uint8Array(buf));
                        Module.FS.writeFile('/tmp/gs_project_display.txt',
                                            looseProject.name);
                        if (Module._gs_wasm_open_project() !== 1)
                            console.error('[gs] 拖入工程包打开失败: '
                                          + looseProject.name);
                    });
                    return;
                }
                var looseGraph = null;
                var looseAssets = [];
                loose.forEach(function(f) {
                    if (!looseGraph
                        && f.name.toLowerCase().endsWith('.json')) {
                        looseGraph = f;
                        return;
                    }
                    looseAssets.push(f);
                });
                Promise.all(dirRoots.map(function(r) {
                    return walkEntry(r, '').catch(function() { return []; });
                })).then(function(lists) {
                    var treeFiles = [];
                    lists.forEach(function(l) {
                        treeFiles = treeFiles.concat(l);
                    });
                    // 图选择：显式松散 json 优先；否则树内首个（排序序）json
                    var graphPath = null;
                    var graphTextPromise = null;
                    if (looseGraph) {
                        graphPath = '/' + looseGraph.name;
                        graphTextPromise = looseGraph.text();
                    } else {
                        for (var k = 0; k < treeFiles.length; ++k) {
                            if (treeFiles[k].name.toLowerCase()
                                    .endsWith('.json')) {
                                graphPath = '/' + treeFiles[k].path;
                                graphTextPromise = treeFiles[k].file.text();
                                break;
                            }
                        }
                    }
                    var treeWrites = treeFiles.map(function(rec) {
                        return rec.file.arrayBuffer().then(function(buf) {
                            writeBytes('/' + rec.path, buf);
                        });
                    });
                    return Promise.all(treeWrites)
                        .then(function() { return graphTextPromise; })
                        .then(function(text) {
                        var refs = text ? collectRefs(text) : [];
                        var used = [];
                        var looseWrites = looseAssets.map(function(f) {
                            return f.arrayBuffer().then(function(buf) {
                                var target = null;
                                for (var r = 0; r < refs.length; ++r) {
                                    if (used.indexOf(r) >= 0) continue;
                                    var base = refs[r].slice(
                                        refs[r].lastIndexOf('/') + 1);
                                    if (base === f.name
                                        || base.toLowerCase()
                                               === f.name.toLowerCase()) {
                                        target = memfsPath(refs[r]);
                                        used.push(r);
                                        break;
                                    }
                                }
                                if (target === null) target = '/' + f.name;
                                writeBytes(target, buf);
                            });
                        });
                        return Promise.all(looseWrites).then(function() {
                            if (!text) return;  // 只拖资产/无 json：落位即可
                            writeBytes(graphPath, text);
                            var graphDir = graphPath.slice(
                                0, graphPath.lastIndexOf('/')) || '/';
                            var missing = refs.filter(function(ref) {
                                return refMissing(graphDir, ref);
                            });
                            var exchange = graphPath + '\x1f0';
                            if (missing.length) {
                                console.warn('[gs] 拖入缺少资产文件（与图一并'
                                             + '拖入或拖入所在文件夹即可）: '
                                             + missing.join(', '));
                                exchange += '\x1f' + missing.join('\n');
                            }
                            Module.FS.writeFile('/tmp/gs_url_open.txt',
                                                exchange);
                            if (Module._gs_wasm_open_graph() !== 1)
                                console.error('[gs] 拖入打开失败: '
                                              + graphPath);
                        });
                    });
                }).catch(function(err) {
                    console.error('[gs] 拖入文件读取失败: '
                                  + (err && err.message ? err.message : err));
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
