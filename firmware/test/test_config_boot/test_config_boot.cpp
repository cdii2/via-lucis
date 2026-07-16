// Boot self-heal decision (B4): Absent = silent defaults (upgrade path),
// Corrupt / unparseable = defaults + re-save + user-visible reset flag.

#include <unity.h>

#include "vialucis/config_boot.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

void test_absent_uses_defaults_silently() {
    SelfHeal h = decideSelfHeal(DocLoad::Absent, /*parsedOk=*/false);
    TEST_ASSERT_TRUE(h.useDefaults);
    TEST_ASSERT_FALSE(h.resave);      // nothing was lost -> don't rewrite
    TEST_ASSERT_FALSE(h.configReset);  // not a reset the user should see
}

void test_ok_and_parsed_keeps_loaded() {
    SelfHeal h = decideSelfHeal(DocLoad::Ok, /*parsedOk=*/true);
    TEST_ASSERT_FALSE(h.useDefaults);
    TEST_ASSERT_FALSE(h.resave);
    TEST_ASSERT_FALSE(h.configReset);
}

void test_corrupt_heals_and_flags() {
    SelfHeal h = decideSelfHeal(DocLoad::Corrupt, /*parsedOk=*/false);
    TEST_ASSERT_TRUE(h.useDefaults);
    TEST_ASSERT_TRUE(h.resave);
    TEST_ASSERT_TRUE(h.configReset);
}

void test_ok_but_unparseable_heals_and_flags() {
    // Read fine but the content won't parse into a valid config — same real
    // "doc went bad" outcome as Corrupt.
    SelfHeal h = decideSelfHeal(DocLoad::Ok, /*parsedOk=*/false);
    TEST_ASSERT_TRUE(h.useDefaults);
    TEST_ASSERT_TRUE(h.resave);
    TEST_ASSERT_TRUE(h.configReset);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_absent_uses_defaults_silently);
    RUN_TEST(test_ok_and_parsed_keeps_loaded);
    RUN_TEST(test_corrupt_heals_and_flags);
    RUN_TEST(test_ok_but_unparseable_heals_and_flags);
    return UNITY_END();
}
