#include "task.h"

#include <cctype>
#include <utility>

namespace todo {
namespace {

std::string toLowerCopy(const std::string& text) {
    std::string result = text;
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

}  // namespace

Priority priorityFromString(const std::string& text) {
    const std::string lower = toLowerCopy(text);
    if (lower == "high") {
        return Priority::High;
    }
    if (lower == "low") {
        return Priority::Low;
    }
    return Priority::Medium;
}

std::string priorityToString(Priority priority) {
    switch (priority) {
        case Priority::High:
            return "High";
        case Priority::Low:
            return "Low";
        case Priority::Medium:
            break;
    }
    return "Medium";
}

Task::Task(int id, std::string description, Priority priority, bool completed)
    : id_(id),
      description_(std::move(description)),
      priority_(priority),
      completed_(completed) {}

int Task::id() const { return id_; }

const std::string& Task::description() const { return description_; }

Priority Task::priority() const { return priority_; }

bool Task::completed() const { return completed_; }

void Task::setDescription(std::string description) {
    description_ = std::move(description);
}

void Task::setPriority(Priority priority) { priority_ = priority; }

void Task::setCompleted(bool completed) { completed_ = completed; }

}  // namespace todo
