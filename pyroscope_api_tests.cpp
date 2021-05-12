#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <rte_cycles.h>

extern "C" {
#include "pyroscope_api.h"
void get_process_cwd(char *app_cwd, pid_t pid);
}
extern std::map<std::string, pid_t> php_apps;
std::string app1_name = "main.php";
std::string app2_name = "main_chdir.php";

static uint64_t estimate_tsc_freq(void) {
  uint64_t start = rte_rdtsc();
  sleep(1);
  return rte_rdtsc() - start;
}

class PyroscopeApiTestsBase : public ::testing::Test {
public:
  PyroscopeApiTestsBase() {
    char cwd_buf[PATH_MAX]{};
    getcwd(&cwd_buf[0], PATH_MAX);
    app1_expected_stacktrace =
        app1_name + ":5 - wait_a_moment; <internal> - sleep; ";
    app2_expected_stacktrace = std::string(cwd_buf) + "/" + app2_name +
                               ":7 - wait_a_moment; <internal> - sleep; ";
  }
  static constexpr int err_len = 4096;
  static constexpr int data_len = 4096;
  char err_buf[err_len]{};
  char data_buf[data_len]{};
  std::string app1_expected_stacktrace{};
  std::string app2_expected_stacktrace{};
};

class PyroscopeApiTestsSingleApp : public PyroscopeApiTestsBase {
public:
  PyroscopeApiTestsSingleApp() : PyroscopeApiTestsSingleApp(app1_name) {}

  PyroscopeApiTestsSingleApp(std::string app_name)
      : app_pid(php_apps[app_name]) {}
  pid_t app_pid{};
};

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_ok) {
  EXPECT_EQ(phpspy_init(app_pid, &err_buf[0], err_len), 0);
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_snapshot_ok) {
  phpspy_init(app_pid, &err_buf[0], err_len);

  int rv =
      phpspy_snapshot(app_pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, app1_expected_stacktrace.size());
  EXPECT_STREQ(data_buf, app1_expected_stacktrace.c_str());
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_wrong_pid) {
  constexpr pid_t wrong_pid = -1;
  constexpr char expected_err_msg[] = "Unknown error: 1";
  EXPECT_EQ(phpspy_init(wrong_pid, &err_buf[0], err_len),
            -static_cast<int>(strlen(expected_err_msg)));
  EXPECT_STREQ(err_buf, expected_err_msg);
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

class PyroscopeApiTestsProfiling : public PyroscopeApiTestsSingleApp {
public:
  static constexpr float loops = 1000;
};

TEST_F(PyroscopeApiTestsProfiling, phpspy_init_profiling) {
  uint64_t a = 0, b = 0, tsc_hz = 0, tsc = 0;
  float us = 0, total_us = 0;
  constexpr float time_constraint_us = 50000.f;
  tsc_hz = estimate_tsc_freq();
  for (int i = 0; i < loops; i++) {
    a = rte_rdtsc();
    phpspy_init(app_pid, &err_buf[0], err_len);
    us = ((((rte_rdtsc() - a) * 1.f) / (tsc_hz * 1.f)) * 1000000.f);
    total_us += us;
    EXPECT_LT(us, time_constraint_us);
  }
  std::cout << "phpspy_init mean over " << loops
            << " runs: " << total_us / loops << " (us)" << std::endl;
  EXPECT_LT(total_us / loops, time_constraint_us / 2);
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsProfiling, phpspy_snapshot_profiling) {
  uint64_t a = 0, b = 0, tsc_hz = 0, tsc = 0;
  float us = 0, total_us = 0;
  constexpr float time_constraint_us = 50.f;
  tsc_hz = estimate_tsc_freq();

  phpspy_init(app_pid, &err_buf[0], err_len);

  for (int i = 0; i < loops; i++) {
    a = rte_rdtsc();
    phpspy_snapshot(app_pid, &data_buf[0], data_len, &err_buf[0], err_len);
    us = ((((rte_rdtsc() - a) * 1.f) / (tsc_hz * 1.f)) * 1000000.f);
    total_us += us;
    EXPECT_LT(us, time_constraint_us);
  }
  std::cout << "phpspy_snapshot mean over " << loops
            << " runs: " << total_us / loops << " (us)" << std::endl;
  EXPECT_LT(total_us / loops, time_constraint_us / 2);
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

class PyroscopeApiTestsChdir : public PyroscopeApiTestsSingleApp {
public:
  PyroscopeApiTestsChdir() : PyroscopeApiTestsSingleApp(app2_name) {}
};

TEST_F(PyroscopeApiTestsChdir, init_ok) {
  EXPECT_EQ(phpspy_init(app_pid, &err_buf[0], err_len), 0);
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsChdir, snapshot_ok) {
  phpspy_init(app_pid, &err_buf[0], err_len);

  int rv =
      phpspy_snapshot(app_pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, app2_expected_stacktrace.size());
  EXPECT_STREQ(data_buf, app2_expected_stacktrace.c_str());
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsChdir, get_process_cwd) {
  char buf[PATH_MAX]{};
  get_process_cwd(&buf[0], app_pid);
  EXPECT_STREQ(buf, "/tmp");
}
