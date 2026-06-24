cmake_minimum_required(VERSION 3.16)

project(MultiMaterialSlicer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets OpenGL)

add_executable(MultiMaterialSlicer
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ModelTypes.h
    src/StlMesh.cpp
    src/StlMesh.h
    src/OpenGLView.cpp
    src/OpenGLView.h
    src/SliceExporter.cpp
    src/SliceExporter.h
    src/ConfigWriter.cpp
    src/ConfigWriter.h
    src/SliceWorker.cpp
    src/SliceWorker.h
    ui/MainWindow.ui
    resources/resources.qrc
)

target_include_directories(MultiMaterialSlicer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(MultiMaterialSlicer PRIVATE Qt5::Widgets Qt5::OpenGL)

if(APPLE)
    set_target_properties(MultiMaterialSlicer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.multimaterial.slicer
        MACOSX_BUNDLE_BUNDLE_NAME MultiMaterialSlicer
        MACOSX_BUNDLE_BUNDLE_VERSION 0.1.0
        MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1.0)
    add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/slice_1080p.py
            $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/MacOS/slice_1080p.py
    )
else()
    add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/slice_1080p.py
            $<TARGET_FILE_DIR:MultiMaterialSlicer>/slice_1080p.py
    )
endif()

if(WIN32)
    set_target_properties(MultiMaterialSlicer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
