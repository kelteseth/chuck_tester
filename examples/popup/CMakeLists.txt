cmake_minimum_required(VERSION 3.18)
project(popup_example LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickControls2 WebSockets Test)

qt_add_executable(popup_example main.cpp)

qt_add_qml_module(popup_example
    URI PopupExample
    VERSION 1.0
    QML_FILES Main.qml)

target_link_libraries(popup_example PRIVATE Qt6::Quick Qt6::Qml Qt6::QuickControls2 tester)

tester_deploy_example(popup_example)
