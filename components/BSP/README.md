/**
    该文件夹用于存放驱动文件
    CMakeLists.txt 文件，其代码内容如下：
① 源文件路径，指本目录下的所有代码驱动
set(src_dirs
74
正点原子 DNESP32S3 开发板教程-IDF 版
 
ESP32-S3 使用指南-IDF 版 
 IIC
 LCD
 LED
 SPI
 XL9555
 KEY
 24CXX
 ADC
 AP3216C
 QMA6100P)
② 头文件路径，指本目录下的所有代码驱动
set(include_dirs
 IIC
 LCD
 LED
 SPI
 XL9555
 KEY
 24CXX
 ADC
 AP3216C
 QMA6100P)
③ 设置依赖库
set(requires
 driver
 fatfs
 esp_adc
 esp32-camera
 newlib
 esp_timer)
④ 注册组件到构建系统的函数
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS 
${include_dirs} REQUIRES ${requires})
⑤ 设置特定组件编译选项的函数
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
-ffast-math: 允许编译器进行某些可能减少数学运算精度的优化，以提高性能。
-O3: 这是一个优化级别选项，指示编译器尽可能地进行高级优化以生成更高效的代码。
-Wno-error=format: 这将编译器关于格式字符串不匹配的警告从错误降级为警告。
-Wno-format: 这将完全禁用关于格式字符串的警告。
在开发过程中，④和⑤是固定不变的设定。而①和②的确定则依赖于项目所需的驱动文件
数量。
 */