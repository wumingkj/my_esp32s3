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
    
    # 简化：只在构建时运行一次检测，而不是在CMake配置时
    add_custom_target(check_littlefs_changes ALL
        COMMAND ${PYTHON} ${CMAKE_BINARY_DIR}/check_littlefs_changes.py 
                "${ABSOLUTE_LITTLEFS_DIR}" "${LITTLEFS_HASH_FILE}"
        COMMENT "检测LittleFS文件系统变化"
        VERBATIM
    )
    
    # 生成文件系统镜像（总是生成，但只在需要时烧录）
    littlefs_create_partition_image(
        littlefs
        ${ABSOLUTE_LITTLEFS_DIR}
        FLASH_IN_PROJECT  # 启用自动烧录
    )
    
    message(STATUS "智能条件烧录配置完成")
    message(STATUS "源目录: ${ABSOLUTE_LITTLEFS_DIR}")
    message(STATUS "哈希文件: ${LITTLEFS_HASH_FILE}")
    message(STATUS "使用说明:")
    message(STATUS "  - 正常开发: idf.py app-flash (仅烧录应用程序)")
    message(STATUS "  - 文件系统变化时: idf.py flash (烧录所有)")
    message(STATUS "  - 仅烧录文件系统: idf.py littlefs-flash")
    
else()
    message(WARNING "littlefs目录不存在: ${ABSOLUTE_LITTLEFS_DIR}")
    message(WARNING "跳过文件系统镜像生成")
endif()