#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iostream>

extern "C" {
#include "phpspy.h"
#include "pyroscope_api.h"
#include "pyroscope_api_struct.h"

extern pyroscope_context_t *first_ctx;

void get_process_cwd(char *app_cwd, pid_t pid);
int formulate_output(struct trace_context_s *context, const char *app_root_dir,
                     char *data_ptr, int data_len, void *err_ptr, int err_len);
pyroscope_context_t *allocate_context();
void deallocate_context(pyroscope_context_t *ctx);
pyroscope_context_t *find_matching_context(pid_t pid);
}

extern std::map<std::string, pid_t> php_apps;

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;

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
                                ":5 - wait_a_moment;<internal> - sleep;";
      apps.push_back(app);
    }
    {
      App app;
      app.name = std::string("main_chdir.php");
      app.pid = php_apps[app.name];
      app.expected_stacktrace = gtest_cwd + "/tests/pyroscope_api/" + app.name +
                                ":7 - wait_a_moment;<internal> - sleep;";
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
/*
TEST_F(PyroscopeApiTestsSingleApp, phpspy_init_allocate_a_lot) {
  constexpr int nof = 512;
  auto &app = apps[0];
  for (int i = 0; i < nof; i++) {
    EXPECT_EQ(phpspy_init(app.pid, &err_buf[0], err_len), 0);
    EXPECT_STREQ(err_buf, "");
  }

  for (int i = 0; i < nof; i++) {
    phpspy_cleanup(app.pid, &err_buf[0], err_len);
  }
}
*/
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

class PyroscopeApiTestsLinkedList : public PyroscopeApiTestsBase {
  void TearDown() { ASSERT_EQ(first_ctx, nullptr); }

 public:
};

TEST_F(PyroscopeApiTestsLinkedList, allocate_context_first) {
  ASSERT_EQ(first_ctx, nullptr);
  pyroscope_context_t empty{};

  pyroscope_context_t *ptr = allocate_context();

  EXPECT_EQ(ptr, first_ctx);
  EXPECT_EQ(memcmp(first_ctx, &empty, sizeof(pyroscope_context_t)), 0);

  deallocate_context(ptr);
  EXPECT_EQ(first_ctx, nullptr);
}

TEST_F(PyroscopeApiTestsLinkedList, allocate_few) {
  pyroscope_context_t *first = allocate_context();
  pyroscope_context_t *middle = allocate_context();
  pyroscope_context_t *last = allocate_context();

  EXPECT_EQ(first, first_ctx);
  EXPECT_EQ(first->next, middle);
  EXPECT_EQ(middle->next, last);
  EXPECT_EQ(last->next, nullptr);

  deallocate_context(first);
  deallocate_context(middle);
  deallocate_context(last);
}

TEST_F(PyroscopeApiTestsLinkedList, allocate_few_deallocate_first) {
  pyroscope_context_t *first = allocate_context();
  pyroscope_context_t *middle = allocate_context();
  pyroscope_context_t *last = allocate_context();

  deallocate_context(first);

  EXPECT_EQ(middle, first_ctx);
  EXPECT_EQ(middle->next, last);
  EXPECT_EQ(last->next, nullptr);

  deallocate_context(middle);
  deallocate_context(last);
}

TEST_F(PyroscopeApiTestsLinkedList, allocate_few_deallocate_middle) {
  pyroscope_context_t *first = allocate_context();
  pyroscope_context_t *middle = allocate_context();
  pyroscope_context_t *last = allocate_context();

  deallocate_context(middle);

  EXPECT_EQ(first, first_ctx);
  EXPECT_EQ(first->next, last);
  EXPECT_EQ(last->next, nullptr);

  deallocate_context(first);
  deallocate_context(last);
}

TEST_F(PyroscopeApiTestsLinkedList, allocate_few_deallocate_last) {
  pyroscope_context_t *first = allocate_context();
  pyroscope_context_t *middle = allocate_context();
  pyroscope_context_t *last = allocate_context();

  deallocate_context(last);

  EXPECT_EQ(first, first_ctx);
  EXPECT_EQ(first->next, middle);
  EXPECT_EQ(middle->next, nullptr);

  deallocate_context(first);
  deallocate_context(middle);
}

TEST_F(PyroscopeApiTestsLinkedList, allocate_context_many) {
  std::vector<pyroscope_context_t *> allocated;

  for (int i = 0; i < 64; i++) {
    pyroscope_context_t *ptr = allocate_context();

    allocated.push_back(ptr);
  }

  for (long unsigned int i = 0; i < allocated.size(); i++) {
    pyroscope_context_t *current = allocated[i];
    pyroscope_context_t *next = allocated[i + 1];

    EXPECT_EQ(current->next, next);
  }

  for (auto *current : allocated) {
    pyroscope_context_t *next = current->next;

    deallocate_context(current);

    if (current != allocated.back()) {
      EXPECT_EQ(first_ctx, next);
    }
  }
  EXPECT_EQ(first_ctx, nullptr);
}

TEST_F(PyroscopeApiTestsLinkedList, deallocate_context) {
  pyroscope_context_t *ptr = allocate_context();
  EXPECT_EQ(ptr, first_ctx);

  deallocate_context(ptr);

  ASSERT_EQ(first_ctx, nullptr);
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
    ASSERT_LT(frameno, PyroscopeApiTestsParseOutput::max_frames);
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
  trace_frame_t frames[PyroscopeApiTestsParseOutput::max_frames]{};
  struct trace_context_s context {};
};

TEST_F(PyroscopeApiTestsParseOutput, formulate_output_ok) {
  const char app_root_dir[] = "/app/root/dir/";
  std::string expected_stacktrace =
      "file2:12 - class2::func2;file1:10 - class1::func1;";
  prepare_frame("func1", "class1", "file1", 10, 0);
  prepare_frame("func2", "class2", "file2", 12, 1);

  EXPECT_EQ(formulate_output(&context, &app_root_dir[0], &data_buf[0], data_len,
                             &err_buf[0], err_len),
            expected_stacktrace.size());
  EXPECT_STREQ(data_buf, expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsParseOutput, formulate_output_no_class) {
  const char app_root_dir[] = "/app/root/dir/";
  std::string expected_stacktrace = "file2:12 - func2;file1:10 - func1;";
  prepare_frame("func1", "", "file1", 10, 0);
  prepare_frame("func2", "", "file2", 12, 1);

  EXPECT_EQ(formulate_output(&context, &app_root_dir[0], &data_buf[0], data_len,
                             &err_buf[0], err_len),
            expected_stacktrace.size());
  EXPECT_STREQ(data_buf, expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsParseOutput, formulate_output_lineno) {
  const char app_root_dir[] = "/app/root/dir/";
  std::string expected_stacktrace = "file2:12 - func2;file1 - func1;";
  prepare_frame("func1", "", "file1", -1, 0);
  prepare_frame("func2", "", "file2", 12, 1);

  EXPECT_EQ(formulate_output(&context, &app_root_dir[0], &data_buf[0], data_len,
                             &err_buf[0], err_len),
            expected_stacktrace.size());
  EXPECT_STREQ(data_buf, expected_stacktrace.c_str());
  EXPECT_STREQ(err_buf, "");
}

TEST_F(PyroscopeApiTestsParseOutput, formulate_output_not_enough_space) {
  std::string expected_error = "Not enough space! 17 > 10";
  const char app_root_dir[] = "/app/root/dir/";
  prepare_frame("func1", "", "file1", 10, 0);
  prepare_frame("func2", "", "file2", 12, 1);

  EXPECT_EQ(formulate_output(&context, &app_root_dir[0], &data_buf[0], 10,
                             &err_buf[0], err_len),
            -static_cast<int>(expected_error.size()));
  EXPECT_STREQ(err_buf, expected_error.c_str());
}

class PyroscopeApiTestsProfiling : public PyroscopeApiTestsSingleApp {
 public:
  static constexpr float loops = 899;
};

TEST_F(PyroscopeApiTestsProfiling, phpspy_init_profiling) {
  auto &app = apps[0];

  constexpr float time_constraint_us = 30000.f;

  auto t1 = high_resolution_clock::now();
  for (int i = 0; i < loops; i++) {
    phpspy_init(app.pid, &err_buf[0], err_len);
  }
  auto t2 = high_resolution_clock::now();
  auto total_us = duration_cast<microseconds>(t2 - t1).count();
  std::cout << "phpspy_init mean over " << loops
            << " runs: " << total_us / loops << " (us)" << std::endl;
  EXPECT_LT(total_us / loops, time_constraint_us / 2);

  for (int i = 0; i < loops; i++) {
    phpspy_cleanup(app.pid, &err_buf[0], err_len);
  }
}

TEST_F(PyroscopeApiTestsProfiling, phpspy_snapshot_profiling) {
  auto &app = apps[0];
  constexpr float time_constraint_us = 20.f;

  phpspy_init(app.pid, &err_buf[0], err_len);

  // std::cout << "Attach perf" << std::endl;
  // std::cin.get();
  auto t1 = high_resolution_clock::now();
  for (int i = 0; i < loops; i++) {
    phpspy_snapshot(app.pid, &data_buf[0], data_len, &err_buf[0], err_len);
  }
  auto t2 = high_resolution_clock::now();
  auto total_us = duration_cast<microseconds>(t2 - t1).count();
  // std::cout << "Detach perf" << std::endl;
  // std::cin.get();

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
