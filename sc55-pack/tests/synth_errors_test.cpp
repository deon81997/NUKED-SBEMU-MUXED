// Host-side checks of the ROM discovery/diagnostics in sc55/sc55synth.cpp.
// A real boot needs your own ROM dumps and is exercised on DOS (see README).
// GPL-2.0-or-later.
#include "../sc55/sc55synth.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: %s -- ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void names_cb(const char* n, void* u) { static_cast<std::vector<std::string>*>(u)->push_back(n); }

int main(int argc, char** argv)
{
    std::string tmp = argc > 1 ? argv[1] : "/tmp/claude-0/synth_test";
    mkdir(tmp.c_str(), 0755);
    std::string empty = tmp + "/empty", junk = tmp + "/junk";
    mkdir(empty.c_str(), 0755);
    mkdir(junk.c_str(), 0755);
    {
        FILE* f = fopen((junk + "/FAKE.ROM").c_str(), "wb");
        std::vector<unsigned char> d(0x80000, 0x55); // right size for ROM2, wrong content
        fwrite(d.data(), 1, d.size(), f); fclose(f);
        f = fopen((junk + "/notes.txt").c_str(), "wb"); fputs("hello", f); fclose(f);
    }

    std::vector<std::string> names;
    SC55_ListModels(names_cb, &names);
    printf("models: %zu (first %s)\n", names.size(), names.empty() ? "-" : names[0].c_str());
    CHECK(names.size() >= 6 && names[0] == "mk1-v2.00", "preferred order");
    bool has_mk2 = false, has_jv = false;
    for (auto& n : names) { has_mk2 |= n == "mk2-v1.01"; has_jv |= n.rfind("jv880", 0) == 0; }
    CHECK(has_mk2 && !has_jv, "SC-55 family only");

    SC55_Config cfg{};
    cfg.gain_percent = 100;

    cfg.romdir = "/definitely/not/here";
    CHECK(!SC55_Init(&cfg), "missing dir must fail");
    printf("  -> %s\n", SC55_LastError());
    CHECK(strstr(SC55_LastError(), "Cannot open ROM folder"), "message");

    cfg.romdir = empty.c_str();
    CHECK(!SC55_Init(&cfg), "empty dir must fail");
    printf("  -> %s\n", SC55_LastError());
    CHECK(strstr(SC55_LastError(), "No SC-55 ROMs found"), "message");

    cfg.romdir = junk.c_str();
    CHECK(!SC55_Init(&cfg), "junk dir must fail");
    printf("  -> %s\n", SC55_LastError());
    CHECK(strstr(SC55_LastError(), "(1 file(s) checked"), "exactly the one plausible file must be hashed (the 5-byte note is skipped)");

    cfg.romset = "no-such-set";
    CHECK(!SC55_Init(&cfg), "unknown set must fail");
    printf("  -> %s\n", SC55_LastError());
    CHECK(strstr(SC55_LastError(), "Unknown ROM set"), "message");

    cfg.romset = "MK1-V1.21"; // case-insensitive name, but nothing to load
    CHECK(!SC55_Init(&cfg), "incomplete set must fail");
    printf("  -> %s\n", SC55_LastError());
    CHECK(strstr(SC55_LastError(), "incomplete") && strstr(SC55_LastError(), "0 of"), "message");

    CHECK(!SC55_IsLoaded() && SC55_Active() == 0 && SC55_PostByte(0x90) == 0, "no state after failures");
    printf(g_fail ? "\n%d FAILURE(S)\n" : "\nsynth discovery tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
