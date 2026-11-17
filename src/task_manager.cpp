#include "task_manager.h"

#include <cstddef>
#include <utility>

namespace todo {
namespace {

const int kFirstId = 1;

}  // namespace

Result success(std::string message) { return Result{true, std::move(message)}; }

Result failure(std::string message) { return Result{false, std::move(message)}; }

TaskManager::TaskManager(TaskStore store)
    : store_(std::move(store)), tasks_(), nextId_(kFirstId) {}

void TaskManager::assignIdsFromTasks() {
    // ids live in the file, so a renumbered list can never happen: the next id
    // is always one past the largest id we have seen.
    int highest = kFirstId - 1;
    for (const Task& task : tasks_) {
        if (task.id() > highest) {
            highest = task.id();
        }
    }
    nextId_ = highest + 1;
}

void TaskManager::load(std::string& warning) {
    tasks_ = store_.load(warning);
    assignIdsFromTasks();
}

bool TaskManager::save(std::string& error) const { return store_.save(tasks_, error); }

Result TaskManager::add(std::string description, Priority priority) {
    if (description.empty()) {
        return failure("description cannot be empty");
    }
    tasks_.emplace_back(nextId_, std::move(description), priority, false);
    ++nextId_;
    return success("task added");
}

Result TaskManager::edit(int id, std::string description, Priority priority) {
    if (description.empty()) {
        return failure("description cannot be empty");
    }
    Task* task = findMutable(id);
    if (task == nullptr) {
        return failure("no task with id " + std::to_string(id));
    }
    task->setDescription(std::move(description));
    task->setPriority(priority);
    return success("task updated");
}

Result TaskManager::remove(int id) {
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].id() == id) {
            tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(i));
            return success("task deleted");
        }
    }
    return failure("no task with id " + std::to_string(id));
}

Result TaskManager::setCompleted(int id, bool completed) {
    Task* task = findMutable(id);
    if (task == nullptr) {
        return failure("no task with id " + std::to_string(id));
    }
    task->setCompleted(completed);
    return success(completed ? "task marked complete" : "task marked incomplete");
}

const std::vector<Task>& TaskManager::tasks() const { return tasks_; }

const Task* TaskManager::find(int id) const {
    for (const Task& task : tasks_) {
        if (task.id() == id) {
            return &task;
        }
    }
    return nullptr;
}

Task* TaskManager::findMutable(int id) {
    for (Task& task : tasks_) {
        if (task.id() == id) {
            return &task;
        }
    }
    return nullptr;
}

const std::string& TaskManager::path() const { return store_.path(); }

}  // namespace todo
