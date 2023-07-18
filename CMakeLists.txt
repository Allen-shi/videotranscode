#设置-std = c++11
SET(CMAKE_CXX_STANDARD 11)
#把当前目录下的所有源代码和头文件放入到变量MEDIA_DIR
AUX_SOURCE_DIRECTORY(. MEDIA_DIR)

#将当前目录下的文件编译成共享库
ADD_LIBRARY(videotoolmedia SHARED ${MEDIA_DIR})

#设置动态库文件的输出目录
MESSAGE("${PROJECT_SOURCE_DIR}")
SET(LIBRARY_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/leopardlib)

#指明共享库需要链接的其他链接库
TARGET_LINK_LIBRARIES(videotoolmedia avformat avcodec avutil avfilter swresample swscale json_linux-gcc-4.8.5_libmt commoncmd commonioamqp)
