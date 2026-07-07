#include <unity.h>

#include "vialucis/version.h"

void setUp() {}
void tearDown() {}

static void test_version_is_semver_like() {
    TEST_ASSERT_NOT_NULL(vialucis::kVersion);
    TEST_ASSERT_EQUAL_STRING("0.1.0", vialucis::kVersion);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_version_is_semver_like);
    return UNITY_END();
}
