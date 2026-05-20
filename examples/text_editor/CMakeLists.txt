cmake_minimum_required(VERSION 3.18)
project(text_editor LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickControls2 WebSockets Test)

qt_add_executable(text_editor
    main.cpp
)

qt_add_qml_module(text_editor
    URI TextEditor
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(text_editor PRIVATE Qt6::Quick Qt6::Qml Qt6::QuickControls2 tester)

tester_deploy_example(text_editor)
