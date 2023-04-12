cmake_minimum_required(VERSION 3.22)
project(phpspy C)


#cc -std=gnu2x -Wall -Werror -Wextra -pedantic -O3  -Wno-unused-parameter -I. $(php-config --includes)  phpspy.c addr_objdump.c pyroscope_api.c phpspy_trace.c -c  -pthread  -fPIC
#ar rcs libphpspy.a *.o

set(CMAKE_C_STANDARD 17)

add_library(phpspy STATIC addr_objdump.c phpspy.c pyroscope_api.c )
target_include_directories(phpspy PUBLIC . ./vendor)
target_compile_definitions(phpspy PUBLIC PYROSCOPE=1)
target_compile_options(phpspy PRIVATE -Wall -Werror -Wextra -pedantic  -Wno-unused-parameter -Wno-address-of-packed-member -fPIC)

add_executable(pyroscope_api_playground playground.c)
target_link_libraries(pyroscope_api_playground phpspy)

