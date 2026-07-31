#!/usr/bin/env python3
"""
task_graph 子模块脚手架生成器

基于 image_filtering 子模块的实现模式，生成一个完整的子模块骨架，包含：
  - CMakeLists.txt          编译配置（可选 OpenCV 依赖）
  - include/{mod}/{mod}.hpp 任务类声明
  - src/{mod}.cpp           任务实现 + 插件注册（constructor/destructor + extern C）

用法（命令行）:
  python3 scripts/generate_submodule.py \\
      --dir submodules/opencv/image_processing \\
      --name my_filter \\
      --tasks BlurTask:my_blur SharpenTask:my_sharpen \\
      --opencv

用法（交互式，不带参数直接运行）:
  python3 scripts/generate_submodule.py

task 格式:
  ClassName                  type_name 自动推导为 {module}_{class去Task后缀转小写}
  ClassName:type_name        显式指定 type_name
"""

import argparse
import os
import re
import sys
import textwrap


# ---------- 命名规范校验 ----------

def validate_module_name(name: str) -> bool:
    """模块名：小写字母/数字/下划线，如 image_filtering"""
    return bool(re.match(r'^[a-z][a-z0-9_]*$', name))


def validate_class_name(name: str) -> bool:
    """类名：大驼峰，如 BlurTask"""
    return bool(re.match(r'^[A-Z][a-zA-Z0-9]*$', name))


def validate_type_name(name: str) -> bool:
    """类型名：小写字母/数字/下划线，如 opencv_blur_filter"""
    return bool(re.match(r'^[a-z][a-z0-9_]*$', name))


def auto_type_name(module_name: str, class_name: str) -> str:
    """从类名推导 type_name：BlurTask + image_filtering → image_filtering_blur"""
    base = re.sub(r'Task$', '', class_name)
    snake = re.sub(r'([A-Z])', r'_\1', base).lower().lstrip('_')
    return f'{module_name}_{snake}'


# ---------- 模板生成 ----------

def gen_cmakelists(module_name: str, module_desc: str, use_opencv: bool) -> str:
    """生成 CMakeLists.txt"""
    lines = [
        'cmake_minimum_required(VERSION 3.16)',
        '',
        f'project({module_name}',
        '    VERSION 1.0.0',
        f'    DESCRIPTION "{module_desc}"',
        '    LANGUAGES CXX',
        ')',
        '',
        'set(CMAKE_CXX_STANDARD 20)',
        'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
        'set(CMAKE_CXX_EXTENSIONS OFF)',
        '',
        '# 主项目根目录：作为子目录构建时由父项目继承，独立构建时可通过 -DTASK_GRAPH_ROOT=... 指定',
        'if(NOT DEFINED TASK_GRAPH_ROOT)',
        '    # add_subdirectory 场景：CMAKE_SOURCE_DIR 指向主项目根',
        '    set(TASK_GRAPH_ROOT ${CMAKE_SOURCE_DIR})',
        'endif()',
        '',
    ]

    if use_opencv:
        lines += [
            'if(NOT TASK_GRAPH_ENABLE_OPENCV)',
            f'    message(WARNING "TASK_GRAPH_ENABLE_OPENCV not set, skipping {module_name} submodule")',
            '    return()',
            'endif()',
            '',
            'find_package(OpenCV REQUIRED)',
            '',
        ]

    lines += [
        f'add_library({module_name} SHARED',
        f'    src/{module_name}.cpp',
        ')',
        '',
        f'target_include_directories({module_name}',
        '    PRIVATE',
        '        ${PROJECT_SOURCE_DIR}/include',
        '        ${TASK_GRAPH_ROOT}/include',
    ]
    if use_opencv:
        lines.append('        ${OpenCV_INCLUDE_DIRS}')
    lines += [
        ')',
        '',
        f'target_link_libraries({module_name}',
        '    PRIVATE',
        '        task_graph',
    ]
    if use_opencv:
        lines.append('        ${OpenCV_LIBS}')
    lines += [
        ')',
        '',
        f'target_compile_options({module_name}',
        '    PRIVATE',
        '        -Wall',
        '        -Wextra',
        '        -Wpedantic',
        '        -O2',
        ')',
        '',
        f'set_target_properties({module_name}',
        '    PROPERTIES',
        '        PREFIX ""',
        '        SUFFIX ".so"',
        ')',
        '',
        'if(APPLE)',
        f'    set_target_properties({module_name}',
        '        PROPERTIES',
        '            SUFFIX ".dylib"',
        '    )',
        'endif()',
        '',
        'if(WIN32)',
        f'    set_target_properties({module_name}',
        '        PROPERTIES',
        '            SUFFIX ".dll"',
        '    )',
        'endif()',
        '',
    ]
    return '\n'.join(lines)


def gen_header(module_name: str, tasks: list, use_opencv: bool) -> str:
    """生成头文件"""
    includes = [
        '#pragma once',
        '',
        '#include <plugin_api.hpp>',
    ]
    if use_opencv:
        includes += [
            '#include <task_graph/data_types.hpp>',
        ]
    includes += [
        '#include <string>',
        '#include <vector>',
        '#include <any>',
        '',
        f'namespace {module_name} {{',
        '',
    ]

    for class_name, _ in tasks:
        includes += [
            f'class {class_name} : public task_graph::INode {{',
            'public:',
            '    using task_graph::INode::INode;',
            '',
            '    const std::string& type() const override;',
            '    task_graph::TaskResult execute(task_graph::TaskContext& ctx) override;',
            '    task_graph::CheckResult check_input(const std::unordered_map<std::string, std::any>& inputs) const override;',
            '};',
            '',
        ]

    includes.append(f'}}  // namespace {module_name}')
    includes.append('')
    return '\n'.join(includes)


def gen_source(module_name: str, tasks: list, use_opencv: bool) -> str:
    """生成源文件"""
    # 1. includes
    includes = [
        f'#include <{module_name}/{module_name}.hpp>',
    ]
    if use_opencv:
        includes += [
            '#include <opencv2/opencv.hpp>',
        ]
    includes.append('')

    # 2. type 名称常量（匿名命名空间）
    constants = [
        'namespace {',
        '// Task type 名称常量：集中定义，避免字符串散落于 type()/注册/注销 各处',
    ]
    for class_name, type_name in tasks:
        const_name = 'k' + class_name + 'Type'
        constants.append(f'const char* const {const_name} = "{type_name}";')
    constants += [
        '}  // namespace',
        '',
    ]

    # 3. 命名空间开始
    ns_lines = [f'namespace {module_name} {{', '']

    if use_opencv:
        # OpenCV 辅助函数
        ns_lines += [
            'namespace {',
            '',
            '// 优先以 cv::Mat 类型获取输入；若上游提供的是 Image 则转换为 cv::Mat',
            'std::optional<cv::Mat> get_input_mat(task_graph::TaskContext& ctx) {',
            '    if (auto mat_opt = ctx.template get_input<cv::Mat>()) {',
            '        return mat_opt;',
            '    }',
            '    if (auto img_opt = ctx.template get_input<task_graph::Image>()) {',
            '        return img_opt->to_mat();',
            '    }',
            '    return std::nullopt;',
            '}',
            '}  // namespace',
            '',
        ]

    # 4. 每个 task 的方法实现
    for class_name, type_name in tasks:
        const_name = 'k' + class_name + 'Type'
        # type()
        ns_lines += [
            f'const std::string& {class_name}::type() const {{',
            f'    static const std::string type({const_name});',
            '    return type;',
            '}',
            '',
        ]

        # execute()
        if use_opencv:
            ns_lines += [
                f'task_graph::TaskResult {class_name}::execute(task_graph::TaskContext& ctx) {{',
                '    auto mat_opt = get_input_mat(ctx);',
                '    if (!mat_opt) {',
                '        return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};',
                '    }',
                '    cv::Mat mat = *mat_opt;',
                '',
                '    // TODO: 在此实现图像处理逻辑',
                '    cv::Mat result = mat.clone();',
                '',
                '    return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = result};',
                '}',
                '',
            ]
        else:
            ns_lines += [
                f'task_graph::TaskResult {class_name}::execute(task_graph::TaskContext& ctx) {{',
                '    // TODO: 实现任务逻辑',
                '    // 示例：',
                '    //   auto input = ctx.get_input<int>();',
                '    //   if (!input) {',
                '    //       return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};',
                '    //   }',
                '    //   int output = *input * 2;',
                '    //   return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = output};',
                '    return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};',
                '}',
                '',
            ]

        # check_input()
        ns_lines += [
            f'task_graph::CheckResult {class_name}::check_input(const std::unordered_map<std::string, std::any>& inputs) const {{',
            '    // 默认实现：按 input_specs() 自动校验必填端口存在 + 类型名匹配',
            '    // 如需自定义校验逻辑，可覆盖此方法',
            '    return task_graph::INode::check_input(inputs);',
            '}',
            '',
        ]

    ns_lines.append(f'}}  // namespace {module_name}')
    ns_lines.append('')

    # 5. 插件注册
    reg_lines = [
        'namespace {',
        '',
        'bool do_register() {',
    ]
    for class_name, type_name in tasks:
        const_name = 'k' + class_name + 'Type'
        reg_lines += [
            '    task_graph::PluginRegistry::instance().register_task(',
            f'        {const_name},',
            '        [](const std::string& id, const task_graph::TaskConfig& config) {',
            f'            return std::make_shared<{module_name}::{class_name}>(id, config);',
            '        }',
            '    );',
        ]
    reg_lines += ['    return true;', '}', '']

    reg_lines.append('void do_unregister() {')
    for class_name, type_name in tasks:
        const_name = 'k' + class_name + 'Type'
        reg_lines.append(f'    task_graph::PluginRegistry::instance().unregister_task({const_name});')
    reg_lines += ['}', '']

    reg_lines += [
        '__attribute__((constructor))',
        'static void plugin_constructor() {',
        '    do_register();',
        '}',
        '',
        '__attribute__((destructor))',
        'static void plugin_destructor() {',
        '    do_unregister();',
        '}',
        '',
        '}  // namespace',
        '',
        'extern "C" TG_EXPORT bool register_plugin() {',
        '    return do_register();',
        '}',
        '',
        'extern "C" TG_EXPORT void unregister_plugin() {',
        '    do_unregister();',
        '}',
        '',
    ]

    return '\n'.join(includes + constants + ns_lines + reg_lines)


# ---------- 文件写入 ----------

def write_file(path: str, content: str, force: bool) -> bool:
    """写入文件，已存在且非 force 时跳过。返回是否写入。"""
    if os.path.exists(path) and not force:
        print(f'  [跳过] {path} (已存在，使用 --force 覆盖)')
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(content)
    print(f'  [生成] {path}')
    return True


# ---------- 交互式输入 ----------

def interactive_input():
    """无命令行参数时，交互式收集输入"""
    print('=' * 60)
    print('  task_graph 子模块脚手架生成器（交互模式）')
    print('=' * 60)
    print()

    # 子模块目录
    while True:
        out_dir = input('1. 子模块输出目录 (如 submodules/opencv/image_processing): ').strip()
        if out_dir:
            break
        print('   错误：目录不能为空')

    # 模块名称
    while True:
        name = input('2. 子模块名称 (如 image_filtering): ').strip()
        if name and validate_module_name(name):
            break
        print('   错误：名称须以小写字母开头，仅含小写字母/数字/下划线')

    # task 列表
    print('3. Task 列表 (每行一个，格式 ClassName 或 ClassName:type_name，空行结束):')
    tasks_raw = []
    while True:
        line = input(f'   task {len(tasks_raw) + 1}: ').strip()
        if not line:
            if not tasks_raw:
                print('   错误：至少需要一个 task')
                continue
            break
        tasks_raw.append(line)

    # OpenCV
    use_opencv = input('4. 启用 OpenCV 支持? [y/N]: ').strip().lower() == 'y'

    # 描述
    desc = input(f'5. 模块描述 (回车使用默认): ').strip()
    if not desc:
        desc = f'{name} plugin for task_graph framework'

    return out_dir, name, tasks_raw, use_opencv, desc


# ---------- 主流程 ----------

def parse_tasks(module_name: str, tasks_raw: list) -> list:
    """解析 task 输入，返回 [(class_name, type_name), ...]"""
    tasks = []
    for raw in tasks_raw:
        if ':' in raw:
            class_name, type_name = raw.split(':', 1)
            class_name = class_name.strip()
            type_name = type_name.strip()
        else:
            class_name = raw.strip()
            type_name = auto_type_name(module_name, class_name)

        if not validate_class_name(class_name):
            print(f'错误：类名 "{class_name}" 不合法（须大驼峰，如 BlurTask）', file=sys.stderr)
            sys.exit(1)
        if not validate_type_name(type_name):
            print(f'错误：类型名 "{type_name}" 不合法（须小写+下划线，如 my_blur）', file=sys.stderr)
            sys.exit(1)

        tasks.append((class_name, type_name))
    return tasks


def main():
    parser = argparse.ArgumentParser(
        description='task_graph 子模块脚手架生成器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent('''
          示例:
            %(prog)s -d submodules/my_plugins -n math_ops -t AddTask MultiplyTask
            %(prog)s -d submodules/opencv -n my_filter -t BlurTask:my_blur --opencv
            %(prog)s   # 交互模式
        '''),
    )
    parser.add_argument('-d', '--dir', help='子模块输出目录')
    parser.add_argument('-n', '--name', help='子模块名称 (如 image_filtering)')
    parser.add_argument('-t', '--tasks', nargs='+',
                        help='task 列表，格式 ClassName 或 ClassName:type_name')
    parser.add_argument('--opencv', action='store_true', help='启用 OpenCV 依赖')
    parser.add_argument('--desc', default=None, help='模块描述')
    parser.add_argument('--force', action='store_true', help='覆盖已存在文件')
    args = parser.parse_args()

    # 无参数 → 交互模式
    if not args.dir and not args.name and not args.tasks:
        out_dir, name, tasks_raw, use_opencv, desc = interactive_input()
    else:
        out_dir = args.dir
        name = args.name
        tasks_raw = args.tasks or []
        use_opencv = args.opencv
        desc = args.desc or f'{name} plugin for task_graph framework'

        if not out_dir:
            parser.error('--dir 必填')
        if not name:
            parser.error('--name 必填')
        if not tasks_raw:
            parser.error('--tasks 至少需要一个')

    # 校验模块名
    if not validate_module_name(name):
        print(f'错误：模块名 "{name}" 不合法（须小写字母开头，仅含小写字母/数字/下划线）', file=sys.stderr)
        sys.exit(1)

    # 解析 task
    tasks = parse_tasks(name, tasks_raw)

    # 生成路径
    module_root = os.path.join(out_dir, name)
    cmakelists_path = os.path.join(module_root, 'CMakeLists.txt')
    header_path = os.path.join(module_root, 'include', name, f'{name}.hpp')
    source_path = os.path.join(module_root, 'src', f'{name}.cpp')

    print()
    print(f'生成子模块: {name}')
    print(f'  目录: {module_root}')
    print(f'  tasks: {len(tasks)} 个')
    for class_name, type_name in tasks:
        print(f'    - {class_name} (type="{type_name}")')
    print(f'  OpenCV: {"是" if use_opencv else "否"}')
    print()

    # 生成内容
    write_file(cmakelists_path, gen_cmakelists(name, desc, use_opencv), args.force)
    write_file(header_path, gen_header(name, tasks, use_opencv), args.force)
    write_file(source_path, gen_source(name, tasks, use_opencv), args.force)

    print()
    print('完成。后续步骤:')
    print(f'  1. 在父项目 CMakeLists.txt 中添加:')
    print(f'       add_subdirectory({module_root})')
    print(f'  2. 实现 {source_path} 中的 TODO 逻辑')
    print(f'  3. 编译后在 DAG 中使用:')
    for _, type_name in tasks:
        print(f'       dag.add_plugin_task("{type_name}")')
    print()


if __name__ == '__main__':
    main()
