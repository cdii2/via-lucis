#include <unity.h>

#include <string>

#include "vialucis/calibration.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static void assertTablesEqual(const KeyLedTable& a, const KeyLedTable& b) {
    TEST_ASSERT_EQUAL_UINT16(a.ledCount(), b.ledCount());
    for (uint8_t n = 21; n <= 108; ++n) {
        TEST_ASSERT_EQUAL_MESSAGE(a.forNote(n).valid, b.forNote(n).valid,
                                  "validity must match");
        if (a.forNote(n).valid) {
            TEST_ASSERT_EQUAL_UINT16(a.forNote(n).first, b.forNote(n).first);
            TEST_ASSERT_EQUAL_UINT16(a.forNote(n).last, b.forNote(n).last);
        }
    }
}

// The CRITICAL boot-fallback pin: no /calibration.json ⇒ the table every
// existing device runs on is integer-identical to v1's two-setting formula.
static void test_from_settings_is_v1_identical() {
    Settings s;
    Calibration c = Calibration::fromSettings(s, 360);
    TEST_ASSERT_EQUAL_STRING("twoPoint", c.tier.c_str());
    TEST_ASSERT_FALSE(c.reversed);
    assertTablesEqual(TableBuilder::fromTwoPoint(s.ledMapConfig(360)), c.table);

    Settings custom;
    custom.offsetMm = 37.5f;
    custom.ledsPerMeter = 96.0f;
    Calibration cc = Calibration::fromSettings(custom, 240);
    assertTablesEqual(TableBuilder::fromTwoPoint(custom.ledMapConfig(240)),
                      cc.table);
    TEST_ASSERT_EQUAL_FLOAT(37.5f, cc.offsetMm);
    TEST_ASSERT_EQUAL_FLOAT(96.0f, cc.ledsPerMeter);
}

static void test_two_point_round_trip() {
    Settings s;
    s.offsetMm = -12.0f;
    s.ledsPerMeter = 144.0f;
    Calibration c = Calibration::fromSettings(s, 360, /*reversed=*/true);
    Calibration back;
    CalibResult r = Calibration::fromJson(c.toJson().c_str(), 360, back);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_EQUAL_STRING("twoPoint", back.tier.c_str());
    TEST_ASSERT_TRUE(back.reversed);
    TEST_ASSERT_EQUAL_FLOAT(-12.0f, back.offsetMm);
    TEST_ASSERT_EQUAL_FLOAT(144.0f, back.ledsPerMeter);
    assertTablesEqual(c.table, back.table);
}

static void test_multi_point_round_trip() {
    const char* body =
        "{\"tier\":\"multiPoint\",\"landmarks\":["
        "{\"note\":21,\"led\":2},{\"note\":60,\"led\":150},"
        "{\"note\":108,\"led\":340}]}";
    Calibration c;
    CalibResult r = Calibration::fromJson(body, 360, c);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_EQUAL_STRING("multiPoint", c.tier.c_str());
    TEST_ASSERT_FALSE(c.reversed);
    TEST_ASSERT_EQUAL_size_t(3, c.landmarks.size());

    Calibration back;
    CalibResult r2 = Calibration::fromJson(c.toJson().c_str(), 360, back);
    TEST_ASSERT_TRUE(r2.ok());
    TEST_ASSERT_EQUAL_size_t(3, back.landmarks.size());
    TEST_ASSERT_EQUAL_UINT8(60, back.landmarks[1].note);
    TEST_ASSERT_EQUAL_UINT16(150, back.landmarks[1].led);
    assertTablesEqual(c.table, back.table);
}

static void test_multi_point_descending_reports_reversed() {
    const char* body =
        "{\"tier\":\"multiPoint\",\"landmarks\":["
        "{\"note\":21,\"led\":340},{\"note\":108,\"led\":2}]}";
    Calibration c;
    TEST_ASSERT_TRUE(Calibration::fromJson(body, 360, c).ok());
    TEST_ASSERT_TRUE(c.reversed);
}

static void test_per_key_round_trip_and_gaps_stay_dark() {
    const char* body =
        "{\"tier\":\"perKey\",\"keys\":["
        "{\"note\":60,\"first\":150,\"last\":152},"
        "{\"note\":62,\"first\":158,\"last\":160}]}";
    Calibration c;
    CalibResult r = Calibration::fromJson(body, 360, c);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_TRUE(c.table.forNote(60).valid);
    TEST_ASSERT_EQUAL_UINT16(150, c.table.forNote(60).first);
    TEST_ASSERT_FALSE(c.table.forNote(61).valid);  // unlisted ⇒ dark
    TEST_ASSERT_FALSE(c.table.forNote(21).valid);

    Calibration back;
    TEST_ASSERT_TRUE(Calibration::fromJson(c.toJson().c_str(), 360, back).ok());
    assertTablesEqual(c.table, back.table);
}

static void test_garbage_and_partial_bodies_are_typed_400s() {
    Calibration c;
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadJson,
                      Calibration::fromJson("not json{{", 360, c).kind);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadJson,
                      Calibration::fromJson("[1,2,3]", 360, c).kind);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadJson,
                      Calibration::fromJson(
                          "{\"tier\":\"perKey\",\"keys\":[{\"note\":60,",
                          360, c).kind);  // truncated upload
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadJson,
                      Calibration::fromJson(nullptr, 360, c).kind);
}

static void test_bad_or_missing_tier_is_typed() {
    Calibration c;
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadTier,
                      Calibration::fromJson("{}", 360, c).kind);
    TEST_ASSERT_EQUAL(
        CalibResult::Kind::BadTier,
        Calibration::fromJson("{\"tier\":\"nope\"}", 360, c).kind);
}

static void test_missing_tier_fields_are_typed() {
    Calibration c;
    TEST_ASSERT_EQUAL(
        CalibResult::Kind::MissingField,
        Calibration::fromJson("{\"tier\":\"twoPoint\",\"offsetMm\":1}", 360,
                              c).kind);
    TEST_ASSERT_EQUAL(
        CalibResult::Kind::MissingField,
        Calibration::fromJson("{\"tier\":\"multiPoint\"}", 360, c).kind);
    TEST_ASSERT_EQUAL(
        CalibResult::Kind::MissingField,
        Calibration::fromJson(
            "{\"tier\":\"multiPoint\",\"landmarks\":[{\"note\":21}]}", 360,
            c).kind);
    TEST_ASSERT_EQUAL(
        CalibResult::Kind::MissingField,
        Calibration::fromJson("{\"tier\":\"perKey\"}", 360, c).kind);
    TEST_ASSERT_EQUAL(
        CalibResult::Kind::MissingField,
        Calibration::fromJson(
            "{\"tier\":\"perKey\",\"keys\":[{\"note\":60,\"first\":1}]}", 360,
            c).kind);
}

static void test_table_errors_propagate_with_detail() {
    Calibration c;
    CalibResult few = Calibration::fromJson(
        "{\"tier\":\"multiPoint\",\"landmarks\":[{\"note\":21,\"led\":2}]}",
        360, c);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadTable, few.kind);
    TEST_ASSERT_EQUAL(TableError::TooFewLandmarks, few.tableError);

    CalibResult zig = Calibration::fromJson(
        "{\"tier\":\"multiPoint\",\"landmarks\":["
        "{\"note\":21,\"led\":2},{\"note\":60,\"led\":150},"
        "{\"note\":108,\"led\":100}]}",
        360, c);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadTable, zig.kind);
    TEST_ASSERT_EQUAL(TableError::DirectionMixed, zig.tableError);

    CalibResult overlap = Calibration::fromJson(
        "{\"tier\":\"perKey\",\"keys\":["
        "{\"note\":60,\"first\":150,\"last\":155},"
        "{\"note\":61,\"first\":155,\"last\":158}]}",
        360, c);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadTable, overlap.kind);
    TEST_ASSERT_EQUAL(TableError::Overlap, overlap.tableError);
    TEST_ASSERT_EQUAL_UINT8(61, overlap.badKey);

    CalibResult off = Calibration::fromJson(
        "{\"tier\":\"perKey\",\"keys\":[{\"note\":60,\"first\":358,"
        "\"last\":365}]}",
        360, c);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadTable, off.kind);
    TEST_ASSERT_EQUAL(TableError::RangeOffStrip, off.tableError);

    CalibResult badNote = Calibration::fromJson(
        "{\"tier\":\"perKey\",\"keys\":[{\"note\":10,\"first\":1,"
        "\"last\":2}]}",
        360, c);
    TEST_ASSERT_EQUAL(CalibResult::Kind::BadTable, badNote.kind);
    TEST_ASSERT_EQUAL(TableError::BadLandmarkNote, badNote.tableError);
}

static void test_error_messages_are_distinct_and_nonempty() {
    Calibration c;
    const char* bodies[] = {
        "junk",
        "{\"tier\":\"nope\"}",
        "{\"tier\":\"perKey\"}",
        "{\"tier\":\"multiPoint\",\"landmarks\":[{\"note\":21,\"led\":2}]}",
        "{\"tier\":\"perKey\",\"keys\":[{\"note\":60,\"first\":150,"
        "\"last\":155},{\"note\":61,\"first\":155,\"last\":158}]}",
    };
    std::string seen[5];
    for (int i = 0; i < 5; ++i) {
        CalibResult r = Calibration::fromJson(bodies[i], 360, c);
        TEST_ASSERT_FALSE(r.ok());
        seen[i] = r.message();
        TEST_ASSERT_TRUE_MESSAGE(!seen[i].empty(), "message must not be empty");
        for (int j = 0; j < i; ++j)
            TEST_ASSERT_TRUE_MESSAGE(seen[i] != seen[j],
                                     "messages must be distinct");
    }
}

// A failed parse must leave the output untouched (the device keeps running
// on its current calibration after a rejected PUT).
static void test_failed_parse_leaves_out_untouched() {
    Settings s;
    Calibration current = Calibration::fromSettings(s, 360);
    KeyLedTable before = current.table;
    Calibration::fromJson("{\"tier\":\"perKey\",\"keys\":[{\"note\":60,"
                          "\"first\":150,\"last\":155},{\"note\":61,"
                          "\"first\":155,\"last\":158}]}",
                          360, current);
    assertTablesEqual(before, current.table);
    TEST_ASSERT_EQUAL_STRING("twoPoint", current.tier.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_from_settings_is_v1_identical);
    RUN_TEST(test_two_point_round_trip);
    RUN_TEST(test_multi_point_round_trip);
    RUN_TEST(test_multi_point_descending_reports_reversed);
    RUN_TEST(test_per_key_round_trip_and_gaps_stay_dark);
    RUN_TEST(test_garbage_and_partial_bodies_are_typed_400s);
    RUN_TEST(test_bad_or_missing_tier_is_typed);
    RUN_TEST(test_missing_tier_fields_are_typed);
    RUN_TEST(test_table_errors_propagate_with_detail);
    RUN_TEST(test_error_messages_are_distinct_and_nonempty);
    RUN_TEST(test_failed_parse_leaves_out_untouched);
    return UNITY_END();
}
