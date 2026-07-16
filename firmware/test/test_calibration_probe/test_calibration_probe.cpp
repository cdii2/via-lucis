#include <unity.h>

#include "vialucis/calibration_probe.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static void test_initial_state_is_idle() {
    CalibrationProbe p;
    TEST_ASSERT_FALSE(p.armed());
    TEST_ASSERT_FALSE(p.hasCapture());
    TEST_ASSERT_FALSE(p.timedOut());
}

static void test_arm_sets_armed_and_led() {
    CalibrationProbe p;
    p.arm(42, 1000, 5000);
    TEST_ASSERT_TRUE(p.armed());
    TEST_ASSERT_EQUAL_UINT16(42, p.led());
    TEST_ASSERT_FALSE(p.hasCapture());
    TEST_ASSERT_FALSE(p.timedOut());
}

static void test_note_on_while_armed_captures_and_disarms() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);
    bool consumed = p.onNoteOn(60);
    TEST_ASSERT_TRUE(consumed);
    TEST_ASSERT_FALSE(p.armed());
    TEST_ASSERT_TRUE(p.hasCapture());
    TEST_ASSERT_EQUAL_UINT8(60, p.capturedNote());
    TEST_ASSERT_FALSE(p.timedOut());  // captured, not expired
}

static void test_note_on_while_disarmed_is_ignored() {
    CalibrationProbe p;
    bool consumed = p.onNoteOn(60);
    TEST_ASSERT_FALSE(consumed);
    TEST_ASSERT_FALSE(p.hasCapture());
}

static void test_cancel_clears_armed_and_capture() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);
    p.cancel();
    TEST_ASSERT_FALSE(p.armed());
    TEST_ASSERT_FALSE(p.hasCapture());
    TEST_ASSERT_FALSE(p.timedOut());
}

// B6d: tickExpire must flip `timedOut` on the expiring edge, and the wizard
// must be able to see it (not just an ambiguous armed:false).
static void test_tick_expire_before_deadline_is_noop() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);
    TEST_ASSERT_FALSE(p.tickExpire(1000));   // exactly at arm time
    TEST_ASSERT_FALSE(p.tickExpire(5999));   // still before deadline (6000)
    TEST_ASSERT_TRUE(p.armed());
    TEST_ASSERT_FALSE(p.timedOut());
}

static void test_tick_expire_at_deadline_sets_timed_out() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);  // deadline = 6000
    TEST_ASSERT_TRUE(p.tickExpire(6000));
    TEST_ASSERT_FALSE(p.armed());
    TEST_ASSERT_TRUE(p.timedOut());
    TEST_ASSERT_FALSE(p.hasCapture());
}

// The edge fires exactly once; polling again after expiry must not re-fire
// tickExpire's return (frame-dirty callers rely on the edge), but the
// STICKY timedOut() flag must keep reading true across those later polls —
// that's the whole point (a wizard poll landing seconds after the actual
// expiring tick still needs to see it).
static void test_timed_out_stays_true_after_the_expiring_tick() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);
    TEST_ASSERT_TRUE(p.tickExpire(6000));
    TEST_ASSERT_FALSE(p.tickExpire(9000));  // already disarmed: no re-fire
    TEST_ASSERT_TRUE(p.timedOut());
}

// Explicit cancel is a distinct terminal state from expiry — the wizard's
// DELETE (user gave up / moved the slider again) must not read as a
// timeout, and a stale timedOut from a PRIOR arm must not leak through a
// cancel of a later one.
static void test_cancel_after_expiry_clears_timed_out() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);
    p.tickExpire(6000);
    TEST_ASSERT_TRUE(p.timedOut());
    p.cancel();
    TEST_ASSERT_FALSE(p.timedOut());
}

static void test_new_arm_clears_previous_timed_out() {
    CalibrationProbe p;
    p.arm(10, 1000, 5000);
    p.tickExpire(6000);
    TEST_ASSERT_TRUE(p.timedOut());
    p.arm(20, 7000, 5000);  // wizard tries the next LED
    TEST_ASSERT_TRUE(p.armed());
    TEST_ASSERT_FALSE(p.timedOut());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_arm_sets_armed_and_led);
    RUN_TEST(test_note_on_while_armed_captures_and_disarms);
    RUN_TEST(test_note_on_while_disarmed_is_ignored);
    RUN_TEST(test_cancel_clears_armed_and_capture);
    RUN_TEST(test_tick_expire_before_deadline_is_noop);
    RUN_TEST(test_tick_expire_at_deadline_sets_timed_out);
    RUN_TEST(test_timed_out_stays_true_after_the_expiring_tick);
    RUN_TEST(test_cancel_after_expiry_clears_timed_out);
    RUN_TEST(test_new_arm_clears_previous_timed_out);
    return UNITY_END();
}
