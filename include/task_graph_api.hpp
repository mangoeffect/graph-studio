#pragma once

// ============================================================================
// task_graph_api.hpp - App 消费者统一入口
//
// 供 graph_studio 等 App 调用框架使用。包含构建 DAG、编译校验、执行、
// 序列化、插件加载、性能采集等全部框架能力。
//
// 子模块开发者请使用 <plugin_api.hpp>（仅含 INode/TaskContext/ParamSpec 等
// task 实现所需的类型）。
// ============================================================================

// ===== 共享基础（INode/TaskConfig/TaskResult/PluginRegistry/日志 等）=====
#include <plugin_api.hpp>

// ===== DAG 构建（DAG/Edge/DAGChangeEvent）=====
#include <task_graph/dag.hpp>

// ===== 编译校验（DAGCompiler/ExecutionPlan/ValidationError）=====
#include <task_graph/compiler.hpp>

// ===== 执行（DAGExecutor/ExecutorConfig/ExecutionEvent）=====
#include <task_graph/executor.hpp>

// ===== 序列化（DAGSerializer）=====
#include <task_graph/dag_serializer.hpp>

// ===== 插件加载（PluginLoader/PluginInfo）=====
#include <task_graph/plugin.hpp>

// ===== 性能采集（ProfileCollector/TaskStats/DagStats）=====
#include <task_graph/profiler.hpp>

// ===== 便捷 task（LambdaNode/TaskFunction）=====
#include <task_graph/task.hpp>

// ===== 独立上下文（ExecutionContext）=====
#include <execution_context.hpp>

// ===== 线程池（ThreadPool）=====
#include <task_graph/thread_pool.hpp>

// ===== TaskManager（独立 task 注册表）=====
#include <task_graph/task_manager.hpp>
