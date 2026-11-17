#ifndef TODO_TASK_STORE_H
#define TODO_TASK_STORE_H

#include "task.h"

#include <string>
#include <vector>

namespace todo {

// Reads and writes the task list.
// This layer knows about files and the on-disk format, and nothing about
// menus, printing, or the rules of the application.
class TaskStore {
public:
    explicit TaskStore(std::string path);

    // Loads every well-formed line. A missing file is not an error: it simply
    // means "no tasks yet". Lines that cannot be parsed are skipped and
    // described in `warning`, so one corrupted line never loses the whole file.
    std::vector<Task> load(std::string& warning) const;

    // Rewrites the whole file. Returns false and fills `error` on failure.
    bool save(const std::vector<Task>& tasks, std::string& error) const;

    const std::string& path() const;

private:
    std::string path_;
};

// On-disk format (one task per line, '|' between fields):
//
//     <id>|<description>|<priority>|<status>
//
// A description may contain '|', '\' or a newline, so those characters are
// escaped. These two helpers are public so the tests can pin the format down.
std::string escapeField(const std::string& raw);
std::vector<std::string> splitEscaped(const std::string& line, char delimiter);

}  // namespace todo

#endif  // TODO_TASK_STORE_H
