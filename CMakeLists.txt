cmake_minimum_required(VERSION 3.21)
project(omacalc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Test)

# Everything but main.cpp, shared by the app and the tests, so each source
# compiles once and a new file in src/ needs no line here.
file(GLOB omacalc_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM omacalc_sources ${CMAKE_SOURCE_DIR}/src/main.cpp)
add_library(omacalc_core STATIC ${omacalc_sources})
target_include_directories(omacalc_core PUBLIC src)
target_link_libraries(omacalc_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus)
target_compile_options(omacalc_core PUBLIC -Wall -Wextra)

add_executable(omacalc src/main.cpp src/resources.qrc)
target_link_libraries(omacalc PRIVATE omacalc_core)

enable_testing()
add_executable(omacalc_tests tests/omacalc_tests.cpp)
target_link_libraries(omacalc_tests PRIVATE omacalc_core Qt6::Test)
add_test(NAME omacalc_tests COMMAND omacalc_tests)
