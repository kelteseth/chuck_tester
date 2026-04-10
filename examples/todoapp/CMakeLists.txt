cmake_minimum_required(VERSION 3.18)
project(todoapp LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickControls2 WebSockets Test)

qt_add_executable(todoapp
    main.cpp
    Todo.h
    Todo.cpp
)

qt_add_qml_module(todoapp
    URI TodoApp
    VERSION 1.0
    QML_FILES
        Main.qml
        TodoListPage.qml
        TodoEditPage.qml
    RESOURCES
        qtquickcontrols2.conf
)

target_link_libraries(todoapp PRIVATE Qt6::Quick Qt6::Qml Qt6::QuickControls2 chuck)
