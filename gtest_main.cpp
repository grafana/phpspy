#include <gtest/gtest.h>

std::map<std::string, pid_t> php_apps;

int main(int argc, char *argv[]) {
  ::testing::InitGoogleTest(&argc, argv);

  for (int i = 1; i < argc; i = i + 2) {
    std::string app_name(argv[i]);
    pid_t app_pid = atoi(argv[i + 1]);
    std::cout << "php app name:pid: " << app_name << ":" << app_pid
              << std::endl;
    php_apps[app_name] = app_pid;
  }

  return RUN_ALL_TESTS();
}
