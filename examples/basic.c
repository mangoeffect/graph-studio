/* ============================================================================
 * basic.c — 基本 DAG 示例(纯 C 消费者 API)
 *
 * 原为程序化 C++ 构图(lambda Task + DAG/DAGExecutor 直连);现改为消费者
 * 路径:JSON graph(io_input/io_output 边界 + 自定义 C 任务)+ TaskGraphSdk
 * 的 C 接口(tg_sdk_c.h)完整生命周期。
 *
 * 流水线:src(io_input,绑定 "user_123_data")
 *           → process(自定义 C 任务,参数 suffix="_processed")
 *           → save(自定义 C 任务,打印并透传)
 *           → dst(io_output,拉取结果)
 * ==========================================================================*/
#include <task_graph/tg_sdk_c.h>
#include <stdio.h>
#include <string.h>

/* 自定义任务:字符串加工(param suffix 追加) */
static int c_process(tg_task_ctx* ctx) {
    const char* in = tg_task_ctx_input_string(ctx, "in");
    const char* suffix = tg_task_ctx_param_string(ctx, "suffix");
    if (!in) return -1;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s", in, suffix ? suffix : "");
    printf("[process_data] Processing...\n");
    tg_task_ctx_set_output_string(ctx, buf);
    return 0;
}

/* 自定义任务:保存(打印)并透传 */
static int c_save(tg_task_ctx* ctx) {
    const char* in = tg_task_ctx_input_string(ctx, "in");
    if (!in) return -1;
    printf("[save_result] Saved: %s\n", in);
    tg_task_ctx_set_output_string(ctx, in);
    return 0;
}

static const char* kGraph = R"({
  "version": "2.0",
  "tasks": [
    { "id": "src",     "type": "io_input", "params": { "data_type": "std::string" } },
    { "id": "process", "type": "c_process", "params": { "suffix": "_processed" } },
    { "id": "save",    "type": "c_save" },
    { "id": "dst",     "type": "io_output" }
  ],
  "edges": [
    { "from": "src",     "from_port": "out", "to": "process", "to_port": "in" },
    { "from": "process", "from_port": "out", "to": "save",    "to_port": "in" },
    { "from": "save",    "from_port": "out", "to": "dst",     "to_port": "in" }
  ]
})";

int main(void) {
    printf("=== Basic DAG Example (pure C consumer API) ===\n\n");
    printf("DAG structure:\n  src -> process -> save -> dst\n\n");

    /* 注册自定义 C 任务(进程级,load 前) */
    static const char* in_ports[] = {"in", NULL};
    static const char* out_ports[] = {"out", NULL};
    static const char* param_names[] = {"suffix"};
    static const int param_types[] = {2 /*string*/};
    if (tg_register_c_task("c_process", c_process, in_ports, out_ports,
                           param_names, param_types, 1) != TG_OK ||
        tg_register_c_task("c_save", c_save, in_ports, out_ports,
                           NULL, NULL, 0) != TG_OK) {
        fprintf(stderr, "failed to register C tasks\n");
        return 1;
    }

    /* SDK 生命周期 */
    tg_sdk_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.log_level = 3 /*WARN*/;
    tg_sdk* sdk = tg_sdk_create();
    if (!sdk || tg_sdk_init(sdk, &cfg) != TG_OK) return 2;
    if (tg_sdk_load_graph_json(sdk, kGraph) != TG_OK) {
        fprintf(stderr, "load graph: %s\n", tg_sdk_last_error(sdk));
        return 3;
    }

    /* 绑定输入 + 执行 */
    if (tg_sdk_bind_input_string(sdk, "src", "user_123_data") != TG_OK) return 4;
    printf("Executing DAG...\n\n");
    if (tg_sdk_execute(sdk) != TG_OK) {
        fprintf(stderr, "execute: %s\n", tg_sdk_last_error(sdk));
        return 5;
    }

    /* 拉取输出 */
    char out[256];
    if (tg_sdk_get_output_string(sdk, "dst", out, sizeof(out)) < 0) return 6;
    printf("\nExecution result: %s\n", out);

    tg_sdk_destroy(sdk);
    tg_unregister_c_task("c_process");
    tg_unregister_c_task("c_save");
    return 0;
}
