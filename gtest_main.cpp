#include <gtest/gtest.h>
#include <rte_eal.h>

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    rte_eal_init(argc, argv);
    return RUN_ALL_TESTS();
}
