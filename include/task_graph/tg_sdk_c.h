/* ============================================================================
 * tg_sdk_c.h - TaskGraph SDK 纯 C API
 *
 * C++ API(<task_graph/sdk.hpp> + <task_graph/dag_config.hpp>)的 C 包装:
 * 完整生命周期 create → init → load → bind → execute → update → shutdown。
 *
 * 约定:
 *  - 所有函数线程安全(内部委托 C++ SDK 的互斥语义),不抛异常,错误走
 *    tg_status 返回码;可读原因用 tg_sdk_last_error()。
 *  - 句柄生命周期归调用方:tg_sdk_destroy / tg_image_destroy 释放。
 *  - const char* 返回值(节点 id / diff / last_error)指向 SDK 内部缓存,
 *    保证到"下一次同族访问器调用或句柄销毁"前有效;跨调用持有请自行拷贝。
 *  - 拷贝式字符串出参(buf/buf_size)语义同 snprintf:返回含 NUL 的所需
 *    长度;buf 为 NULL 或不足时仍返回所需长度(不写入超过 buf_size 字节)。
 *  - 数值类型按 C++ TypeRegistry 注册面:int32 / double / const char* /
 *    bool / tg_image(Image)。绑定期即做 data_type 前置校验。
 *  - 推模式输出回调只做"就绪通知"(带 node_id);数据在回调内(或之后)用
 *    tg_sdk_get_output_* 拉取——回调内调用只读 get_output 是安全的,
 *    其余 SDK 方法一律禁止(与 C++ 契约一致)。
 * ============================================================================ */
#ifndef TASK_GRAPH_TG_SDK_C_H
#define TASK_GRAPH_TG_SDK_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 状态码(与 task_graph::SdkStatus 数值一致) ---- */
enum {
    TG_OK = 0,
    TG_ERR_NOT_INITIALIZED = 1,
    TG_ERR_ALREADY_INITIALIZED = 2,
    TG_ERR_GRAPH_NOT_LOADED = 3,
    TG_ERR_INVALID_ARGUMENT = 4,
    TG_ERR_TYPE_MISMATCH = 5,
    TG_ERR_GRAPH_INVALID = 6,
    TG_ERR_BUSY = 7,
    TG_ERR_INTERNAL = 8
};

/* 不透明句柄 */
typedef struct tg_sdk tg_sdk;     /* TaskGraphSdk 实例 */
typedef struct tg_image tg_image; /* CPU 侧 Image(像素缓冲共享,零拷贝传递) */

/* ---- 回调 ---- */
/* level: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=FATAL;任意线程触发 */
typedef void (*tg_log_fn)(void* user_data, int level, const char* msg,
                          const char* file, int line);
/* 输出就绪通知(推模式):数据用 tg_sdk_get_output_* 拉取;user 透传 */
typedef void (*tg_output_fn)(void* user_data, const char* node_id);

/* ---- 初始化配置(对应 C++ SdkConfig 的 C 子集;
 *      env/globals 用 init 后的 tg_sdk_set_env / tg_sdk_set_global_* 增补) ---- */
typedef struct {
    int log_level;            /* 0..5,默认 2 (INFO) */
    tg_log_fn log_callback;   /* 可 NULL;SDK 注销前有效 */
    void* log_user_data;
    size_t thread_pool_size;  /* 0 = 硬件并发 */
    int64_t default_timeout_ms;
    int enable_profiling;     /* 0/1 */
    int require_known_types;  /* 0/1:未知 task type 视为加载错误 */
} tg_sdk_config;

/* ====================== 生命周期 ====================== */

/* 创建实例(零副作用)。失败(内存不足)返回 NULL。 */
tg_sdk* tg_sdk_create(void);
/* 初始化(恰好一次)。cfg 可 NULL(全默认)。 */
int tg_sdk_init(tg_sdk* sdk, const tg_sdk_config* cfg);
int tg_sdk_is_initialized(const tg_sdk* sdk);
/* 幂等销毁:cancel + 等待在途执行 + 注销日志回调 + 释放全部。 */
int tg_sdk_shutdown(tg_sdk* sdk);
/* = shutdown + 释放句柄。句柄此后不可再用。 */
void tg_sdk_destroy(tg_sdk* sdk);

/* 最近一次非 OK 的可读原因(到下次调用前有效) */
const char* tg_sdk_last_error(tg_sdk* sdk);

/* ====================== graph 加载/更新 ====================== */

/* 只消费 JSON 文件路径或 JSON 字符串 */
int tg_sdk_load_graph_file(tg_sdk* sdk, const char* path);
int tg_sdk_load_graph_json(tg_sdk* sdk, const char* json);
int tg_sdk_update_graph_file(tg_sdk* sdk, const char* path);
int tg_sdk_update_graph_json(tg_sdk* sdk, const char* json);

/* ---- 加载诊断(GRAPH_INVALID 时) ---- */
/* 返回 issue 条数;同时刷新诊断缓存(下列访问器缓存源) */
int tg_sdk_issue_count(tg_sdk* sdk);
/* 取第 index 条:severity 0=ERROR 1=WARNING;stage 0=Parse 1=Schema
 * 2=Semantics 3=TypeCheck;line/column 仅 Parse 阶段有效。
 * 返回 TG_OK 或 TG_ERR_INVALID_ARGUMENT(index 越界)。 */
int tg_sdk_issue(tg_sdk* sdk, int index, int* severity, int* stage,
                 char* pointer_buf, int pointer_buf_size,
                 int64_t* line, int64_t* column,
                 char* message_buf, int message_buf_size);

/* ---- 图边界枚举 ---- */
int tg_sdk_input_node_count(tg_sdk* sdk);
const char* tg_sdk_input_node(tg_sdk* sdk, int index);   /* 越界返回 NULL */
int tg_sdk_output_node_count(tg_sdk* sdk);
const char* tg_sdk_output_node(tg_sdk* sdk, int index);

/* ====================== 全局配置(env/globals) ====================== */

/* env:字符串形态,任务侧以 "_env.<KEY>" 读取;init 后可增补,下次执行生效 */
int tg_sdk_set_env(tg_sdk* sdk, const char* key, const char* value);
int tg_sdk_set_global_string(tg_sdk* sdk, const char* key, const char* value);
int tg_sdk_set_global_int(tg_sdk* sdk, const char* key, int32_t value);
int tg_sdk_set_global_double(tg_sdk* sdk, const char* key, double value);

/* ====================== 图像句柄(C 侧数据负载) ====================== */

/* 从裸缓冲创建(深拷贝一份像素);data 可 NULL(零初始化缓冲)。
 * pixel_format: task_graph::PixelFormat 数值(1=GRAY 3=RGB/BGR 4=RGBA/BGRA...)
 * data_type:    task_graph::DataType 数值(0=UINT8 6=FLOAT32 ...) */
tg_image* tg_image_create(int width, int height, int channels,
                          int pixel_format, int data_type,
                          const void* data);
int tg_image_width(const tg_image* img);
int tg_image_height(const tg_image* img);
int tg_image_channels(const tg_image* img);
int tg_image_pixel_format(const tg_image* img);
int tg_image_data_type(const tg_image* img);
/* 只读像素指针;长度 = tg_image_data_size() */
const uint8_t* tg_image_data(const tg_image* img);
size_t tg_image_data_size(const tg_image* img);
void tg_image_destroy(tg_image* img);

/* ====================== 输入/输出绑定 ====================== */

int tg_sdk_bind_input_string(tg_sdk* sdk, const char* node, const char* value);
int tg_sdk_bind_input_int(tg_sdk* sdk, const char* node, int32_t value);
int tg_sdk_bind_input_double(tg_sdk* sdk, const char* node, double value);
int tg_sdk_bind_input_bool(tg_sdk* sdk, const char* node, int value);
int tg_sdk_bind_input_image(tg_sdk* sdk, const char* node, const tg_image* img);
int tg_sdk_unbind_input(tg_sdk* sdk, const char* node);

/* 拉模式:tg_sdk_bind_output(sdk, node, NULL, NULL) */
int tg_sdk_bind_output(tg_sdk* sdk, const char* node,
                       tg_output_fn callback, void* user_data);
int tg_sdk_unbind_output(tg_sdk* sdk, const char* node);

/* ---- 输出拉取(类型不匹配返回 TG_ERR_TYPE_MISMATCH) ---- */
/* 拷贝式:返回含 NUL 的所需长度;出错返回 -status(负的 TG_ERR_*) */
int tg_sdk_get_output_string(tg_sdk* sdk, const char* node,
                             char* buf, int buf_size);
int tg_sdk_get_output_int(tg_sdk* sdk, const char* node, int32_t* out);
int tg_sdk_get_output_double(tg_sdk* sdk, const char* node, double* out);
/* 返回新句柄(与图输出共享像素缓冲,零拷贝);调用方 tg_image_destroy。
 * GPU 驻留输出会先 ensure_cpu,失败返回 TG_ERR_INTERNAL。 */
int tg_sdk_get_output_image(tg_sdk* sdk, const char* node, tg_image** out);

/* ====================== 执行 ====================== */

/* 同步执行:阻塞到完成。执行前自动灌 env/globals、快照绑定值。 */
int tg_sdk_execute(tg_sdk* sdk);
/* 异步执行:立即返回 TG_OK;完成后用 tg_sdk_execute_wait 取状态。
 * 同一时刻只允许一个未 wait 的异步执行(TG_ERR_BUSY 否则)。 */
int tg_sdk_execute_async(tg_sdk* sdk);
int tg_sdk_execute_wait(tg_sdk* sdk, int* status_out);  /* 阻塞取回 */
int tg_sdk_is_running(tg_sdk* sdk);
int tg_sdk_cancel(tg_sdk* sdk);

/* ====================== diff 报告(update 后) ====================== */

/* kind: 0=tasks_added 1=tasks_removed 2=tasks_updated 3=edges_added 4=edges_removed */
int tg_sdk_diff_count(tg_sdk* sdk, int kind);
const char* tg_sdk_diff_item(tg_sdk* sdk, int kind, int index);  /* 越界 NULL */
int tg_sdk_diff_topology_changed(tg_sdk* sdk);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* TASK_GRAPH_TG_SDK_C_H */
