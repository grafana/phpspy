#!/bin/bash

./tests/pyroscope_api/main.php&
pid_1=$!
./tests/pyroscope_api/main_chdir.php&
pid_2=$!

#valgrind --leak-check=full --show-leak-kinds=all ./pyroscope_api_tests main.php ${pid_1} main_chdir.php ${pid_2}
#gdb --args ./pyroscope_api_tests main.php ${pid_1} main_chdir.php ${pid_2}
#strace --syscall-times -o out.txt ./pyroscope_api_tests main.php ${pid_1} main_chdir.php ${pid_2}
./pyroscope_api_tests main.php ${pid_1} main_chdir.php ${pid_2}

kill ${pid_1}
kill ${pid_2}
