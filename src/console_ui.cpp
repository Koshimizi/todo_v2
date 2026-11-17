#include "console_ui.h"

#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

namespace todo {
namespace {

const char kRule[] = "----------------------------------------------";

}  // namespace

ConsoleUi::ConsoleUi(TaskManager& manager, std::istream& in, std::ostream& out)
    : manager_(manager), in_(in), out_(out), running_(false) {}

void ConsoleUi::run() {
    std::string warning;
    manager_.load(warning);
    if (!warning.empty()) {
        out_ << "[!] " << warning << '\n';
    }

    running_ = true;
    while (running_) {
        showMenu();
        const int choice = readInt("Enter your choice (1-9): ");
        if (!running_) {
            break;
        }

        switch (choice) {
            case 1:
                handleAdd();
                break;
            case 2:
                showTasks();
                break;
            case 3:
                handleToggle(true);
                break;
            case 4:
                handleToggle(false);
                break;
            case 5:
                handleEdit();
                break;
            case 6:
                handleRemove();
                break;
            case 7:
                handleSave();
                break;
            case 8:
                handleLoad();
                break;
            case 9:
                handleSave();
                out_ << "Goodbye.\n";
                running_ = false;
                break;
            default:
                out_ << "[!] please choose a number from 1 to 9\n";
                break;
        }
    }
}

void ConsoleUi::showMenu() const {
    out_ << '\n' << kRule << '\n'
         << "   To-Do List\n"
         << kRule << '\n'
         << "1. Add task\n"
         << "2. View all tasks\n"
         << "3. Mark task complete\n"
         << "4. Mark task incomplete\n"
         << "5. Edit task\n"
         << "6. Delete task\n"
         << "7. Save tasks\n"
         << "8. Load tasks\n"
         << "9. Save and quit\n"
         << kRule << '\n';
}

void ConsoleUi::showTasks() const {
    const std::vector<Task>& tasks = manager_.tasks();
    if (tasks.empty()) {
        out_ << "No tasks yet.\n";
        return;
    }

    out_ << kRule << '\n';
    for (const Task& task : tasks) {
        out_ << "ID " << task.id() << "  " << (task.completed() ? "[x]" : "[ ]")
             << "  " << priorityToString(task.priority()) << "  "
             << task.description() << '\n';
    }
    out_ << kRule << '\n';
}

void ConsoleUi::report(const Result& result) const {
    out_ << (result.ok ? "[OK] " : "[X] ") << result.message << '\n';
}

void ConsoleUi::handleAdd() {
    const std::string description = readLine("Description: ");
    if (!running_) {
        return;
    }
    const Priority priority = readPriority();
    if (!running_) {
        return;
    }
    report(manager_.add(description, priority));
}

void ConsoleUi::handleEdit() {
    showTasks();
    if (manager_.tasks().empty()) {
        return;
    }

    const int id = readInt("Task id to edit: ");
    if (!running_) {
        return;
    }

    const Task* existing = manager_.find(id);
    if (existing == nullptr) {
        out_ << "[X] no task with id " << id << '\n';
        return;
    }

    out_ << "Current: " << existing->description() << " ("
         << priorityToString(existing->priority()) << ")\n";

    // Unlike "add", an empty answer here is a deliberate "keep what is there",
    // so editing one field never forces you to retype the other.
    const std::string typed = readLine("New description (empty keeps current): ");
    if (!running_) {
        return;
    }
    const std::string description = typed.empty() ? existing->description() : typed;

    const Priority priority = readPriority();
    if (!running_) {
        return;
    }
    report(manager_.edit(id, description, priority));
}

void ConsoleUi::handleRemove() {
    showTasks();
    if (manager_.tasks().empty()) {
        return;
    }
    const int id = readInt("Task id to delete: ");
    if (!running_) {
        return;
    }
    report(manager_.remove(id));
}

void ConsoleUi::handleToggle(bool completed) {
    showTasks();
    if (manager_.tasks().empty()) {
        return;
    }
    const int id = readInt(completed ? "Task id to mark complete: "
                                     : "Task id to mark incomplete: ");
    if (!running_) {
        return;
    }
    report(manager_.setCompleted(id, completed));
}

void ConsoleUi::handleSave() {
    std::string error;
    if (!manager_.save(error)) {
        out_ << "[X] could not save: " << error << '\n';
        return;
    }
    out_ << "[OK] saved " << manager_.tasks().size() << " task(s) to "
         << manager_.path() << '\n';
}

void ConsoleUi::handleLoad() {
    std::string warning;
    manager_.load(warning);
    out_ << "[OK] loaded " << manager_.tasks().size() << " task(s)\n";
    if (!warning.empty()) {
        out_ << "[!] " << warning << '\n';
    }
}

int ConsoleUi::readInt(const std::string& prompt) {
    out_ << prompt;

    while (true) {
        int value = 0;
        if (in_ >> value) {
            // Consume the rest of the line so the next readLine() starts clean.
            // Doing it here once is why no caller needs a bare cin.ignore().
            in_.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return value;
        }
        if (in_.eof()) {
            running_ = false;  // input ended: do not spin forever on EOF
            return 0;
        }
        in_.clear();
        in_.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        out_ << "Not a number, try again: ";
    }
}

std::string ConsoleUi::readLine(const std::string& prompt) {
    out_ << prompt;

    std::string line;
    if (!std::getline(in_, line)) {
        running_ = false;
        return std::string();
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();  // tolerate CRLF input files
    }
    return line;
}

Priority ConsoleUi::readPriority() {
    out_ << "Priority: 1 = High, 2 = Medium, 3 = Low\n";
    const int choice = readInt("Choose priority (1-3): ");
    switch (choice) {
        case 1:
            return Priority::High;
        case 3:
            return Priority::Low;
        default:
            return Priority::Medium;
    }
}

}  // namespace todo
