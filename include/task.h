#ifndef TODO_TASK_H
#define TODO_TASK_H

#include <string>

namespace todo {

// Priority is a closed set of three values, so we use an enum instead of a
// raw string. "High"/"Medium"/"Low" spelled by hand is how typos become bugs.
enum class Priority { Low = 0, Medium = 1, High = 2 };

// Text form used both in the data file and in the user interface.
// Unknown text falls back to Medium, so a hand-edited file never crashes us.
Priority priorityFromString(const std::string& text);
std::string priorityToString(Priority priority);

// One to-do entry.
// Pure data: it never prints, never reads input, never touches a file.
// Keeping it dumb is what lets the same Task be shown in a console today and
// in a window tomorrow without being rewritten.
class Task {
public:
    Task(int id, std::string description, Priority priority, bool completed);

    int id() const;
    const std::string& description() const;
    Priority priority() const;
    bool completed() const;

    void setDescription(std::string description);
    void setPriority(Priority priority);
    void setCompleted(bool completed);

private:
    int id_;
    std::string description_;
    Priority priority_;
    bool completed_;
};

}  // namespace todo

#endif  // TODO_TASK_H
