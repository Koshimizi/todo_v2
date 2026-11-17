#ifndef TODO_TASK_MANAGER_H
#define TODO_TASK_MANAGER_H

#include "task.h"
#include "task_store.h"

#include <string>
#include <vector>

namespace todo {

// The outcome of one operation.
// The manager never prints: it reports what happened and lets the caller
// decide how to show it. Messages are short reasons, not decorated UI text.
struct Result {
    bool ok;
    std::string message;
};

Result success(std::string message);
Result failure(std::string message);

// Owns the in-memory list and the rules: assign ids, find, add, edit, remove.
// Persistence is delegated to TaskStore.
class TaskManager {
public:
    explicit TaskManager(TaskStore store);

    // Never fails as a whole: an unreadable file just means an empty list.
    // Problems with individual lines come back through `warning`.
    void load(std::string& warning);
    bool save(std::string& error) const;

    Result add(std::string description, Priority priority);
    Result edit(int id, std::string description, Priority priority);
    Result remove(int id);
    Result setCompleted(int id, bool completed);

    const std::vector<Task>& tasks() const;

    // Returns nullptr when no task has that id.
    // The pointer is borrowed: it stays valid only until the next mutation.
    const Task* find(int id) const;

    const std::string& path() const;

private:
    Task* findMutable(int id);
    void assignIdsFromTasks();

    TaskStore store_;
    std::vector<Task> tasks_;
    int nextId_;
};

}  // namespace todo

#endif  // TODO_TASK_MANAGER_H
