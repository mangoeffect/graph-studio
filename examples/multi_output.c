/* ============================================================================
 * multi_output.c — 多输出示例(纯 C 消费者 API)
 *
 * 一个图像源分两路:annotator(透传图像)→ 图像输出;stats(面积)→ 数值
 * 输出。演示:图像句柄绑定(tg_image_create,零拷贝共享)+ 双 io_output
 * 拉取 + update_graph 换绑复用同一张图。
 * ==========================================================================*/
#include <task_graph/tg_sdk_c.h>
#include <stdio.h>
#include <string.h>

/* 自定义任务:标注(透传图像) */
static int c_annotate(tg_task_ctx* ctx) {
    const tg_image* in = tg_task_ctx_input_image(ctx, "in");
    if (!in) return -1;
    printf("[annotator] annotated %dx%d image\n",
           tg_image_width(in), tg_image_height(in));
    tg_task_ctx_set_output_image(ctx, in);
    return 0;
}

/* 自定义任务:统计(面积 int) */
static int c_stats(tg_task_ctx* ctx) {
    const tg_image* in = tg_task_ctx_input_image(ctx, "in");
    if (!in) return -1;
    int area = tg_image_width(in) * tg_image_height(in);
    printf("[stats] area = %d\n", area);
    tg_task_ctx_set_output_int(ctx, area);
    return 0;
}

static const char* kGraph = R"({
  "version": "2.0",
  "tasks": [
    { "id": "src",   "type": "io_input", "params": { "data_type": "task_graph::Image" } },
    { "id": "ann",   "type": "c_annotate" },
    { "id": "stats", "type": "c_stats" },
    { "id": "img",   "type": "io_output" },
    { "id": "area",  "type": "io_output" }
  ],
  "edges": [
    { "from": "src",   "from_port": "out", "to": "ann",   "to_port": "in" },
    { "from": "src",   "from_port": "out", "to": "stats", "to_port": "in" },
    { "from": "ann",   "from_port": "out", "to": "img",   "to_port": "in" },
    { "from": "stats", "from_port": "out", "to": "area",  "to_port": "in" }
  ]
})";

int main(void) {
    printf("=== Multi-Output Example (pure C consumer API) ===\n\n");

    static const char* in_ports[] = {"in", NULL};
    static const char* out_ports[] = {"out", NULL};
    if (tg_register_c_task("c_annotate", c_annotate, in_ports, out_ports,
                           NULL, NULL, 0) != TG_OK ||
        tg_register_c_task("c_stats", c_stats, in_ports, out_ports,
                           NULL, NULL, 0) != TG_OK) {
        fprintf(stderr, "failed to register C tasks\n");
        return 1;
    }

    /* 构造 64x64 RGB 测试图像(棋盘填充) */
    unsigned char pixels[64 * 64 * 3];
    for (int i = 0; i < 64 * 64; ++i) {
        int checker = ((i / 64) + i) % 2;
        pixels[i * 3 + 0] = (unsigned char)(checker ? 200 : 40);
        pixels[i * 3 + 1] = (unsigned char)(checker ? 40 : 200);
        pixels[i * 3 + 2] = 128;
    }
    tg_image* image = tg_image_create(64, 64, 3, 3 /*RGB*/, 0 /*UINT8*/, pixels);
    if (!image) return 2;

    tg_sdk_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.log_level = 3 /*WARN*/;
    tg_sdk* sdk = tg_sdk_create();
    if (!sdk || tg_sdk_init(sdk, &cfg) != TG_OK) return 3;
    if (tg_sdk_load_graph_json(sdk, kGraph) != TG_OK) {
        fprintf(stderr, "load graph: %s\n", tg_sdk_last_error(sdk));
        return 4;
    }
    if (tg_sdk_bind_output(sdk, "img", NULL, NULL) != TG_OK ||
        tg_sdk_bind_output(sdk, "area", NULL, NULL) != TG_OK) return 5;
    if (tg_sdk_bind_input_image(sdk, "src", image) != TG_OK) return 6;

    printf("Executing DAG (two outputs)...\n\n");
    if (tg_sdk_execute(sdk) != TG_OK) {
        fprintf(stderr, "execute: %s\n", tg_sdk_last_error(sdk));
        return 7;
    }

    /* 双输出拉取 */
    tg_image* out_img = NULL;
    int32_t area = 0;
    if (tg_sdk_get_output_image(sdk, "img", &out_img) != TG_OK) return 8;
    if (tg_sdk_get_output_int(sdk, "area", &area) != TG_OK) return 9;
    printf("\nimage output: %dx%dx%d (%zu bytes), area output: %d\n",
           tg_image_width(out_img), tg_image_height(out_img),
           tg_image_channels(out_img), tg_image_data_size(out_img), area);

    /* 复用同一张图:换绑更大图像再执行(diff 更新演示可另行扩展) */
    tg_image* big = tg_image_create(128, 32, 3, 3, 0, pixels);
    if (tg_sdk_bind_input_image(sdk, "src", big) != TG_OK) return 10;
    if (tg_sdk_execute(sdk) != TG_OK) return 11;
    tg_sdk_get_output_int(sdk, "area", &area);
    printf("re-bound 128x32: area = %d\n", area);

    tg_image_destroy(out_img);
    tg_image_destroy(big);
    tg_image_destroy(image);
    tg_sdk_destroy(sdk);
    tg_unregister_c_task("c_annotate");
    tg_unregister_c_task("c_stats");
    return 0;
}
