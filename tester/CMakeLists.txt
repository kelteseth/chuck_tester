cmake_minimum_required(VERSION 3.18)

project(tester VERSION 0.2 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml WebSockets Test)

set(SOURCES
    src/TesterServer.cpp
    src/CommandHandler.cpp
    src/ItemFinder.cpp
    src/PendingWait.cpp)

set(HEADERS
    include/tester/TesterServer.h
    src/CommandHandler.h
    src/ItemFinder.h
    src/PendingWait.h)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    PRIVATE Qt6::WebSockets Qt6::Test
    PUBLIC Qt6::Core Qt6::Quick Qt6::Qml)

target_compile_definitions(${PROJECT_NAME} PRIVATE TESTER_VERSION="${PROJECT_VERSION}")

target_include_directories(${PROJECT_NAME}
    PUBLIC include/
    PRIVATE src/)

message(STATUS "Configured ${PROJECT_NAME} library")

