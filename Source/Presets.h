#pragma once
#include "Common.h"

// Presets tuned from analysing the reference "gun" bass samples (see docs/ANALYSIS.md).
// Any parameter not listed falls back to its default.
struct PresetValue { const char* id; float value; };
struct Preset      { const char* name; std::vector<PresetValue> values; };

inline const std::vector<Preset>& getPresets()
{
    static const std::vector<Preset> presets =
    {
        { "Default", {} },
        { "Gun Burp",    { {"diveAmt",4},  {"diveTime",55},  {"subDrive",30}, {"trans",-10}, {"transDecay",110},
                           {"body",-14}, {"bodyDecay",90},  {"bodyDrive",20}, {"crunch",0.15f},
                           {"spice",-16}, {"spicePos",20}, {"spiceLen",160}, {"hold",450}, {"release",60}, {"high",4} } },
        { "Gun Cracked", { {"diveAmt",10}, {"diveTime",85},  {"subDrive",30}, {"trans",-8},  {"transDecay",90},
                           {"body",-6},  {"bodyDecay",210}, {"bodyDrive",26}, {"crunch",0.45f},
                           {"spice",-12}, {"spicePos",30}, {"spiceLen",180}, {"spiceTone",3200}, {"hold",400}, {"high",5} } },
        { "Gun Heavy",   { {"diveAmt",11}, {"diveTime",170}, {"subDrive",24}, {"trans",-9},  {"transDecay",80},
                           {"body",-8},  {"bodyDecay",150}, {"bodyDrive",22}, {"crunch",0.2f},
                           {"spice",-14}, {"hold",700}, {"release",100}, {"high",3} } },
        { "Gun Low",     { {"diveAmt",7},  {"diveTime",140}, {"subDrive",32}, {"trans",-14}, {"transDecay",60},
                           {"body",-18}, {"bodyDecay",90},  {"spice",-30}, {"hold",600} } },
        { "Newbie Saw",  { {"diveAmt",3},  {"diveTime",40},  {"swell",8},     {"subDrive",3}, {"trans",-16}, {"transDecay",40},
                           {"body",-3},  {"bodyDecay",2000}, {"shape",1}, {"bodyDrive",6}, {"spice",-40},
                           {"hold",2000}, {"release",150}, {"high",2} } },
        { "Resample",    { {"diveAmt",12}, {"diveTime",50},  {"subDrive",28}, {"trans",-12}, {"transDecay",100},
                           {"body",-10}, {"bodyDecay",120}, {"bodyDrive",24}, {"crunch",0.3f},
                           {"spice",-15}, {"hold",300}, {"release",40} } },
    };
    return presets;
}
