| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Hello World Example

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.


# ESP32 Web文件部署指南

## 工作原理

### 编译流程
1. **App固件编译**：您的C/C++代码 → 编译成 `my_esp32s3.bin`
2. **文件系统镜像生成**：`web_pages`文件夹 → 打包成 `littlefs.bin`
3. **烧录**：两个.bin文件分别烧录到不同的分区

### 分区布局
```
+-------------------+ 0x00000000
|     Bootloader    |
+-------------------+ 0x00008000
|   Partition Table |
+-------------------+ 0x00009000
|     App固件       | (factory分区)
+-------------------+ 0x00309000
|   LittleFS文件系统 | (littlefs分区)
+-------------------+ 0x00D09000
```

## 部署步骤

### 1. 构建项目（自动生成文件系统镜像）
```bash
idf.py build
```

构建过程中，ESP-IDF会自动：
- 检测到`littlefs`文件夹存在
- 使用自定义脚本将`littlefs/web_pages`文件夹内容打包成LittleFS镜像
- 生成`build/littlefs_custom.bin`文件系统镜像

### 2. 烧录固件和文件系统
```bash
idf.py -p COMx flash
```

这个命令会自动：
- 烧录App固件到`factory`分区
- 烧录文件系统镜像到`littlefs`分区

### 3. 验证部署
烧录完成后，ESP32启动时会：
- 初始化LittleFS文件系统
- 挂载`/littlefs`目录
- 您的Web文件就可以通过`/littlefs/web_pages/html/login.html`路径访问了

## 文件系统访问

在代码中访问Web文件：
```c
// 使用标准文件操作
FILE* file = fopen("/littlefs/web_pages/html/login.html", "r");
if (file) {
    // 读取文件内容
    fclose(file);
}

// 或者使用littlefs_manager接口
char* content = littlefs_manager_read_file("/littlefs/web_pages/html/login.html");

// 在WiFi管理器中，使用相对路径（会自动拼接挂载点）
serve_file(req, "web_pages/html/login.html", "text/html");
```

## 优势

1. **分离部署**：Web文件修改后只需重新烧录文件系统镜像，无需重新编译App
2. **节省空间**：Web文件不占用App固件的Flash空间
3. **灵活管理**：可以独立更新Web界面
4. **标准兼容**：使用标准的文件系统接口

## 注意事项

1. **文件大小限制**：确保Web文件总大小不超过分区大小（0xce0000 = 13.5MB）
2. **文件路径**：在代码中使用绝对路径 `/web_pages/...`
3. **修改Web文件**：修改后需要重新执行 `idf.py build` 和 `idf.py flash`

## 故障排除

### 文件找不到错误
如果出现"Failed to open file"错误：
1. 检查是否执行了 `idf.py build`（生成文件系统镜像）
2. 检查是否执行了 `idf.py flash`（烧录文件系统）
3. 检查`web_pages`文件夹结构是否正确

### 手动烧录文件系统
如果需要单独烧录文件系统：
```bash
# 查看分区偏移地址
idf.py partition-table

# 手动烧录文件系统
python -m esptool --chip esp32s3 --port COMx --baud 921600 write_flash 0x00309000 build/littlefs.bin
```