/* 以纯 C 编译的翻译单元:验证 tg_sdk_c.h 是合法 C 头(无 C++ 依赖)。 */
#include <task_graph/tg_sdk_c.h>

/* 触摸全部声明,链接期由测试目标保证符号存在(C++ 侧实现)。 */
int tg_c_header_smoke(tg_sdk* sdk, tg_image* img)
{
    tg_sdk_config cfg;
    cfg.log_level = TG_OK;
    cfg.log_callback = 0;
    cfg.log_user_data = 0;
    cfg.thread_pool_size = 0;
    cfg.default_timeout_ms = 0;
    cfg.enable_profiling = 0;
    cfg.require_known_types = 0;

    int ok = tg_sdk_init(sdk, &cfg);
    ok += tg_sdk_issue_count(sdk);
    ok += tg_sdk_input_node_count(sdk);
    ok += tg_sdk_output_node_count(sdk);
    ok += tg_sdk_diff_count(sdk, 0);
    ok += tg_image_width(img);
    ok += (int)tg_image_data_size(img);
    return ok;
}
