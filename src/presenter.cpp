#include "presenter.h"

#include <cstddef>

namespace todo {
namespace {

struct MessageText {
    const char* english;
    const char* chinese;
};

// TaskManager 会返回的原因就这么多，是一套封闭的集合，可以安全地一张表映射完。
const MessageText kMessages[] = {
    {"task added", "已添加任务"},
    {"task updated", "已保存修改"},
    {"task deleted", "已删除任务"},
    {"task marked complete", "已标记为完成"},
    {"task marked incomplete", "已标记为待办"},
    {"description cannot be empty", "任务描述不能为空"},
};

const char kMissingIdPrefix[] = "no task with id ";

}  // namespace

std::string statusLabel(bool completed) { return completed ? "已完成" : "待办"; }

std::string priorityLabel(Priority priority) {
    switch (priority) {
        case Priority::High:
            return "高";
        case Priority::Low:
            return "低";
        case Priority::Medium:
            break;
    }
    return "中";
}

std::string singleLine(const std::string& text) {
    std::string oneLine;
    oneLine.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            // "\r\n" 只算一次换行，别输出两个 \n
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            oneLine += "\\n";
        } else if (c == '\n') {
            oneLine += "\\n";
        } else {
            oneLine += c;
        }
    }
    return oneLine;
}

std::string summaryLabel(std::size_t total, std::size_t completed) {
    if (total == 0u) {
        return "还没有任务，在上面输入一条开始吧";
    }
    return "共 " + std::to_string(total) + " 条 · 已完成 " +
           std::to_string(completed) + " 条";
}

std::string windowTitle(const std::string& path, bool dirty) {
    std::string title = "待办清单 — " + path;
    if (dirty) {
        title += " *";
    }
    return title;
}

std::string resultLabel(const Result& result) {
    for (const MessageText& entry : kMessages) {
        if (result.message == entry.english) {
            return entry.chinese;
        }
    }

    // "no task with id 7" 这种带参数的提示没法写进表里，前缀匹配后把 ID 拼回去。
    const std::size_t prefixLength = sizeof(kMissingIdPrefix) - 1u;
    if (result.message.compare(0u, prefixLength, kMissingIdPrefix) == 0) {
        return "找不到 ID 为 " + result.message.substr(prefixLength) + " 的任务";
    }

    return result.message;
}

}  // namespace todo
