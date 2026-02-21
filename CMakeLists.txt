cmake_minimum_required(VERSION 3.16)

project(SimpleEDTerraform VERSION 0.1.43 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network Test)

add_executable(SimpleEDTerraform
    src/main.cpp
    src/MainWindow.cpp
    src/EdsmApiClient.cpp
    src/SystemModelBuilder.cpp
    src/SystemLayoutEngine.cpp
    src/OrbitClassifier.cpp
    src/SystemSceneWidget.cpp
    src/SystemIdsWindow.cpp
)

target_include_directories(SimpleEDTerraform PRIVATE src)

target_link_libraries(SimpleEDTerraform PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
)


enable_testing()

add_executable(SimpleEDTerraformTests
    tests/EdastroHierarchyTests.cpp
    src/EdsmApiClient.cpp
    src/OrbitClassifier.cpp
)

target_include_directories(SimpleEDTerraformTests PRIVATE src)

target_link_libraries(SimpleEDTerraformTests PRIVATE
    Qt5::Core
    Qt5::Network
    Qt5::Test
)

add_test(NAME SimpleEDTerraformTests COMMAND SimpleEDTerraformTests)
