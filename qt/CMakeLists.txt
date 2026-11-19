cmake_minimum_required(VERSION 3.16)
project(Proyecto_Final_Informatica_2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort)
add_executable(Proyecto_Final_Informatica_2
    main.cpp mainwindow.cpp mainwindow.h ambient_classification.h jointbar.cpp jointbar.h)
target_link_libraries(Proyecto_Final_Informatica_2
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::SerialPort)
