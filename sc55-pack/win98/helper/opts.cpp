// opts.cpp - parse/format SBEMU-style option lines (see opts.h)
#include "opts.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static std::string upper(std::string s)
{
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static int num(const std::string& v, int hex = 0)
{
    return (int)strtol(v.c_str(), nullptr, hex ? 16 : 10);
}

void Options::parse(const std::string& text)
{
    std::vector<std::string> toks;
    std::string cur;
    for (char c : text) {
        if (isspace((unsigned char)c)) { if (!cur.empty()) toks.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) toks.push_back(cur);

    for (auto& t : toks) {
        if (t.size() < 2 || (t[0] != '/' && t[0] != '-')) { passthru += (passthru.empty() ? "" : " ") + t; continue; }
        // split name / value: /NAME:value, /NAME=value, or /Xnnn
        std::string body = t.substr(1), name, val;
        size_t p = body.find_first_of(":=");
        if (p != std::string::npos) { name = upper(body.substr(0, p)); val = body.substr(p + 1); }
        else {
            size_t i = 0;
            while (i < body.size() && isalpha((unsigned char)body[i])) i++;
            name = upper(body.substr(0, i)); val = body.substr(i);
        }
        bool has = !val.empty();
        if (name == "A") sb_port = num(val, 1);
        else if (name == "I") irq = num(val);
        else if (name == "D") dma8 = num(val);
        else if (name == "H") dma16 = num(val);
        else if (name == "T") sbtype = num(val);
        else if (name == "OPL") opl = num(val);
        else if (name == "FMCORE") fmcore = num(val);
        else if (name == "FIXTC") fixtc = has ? num(val) : 1;
        else if (name == "VOL") vol = num(val);
        else if (name == "VSBVOL") sbvol = num(val);
        else if (name == "VOPLVOL") oplvol = num(val);
        else if (name == "VMIDIVOL") midivol = num(val);
        else if (name == "K") rate = num(val);
        else if (name == "P") mpu_port = num(val, 1);
        else if (name == "VMPU") vmpu = has ? num(val) : 1;
        else if (name == "VSCROM") scrom = val;
        else if (name == "VSCSET") scset = val;
        else if (name == "VSCGAIN") scgain = num(val);
        else if (name == "VSCVOICES") scvoices = num(val);
        else if (name == "VSCFX") scfx = num(val);
        else if (name == "VSCAUTO") scauto = num(val);
        else if (name == "VSCAHEAD") scahead = num(val);
        else if (name == "VTIMER") vtimer = num(val);
        else if (name == "VSF") sf = val;
        else if (name == "VSFVOICES") sfvoices = num(val);
        else if (name == "VMT32") mt32 = upper(val.empty() ? "AUTO" : val);
        else if (name == "VSFREV") sfrev = num(val);
        else if (name == "VSFROOM") sfroom = num(val);
        else if (name == "VSFDAMP") sfdamp = num(val);
        else if (name == "VSFCHO") sfcho = num(val);
        else if (name == "VSFCRATE") sfcrate = num(val);
        else if (name == "VSFCDEPTH") sfcdepth = num(val);
        else if (name == "VSFSEND") sfsend = num(val);
        else passthru += (passthru.empty() ? "" : " ") + t;   // /O /PM /RM /SC /MCOM /VCDA ...
    }
    // clamp to the ranges SBSET offers
    auto clamp = [](int& v, int lo, int hi) { if (v < lo) v = lo; if (v > hi) v = hi; };
    clamp(sbtype, 1, 6); clamp(vol, 0, 100); clamp(sbvol, 0, 100); clamp(oplvol, 0, 100);
    clamp(midivol, 0, 400); clamp(scgain, 0, 1600); clamp(sfvoices, 8, 128);
    clamp(sfrev, 0, 100); clamp(sfroom, 0, 100); clamp(sfdamp, 0, 100); clamp(sfcho, 0, 100);
    clamp(sfcrate, 1, 30); clamp(sfcdepth, 0, 100); clamp(scahead, 0, 500);
    if (scvoices && scvoices < 4) scvoices = 4;
    if (scvoices > 24) scvoices = 24;
}

std::string Options::format() const
{
    Options d;   // defaults
    std::string s;
    char b[300];
    auto add = [&](const char* fmt, auto v) { snprintf(b, sizeof b, fmt, v); s += (s.empty() ? "" : " "); s += b; };
    if (sb_port != d.sb_port) add("/A:%X", sb_port);
    if (irq != d.irq) add("/I:%d", irq);
    if (dma8 != d.dma8) add("/D:%d", dma8);
    if (dma16 != d.dma16) add("/H:%d", dma16);
    if (sbtype != d.sbtype) add("/T:%d", sbtype);
    if (opl != d.opl) add("/OPL:%d", opl);
    if (fmcore != d.fmcore) add("/FMCORE:%d", fmcore);
    if (fixtc != d.fixtc) add("/FIXTC:%d", fixtc);
    if (vol != d.vol) add("/VOL:%d", vol);
    if (sbvol != d.sbvol) add("/VSBVOL:%d", sbvol);
    if (oplvol != d.oplvol) add("/VOPLVOL:%d", oplvol);
    if (midivol != d.midivol) add("/VMIDIVOL:%d", midivol);
    if (rate != d.rate) add("/K:%d", rate);
    if (mpu_port != d.mpu_port) add("/P:%X", mpu_port);
    if (!vmpu) add("/VMPU:%d", 0);
    if (synth() == SYN_SF2) {
        add("/VSF:%s", sf.c_str());
        if (sfvoices != d.sfvoices) add("/VSFVOICES:%d", sfvoices);
        if (sfrev != d.sfrev) add("/VSFREV:%d", sfrev);
        if (sfroom != d.sfroom) add("/VSFROOM:%d", sfroom);
        if (sfdamp != d.sfdamp) add("/VSFDAMP:%d", sfdamp);
        if (sfcho != d.sfcho) add("/VSFCHO:%d", sfcho);
        if (sfcrate != d.sfcrate) add("/VSFCRATE:%d", sfcrate);
        if (sfcdepth != d.sfcdepth) add("/VSFCDEPTH:%d", sfcdepth);
        if (sfsend != d.sfsend) add("/VSFSEND:%d", sfsend);
    } else if (synth() == SYN_MT32) {
        add("/VMT32:%s", mt32.c_str());
        if (scgain != d.scgain) add("/VSCGAIN:%d", scgain);
    } else {
        if (upper(scset) != "AUTO") add("/VSCSET:%s", scset.c_str());
        if (scgain != d.scgain) add("/VSCGAIN:%d", scgain);
        if (scvoices != d.scvoices) add("/VSCVOICES:%d", scvoices);
        if (scfx != d.scfx) add("/VSCFX:%d", scfx);
        if (scauto != d.scauto) add("/VSCAUTO:%d", scauto);
    }
    if (scahead != d.scahead) add("/VSCAHEAD:%d", scahead);
    if (vtimer != d.vtimer) add("/VTIMER:%d", vtimer);
    if (!passthru.empty()) { s += (s.empty() ? "" : " "); s += passthru; }
    return s;
}

bool load_text(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool save_text(const std::string& path, const std::string& text)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fwrite("\r\n", 1, 2, f);
    fclose(f);
    return true;
}
