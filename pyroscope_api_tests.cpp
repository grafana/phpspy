#include <gtest/gtest.h>
#include <rte_cycles.h>
#include <iostream>

extern "C" {
#include "pyroscope_api.h"
}

static uint64_t
estimate_tsc_freq(void)
{
    uint64_t start = rte_rdtsc();
    sleep(1);
    return rte_rdtsc() - start;
}

class PyroscopeApiTests: public ::testing::Test {
    public:

    void SetUp()
    {
        /*TODO: May be safer to deliver the pid from cmdline */
        system("php main.php &");
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
    phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTests, snapshot_ok)
{
    const std::string expected_stacktrace = "main.php:6 - wait_a_moment; <internal> - sleep; ";
    phpspy_init(app_pid, &err_buf[0], err_len);

    int rv = phpspy_snapshot(app_pid, &data_buf[0], data_len,  &err_buf[0], err_len);

    ASSERT_EQ(rv, expected_stacktrace.size());
    ASSERT_STREQ(data_buf, expected_stacktrace.c_str());
    phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTests, init_profiling)
{
    uint64_t a = 0, b = 0, tsc_hz = 0, tsc = 0;
    float us = 0, total_us = 0;
    constexpr float time_constraint_us = 100000.f;
    constexpr float loops = 100;
    tsc_hz = estimate_tsc_freq();
    for(int i = 0; i < loops; i++)
    {
        a = rte_rdtsc();
        phpspy_init(app_pid, &err_buf[0], err_len);
        us = ((((rte_rdtsc() - a) * 1.f) / (tsc_hz * 1.f)) * 1000000.f);
        total_us += us;
        ASSERT_LT(us, time_constraint_us);
    }
    std::cout << "phpspy_init mean over " << loops << " runs: " << total_us / loops << " (us)" << std::endl;
    phpspy_cleanup(app_pid, &err_buf[0], err_len);
}

TEST_F(PyroscopeApiTests, snapshot_profiling_mean)
{
    uint64_t a = 0, b = 0, tsc_hz = 0, tsc = 0;
    float us = 0, total_us = 0;
    constexpr float time_constraint_us = 100.f;
    constexpr float loops = 100;
    tsc_hz = estimate_tsc_freq();

    phpspy_init(app_pid, &err_buf[0], err_len);

    for(int i = 0; i < loops; i++)
    {
        a = rte_rdtsc();
        phpspy_snapshot(app_pid, &data_buf[0], data_len,  &err_buf[0], err_len);
        us = ((((rte_rdtsc() - a) * 1.f) / (tsc_hz * 1.f)) * 1000000.f);
        total_us += us;
        ASSERT_LT(us, time_constraint_us);
    }
    std::cout << "phpspy_snapshot mean over " << loops << " runs: " << total_us / loops << " (us)" << std::endl;
    phpspy_cleanup(app_pid, &err_buf[0], err_len);
}
