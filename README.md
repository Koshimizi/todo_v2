# C++ To-Do List（重构版）

[repo_todo](../repo_todo) 的修好 + 分层 + 可测试版本。用来给"会刷力扣但没有项目经验"的同学做第一个真正的工程练习。

- **想动手**：先看 [ISSUES.md](ISSUES.md)，那是一份带复现步骤的 bug 清单，请在**旧版**上一条条跑出来。
- **想理解为什么这么改**：看 [REFACTOR_NOTES.md](REFACTOR_NOTES.md)。

## 构建

需要 CMake 3.16+ 和一个 C++17 编译器。

```bash
cmake -S . -B build
cmake --build build
```

Windows 上如果默认生成的是 Visual Studio 工程：

```bash
cmake -S . -B build
cmake --build build --config Debug
```

## 运行

```bash
# Linux / macOS / MinGW
./build/todo_app

# Visual Studio 生成器
./build/Debug/todo_app.exe
```

数据存在**当前工作目录**的 `tasks.txt`。所以从 `build/` 里跑和从项目根跑，看到的是两份不同的数据——这是最容易让人困惑的一点，改之前先确认自己在哪个目录。

## 测试

```bash
cd build
ctest -C Debug --output-on-failure
```

或者直接跑测试程序看逐项输出：

```bash
./build/Debug/todo_tests.exe
```

预期看到：

```
- escaping round-trip
- file round-trip keeps a description containing '|'
- ids survive reload (the original renumbered them)
...
49 checks, 0 failure(s)
```

## 目录

```
todo_v2/
├── CMakeLists.txt
├── README.md              ← 你在这里
├── ISSUES.md              ← 旧版的 7 个问题 + 复现步骤（练习单）
├── REFACTOR_NOTES.md      ← 设计决定、踩坑记录、七天计划
├── include/
│   ├── task.h             ← 纯数据
│   ├── task_store.h       ← 文件格式与转义
│   ├── task_manager.h     ← 业务规则，返回 Result
│   └── console_ui.h       ← 所有输入输出
├── src/
│   ├── main.cpp           ← 装配，10 行
│   ├── task.cpp
│   ├── task_store.cpp
│   ├── task_manager.cpp
│   └── console_ui.cpp
└── tests/
    └── test_todo.cpp      ← 无框架，只有 EXPECT
```

## 数据格式

每行一条任务，`|` 分隔四个字段：

```
<id>|<描述>|<优先级>|<状态>
```

- 描述里的 `|`、`\`、换行会被转义成 `\|`、`\\`、`\n`
- 优先级：`High` / `Medium` / `Low`（大小写不敏感，认不出就当 `Medium`）
- 状态：`0` 未完成，`1` 已完成

```
1|Study C++ classes|High|0
2|Fix bug \| urgent|Medium|1
```

**和旧版的区别**：多了 ID 这一列。这就是"ID 不再跳号"的全部秘密。

## 菜单

```
1. Add task
2. View all tasks
3. Mark task complete
4. Mark task incomplete
5. Edit task
6. Delete task
7. Save tasks
8. Load tasks
9. Save and quit
```

（旧版 README 写的是 8 项，和程序实际不符——这里已经对齐了。）
