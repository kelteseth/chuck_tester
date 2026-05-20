cmake_minimum_required(VERSION 3.18)
project(singleton_example LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml WebSockets Test)

qt_add_executable(singleton_example main.cpp)

qt_add_qml_module(singleton_example
    URI SingletonExample
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES AppState.h)

target_link_libraries(singleton_example PRIVATE Qt6::Quick Qt6::Qml tester)

tester_deploy_example(singleton_example)
