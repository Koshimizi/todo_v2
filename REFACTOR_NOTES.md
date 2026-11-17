# 重构说明

这个目录是旧版 [repo_todo](../repo_todo) 的**修好 + 分层 + 可测试**版本。
读它之前，先按 [ISSUES.md](ISSUES.md) 在旧版上把 bug 亲手复现一遍——不然你会觉得"本来就该这么写"，学不到东西。

## 前后对照

| | 旧版 | 新版 |
|---|---|---|
| 源文件 | 5 个 | 9 个（含 4 个头文件） |
| 代码行数 | 475 | 785 |
| 测试 | 0 | 272 行 / 49 个断言 |
| 编译警告（MSVC /W4） | 2 个 | 0 个 |
| 已知行为 bug | 4 个（ID 跳号、`\|` 损坏、EOF 死循环、编辑空描述不校验） | 0 |
| 中文 Windows 乱码 | 是 | 否（输出全 ASCII） |
| 能否自动测试 | 不能 | 能（`ctest`） |

**行数变多了 65%，这不是退步。** 增加的部分全都在做旧版没做的事：错误处理、转义、分层接口、测试。旧版是"能跑就行"，新版是"能改、能测、能维护"。真实项目的代码量大多花在这些地方，而不是花在功能上——这一点是力扣永远不会告诉你的。

## 新的分层

```
                +------------------+
                |     main.cpp     |   只负责把东西接起来（10 行）
                +--------+---------+
                         |
                +--------v---------+
                |   ConsoleUi      |   所有 cin / cout 都在这里
                |  菜单、提示、显示  |   流是注入的，所以能被测试驱动
                +--------+---------+
                         |
                +--------v---------+
                |   TaskManager    |   业务规则：发 ID、增删改、校验
                |  返回 Result      |   不打印任何东西
                +--------+---------+
                         |
                +--------v---------+
                |    TaskStore     |   只负责文件读写 + 格式转义
                |  load() / save() |
                +--------+---------+
                         |
                +--------v---------+
                |      Task        |   纯数据：4 个字段 + 读写方法
                |  无任何 I/O       |   一行 cout 都没有
                +------------------+
```

一句话记住这套东西为什么这么分：**每一层只能"往下"依赖，而且只做一件事**。
`Task` 不知道有文件，`TaskStore` 不知道有菜单，`TaskManager` 不知道有屏幕，`ConsoleUi` 不知道数据存在哪。

好处很具体：

- 想加"按优先级排序"？只动 `TaskManager`，界面和存储都不用碰。
- 想换 JSON 存储？只重写 `TaskStore`，其他三层的代码一个字都不用改。
- 想加图形界面？写一个新的 UI 类去调 `TaskManager`，现在的代码全留着。

## 九个关键决定

### 1. ID 存进文件，不再用静态计数器

旧版靠 `static int taskCount` 递增发号，于是"每 Load 一次就跳号"。
新版把 ID 写进文件第一列，加载时 `nextId = 最大ID + 1`。

```cpp
// task_manager.cpp
void TaskManager::assignIdsFromTasks() {
    int highest = kFirstId - 1;
    for (const Task& task : tasks_) {
        if (task.id() > highest) highest = task.id();
    }
    nextId_ = highest + 1;
}
```

**学到的原则**：凡是"必须唯一且长期不变"的东西，就应该被持久化，而不是从别的状态推导出来。

### 2. 优先级用 `enum class`，不用裸字符串

```cpp
enum class Priority { Low = 0, Medium = 1, High = 2 };
```

旧版用 `"High"` / `"Medium"` / `"Low"` 三个字符串传来传去，打错字编译器不管，运行时才发现。
换成枚举后，`Priority::Hihg` 直接编译不过。**把错误从运行时提前到编译期，是 C++ 最值钱的用法之一。**

### 3. 转义，让描述可以包含任何字符

写入 `|` → `\|`，`\` → `\\`，换行 → `\n`；读取时按"是否被转义"决定切不切。
见 [task_store.cpp](src/task_store.cpp) 的 `escapeField` 和 `splitEscaped`。

两个函数故意做成 public，因为**测试要能直接盯住磁盘格式**。

### 4. 一行坏数据不能让整个文件报废

旧版对字段数不作任何检查，错位了就把垃圾当数据吃进去。
新版：字段数不是 4、ID 解析不出来 → 跳过这一行、记下警告、继续读下一行。

```cpp
EXPECT(!warning.empty());              // 问题被报告
EXPECT(manager.tasks().size() == 2u);  // 好的行照样加载
```

**学到的原则**：解析外部数据时，默认它可能是坏的。宁可丢一行并说出来，也不要静悄悄地丢全部。

### 5. 操作返回结果，而不是自己打印

```cpp
struct Result { bool ok; std::string message; };

Result TaskManager::add(std::string description, Priority priority);
```

旧版 `addTask()` 打印完就完了，你**没法在代码里问"它到底成功了没有"**。
这就是它没法测试的根因——不是"作者懒"，是**接口设计让测试变得不可能**。

### 6. 流是注入的

```cpp
ConsoleUi ui(manager, input, output);   // 测试里传 istringstream / ostringstream
```

于是整个菜单可以在测试里被一段字符串驱动，跑一次只要几毫秒：

```cpp
std::istringstream input("1\nBuy milk\n2\n2\n9\n");
ui.run();
EXPECT(manager.tasks().size() == 1u);
```

**这一条是"能测试"和"不能测试"的分水岭。** 直接写 `cin`/`cout` 的实现，永远只能靠人手点。

### 7. 输入纪律集中在一个地方

旧版每个 `case` 里散落着 `cin.ignore()`，改一处忘一处（编辑路径就漏了空描述校验）。
新版 `readInt()` 读完整行、`readLine()` 读完整行，**调用方永远不需要关心缓冲区里剩什么**。

### 8. 头文件不用 `using namespace std;`，字符串不按值传

见 [task.h](include/task.h)：全写 `std::string`；构造函数按值收 + `std::move`，getter 返回 `const std::string&`。

### 9. 输出全 ASCII，编译加 `/utf-8`

去掉所有 emoji，用 `[OK]` / `[X]` / `[!]` 表示状态，任何代码页的终端都不会乱码。
`CMakeLists.txt` 里给 MSVC 加 `/utf-8`，把"源文件是 UTF-8"这件事显式告诉编译器。

## 我写的时候踩到的三个坑（都值得你知道）

### 坑一：most vexing parse

测试里写：

```cpp
todo::TaskManager manager(todo::TaskStore(path));   // 编译报错：".add 左边必须有类"
```

`path` 是个**变量**，所以编译器把整行解释成了**函数声明**——一个叫 `manager`、参数类型是 `TaskStore`、返回 `TaskManager` 的函数。

改成花括号就好了：

```cpp
todo::TaskManager manager{todo::TaskStore(path)};
```

（`main.cpp` 里同样的写法因为传的是字面量 `"tasks.txt"`，无法被当成参数名，反而没触发。）

**这就是"能编译 ≠ 是我想要的意思"的经典案例。**

### 坑二：`clear()` 会把 `eofbit` 一起清掉

见 [ISSUES.md 问题 7](ISSUES.md)。修法不是"别用 clear"，而是**先判断 `eof()`，再 clear**。

### 坑三：`size_t` 和 `int` 混用

`tasks.size()` 是 `size_t`（无符号），`findTaskById` 返回 `int`。旧版：

```cpp
int TaskManager::findTaskById(int id) {
    for (int i = 0; i < tasks.size(); i++)   // 有符号/无符号比较
```

MSVC `/W4` 直接报了 C4267。新版用 `std::size_t` 循环 + 显式 `static_cast<std::ptrdiff_t>` 转成 `erase` 要的类型。

**警告不是噪音。** 那些"能跑但编译器在抱怨"的地方，通常正是隐藏 bug 的地方。

## 建议的阅读顺序

不要从 `main.cpp` 开始。按**依赖从下往上**读：

1. [include/task.h](include/task.h) + [src/task.cpp](src/task.cpp) —— 40 行，先看懂"纯数据类"长什么样
2. [include/task_store.h](include/task_store.h) → [src/task_store.cpp](src/task_store.cpp) 的 `escapeField` / `splitEscaped` —— 最难但最有价值的部分
3. [include/task_manager.h](include/task_manager.h) → [src/task_manager.cpp](src/task_manager.cpp) —— 看 `Result` 怎么替代"直接打印"
4. [src/console_ui.cpp](src/console_ui.cpp) —— 最后看，此时你已经知道它在调什么了
5. [src/main.cpp](src/main.cpp) —— 10 行，看"装配"该有多短
6. [tests/test_todo.cpp](tests/test_todo.cpp) —— 回头看测试，每个 `EXPECT` 都是前面某个决定的存在理由

## 故意没做的事（留给你）

这个版本刻意停在"够用且好读"，下面这些**都不难，但会让代码变复杂**，所以留给你：

1. **原子写入**：先写 `tasks.txt.tmp`，成功了再 `rename` 覆盖。现在如果写到一半断电，文件会半残。这是真实的工业级细节。
2. **保存前自动保存**：现在退出时才存。断电就丢。
3. **排序 / 搜索 / 过滤**：`std::sort` + 比较器，正好练力扣里的排序思维，但这次对象是你自己的类。
4. **撤销**：用 `std::stack` 存操作历史。
5. **CSV / JSON 存储**：对比一下转义方案和 JSON 库方案的取舍。
6. **CI**：GitHub Actions 里跑 `ctest`，让"测试通过"变成自动的。
7. **文件锁 / 多实例**：两个程序同时开，会互相覆盖。

## 七天计划对应到哪里

| 天 | 做什么 | 看什么 |
|---|---|---|
| 1 | 只复现 bug，不写代码 | [ISSUES.md](ISSUES.md) 问题 1-3、7 |
| 2-3 | 逐个修 ID / 转义 / 编码 | [task_store.cpp](src/task_store.cpp)、[task_manager.cpp](src/task_manager.cpp) 对照 |
| 4 | 把 `cout` 从数据和逻辑里剥出去 | [task_manager.cpp](src/task_manager.cpp) 的 `Result` 用法 |
| 5 | 改头文件：`std::` 全名、`const&` | [task.h](include/task.h)、[task_manager.h](include/task_manager.h) |
| 6 | 写测试（先写测试再改代码，会更爽） | [test_todo.cpp](tests/test_todo.cpp) |
| 7 | 换个领域自己重写：记账本 / 图书借阅 / 单词本 | 只允许看自己第 4 天写的笔记 |

第 7 天的规则很重要：**不许再打开这个仓库**。能独立重写出来，才算真的会了。
