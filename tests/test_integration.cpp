#include "test_helpers.h"

std::string shell_binary;

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    const char *bin = getenv("AMISH_SHELL_BIN");
    if (bin) {
        shell_binary = bin;
    } else {
        shell_binary = "./shell.out";
    }

    return RUN_ALL_TESTS();
}
