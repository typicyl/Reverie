// Reverie/Studio/StudioProject.cpp - see StudioProject.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Line-based text format (identifiers are space-free; an asset path is the rest of its line):
//   RVPROJ 1
//   A <key> <path...>
//   B <name> <parent|-> <gain> <muted> <soloed>
//   P <name> <default> <min> <max> <smoothMs>
//   E <name> <priority> <maxInstances> <group>
//   L <eventName> <soundKey> <vol> <volVar> <pitch> <pitchVar> <loop> <prob> <bus|-> <param|-> <lo> <hi>
//   M <name> <bpm> <beatsPerBar>
//   K <musicName> <soundKey> <gain> <param|-> <lo> <hi>
#include "StudioProject.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace reverie::studio {

namespace {
std::string Dash(const std::string& s) { return s.empty() ? "-" : s; }
std::string UnDash(const std::string& s) { return s == "-" ? std::string() : s; }

StudioEvent* FindEvent(StudioProject& p, const std::string& name) {
    for (StudioEvent& e : p.events)
        if (e.name == name) return &e;
    return nullptr;
}
StudioMusicState* FindMusic(StudioProject& p, const std::string& name) {
    for (StudioMusicState& m : p.music)
        if (m.name == name) return &m;
    return nullptr;
}
} // namespace

std::string WriteProjectString(const StudioProject& project) {
    std::ostringstream o;
    o << std::setprecision(9);
    o << "RVPROJ 1\n";
    for (const StudioAsset& a : project.assets) o << "A " << a.key << ' ' << a.path << '\n';
    for (const StudioBus& b : project.buses)
        o << "B " << b.name << ' ' << Dash(b.parent) << ' ' << b.gain << ' ' << (b.muted ? 1 : 0)
          << ' ' << (b.soloed ? 1 : 0) << '\n';
    for (const StudioParam& p : project.parameters)
        o << "P " << p.name << ' ' << p.defaultValue << ' ' << p.minValue << ' ' << p.maxValue << ' '
          << p.smoothMs << '\n';
    for (const StudioEvent& e : project.events) {
        o << "E " << e.name << ' ' << e.priority << ' ' << e.maxInstances << ' '
          << e.concurrencyGroup << '\n';
        for (const StudioEventLayer& l : e.layers)
            o << "L " << e.name << ' ' << l.soundKey << ' ' << l.volume << ' ' << l.volumeVariance
              << ' ' << l.pitch << ' ' << l.pitchVariance << ' ' << (l.loop ? 1 : 0) << ' '
              << l.probability << ' ' << Dash(l.bus) << ' ' << Dash(l.gainParam) << ' ' << l.paramLo
              << ' ' << l.paramHi << '\n';
    }
    for (const StudioMusicState& m : project.music) {
        o << "M " << m.name << ' ' << m.bpm << ' ' << m.beatsPerBar << '\n';
        for (const StudioMusicLayer& k : m.layers)
            o << "K " << m.name << ' ' << k.soundKey << ' ' << k.gain << ' ' << Dash(k.gainParam)
              << ' ' << k.paramLo << ' ' << k.paramHi << '\n';
    }
    return o.str();
}

bool ReadProjectString(StudioProject& out, const std::string& text) {
    out = StudioProject{};
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return false;
    {
        std::istringstream h(line);
        std::string magic;
        int version = 0;
        h >> magic >> version;
        if (magic != "RVPROJ" || version != 1) return false;
    }
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream s(line);
        std::string type;
        s >> type;
        if (type == "A") {
            StudioAsset a;
            s >> a.key;
            std::string rest;
            std::getline(s, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            a.path = rest;
            out.assets.push_back(std::move(a));
        } else if (type == "B") {
            StudioBus b;
            std::string parent;
            int muted = 0, soloed = 0;
            s >> b.name >> parent >> b.gain >> muted >> soloed;
            b.parent = UnDash(parent);
            b.muted = muted != 0;
            b.soloed = soloed != 0;
            out.buses.push_back(std::move(b));
        } else if (type == "P") {
            StudioParam p;
            s >> p.name >> p.defaultValue >> p.minValue >> p.maxValue >> p.smoothMs;
            out.parameters.push_back(std::move(p));
        } else if (type == "E") {
            StudioEvent e;
            s >> e.name >> e.priority >> e.maxInstances >> e.concurrencyGroup;
            out.events.push_back(std::move(e));
        } else if (type == "L") {
            std::string eventName, bus, param;
            StudioEventLayer l;
            int loop = 0;
            s >> eventName >> l.soundKey >> l.volume >> l.volumeVariance >> l.pitch >>
                l.pitchVariance >> loop >> l.probability >> bus >> param >> l.paramLo >> l.paramHi;
            l.loop = loop != 0;
            l.bus = UnDash(bus);
            l.gainParam = UnDash(param);
            if (StudioEvent* e = FindEvent(out, eventName)) e->layers.push_back(std::move(l));
        } else if (type == "M") {
            StudioMusicState m;
            s >> m.name >> m.bpm >> m.beatsPerBar;
            out.music.push_back(std::move(m));
        } else if (type == "K") {
            std::string musicName, param;
            StudioMusicLayer k;
            s >> musicName >> k.soundKey >> k.gain >> param >> k.paramLo >> k.paramHi;
            k.gainParam = UnDash(param);
            if (StudioMusicState* m = FindMusic(out, musicName)) m->layers.push_back(std::move(k));
        }
        // Unknown line types are ignored (forward-compatible).
    }
    return true;
}

bool SaveProject(const StudioProject& project, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::string text = WriteProjectString(project);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

bool LoadProject(StudioProject& out, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ReadProjectString(out, ss.str());
}

} // namespace reverie::studio
