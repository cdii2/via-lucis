// SoundingSet (R3): the one "notes currently sounding" concept. Three
// consumers (engine lights, scheduler, note emitter) add on note-on and
// erase the FIRST matching entry on note-off; stop/flush drains everything.

#include <unity.h>

#include <vector>

#include "vialucis/sounding_set.h"

using namespace vialucis;

namespace {
struct Entry {
    int note;
    int channel;
};
}  // namespace

void setUp() {}
void tearDown() {}

void test_erase_first_removes_only_the_first_match() {
    SoundingSet<Entry> set;
    set.add({60, 0});
    set.add({60, 0});  // same note struck twice (overlapping voices)
    set.add({64, 0});

    bool erased = set.eraseFirst([](const Entry& e) { return e.note == 60; });
    TEST_ASSERT_TRUE(erased);
    TEST_ASSERT_EQUAL_UINT(2, set.items().size());
    TEST_ASSERT_EQUAL_INT(60, set.items()[0].note);  // the second 60 survives
    TEST_ASSERT_EQUAL_INT(64, set.items()[1].note);
}

void test_erase_first_without_match_reports_false() {
    SoundingSet<Entry> set;
    set.add({60, 0});
    TEST_ASSERT_FALSE(
        set.eraseFirst([](const Entry& e) { return e.note == 99; }));
    TEST_ASSERT_EQUAL_UINT(1, set.items().size());
}

void test_drain_visits_everything_then_empties() {
    SoundingSet<Entry> set;
    set.add({60, 0});
    set.add({64, 1});
    std::vector<int> seen;
    set.drain([&](const Entry& e) { seen.push_back(e.note); });
    TEST_ASSERT_EQUAL_UINT(2, seen.size());
    TEST_ASSERT_EQUAL_INT(60, seen[0]);
    TEST_ASSERT_EQUAL_INT(64, seen[1]);
    TEST_ASSERT_TRUE(set.empty());
}

void test_clear_discards_without_visiting() {
    SoundingSet<Entry> set;
    set.add({60, 0});
    set.clear();
    TEST_ASSERT_TRUE(set.empty());
    TEST_ASSERT_EQUAL_UINT(0, set.items().size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_erase_first_removes_only_the_first_match);
    RUN_TEST(test_erase_first_without_match_reports_false);
    RUN_TEST(test_drain_visits_everything_then_empties);
    RUN_TEST(test_clear_discards_without_visiting);
    return UNITY_END();
}
