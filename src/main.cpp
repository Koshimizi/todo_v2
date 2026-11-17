#include "console_ui.h"
#include "task_manager.h"
#include "task_store.h"

#include <iostream>

int main() {
    todo::TaskManager manager(todo::TaskStore("tasks.txt"));
    todo::ConsoleUi ui(manager, std::cin, std::cout);
    ui.run();
    return 0;
}
