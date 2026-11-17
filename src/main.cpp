#include "console_ui.h"
#include "task_manager.h"
#include "task_store.h"

#include <iostream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX  // 别让 windows.h 定义 min/max 宏，否则会和标准库打架
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main() {
#if defined(_WIN32)
    // 数据文件统一用 UTF-8（图形界面也按 UTF-8 读写），所以让控制台也按 UTF-8 收发。
    // 不然在简体中文 Windows 上，输入的中文会以 GBK 字节写进文件，
    // 图形界面读到的就是乱码 —— 这正是 ISSUES.md 问题 3 没覆盖到的那一半。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    todo::TaskManager manager(todo::TaskStore("tasks.txt"));
    todo::ConsoleUi ui(manager, std::cin, std::cout);
    ui.run();
    return 0;
}
