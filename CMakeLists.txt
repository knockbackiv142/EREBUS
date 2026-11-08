cmake_minimum_required(VERSION 3.24)
project(Erebus VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenMP)
find_package(HDF5 COMPONENTS CXX QUIET)

add_executable(erebus
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/physics/Body.h
    src/physics/Simulation.cpp
    src/physics/Simulation.h
    src/telemetry/TelemetryRecorder.h
    src/render/ChartWidget.h
    src/render/ChartWidget.cpp
    src/render/HeatmapWidget.h
    src/render/HeatmapWidget.cpp
    src/render/TelemetryPanel.h
    src/render/TelemetryPanel.cpp
    src/render/NotesPanel.h
    src/render/NotesPanel.cpp
    src/render/UniverseView.cpp
    src/render/UniverseView.h
)

target_include_directories(erebus PRIVATE src)

target_link_libraries(erebus PRIVATE
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets OpenGL::GL Eigen3::Eigen
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(erebus PRIVATE OpenMP::OpenMP_CXX)
endif()

if(HDF5_FOUND)
    target_compile_definitions(erebus PRIVATE EREBUS_HAS_HDF5=1)
    target_include_directories(erebus PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(erebus PRIVATE ${HDF5_CXX_LIBRARIES})
endif()

target_compile_options(erebus PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
