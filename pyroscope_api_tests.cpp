#include <gtest/gtest.h>

extern "C" {
#include "pyroscope_api.h"
}

class PyroscopeApiTests: public ::testing::Test {
    public:

    void SetUp()
    {
        system("php main.php 1>/dev/null &");
        fp = popen("pidof php", "r");
        char pid_buf[4096] = {};
        fgets(pid_buf, 4096, fp);
        app_pid = strtoul(pid_buf, nullptr, 10);
        fclose(fp);
    }

    void TearDown()
    {
        kill(app_pid, SIGKILL);
    }

    static constexpr int err_len = 4096;
    static constexpr int data_len = 4096;
    char err_buf[err_len] = {};
    char data_buf[data_len] = {};
    pid_t app_pid = 0;
    FILE* fp = nullptr;
};

TEST_F(PyroscopeApiTests, init_ok)
{
    ASSERT_EQ(phpspy_init(app_pid, &err_buf[0], err_len), 0);
}

TEST_F(PyroscopeApiTests, snapshot_ok)
{
    const std::string expected_stacktrace = "<internal> - sleep; ";
    phpspy_init(app_pid, &err_buf[0], err_len);

    int rv = phpspy_snapshot(app_pid, &data_buf[0], data_len,  &err_buf[0], err_len);

    ASSERT_EQ(rv, expected_stacktrace.size());
    ASSERT_STREQ(data_buf, expected_stacktrace.c_str());
}
