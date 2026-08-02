cmake_minimum_required(VERSION 3.10)
project(kblswitch LANGUAGES CXX)

# Настройка для Windows
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    
    # Флаги компиляции
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /EHsc")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    
    # Настройка для сборки Windows приложения
    set(WIN32_EXECUTABLE TRUE)
endif()

# Исходные файлы
set(SOURCES
    kblswitch.cpp
    input.cpp
    overlay.cpp
    tray_menu.cpp
    settings_apply.cpp
    setting.cpp
    lng_manager.cpp
)

# Файлы ресурсов
set(RESOURCES
    resource.rc
)

# Добавляем исполняемый файл
add_executable(kblswitch ${SOURCES} ${RESOURCES})

# Целевые свойства для Windows
if(WIN32)
    # Подключаем необходимые библиотеки
    target_link_libraries(kblswitch
        user32
        gdi32
        shell32
        advapi32
        dwmapi
        comctl32
        gdiplus
        comdlg32
    )
    
    # Устанавливаем подсистему Windows (без консоли)
    set_target_properties(kblswitch PROPERTIES
        WIN32_EXECUTABLE TRUE
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
    
    # Копируем иконку в выходную директорию после сборки
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.ico")
        add_custom_command(TARGET kblswitch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
                "$<TARGET_FILE_DIR:kblswitch>/app.ico"
            COMMENT "Copying app.ico to output directory"
        )
    endif()
    
    # Копируем INI файл для тестирования после сборки
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kblswitch.ini")
        add_custom_command(TARGET kblswitch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/kblswitch.ini"
                "$<TARGET_FILE_DIR:kblswitch>/kblswitch.ini"
            COMMENT "Copying kblswitch.ini to output directory"
        )
    endif()

    # Копируем языковые файлы (rus.lng / eng.lng)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/language")
        add_custom_command(TARGET kblswitch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/language"
                "$<TARGET_FILE_DIR:kblswitch>/language"
            COMMENT "Copying language files to output directory"
        )
    endif()
endif()

# Установка выходной директории
set_target_properties(kblswitch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Опционально: установка версии
set(VERSION_MAJOR 2)
set(VERSION_MINOR 0)
set(VERSION_PATCH 1)
set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# Добавляем цель для сборки релиза
add_custom_target(release
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config Release
    COMMENT "Building release version"
)

# Добавляем цель для очистки
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing build directory"
)

# Информация о проекте
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${VERSION}")
message(STATUS "Source files: ${SOURCES}")
message(STATUS "Resource files: ${RESOURCES}")
