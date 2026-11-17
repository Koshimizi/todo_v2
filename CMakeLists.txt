cmake_minimum_required(VERSION 3.16)
project(TodoAppV2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings are part of the lesson: if the build is noisy, the code is suspect.
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# The application logic, built once and shared by the app and the tests.
# Splitting it out is what makes the code testable at all.
add_library(todo_core STATIC
    src/task.cpp
    src/task_store.cpp
    src/task_manager.cpp
    src/presenter.cpp
    src/console_ui.cpp
)
target_include_directories(todo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 控制台版。数据文件是"当前工作目录"下的 tasks.txt，所以调试时把工作目录
# 固定在项目根目录，免得从 build/ 里跑时看到的是另一份数据。
add_executable(todo_app src/main.cpp)
target_link_libraries(todo_app PRIVATE todo_core)
set_target_properties(todo_app PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# 图形界面版：Win32，纯代码建界面，没有 .rc、没有第三方库。
# WIN32 表示"子系统是 Windows"，入口变成 wWinMain，运行时不弹黑色控制台。
# 链接的 comctl32 / uxtheme / user32 / gdi32 全部来自 Windows SDK。
if(WIN32)
    add_executable(todo_gui WIN32 gui/win32_ui.cpp)
    target_link_libraries(todo_gui PRIVATE todo_core comctl32 uxtheme user32 gdi32)
    target_compile_definitions(todo_gui PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(todo_gui PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

enable_testing()
add_executable(todo_tests tests/test_todo.cpp)
target_link_libraries(todo_tests PRIVATE todo_core)
add_test(NAME todo_tests COMMAND todo_tests)
