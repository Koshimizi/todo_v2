// A deliberately tiny test harness: no framework to install, no magic.
// Every EXPECT either passes silently or prints the expression that failed.
//
// This file is the part the original project was missing entirely. Its job is
// to answer "did I break something?" without a human clicking through a menu.

#include "console_ui.h"
#include "presenter.h"
#include "task.h"
#include "task_manager.h"
#include "task_store.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void expect(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cout << "  FAIL line " << line << ": " << expression << '\n';
    }
}

#define EXPECT(cond) expect((cond), #cond, __LINE__)

void section(const char* name) { std::cout << "- " << name << '\n'; }

// ---------------------------------------------------------------- escaping --

void testEscaping() {
    section("escaping round-trip");

    const std::string raw = "Fix bug | urgent\\now\nsecond line";
    const std::vector<std::string> fields =
        todo::splitEscaped(todo::escapeField(raw), '|');
    EXPECT(fields.size() == 1u);
    if (fields.size() == 1u) {
        EXPECT(fields[0] == raw);
    }

    // An escaped pipe must not split the field, an unescaped one must.
    const std::vector<std::string> two = todo::splitEscaped("a\\|b|c", '|');
    EXPECT(two.size() == 2u);
    if (two.size() == 2u) {
        EXPECT(two[0] == "a|b");
        EXPECT(two[1] == "c");
    }

    // A lone trailing backslash is data, not a broken escape.
    const std::vector<std::string> trailing = todo::splitEscaped("tail\\", '|');
    EXPECT(trailing.size() == 1u);
    if (trailing.size() == 1u) {
        EXPECT(trailing[0] == "tail\\");
    }
}

// -------------------------------------------------------------- round trip --

void testFileRoundTrip() {
    section("file round-trip keeps a description containing '|'");

    const std::string path = "test_roundtrip.txt";
    std::remove(path.c_str());

    {
        // Braces, not parentheses: "TaskManager m(TaskStore(path));" would be
        // read as a function declaration (the "most vexing parse").
        todo::TaskManager manager{todo::TaskStore(path)};
        EXPECT(manager.add("Fix bug | urgent", todo::Priority::High).ok);
        std::string error;
        EXPECT(manager.save(error));
        EXPECT(error.empty());
    }

    {
        // Braces, not parentheses: "TaskManager m(TaskStore(path));" would be
        // read as a function declaration (the "most vexing parse").
        todo::TaskManager manager{todo::TaskStore(path)};
        std::string warning;
        manager.load(warning);
        EXPECT(warning.empty());
        EXPECT(manager.tasks().size() == 1u);
        if (manager.tasks().size() == 1u) {
            EXPECT(manager.tasks()[0].description() == "Fix bug | urgent");
            EXPECT(manager.tasks()[0].priority() == todo::Priority::High);
            EXPECT(!manager.tasks()[0].completed());
        }
    }

    std::remove(path.c_str());
}

// ------------------------------------------------------------- id handling --

void testIdsAreStable() {
    section("ids survive reload (the original renumbered them)");

    const std::string path = "test_ids.txt";
    std::remove(path.c_str());

    {
        // Braces, not parentheses: "TaskManager m(TaskStore(path));" would be
        // read as a function declaration (the "most vexing parse").
        todo::TaskManager manager{todo::TaskStore(path)};
        EXPECT(manager.add("first", todo::Priority::Low).ok);   // id 1
        EXPECT(manager.add("second", todo::Priority::Low).ok);  // id 2
        EXPECT(manager.add("third", todo::Priority::Low).ok);   // id 3
        EXPECT(manager.remove(2).ok);
        std::string error;
        EXPECT(manager.save(error));
    }

    todo::TaskManager manager{todo::TaskStore(path)};
    std::string warning;

    // Loading three times in a row: with a static creation counter (the old
    // design) every load pushed the ids up again.
    manager.load(warning);
    manager.load(warning);
    manager.load(warning);

    EXPECT(manager.tasks().size() == 2u);
    if (manager.tasks().size() == 2u) {
        EXPECT(manager.tasks()[0].id() == 1);
        EXPECT(manager.tasks()[1].id() == 3);
    }

    EXPECT(manager.add("fourth", todo::Priority::Low).ok);
    EXPECT(manager.tasks().size() == 3u);
    if (manager.tasks().size() == 3u) {
        // One past the biggest id on disk: no reuse, no drift.
        EXPECT(manager.tasks()[2].id() == 4);
    }

    std::remove(path.c_str());
}

// -------------------------------------------------------------- validation --

void testValidation() {
    section("bad operations are rejected, not silently accepted");

    todo::TaskManager manager{todo::TaskStore("unused_test.txt")};

    EXPECT(!manager.add("", todo::Priority::High).ok);
    EXPECT(!manager.edit(99, "x", todo::Priority::Low).ok);
    EXPECT(!manager.edit(99, "", todo::Priority::Low).ok);
    EXPECT(!manager.remove(99).ok);
    EXPECT(!manager.setCompleted(99, true).ok);
    EXPECT(manager.find(99) == nullptr);
    EXPECT(manager.tasks().empty());
}

void testCorruptLineIsSkipped() {
    section("one corrupt line does not lose the whole file");

    const std::string path = "test_corrupt.txt";
    {
        std::ofstream out(path);
        out << "1|keep me|High|0\n";
        out << "this line is broken\n";
        out << "2|also keep me|Low|1\n";
    }

    todo::TaskManager manager{todo::TaskStore(path)};
    std::string warning;
    manager.load(warning);

    EXPECT(!warning.empty());          // the problem is reported
    EXPECT(manager.tasks().size() == 2u);  // the good lines still load

    std::remove(path.c_str());
}

void testPriorityParsing() {
    section("priority text is parsed case-insensitively");

    EXPECT(todo::priorityFromString("high") == todo::Priority::High);
    EXPECT(todo::priorityFromString("HIGH") == todo::Priority::High);
    EXPECT(todo::priorityFromString("Low") == todo::Priority::Low);
    EXPECT(todo::priorityFromString("nonsense") == todo::Priority::Medium);
    EXPECT(todo::priorityToString(todo::Priority::Low) == "Low");
}

// ------------------------------------------------------------- presentation --

// presenter 里全是纯字符串，所以不需要开窗口就能把"界面显示什么"测死。
void testPresenter() {
    section("presenter turns data into display text");

    EXPECT(todo::statusLabel(true) == "已完成");
    EXPECT(todo::statusLabel(false) == "待办");
    EXPECT(todo::priorityLabel(todo::Priority::High) == "高");
    EXPECT(todo::priorityLabel(todo::Priority::Medium) == "中");
    EXPECT(todo::priorityLabel(todo::Priority::Low) == "低");

    // 换行必须压成字面量 \n，否则表格行会被撑坏
    EXPECT(todo::singleLine("plain") == "plain");
    EXPECT(todo::singleLine("a\nb") == "a\\nb");
    EXPECT(todo::singleLine("a\r\nb") == "a\\nb");  // CRLF 只算一次换行

    EXPECT(todo::summaryLabel(0, 0).find("还没有任务") == 0u);
    EXPECT(todo::summaryLabel(3, 1) == "共 3 条 · 已完成 1 条");

    EXPECT(todo::windowTitle("tasks.txt", false) == "待办清单 — tasks.txt");
    EXPECT(todo::windowTitle("tasks.txt", true) == "待办清单 — tasks.txt *");

    EXPECT(todo::resultLabel(todo::success("task added")) == "已添加任务");
    EXPECT(todo::resultLabel(todo::failure("description cannot be empty")) ==
           "任务描述不能为空");
    EXPECT(todo::resultLabel(todo::failure("no task with id 7")) == "找不到 ID 为 7 的任务");
    // 认不出来的消息原样透出，绝不能悄悄吞掉
    EXPECT(todo::resultLabel(todo::failure("something new")) == "something new");
}

// ---------------------------------------------------------------- the menu --

void testScriptedUi() {
    section("the whole menu can be driven by a string");

    const std::string path = "test_ui.txt";
    std::remove(path.c_str());

    {
        // Braces, not parentheses: "TaskManager m(TaskStore(path));" would be
        // read as a function declaration (the "most vexing parse").
        todo::TaskManager manager{todo::TaskStore(path)};
        std::istringstream input("1\nBuy milk\n2\n2\n9\n");
        std::ostringstream output;

        todo::ConsoleUi ui(manager, input, output);
        ui.run();

        EXPECT(manager.tasks().size() == 1u);
        EXPECT(output.str().find("Buy milk") != std::string::npos);
        EXPECT(output.str().find("Goodbye") != std::string::npos);
    }

    // Quitting saved, so a fresh manager must see the task again.
    {
        // Braces, not parentheses: "TaskManager m(TaskStore(path));" would be
        // read as a function declaration (the "most vexing parse").
        todo::TaskManager manager{todo::TaskStore(path)};
        std::string warning;
        manager.load(warning);
        EXPECT(manager.tasks().size() == 1u);
        if (manager.tasks().size() == 1u) {
            EXPECT(manager.tasks()[0].description() == "Buy milk");
            EXPECT(manager.tasks()[0].priority() == todo::Priority::Medium);
        }
    }

    std::remove(path.c_str());
}

void testEofDoesNotHang() {
    section("input that ends mid-menu terminates instead of spinning");

    todo::TaskManager manager{todo::TaskStore("test_eof.txt")};
    std::istringstream input("1\n");
    std::ostringstream output;

    todo::ConsoleUi ui(manager, input, output);
    ui.run();  // must return

    EXPECT(manager.tasks().empty());  // no description was ever typed
}

void testSaveFailureIsReported() {
    section("a failed save is reported, not swallowed");

    todo::TaskManager manager{todo::TaskStore("no_such_dir/test.txt")};
    std::string error;
    const bool saved = manager.save(error);

    EXPECT(!saved);
    EXPECT(!error.empty());
}

}  // namespace

int main() {
    testEscaping();
    testFileRoundTrip();
    testIdsAreStable();
    testValidation();
    testCorruptLineIsSkipped();
    testPriorityParsing();
    testPresenter();
    testScriptedUi();
    testEofDoesNotHang();
    testSaveFailureIsReported();

    std::cout << '\n' << g_checks << " checks, " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
