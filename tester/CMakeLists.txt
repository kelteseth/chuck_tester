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
    src/QmlDebug.cpp
    src/SourceLocation.cpp
    src/PendingWait.cpp)

set(HEADERS
    include/tester/TesterServer.h
    include/tester/QmlDebug.h
    src/CommandHandler.h
    src/ItemFinder.h
    src/PendingWait.h
    src/SourceLocation.h)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    PRIVATE Qt6::WebSockets Qt6::Test
    PUBLIC Qt6::Core Qt6::Quick Qt6::Qml)

target_compile_definitions(${PROJECT_NAME} PRIVATE TESTER_VERSION="${PROJECT_VERSION}")

# Only used to report each item's QML source location. Distro Qt packages ship
# the private headers separately, so a missing QmlPrivate just drops that field.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 QUIET COMPONENTS QmlPrivate)
if(TARGET Qt6::QmlPrivate)
    # Take only the headers: linking the target would put it into the static
    # lib's link interface, and every app linking tester would have to find it.
    target_include_directories(${PROJECT_NAME} PRIVATE $<TARGET_PROPERTY:Qt6::QmlPrivate,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TESTER_HAS_QML_PRIVATE)
else()
    message(STATUS "tester: Qt6::QmlPrivate not found, items will have no 'source' location")
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC include/
    PRIVATE src/)

message(STATUS "Configured ${PROJECT_NAME} library")

