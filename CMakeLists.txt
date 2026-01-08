# The following lines of boilerplate have to be in your project's
# CMakeLists.txt file.
cmake_minimum_required(VERSION 3.5)

set(SUPPORTED_TARGETS esp32s3)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(my_esp32s3)
endif()

set(PARTITION_TABLE partitions.csv)

# 配置LittleFS文件系统镜像生成
set(LITTLEFS_BASE_DIR "littlefs")

# 添加TFT显示组件
set(EXTRA_COMPONENT_DIRS components)

# 检查littlefs目录是否存在 - 使用绝对路径检测
get_filename_component(ABSOLUTE_LITTLEFS_DIR "${LITTLEFS_BASE_DIR}" ABSOLUTE)

# 智能条件烧录配置
set(LITTLEFS_HASH_FILE "${CMAKE_BINARY_DIR}/littlefs_hash.json")

# 只在littlefs目录存在时配置
if(EXISTS "${ABSOLUTE_LITTLEFS_DIR}")
    message(STATUS "Found littlefs directory: ${ABSOLUTE_LITTLEFS_DIR}")
    
    # 创建文件系统变化检测脚本
    configure_file(
        ${CMAKE_SOURCE_DIR}/check_littlefs_changes.py
        ${CMAKE_BINARY_DIR}/check_littlefs_changes.py
        COPYONLY
    )
    
    # 修复：使用英文注释避免编码问题，并移除ALL标志
    add_custom_target(check_littlefs_changes
        COMMAND ${PYTHON} ${CMAKE_BINARY_DIR}/check_littlefs_changes.py 
                "${ABSOLUTE_LITTLEFS_DIR}" "${LITTLEFS_HASH_FILE}"
        COMMENT "Checking LittleFS file system changes"
        VERBATIM
    )
    
    # 生成文件系统镜像（总是生成，但只在需要时烧录）
    littlefs_create_partition_image(
        littlefs
        ${ABSOLUTE_LITTLEFS_DIR}
        FLASH_IN_PROJECT  # 启用自动烧录
    )
    
    message(STATUS "Smart conditional flash configuration completed")
    message(STATUS "Source directory: ${ABSOLUTE_LITTLEFS_DIR}")
    message(STATUS "Hash file: ${LITTLEFS_HASH_FILE}")
    message(STATUS "Usage instructions:")
    message(STATUS "  - Normal development: idf.py app-flash (flash app only)")
    message(STATUS "  - When filesystem changes: idf.py flash (flash all)")
    message(STATUS "  - Flash filesystem only: idf.py littlefs-flash")
    
else()
    message(WARNING "littlefs directory not found: ${ABSOLUTE_LITTLEFS_DIR}")
    message(WARNING "Skipping filesystem image generation")
endif()