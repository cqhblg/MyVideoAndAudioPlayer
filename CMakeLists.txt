# CMakeList.txt: MyVideoAndAudioPlayer 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

project ("MyVideoAndAudioPlayer" VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")


# 将源代码添加到此项目的可执行文件。
add_executable (MyVideoAndAudioPlayer "main.cpp"  )

# TODO: 如有需要，请添加测试并安装目标。

target_include_directories( ${PROJECT_NAME}  
	PRIVATE 
		${PROJECT_SOURCE_DIR}/include

)
target_link_libraries( ${PROJECT_NAME}  
	PRIVATE 
		${PROJECT_SOURCE_DIR}/lib/avcodec.lib
		${PROJECT_SOURCE_DIR}/lib/avformat.lib
		${PROJECT_SOURCE_DIR}/lib/avutil.lib
		${PROJECT_SOURCE_DIR}/lib/avdevice.lib
		${PROJECT_SOURCE_DIR}/lib/avfilter.lib
		${PROJECT_SOURCE_DIR}/lib/postproc.lib
		${PROJECT_SOURCE_DIR}/lib/swresample.lib
		${PROJECT_SOURCE_DIR}/lib/swscale.lib
		${PROJECT_SOURCE_DIR}/lib/SDL2.lib

)