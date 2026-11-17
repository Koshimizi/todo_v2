#include "task_store.h"

#include <cstddef>
#include <exception>
#include <fstream>
#include <sstream>
#include <utility>

namespace todo {
namespace {

const char kDelimiter = '|';
const std::size_t kFieldCount = 4u;

bool parseInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    std::size_t consumed = 0;
    try {
        value = std::stoi(text, &consumed);
    } catch (const std::exception&) {
        return false;
    }
    return consumed == text.size();
}

std::string makeWarning(const std::string& path, int lineNumber,
                        const std::string& detail) {
    std::ostringstream message;
    message << path << ":" << lineNumber << ": " << detail;
    return message.str();
}

}  // namespace

std::string escapeField(const std::string& raw) {
    std::string escaped;
    escaped.reserve(raw.size());
    for (char c : raw) {
        switch (c) {
            case '\\':
                escaped += "\\\\";
                break;
            case '|':
                escaped += "\\|";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

std::vector<std::string> splitEscaped(const std::string& line, char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;

    for (char c : line) {
        if (escaped) {
            switch (c) {
                case 'n':
                    current += '\n';
                    break;
                case 'r':
                    current += '\r';
                    break;
                default:
                    // Covers "\\" and "\|": the backslash simply protects the
                    // next character.
                    current += c;
                    break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == delimiter) {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

    if (escaped) {
        current += '\\';  // a lone trailing backslash is kept literally
    }
    fields.push_back(current);
    return fields;
}

TaskStore::TaskStore(std::string path) : path_(std::move(path)) {}

const std::string& TaskStore::path() const { return path_; }

std::vector<Task> TaskStore::load(std::string& warning) const {
    warning.clear();

    std::vector<Task> tasks;
    std::ifstream in(path_);
    if (!in) {
        return tasks;  // first run: there is simply no file yet
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = splitEscaped(line, kDelimiter);
        if (fields.size() != kFieldCount) {
            if (warning.empty()) {
                warning = makeWarning(path_, lineNumber,
                                      "expected 4 fields, line skipped");
            }
            continue;
        }

        int id = 0;
        if (!parseInt(fields[0], id)) {
            if (warning.empty()) {
                warning = makeWarning(path_, lineNumber,
                                      "bad id \"" + fields[0] + "\", line skipped");
            }
            continue;
        }

        tasks.emplace_back(id, fields[1], priorityFromString(fields[2]),
                           fields[3] == "1");
    }

    return tasks;
}

bool TaskStore::save(const std::vector<Task>& tasks, std::string& error) const {
    error.clear();

    std::ofstream out(path_, std::ios::trunc);
    if (!out) {
        error = "cannot open " + path_ + " for writing";
        return false;
    }

    for (const Task& task : tasks) {
        out << task.id() << kDelimiter
            << escapeField(task.description()) << kDelimiter
            << escapeField(priorityToString(task.priority())) << kDelimiter
            << (task.completed() ? 1 : 0) << '\n';
    }

    out.flush();
    if (!out) {
        error = "failed while writing " + path_;
        return false;
    }
    return true;
}

}  // namespace todo
