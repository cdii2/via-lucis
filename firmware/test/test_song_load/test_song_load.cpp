// §3-E item 2 / A154: the pure decision behind 404-vs-400 on song load —
// see vialucis/song_load.h for why this is split from App::loadSong's
// esp32-only file IO.

#include <unity.h>

#include "vialucis/song_load.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static void test_missing_file_is_not_found_regardless_of_parse_error() {
    TEST_ASSERT_TRUE(classifySongLoad(false, MidiParseError::Ok) ==
                     SongLoadOutcome::NotFound);
    TEST_ASSERT_TRUE(classifySongLoad(false, MidiParseError::Truncated) ==
                     SongLoadOutcome::NotFound);
    TEST_ASSERT_TRUE(classifySongLoad(false, MidiParseError::NotMidi) ==
                     SongLoadOutcome::NotFound);
}

static void test_read_file_with_parse_error_is_parse_error() {
    TEST_ASSERT_TRUE(classifySongLoad(true, MidiParseError::Truncated) ==
                     SongLoadOutcome::ParseError);
    TEST_ASSERT_TRUE(classifySongLoad(true, MidiParseError::NotMidi) ==
                     SongLoadOutcome::ParseError);
    TEST_ASSERT_TRUE(classifySongLoad(true, MidiParseError::BadTrack) ==
                     SongLoadOutcome::ParseError);
    TEST_ASSERT_TRUE(classifySongLoad(true, MidiParseError::SmpteDivision) ==
                     SongLoadOutcome::ParseError);
}

static void test_read_file_parsed_ok_is_ok() {
    TEST_ASSERT_TRUE(classifySongLoad(true, MidiParseError::Ok) ==
                     SongLoadOutcome::Ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_missing_file_is_not_found_regardless_of_parse_error);
    RUN_TEST(test_read_file_with_parse_error_is_parse_error);
    RUN_TEST(test_read_file_parsed_ok_is_ok);
    return UNITY_END();
}
