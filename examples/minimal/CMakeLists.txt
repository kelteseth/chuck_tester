cmake_minimum_required(VERSION 3.18)
project(minimal_example LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml WebSockets Test)

qt_add_executable(minimal_example main.cpp)
qt_add_qml_module(minimal_example URI MinimalExample VERSION 1.0 QML_FILES Main.qml)

target_link_libraries(minimal_example PRIVATE Qt6::Quick Qt6::Qml tester)

tester_deploy_example(minimal_example)
