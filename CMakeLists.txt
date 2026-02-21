cmake_minimum_required(VERSION 3.16)

project(SimpleEDTerraform VERSION 0.1.34 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network)

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
