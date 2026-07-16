// Atomic-persist decision logic (B4). The pure stage->rename->cleanup sequence
// is exercised over an in-memory fake FS so the decision path is native-tested;
// only the real LittleFS syscalls (song_store.cpp) stay esp32-only.

#include <unity.h>

#include <map>
#include <string>

#include "vialucis/atomic_store.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

// --- tmp path helpers ---------------------------------------------------

void test_tmp_path_for_appends_suffix() {
    TEST_ASSERT_EQUAL_STRING("/settings.json.tmp",
                             tmpPathFor("/settings.json").c_str());
    TEST_ASSERT_EQUAL_STRING("/songs/a.mid.tmp",
                             tmpPathFor("/songs/a.mid").c_str());
}

void test_is_tmp_path() {
    TEST_ASSERT_TRUE(isTmpPath("/songs/a.mid.tmp"));
    TEST_ASSERT_TRUE(isTmpPath("x.tmp"));
    TEST_ASSERT_FALSE(isTmpPath("/songs/a.mid"));
    TEST_ASSERT_FALSE(isTmpPath("tmp"));
    TEST_ASSERT_FALSE(isTmpPath(".tm"));
}

// --- a tiny in-memory FS to drive the pure sequence ----------------------

namespace {
struct FakeFs {
    std::map<std::string, std::string> files;
    bool writeFails = false;
    bool renameFails = false;

    // writeFn: returns false when we want to simulate a short/failed write.
    bool write(const std::string& path, const std::string& body) {
        if (writeFails) return false;
        files[path] = body;
        return true;
    }
    bool rename(const std::string& from, const std::string& to) {
        if (renameFails) return false;
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;  // replace-over-target (LittleFS semantics)
        files.erase(it);
        return true;
    }
    void remove(const std::string& path) { files.erase(path); }

    bool persist(const std::string& dst, const std::string& body) {
        return atomicPersist(
            dst, [&](const std::string& t) { return write(t, body); },
            [&](const std::string& f, const std::string& t) {
                return rename(f, t);
            },
            [&](const std::string& t) { remove(t); });
    }
};
}  // namespace

void test_success_leaves_only_target() {
    FakeFs fs;
    TEST_ASSERT_TRUE(fs.persist("/settings.json", "{\"a\":1}"));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", fs.files["/settings.json"].c_str());
    // No temp left behind.
    TEST_ASSERT_TRUE(fs.files.find("/settings.json.tmp") == fs.files.end());
    TEST_ASSERT_EQUAL_UINT32(1, fs.files.size());
}

void test_write_failure_leaves_no_temp_and_keeps_old() {
    FakeFs fs;
    fs.files["/settings.json"] = "OLD";  // an existing good file
    fs.writeFails = true;
    TEST_ASSERT_FALSE(fs.persist("/settings.json", "NEW"));
    // Old file untouched; no temp lingering.
    TEST_ASSERT_EQUAL_STRING("OLD", fs.files["/settings.json"].c_str());
    TEST_ASSERT_TRUE(fs.files.find("/settings.json.tmp") == fs.files.end());
}

void test_rename_failure_removes_temp_and_keeps_old() {
    FakeFs fs;
    fs.files["/settings.json"] = "OLD";
    fs.renameFails = true;
    TEST_ASSERT_FALSE(fs.persist("/settings.json", "NEW"));
    // The staged temp was written then discarded; old file survives intact.
    TEST_ASSERT_EQUAL_STRING("OLD", fs.files["/settings.json"].c_str());
    TEST_ASSERT_TRUE(fs.files.find("/settings.json.tmp") == fs.files.end());
    TEST_ASSERT_EQUAL_UINT32(1, fs.files.size());
}

void test_success_replaces_existing_atomically() {
    FakeFs fs;
    fs.files["/afk.json"] = "OLD";
    TEST_ASSERT_TRUE(fs.persist("/afk.json", "NEW"));
    TEST_ASSERT_EQUAL_STRING("NEW", fs.files["/afk.json"].c_str());
    TEST_ASSERT_EQUAL_UINT32(1, fs.files.size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tmp_path_for_appends_suffix);
    RUN_TEST(test_is_tmp_path);
    RUN_TEST(test_success_leaves_only_target);
    RUN_TEST(test_write_failure_leaves_no_temp_and_keeps_old);
    RUN_TEST(test_rename_failure_removes_temp_and_keeps_old);
    RUN_TEST(test_success_replaces_existing_atomically);
    return UNITY_END();
}
