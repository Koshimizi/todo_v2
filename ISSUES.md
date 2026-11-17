# 问题复现清单

这份清单针对的是**旧版本**（[repo_todo](../repo_todo)），不是本目录的 `todo_v2`。
`todo_v2` 是「这些问题都修好之后」的样子——所以这个文件同时是一份**练习单**和一份**对照答案**。

## 怎么用

1. 先**别读** `todo_v2` 的代码。
2. 在旧版本上按「复现步骤」亲手跑一遍，亲眼看到问题。**看到现象**比读十遍解释有用。
3. 自己试着修。修不动再看「参考做法」。
4. 全部修完，再打开 `todo_v2` 对比——差异会非常刺眼，那时候你就真的懂了。

建议顺序：问题 1 → 7 依次做。其中 **1、2、3、7 是"跑起来就能看见"的行为 bug**；**4、5、6 是"能跑但会养成坏习惯"的设计问题**；附加 A、B 属于工程层缺失，放到最后。

---

## 问题 1：任务 ID 会莫名跳号

**严重程度**：高（会被当成"玄学 bug"，最打击新手信心）

**复现步骤**

```
启动程序 → 按 1 加两条任务 → 按 9 退出
再启动程序 → 按 8(Load) → 再按 8(Load) → 按 2(View)
```

**实际结果**（我在本机跑出来的）

```
✓ 2 task(s) loaded from tasks.txt!      ← 启动时自动 load 一次
✓ 2 task(s) loaded from tasks.txt!      ← 你按的第一个 8
✓ 2 task(s) loaded from tasks.txt!      ← 你按的第二个 8
ID: 5 | Status: [ ] | Priority: High   | Task: Study C++ classes
ID: 6 | Status: [ ] | Priority: Medium | Task: Buy milk
```

只有两条任务，ID 却变成了 5 和 6。再新增一条是 7。

**为什么会这样**

- [Task.cpp:11](../repo_todo/src/Task.cpp#L11) 用 `id = ++taskCount;` 发号，`taskCount` 是**静态变量**，全程序只有一个。
- [TaskManager.cpp:121](../repo_todo/src/TaskManager.cpp#L121) 的 `loadTasks()` 会先 `tasks.clear()`，再把文件里的每一行**重新构造成新的 Task 对象**——每构造一个，`taskCount` 就 +1。
- 所以**每按一次 Load，计数器就往前跳一次**，而文件里根本没存 ID。

还有更隐蔽的一半：`tasks.txt` 里没有 ID，所以**重启后 ID 会按行号重排**。你在会话里叫"3 号任务"的东西，重启后可能变成 2 号。

**根因一句话**：把"身份"绑在了"创建顺序"上，而身份没有被持久化。

**参考做法**（[todo_v2/src/task_store.cpp](src/task_store.cpp)、[task_manager.cpp](src/task_manager.cpp)）

- ID 写进文件，成为数据的一部分：`1|Study C++ classes|High|0`
- 加载时 `nextId = 最大 ID + 1`，不再依赖任何计数器
- 结果：ID 一旦分配就永不改变，删除中间任务也不会让别人改号

**验收标准**：连按十次 Load，ID 一动不动；删掉 2 号后重启，剩下的是 1 和 3，新增的是 4。

---

## 问题 2：任务描述里带 `|` 会静默损坏数据

**严重程度**：高（不报错的数据损坏，最危险的一类）

**复现步骤**

```
按 1 新增任务，描述输入：Fix bug | urgent
优先级选 1(High)，然后按 9 退出
查看 tasks.txt，再重新启动按 2(View)
```

**实际结果**

存进去的文件是：

```
Fix bug | urgent|High|0
```

重新加载后显示：

```
ID: 1 | Status: [ ] | Priority:  urgent | Task: Fix bug
```

优先级从 `High` 变成了 ` urgent`，真正的 `High` 和状态位 `0` 被整个挤掉了。
**程序全程不报任何错。**

**为什么会这样**

- [TaskManager.cpp:103](../repo_todo/src/TaskManager.cpp#L103) 保存时用 `|` 拼字段，但对描述内容不做任何处理。
- [TaskManager.cpp:132](../repo_todo/src/TaskManager.cpp#L132) 读取时用 `getline(ss, ..., '|')` 按 `|` 切——描述里那个 `|` 被当成了字段分隔符。
- 字段数对不上时，代码没有任何校验，直接把错位的结果当真。

**参考做法**（[todo_v2/src/task_store.cpp](src/task_store.cpp)）

- 写入时转义：`|` → `\|`，`\` → `\\`，换行 → `\n`
- 读取时按"是否被转义"来切分，而不是傻切
- 字段数不是 4 就**跳过该行并记录警告**，而不是硬解析

真正的工程选择其实有三种，都值得知道：
1. 转义（本项目采用，零依赖，但要小心处理）
2. 换分隔符 + 输入校验（治标）
3. 用 JSON / SQLite（治本，但要引入依赖）——**这就是"为什么真实项目最后都会用数据库/序列化库"的起点**

**验收标准**：描述里同时包含 `|`、`\`、换行，存盘再加载必须原样回来。

---

## 问题 3：在中文 Windows 上必然乱码

**严重程度**：中（不致命，但会让新手误判"我环境坏了"）

**复现步骤**：直接在中文版 Windows 的 cmd / PowerShell 里跑。

**实际结果**

```
�?No saved tasks found. Starting fresh!
�?Task added successfully! (ID: 1)
```

编译时也会报：

```
warning C4819: 该文件包含不能在当前代码页(936)中表示的字符
```

**为什么会这样**

- 源码是 UTF-8，里面用了 `✓ ✗ 📋 💾 👋`（[main.cpp:33](../repo_todo/src/main.cpp#L33)、[main.cpp:117](../repo_todo/src/main.cpp#L117)、[main.cpp:119](../repo_todo/src/main.cpp#L119) 等）。
- 中文 Windows 的控制台默认代码页是 **936(GBK)**，不认识这些字符。
- MSVC 在没加 `/utf-8` 时按系统代码页理解源文件，所以连编译都在警告。

**参考做法**

- 控制台程序的输出统一用 **ASCII**：`[OK]` / `[X]` / `[!]`。跨平台、跨代码页、跨终端都不会炸。
- 编译加 `/utf-8`，把"源文件是 UTF-8"这件事显式告诉编译器（[todo_v2/CMakeLists.txt](CMakeLists.txt)）。
- 真要输出中文/emoji，就得在运行时把控制台设成 UTF-8 代码页（`SetConsoleOutputCP(CP_UTF8)`），并处理终端不支持的情况——这属于额外工作量，不是初学该踩的坑。

**验收标准**：在 936 代码页的 PowerShell 里，输出一个乱码都没有。

---

## 问题 4：头文件里写了 `using namespace std;`

**严重程度**：中（不影响跑，但会形成长期坏习惯）

**位置**：[Task.h:5](../repo_todo/include/Task.h#L5)、[TaskManager.h:7](../repo_todo/include/TaskManager.h#L7)

```cpp
#include <string>
using namespace std;   // ← 在头文件里
```

**为什么危险**

头文件会被别人 `#include`，于是**每一个**包含它的文件都被强行拖进 `std` 命名空间。后果：

- 你自己的 `count`、`distance`、`data`、`size`、`swap` 这类名字可能和 `std::` 撞车，报出莫名其妙的编译错误；
- 这在单人小项目里几乎看不出来，所以特别容易养成；
- 一旦进了多人项目，你写的东西会成为别人的麻烦。

**参考做法**：头文件里一律写全名 `std::string`；实在想省事，只在 `.cpp` 里用，或者用 `using std::string;` 这种**窄化**写法。`todo_v2` 全部写全名。

**验收标准**：你的所有 `.h` 文件里搜不到 `using namespace`。

---

## 问题 5：`string` 参数全部按值传

**严重程度**：中（性能 + 习惯）

**位置**：[Task.h:17](../repo_todo/include/Task.h#L17) `Task(string desc, string prio);`、[TaskManager.h:22-27](../repo_todo/include/TaskManager.h#L22-L27)、以及所有 getter

**为什么会这样**

按值传参意味着**每次调用都完整复制一份字符串**。`getDescription()` 返回 `string` 也是复制。在力扣里这无所谓（一次调用、一个字符串），在真实程序里"列表里有 1 万条任务、每次显示都全量复制"就会疼。

**参考做法**

- 传进来的、不修改的：`const std::string&`
- 要存起来的：按值传 + `std::move`（[task.cpp](src/task.cpp) 的构造函数就是这么做的，既能接受左值也能接受右值，还只复制一次）
- 返回成员字符串：`const std::string&`

**验收标准**：你的 public 接口里，没有一处是"按值传 `std::string` 只是为了读一下"。

---

## 问题 6：数据和打印黏在一起

**严重程度**：架构层面（现在不疼，将来很疼）

**位置**：[Task.cpp:53](../repo_todo/src/Task.cpp#L53) 的 `Task::display()`、[TaskManager.cpp](../repo_todo/src/TaskManager.cpp) 里几乎每个方法

```cpp
void Task::display() const {
    cout << "ID: " << id << ... ;   // 数据类自己负责打印
}

void TaskManager::addTask(string description, string priority) {
    ...
    cout << "\n✓ Task added successfully! ..." << endl;   // 业务逻辑自己负责打印
}
```

**为什么是问题**

- `Task` 作为数据类，被绑死在"控制台"这一个出口上。将来要加图形界面、网页、或者只是想把输出写进日志文件，就得改动数据类本身。
- 更现实的痛：**这样没法测试**。你无法在测试里"验证添加成功后列表里多了一条"，因为方法不返回任何结果，只往屏幕上打了一行字。这一条直接导致了问题 8。

**参考做法**（[todo_v2/include/task_manager.h](include/task_manager.h)）

- `Task` 变成纯数据：只有 id / 描述 / 优先级 / 状态和对应的读写方法，**一行 `cout` 都没有**
- `TaskManager` 的操作返回 `Result{bool ok; std::string message;}`，**只报告结果，不负责展示**
- 所有输入输出集中在 `ConsoleUi` 里，而且 `cin`/`cout` 是**传进去的**（[console_ui.h](include/console_ui.h)），所以测试可以用字符串驱动整个菜单

**验收标准**：在 `grep -r "cout"` 的结果里，只有 `console_ui.cpp` 和 `main.cpp` 出现。

---

## 问题 7：输入结束（EOF）时会无限死循环

**严重程度**：高（程序挂死，而且是最诡异的一种挂法）

**复现步骤**

```bash
# 用管道把输入喂完，或者交互式地按 Ctrl+Z / Ctrl+D
echo 1 | todo_app.exe
```

**实际结果**

程序不退出，开始**疯狂打印**：

```
Invalid input! Please enter a number: Invalid input! Please enter a number: ...
```

四秒之内就能写满输出文件，只能手动杀进程。我在本机实测确认。

**为什么会这样**

看 [main.cpp:174-181](../repo_todo/src/main.cpp#L174-L181)：

```cpp
int getIntInput() {
    int value;
    while (!(cin >> value)) {
        cin.clear();              // ← 问题在这一行
        cin.ignore(10000, '\n');
        cout << "Invalid input! Please enter a number: ";
    }
    return value;
}
```

`cin.clear()` 不带参数时，清除的是**全部**状态位——包括 `eofbit`。

于是：输入已经结束了 → `cin >> value` 失败（`failbit` + `eofbit`）→ `clear()` 把 `eofbit` 也擦了 → 下一轮 `cin >> value` 又在 EOF 上失败 → 再 `clear()`……**永远出不来**。

`cin.ignore()` 在 EOF 上同样会失败，所以它也帮不上忙。

**这个坑特别值得记住**，因为它看起来非常"稳妥"：清错误、跳过坏字符、提示重试，教科书标准写法。但它少了一个前提判断——**输入流是不是已经彻底没数据了**。

**参考做法**（[todo_v2/src/console_ui.cpp](src/console_ui.cpp) 的 `readInt`）

```cpp
if (in_ >> value) { ...读到了... }
if (in_.eof()) { running_ = false; return 0; }   // 先判断"还有没有输入"
in_.clear();                                      // 才轮到清错
in_.ignore(...);
```

顺序很关键：**先判断 EOF，再 clear**。一旦输入结束，就正常收摊，而不是把它当成"用户输入错了"。

测试里有一个专门盯这条的用例：

```cpp
std::istringstream input("1\n");   // 输入在这里就断了
ui.run();                          // 必须返回，不能挂死
```

**验收标准**：`echo 1 | todo_app.exe` 立刻退出，不打印任何 "Invalid input"。

---

## 附加 A：文档和程序对不上

**位置**：[README.md:128-135](../repo_todo/README.md#L128-L135)

README 里的菜单是 **8 项**，程序实际是 **9 项**（多了「4. Mark Task Incomplete」），顺序也不一样。

同类问题：

- [README.md:22-28](../repo_todo/README.md#L22-L28) 项目结构树里 `include/` 出现了两次
- [README.md:42](../repo_todo/README.md#L42) 「Learning Concepts」里写 "Structs"，但代码里是 `class`，全项目没有 struct
- [.gitignore:30](../repo_todo/.gitignore#L30) 忽略了 `Makefile`，可仓库自带一个手写的 `Makefile`——新手改它时会疑惑"为什么我的修改没被 git 记录"
- README 声称用 `-Wall -Wextra` 是干净构建，实际 MSVC `/W4` 下有 2 个警告

**为什么值得单独列一条**：文档漂移是真实项目里最常见的"技术债"形态，而它**恰好是新手最容易贡献、也最容易忽略**的地方。养成"改代码就同步改文档"的习惯，从第一个项目开始。

---

## 附加 B：一个测试都没有

项目零测试、零 CI。这意味着：

- 你改完代码，唯一的验证方式是**手动点一遍菜单**。改动越多，越不敢改。
- 力扣给了你一个坏习惯：**永远有人在帮你判题**。真实项目里没有判题机，`main` 返回 0 只代表"没崩"。

`todo_v2` 补了一小套测试（[tests/test_todo.cpp](tests/test_todo.cpp)，272 行，49 个断言），故意**不用任何测试框架**——就是几个 `if` 加一个计数器，所以你不会有"又要装东西"的门槛。

它专门覆盖了上面这些坑：

```cpp
// 连按三次 Load，ID 必须不变
manager.load(warning);
manager.load(warning);
manager.load(warning);
EXPECT(manager.tasks()[0].id() == 1);

// 描述里带 | 必须原样往返
EXPECT(manager.tasks()[0].description() == "Fix bug | urgent");
```

跑法：

```bash
cd build && ctest -C Debug --output-on-failure
```

---

## 修完之后，你应该能回答这些

不写代码，只用嘴说清楚。说得清，说明真会了：

1. 为什么 `static int taskCount` 不适合做 ID？换成什么？为什么？
2. `tasks.txt` 里为什么要存 ID？不存会怎样？
3. 为什么"描述里不能有 `|`"这种约束不可靠？除了转义还有什么做法？
4. `const std::string&` 和 `std::string` 做参数，区别是什么？什么时候必须按值传？
5. 为什么 `Task` 不该有 `display()` 方法？那它的信息由谁负责显示？
6. 如果没有测试，你怎么证明"我这次改动没有破坏保存功能"？
7. 一个函数既做事又打印，会给你带来什么具体麻烦？（提示：想想怎么自动验证"添加成功"）
