#ifndef TODO_CONSOLE_UI_H
#define TODO_CONSOLE_UI_H

#include "task_manager.h"

#include <iosfwd>
#include <string>

namespace todo {

// Every bit of reading from the user and every bit of printing lives here.
// The streams are injected instead of using std::cin / std::cout directly so
// the whole menu can be driven by a string in a test.
class ConsoleUi {
public:
    ConsoleUi(TaskManager& manager, std::istream& in, std::ostream& out);

    // Runs until the user quits or the input stream ends.
    void run();

private:
    void showMenu() const;
    void showTasks() const;
    void report(const Result& result) const;

    void handleAdd();
    void handleEdit();
    void handleRemove();
    void handleToggle(bool completed);
    void handleSave();
    void handleLoad();

    // Reads one whole line of input, consuming the trailing newline, so the
    // caller never has to reason about what is left in the buffer.
    int readInt(const std::string& prompt);
    std::string readLine(const std::string& prompt);
    Priority readPriority();

    TaskManager& manager_;
    std::istream& in_;
    std::ostream& out_;
    bool running_;
};

}  // namespace todo

#endif  // TODO_CONSOLE_UI_H
