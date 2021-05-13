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
    gtest_cwd = std::string(cwd_buf);
    app1_expected_stacktrace =
        app1_name + ":5 - wait_a_moment; <internal> - sleep; ";
    app2_expected_stacktrace = gtest_cwd + "/" + app2_name +
                               ":7 - wait_a_moment; <internal> - sleep; ";
  }
  void SetUp() {
    memset(err_buf, 0, err_len);
    memset(data_buf, 0, data_len);
  }
  static constexpr int err_len = 4096;
  static constexpr int data_len = 4096;
  char err_buf[err_len]{};
  char data_buf[data_len]{};
  std::string app1_expected_stacktrace{};
  std::string app2_expected_stacktrace{};
  std::string gtest_cwd{};
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
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_same_pid) {
  ASSERT_EQ(phpspy_init(app_pid, &err_buf[0], err_len), 0);
  EXPECT_EQ(phpspy_init(app_pid, &err_buf[0], err_len), 0);
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_wrong_pid) {
  constexpr pid_t wrong_pid = -1;
  std::string expected_err_msg = "Unknown error: 1";
  EXPECT_EQ(phpspy_init(wrong_pid, &err_buf[0], err_len),
            -static_cast<int>(expected_err_msg.size()));
  EXPECT_STREQ(err_buf, expected_err_msg.c_str());
  phpspy_cleanup(wrong_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_snapshot_ok) {
  phpspy_init(app_pid, &err_buf[0], err_len);

  int rv =
      phpspy_snapshot(app_pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, app1_expected_stacktrace.size());
  EXPECT_STREQ(data_buf, app1_expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_snapshot_without_init) {
  std::string expected_err_msg =
      "Phpspy not initialized for " + std::to_string(app_pid) + " pid";
  int rv =
      phpspy_snapshot(app_pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, -static_cast<int>(expected_err_msg.size()));
  EXPECT_STREQ(data_buf, "");
  EXPECT_STREQ(err_buf, expected_err_msg.c_str());
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_cleanup_ok) {
  phpspy_init(app_pid, &err_buf[0], err_len);
  EXPECT_EQ(phpspy_cleanup(app_pid, &err_buf[0], err_len), 0);
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_cleanup_without_init) {
  int rv = 0;
  std::string expected_err_msg =
      "Phpspy not initialized for " + std::to_string(app_pid) + " pid";
  rv = phpspy_cleanup(app_pid, &err_buf[0], err_len);
  EXPECT_EQ(rv, -static_cast<int>(expected_err_msg.size()));
  EXPECT_STREQ(data_buf, "");
  EXPECT_STREQ(err_buf, expected_err_msg.c_str());
}

TEST_F(PyroscopeApiTestsSingleApp, get_process_cwd) {
  char buf[PATH_MAX]{};
  get_process_cwd(&buf[0], app_pid);
  EXPECT_STREQ(buf, gtest_cwd.c_str());
}

class PyroscopeApiTestsProfiling : public PyroscopeApiTestsSingleApp {
public:
  static constexpr float loops = 50; /* TODO: Cannot exceed MAX_PIDS */
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

  for (int i = 0; i < loops; i++) {
    phpspy_cleanup(app_pid, &err_buf[0], err_len);
  }
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
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsChdir, snapshot_ok) {
  phpspy_init(app_pid, &err_buf[0], err_len);

  int rv =
      phpspy_snapshot(app_pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, app2_expected_stacktrace.size());
  EXPECT_STREQ(data_buf, app2_expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsChdir, get_process_cwd) {
  char buf[PATH_MAX]{};
  get_process_cwd(&buf[0], app_pid);
  EXPECT_STREQ(buf, "/tmp");
}

class PyroscopeApiTestsMultipleApp : public PyroscopeApiTestsBase {
  class App {
  public:
    std::string app_name;
    pid_t pid;
    std::string expected_stacktrace;
  };

public:
  PyroscopeApiTestsMultipleApp() {
      /* TODO: move to the base class */
    {
      App app;
      app.app_name = std::string("main.php");
      app.pid = php_apps[app.app_name];
      app.expected_stacktrace = app1_expected_stacktrace;
      apps.push_back(app);
    }
    {
      App app;
      app.app_name = std::string("main_chdir.php");
      app.pid = php_apps[app.app_name];
      app.expected_stacktrace = app2_expected_stacktrace;
      apps.push_back(app);
    }
  }
  std::vector<App> apps;
};

TEST_F(PyroscopeApiTestsMultipleApp, init_ok) {
  for (auto const &app : apps) {
    EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
    EXPECT_STREQ(err_buf, "");
  }
  for (auto const &app : apps) {
    phpspy_cleanup(app.pid, &err_buf[0], err_len);
    EXPECT_STREQ(err_buf, "");
  }
}

TEST_F(PyroscopeApiTestsMultipleApp, phpspy_snapshot_ok) {
  for (auto const &app : apps) {
    phpspy_init(app.pid, &err_buf[0], err_len);
  }

  for (auto const &app : apps) {
    int rv =
        phpspy_snapshot(app.pid, &data_buf[0], data_len, &err_buf[0], err_len);
    EXPECT_EQ(rv, app.expected_stacktrace.size());
    EXPECT_STREQ(data_buf, app.expected_stacktrace.c_str());
    EXPECT_STREQ(err_buf, "");
  }

  for (auto const &app : apps) {
    phpspy_cleanup(app.pid, &err_buf[0], err_len);
  }
}
