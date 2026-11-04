// romscan.h - find the SC-55 and MT-32 ROM sets in a folder by content
#pragma once
#include <string>
#include <vector>

struct RomFile {
    std::string name;        // file name inside the folder
    unsigned long size = 0;
    unsigned long mtime = 0;
    std::string sha256;      // lower-case hex
};

// List the folder and hash every file (cached in <dir>/SC55W.CAC).
std::vector<RomFile> scan_roms(const std::string& dir);

struct Sc55Choice {
    std::string set_name;              // e.g. "mk2-v1.01"
    int romset = -1;                   // Romset enum value
    std::string paths[8];              // per RomLocation, full paths, "" = unused
};

// Pick an SC-55 set: wanted = "auto" or a set name. Returns false with a
// message if not found. complete = names of all complete sets found.
bool find_sc55(const std::string& dir, const std::vector<RomFile>& files, const std::string& wanted,
               Sc55Choice& out, std::vector<std::string>& complete, std::string& msg);

struct Mt32Choice {
    std::string control, pcm;          // full paths
    std::string control_desc;          // e.g. "MT-32 Control v1.07"
    bool cm32l = false;
};

// wanted = "AUTO", "MT32" or "CM32L"
bool find_mt32(const std::string& dir, const std::vector<RomFile>& files, const std::string& wanted,
               Mt32Choice& out, std::string& msg);

std::string path_join(const std::string& dir, const std::string& name);
