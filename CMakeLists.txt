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
    src/console_ui.cpp
)
target_include_directories(todo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(todo_app src/main.cpp)
target_link_libraries(todo_app PRIVATE todo_core)

enable_testing()
add_executable(todo_tests tests/test_todo.cpp)
target_link_libraries(todo_tests PRIVATE todo_core)
add_test(NAME todo_tests COMMAND todo_tests)
