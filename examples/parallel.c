/* ============================================================================
 * parallel.c — 并行分支汇合示例(纯 C 消费者 API)
 *
 * 一个 int 源并行两路(health_check:数值→状态串;data_analysis:数值→
 * 分析串),汇合到 generate_report(双输入端口 health + analysis)→ 输出。
 * 演示:多输入端口 C 任务 + typed int 绑定。
 * ==========================================================================*/
#include <task_graph/tg_sdk_c.h>
#include <stdio.h>
#include <string.h>

/* health_check:int 透传(原版语义:输出健康数值 95) */
static int c_health(tg_task_ctx* ctx) {
    int32_t v = tg_task_ctx_input_int(ctx, "in");
    printf("[health_check] Checking health (value=%d)...\n", v);
    tg_task_ctx_set_output_int(ctx, v);
    return 0;
}

/* data_analysis:int → 分析字符串 */
static int c_analysis(tg_task_ctx* ctx) {
    int32_t v = tg_task_ctx_input_int(ctx, "in");
    printf("[data_analysis] Analyzing (value=%d)...\n", v);
    char buf[64];
    snprintf(buf, sizeof(buf), "analysis_of_%d", v);
    tg_task_ctx_set_output_string(ctx, buf);
    return 0;
}

/* generate_report:双输入端口(health int + analysis string)→ 报告 */
static int c_report(tg_task_ctx* ctx) {
    int32_t health = tg_task_ctx_input_int(ctx, "health");
    const char* analysis = tg_task_ctx_input_string(ctx, "analysis");
    char buf[128];
    snprintf(buf, sizeof(buf), "Health=%d, Analysis=%s",
             health, analysis ? analysis : "?");
    printf("[generate_report] Report: %s\n", buf);
    tg_task_ctx_set_output_string(ctx, buf);
    return 0;
}

/* 纯 C 没有 raw string literal,用相邻字面量拼接保持 JSON 可读 */
static const char* kGraph =
    "{\n"
    "  \"version\": \"2.0\",\n"
    "  \"tasks\": [\n"
    "    { \"id\": \"src\",      \"type\": \"io_input\", \"params\": { \"data_type\": \"int\" } },\n"
    "    { \"id\": \"health\",   \"type\": \"c_health\" },\n"
    "    { \"id\": \"analysis\", \"type\": \"c_analysis\" },\n"
    "    { \"id\": \"report\",   \"type\": \"c_report\" },\n"
    "    { \"id\": \"dst\",      \"type\": \"io_output\" }\n"
    "  ],\n"
    "  \"edges\": [\n"
    "    { \"from\": \"src\",      \"from_port\": \"out\", \"to\": \"health\",   \"to_port\": \"in\" },\n"
    "    { \"from\": \"src\",      \"from_port\": \"out\", \"to\": \"analysis\", \"to_port\": \"in\" },\n"
    "    { \"from\": \"health\",   \"from_port\": \"out\", \"to\": \"report\",   \"to_port\": \"health\" },\n"
    "    { \"from\": \"analysis\", \"from_port\": \"out\", \"to\": \"report\",   \"to_port\": \"analysis\" },\n"
    "    { \"from\": \"report\",   \"from_port\": \"out\", \"to\": \"dst\",      \"to_port\": \"in\" }\n"
    "  ]\n"
    "}";

int main(void) {
    printf("=== Parallel DAG Example (pure C consumer API) ===\n\n");
    printf("DAG structure:\n  src -> health_check   -> generate_report -> dst\n"
           "      \\-> data_analysis /\n\n");

    static const char* in_ports[] = {"in", NULL};
    static const char* report_ports[] = {"health", "analysis", NULL};
    static const char* out_ports[] = {"out", NULL};
    if (tg_register_c_task("c_health", c_health, in_ports, out_ports,
                           NULL, NULL, 0) != TG_OK ||
        tg_register_c_task("c_analysis", c_analysis, in_ports, out_ports,
                           NULL, NULL, 0) != TG_OK ||
        tg_register_c_task("c_report", c_report, report_ports, out_ports,
                           NULL, NULL, 0) != TG_OK) {
        fprintf(stderr, "failed to register C tasks\n");
        return 1;
    }

    tg_sdk_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.log_level = 3 /*WARN*/;
    tg_sdk* sdk = tg_sdk_create();
    if (!sdk || tg_sdk_init(sdk, &cfg) != TG_OK) return 2;
    if (tg_sdk_load_graph_json(sdk, kGraph) != TG_OK) {
        fprintf(stderr, "load graph: %s\n", tg_sdk_last_error(sdk));
        return 3;
    }
    if (tg_sdk_bind_input_int(sdk, "src", 95) != TG_OK) return 4;

    printf("Executing DAG...\n\n");
    if (tg_sdk_execute(sdk) != TG_OK) {
        fprintf(stderr, "execute: %s\n", tg_sdk_last_error(sdk));
        return 5;
    }

    char out[128];
    if (tg_sdk_get_output_string(sdk, "dst", out, sizeof(out)) < 0) return 6;
    printf("\nFinal report: %s\n", out);

    tg_sdk_destroy(sdk);
    tg_unregister_c_task("c_health");
    tg_unregister_c_task("c_analysis");
    tg_unregister_c_task("c_report");
    return 0;
}
