#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <rte_cycles.h>

extern "C" {
#include "phpspy.h"
#include "pyroscope_api.h"
void get_process_cwd(char *app_cwd, pid_t pid);
int parse_output(struct trace_context_s *context, const char *app_root_dir,
                 char *data_ptr, int data_len, void *err_ptr, int err_len);
}

extern std::map<std::string, pid_t> php_apps;

class PyroscopeApiTestsBase : public ::testing::Test {
  class App {
  public:
    std::string name;
    pid_t pid;
    std::string expected_stacktrace;
  };

public:
  PyroscopeApiTestsBase() {
    char cwd_buf[PATH_MAX]{};
    getcwd(&cwd_buf[0], PATH_MAX);
    gtest_cwd = std::string(cwd_buf);

    {
      App app;
      app.name = std::string("main.php");
      app.pid = php_apps[app.name];
      app.expected_stacktrace = "tests/pyroscope_api/" + app.name +
                                ":5 - wait_a_moment; <internal> - sleep; ";
      apps.push_back(app);
    }
    {
      App app;
      app.name = std::string("main_chdir.php");
      app.pid = php_apps[app.name];
      app.expected_stacktrace = gtest_cwd + "/tests/pyroscope_api/" + app.name +
                                ":7 - wait_a_moment; <internal> - sleep; ";
      apps.push_back(app);
    }
  }

  void SetUp() {
    memset(err_buf, 0, err_len);
    memset(data_buf, 0, data_len);
  }

  static constexpr int err_len = 4096;
  static constexpr int data_len = 4096;
  char err_buf[err_len]{};
  char data_buf[data_len]{};
  std::string gtest_cwd{};
  std::vector<App> apps;
};

class PyroscopeApiTestsSingleApp : public PyroscopeApiTestsBase {
public:
};

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_ok) {
  auto &app = apps[0];
  EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_exceed_max) {
  auto &app = apps[0];
  std::string expected_error_msg =
      "Exceeded maximum allowed number of processes: 50";
  for (int i = 0; i < 50; i++) {
    EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
    EXPECT_STREQ(err_buf, "");
  }

  EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len),
            -static_cast<int>(expected_error_msg.size()));
  EXPECT_STREQ(err_buf, expected_error_msg.c_str());

  for (int i = 0; i < 51; i++) {
    phpspy_cleanup(app.pid, &err_buf[0], err_len);
  }
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_same_pid) {
  auto &app = apps[0];
  ASSERT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
  EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_wrong_pid) {
  constexpr pid_t wrong_pid = -1;
  std::string expected_err_msg = "General error!";
  EXPECT_EQ(phpspy_init(wrong_pid, &err_buf[0], err_len),
            -static_cast<int>(expected_err_msg.size()));
  EXPECT_STREQ(err_buf, expected_err_msg.c_str());
  phpspy_cleanup(wrong_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_snapshot_ok) {
  auto &app = apps[0];
  phpspy_init(app.pid, &err_buf[0], err_len);

  int rv =
      phpspy_snapshot(app.pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, app.expected_stacktrace.size());
  EXPECT_STREQ(data_buf, app.expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_snapshot_without_init) {
  auto &app = apps[0];
  std::string expected_err_msg =
      "Phpspy not initialized for " + std::to_string(app.pid) + " pid";
  int rv =
      phpspy_snapshot(app.pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, -static_cast<int>(expected_err_msg.size()));
  EXPECT_STREQ(data_buf, "");
  EXPECT_STREQ(err_buf, expected_err_msg.c_str());
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_cleanup_ok) {
  auto &app = apps[0];
  phpspy_init(app.pid, &err_buf[0], err_len);
  EXPECT_EQ(phpspy_cleanup(app.pid, &err_buf[0], err_len), 0);
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsSingleApp, phpspy_cleanup_without_init) {
  auto &app = apps[0];
  int rv = 0;
  std::string expected_err_msg =
      "Phpspy not initialized for " + std::to_string(app.pid) + " pid";
  rv = phpspy_cleanup(app.pid, &err_buf[0], err_len);
  EXPECT_EQ(rv, -static_cast<int>(expected_err_msg.size()));
  EXPECT_STREQ(data_buf, "");
  EXPECT_STREQ(err_buf, expected_err_msg.c_str());
}

TEST_F(PyroscopeApiTestsSingleApp, get_process_cwd) {
  auto &app = apps[0];
  char buf[PATH_MAX]{};
  get_process_cwd(&buf[0], app.pid);
  EXPECT_STREQ(buf, gtest_cwd.c_str());
}

class PyroscopeApiTestsParseOutput : public PyroscopeApiTestsSingleApp {
public:
  void SetUp() {
    memset(&context, 0, sizeof(struct trace_context_s));
    memset(&frames, 0, sizeof(frames));
    context.event_udata = static_cast<void *>(&frames);
  }

  void prepare_frame(std::string func, std::string class_name, std::string file,
                     int lineno, int frameno) {
    ASSERT_LT(frameno, max_frames);
    auto &frame = frames[frameno];
    strcpy(frame.loc.func, func.c_str());
    strcpy(frame.loc.class_name, class_name.c_str());
    strcpy(frame.loc.file, file.c_str());
    frame.loc.func_len = strlen(frame.loc.func);
    frame.loc.file_len = strlen(frame.loc.file);
    frame.loc.class_len = strlen(frame.loc.class_name);
    frame.loc.lineno = lineno;
    context.event.frame.depth++;
  }
  static constexpr int max_frames = 50;
  trace_frame_t frames[max_frames]{};
  struct trace_context_s context {};
};

TEST_F(PyroscopeApiTestsParseOutput, parse_output_ok) {
  const char app_root_dir[] = "/app/root/dir/";
  std::string expected_stacktrace =
      "file2:12 - class2::func2; file1:10 - class1::func1; ";
  prepare_frame("func1", "class1", "file1", 10, 0);
  prepare_frame("func2", "class2", "file2", 12, 1);

  EXPECT_EQ(parse_output(&context, &app_root_dir[0], &data_buf[0], data_len,
                         &err_buf[0], err_len),
            expected_stacktrace.size());
  EXPECT_STREQ(data_buf, expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsParseOutput, parse_output_no_class) {
  const char app_root_dir[] = "/app/root/dir/";
  std::string expected_stacktrace = "file2:12 - func2; file1:10 - func1; ";
  prepare_frame("func1", "", "file1", 10, 0);
  prepare_frame("func2", "", "file2", 12, 1);

  EXPECT_EQ(parse_output(&context, &app_root_dir[0], &data_buf[0], data_len,
                         &err_buf[0], err_len),
            expected_stacktrace.size());
  EXPECT_STREQ(data_buf, expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsParseOutput, parse_output_lineno) {
  const char app_root_dir[] = "/app/root/dir/";
  std::string expected_stacktrace = "file2:12 - func2; file1 - func1; ";
  prepare_frame("func1", "", "file1", -1, 0);
  prepare_frame("func2", "", "file2", 12, 1);

  EXPECT_EQ(parse_output(&context, &app_root_dir[0], &data_buf[0], data_len,
                         &err_buf[0], err_len),
            expected_stacktrace.size());
  EXPECT_STREQ(data_buf, expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsParseOutput, parse_output_not_enough_space) {
  std::string expected_error = "Not enough space! 18 > 10";
  const char app_root_dir[] = "/app/root/dir/";
  prepare_frame("func1", "", "file1", 10, 0);
  prepare_frame("func2", "", "file2", 12, 1);

  EXPECT_EQ(parse_output(&context, &app_root_dir[0], &data_buf[0], 10,
                         &err_buf[0], err_len),
            -static_cast<int>(expected_error.size()));
  EXPECT_STREQ(err_buf, expected_error.c_str());
}

class PyroscopeApiTestsProfiling : public PyroscopeApiTestsSingleApp {
public:
  static constexpr float loops = 50; // TODO: Cannot exceed MAX_PIDS

  uint64_t estimate_tsc_freq(void) {
    uint64_t start = rte_rdtsc();
    sleep(1);
    return rte_rdtsc() - start;
  }
};

TEST_F(PyroscopeApiTestsProfiling, phpspy_init_profiling) {
  auto &app = apps[0];
  uint64_t a = 0, b = 0, tsc_hz = 0, tsc = 0;
  float us = 0, total_us = 0;
  constexpr float time_constraint_us = 50000.f;
  tsc_hz = estimate_tsc_freq();
  for (int i = 0; i < loops; i++) {
    a = rte_rdtsc();
    phpspy_init(app.pid, &err_buf[0], err_len);
    us = ((((rte_rdtsc() - a) * 1.f) / (tsc_hz * 1.f)) * 1000000.f);
    total_us += us;
    EXPECT_LT(us, time_constraint_us);
  }
  std::cout << "phpspy_init mean over " << loops
            << " runs: " << total_us / loops << " (us)" << std::endl;
  EXPECT_LT(total_us / loops, time_constraint_us / 2);

  for (int i = 0; i < loops; i++) {
    phpspy_cleanup(app.pid, &err_buf[0], err_len);
  }
}

TEST_F(PyroscopeApiTestsProfiling, phpspy_snapshot_profiling) {
  auto &app = apps[0];
  uint64_t a = 0, b = 0, tsc_hz = 0, tsc = 0;
  float us = 0, total_us = 0;
  constexpr float time_constraint_us = 50.f;
  tsc_hz = estimate_tsc_freq();

  phpspy_init(app.pid, &err_buf[0], err_len);

  for (int i = 0; i < loops; i++) {
    a = rte_rdtsc();
    phpspy_snapshot(app.pid, &data_buf[0], data_len, &err_buf[0], err_len);
    us = ((((rte_rdtsc() - a) * 1.f) / (tsc_hz * 1.f)) * 1000000.f);
    total_us += us;
    EXPECT_LT(us, time_constraint_us);
  }
  std::cout << "phpspy_snapshot mean over " << loops
            << " runs: " << total_us / loops << " (us)" << std::endl;
  EXPECT_LT(total_us / loops, time_constraint_us / 2);
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

class PyroscopeApiTestsChdir : public PyroscopeApiTestsSingleApp {};

TEST_F(PyroscopeApiTestsChdir, init_ok) {
  auto &app = apps[1];
  EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsChdir, snapshot_ok) {
  auto &app = apps[1];
  phpspy_init(app.pid, &err_buf[0], err_len);

  int rv =
      phpspy_snapshot(app.pid, &data_buf[0], data_len, &err_buf[0], err_len);

  EXPECT_EQ(rv, app.expected_stacktrace.size());
  EXPECT_STREQ(data_buf, app.expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
  phpspy_cleanup(app.pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTestsChdir, get_process_cwd) {
  auto &app = apps[1];
  char buf[PATH_MAX]{};
  get_process_cwd(&buf[0], app.pid);
  EXPECT_STREQ(buf, "/tmp");
}

class PyroscopeApiTestsMultipleApp : public PyroscopeApiTestsBase {};

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
