#ifndef TODO_PRESENTER_H
#define TODO_PRESENTER_H

#include "task.h"
#include "task_manager.h"

#include <cstddef>
#include <string>

namespace todo {

// 把数据翻译成"界面上要显示的文字"。
//
// 这一层故意只做字符串，不碰任何操作系统 API：所以它既能被控制台版用到，
// 也能被图形界面用到，还能被普通单元测试直接覆盖。
// 窗口代码里剩下的就只有"把这段文字贴到哪个控件上"，不会再有判断逻辑。
//
// 所有函数返回 UTF-8。图形界面在边界处转成 UTF-16（见 gui/win32_ui.cpp）。

// 状态列："已完成" / "待办"
std::string statusLabel(bool completed);

// 优先级列："高" / "中" / "低"
std::string priorityLabel(Priority priority);

// 把描述压成一行：换行符显示成字面量 \n，否则会把表格行撑坏。
std::string singleLine(const std::string& text);

// 状态栏的汇总："共 3 条 · 已完成 1 条"
std::string summaryLabel(std::size_t total, std::size_t completed);

// 窗口标题："待办清单 — tasks.txt"，有未保存改动时加 " *"
std::string windowTitle(const std::string& path, bool dirty);

// 把 Result 里的英文原因翻译成给人看的中文。
// 认不出来的消息原样返回——宁可露出英文，也不能悄悄吞掉信息。
std::string resultLabel(const Result& result);

}  // namespace todo

#endif  // TODO_PRESENTER_H
