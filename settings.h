#pragma once
// Shared between the menu (main.cpp) and the logic (cheat.cpp). Everything is OFF by default.
struct Settings {
    bool esp = false;        // WH master switch
    bool espBox = true;
    bool espLine = true;
    bool skipTeam = true;    // don't draw teammates
    bool aim = false;        // placeholder until the aim setter is confirmed
    float aimFov = 180.f;
    float aimSmooth = 6.f;
};
extern Settings S;
