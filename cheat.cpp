// Standrise rework - ESP + Aim (arm64, libil2cpp.so)
// Offsets taken from your dump.cs. Names are obfuscated, so after any
// game update re-check every RVA/offset below.
//
// WARNING: using this violates the game's ToS and can get you banned.

#include <cstdint>
#include <cmath>
#include <dlfcn.h>
#include <link.h>
#include <string.h>
#include <vector>
#include "And64InlineHook.hpp"

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

// ---------- il2cpp base ----------
static uintptr_t g_base = 0;

static int phdr_cb(struct dl_phdr_info* i, size_t, void*) {
    if (i->dlpi_name && strstr(i->dlpi_name, "libil2cpp.so")) {
        g_base = (uintptr_t)i->dlpi_addr;
        return 1;
    }
    return 0;
}
static uintptr_t Base() {
    if (!g_base) dl_iterate_phdr(phdr_cb, nullptr);
    return g_base;
}
template <typename T> static T Fn(uintptr_t rva) { return (T)(Base() + rva); }

// ---------- RVAs (dump.cs) ----------
constexpr uintptr_t RVA_Camera_get_main       = 0x2DD22B8;
constexpr uintptr_t RVA_Camera_WorldToScreen  = 0x2DD1F2C; // (Camera*, Vec3)
constexpr uintptr_t RVA_Camera_pixelWidth     = 0x2DD16B0;
constexpr uintptr_t RVA_Camera_pixelHeight    = 0x2DD16EC;
constexpr uintptr_t RVA_Transform_get_pos     = 0x2DDE140;
constexpr uintptr_t RVA_PlayerMainCam_get     = 0x10F7EF4; // static, local camera
constexpr uintptr_t RVA_PlayerMainCam_getCam  = 0x10F81AC; // -> Camera*
constexpr uintptr_t RVA_PlayerMainCam_getPC   = 0x10F81B4; // -> local PlayerController*

// ---------- field offsets (dump.cs) ----------
// PlayerManager (LazySingleton)
constexpr uintptr_t OFF_PM_PlayersDict = 0x20;  // Dictionary<int, PlayerController>
constexpr uintptr_t OFF_PM_PlayersSet  = 0x28;  // HashSet<PlayerController>
// PlayerController
constexpr uintptr_t OFF_PC_Team        = 0x59;  // byte: 1=Tr 2=Ct
constexpr uintptr_t OFF_PC_Aim         = 0x60;  // AimController*
constexpr uintptr_t OFF_PC_Biped       = 0x30;  // _characterBiped (BipedMap*)
// BipedMap
constexpr uintptr_t OFF_BM_Head        = 0x18;
constexpr uintptr_t OFF_BM_Hip         = 0x40 + 0;  // verify in dump.cs (Pelvis field)
constexpr uintptr_t OFF_BM_LFoot       = 0;         // fill from dump.cs if you want a full skeleton
// AimController
constexpr uintptr_t OFF_AIM_CamTransform = 0x80;
constexpr uintptr_t RVA_Aim_SetAngles    = 0x112E234; // public void (Vector3) - verify meaning

typedef void* (*get_main_t)();
typedef Vec3  (*w2s_t)(void*, Vec3);
typedef int   (*get_int_t)(void*);
typedef Vec3  (*get_pos_t)(void*);
typedef void* (*get_ptr_t)(void*);
typedef void* (*get_static_ptr_t)();

template <typename T> static T Rd(uintptr_t addr) { return *(T*)addr; }

// ---------- settings ----------
#include "settings.h"
Settings S;

struct Target { Vec2 head, feet; bool ok; float dist; };

// ---------- capture PlayerManager instance ----------
static void* g_pm = nullptr;
constexpr uintptr_t RVA_PM_ctor   = 0x10F88A4;  // PlayerManager..ctor
constexpr uintptr_t RVA_PM_getSet = 0x10F8AD8;  // PlayerManager.get_CEADFCHEEGGGDHC (instance getter)
static void (*orig_pm_ctor)(void*) = nullptr;
static void* (*orig_pm_get)(void*) = nullptr;
static void hk_pm_ctor(void* self) { g_pm = self; orig_pm_ctor(self); }
static void* hk_pm_get(void* self) { g_pm = self; return orig_pm_get(self); }

void InstallHooks() {
    A64HookFunction((void*)(Base() + RVA_PM_ctor),   (void*)hk_pm_ctor, (void**)&orig_pm_ctor);
    A64HookFunction((void*)(Base() + RVA_PM_getSet), (void*)hk_pm_get,  (void**)&orig_pm_get);
}

// ---------- enumerate players ----------
// Dictionary<int,PlayerController> layout in il2cpp (mono-like):
//   entries array at +0x18, count at +0x20; Entry = {int hash,int next,int key,pad,void* value} (0x18)
static std::vector<void*> GetPlayers() {
    std::vector<void*> out;
    void* pm = g_pm;   // captured by hooks below
    if (!pm) return out;
    uintptr_t dict = Rd<uintptr_t>((uintptr_t)pm + OFF_PM_PlayersDict);
    if (!dict) return out;
    uintptr_t entries = Rd<uintptr_t>(dict + 0x18);
    int count         = Rd<int>(dict + 0x20);
    if (!entries || count <= 0 || count > 64) return out;
    for (int i = 0; i < count; i++) {
        uintptr_t e = entries + 0x20 + i * 0x18;
        void* pc = (void*)Rd<uintptr_t>(e + 0x10);
        if (pc) out.push_back(pc);
    }
    return out;
}

static bool W2S(void* cam, Vec3 w, Vec2& s, int sw, int sh) {
    Vec3 r = Fn<w2s_t>(RVA_Camera_WorldToScreen)(cam, w);
    if (r.z <= 0.f) return false;
    s = { r.x, sh - r.y };
    return s.x > -sw && s.x < sw * 2 && s.y > -sh && s.y < sh * 2;
}

// Called every frame from your render hook (ImGui / eglSwapBuffers).
// 'draw' callbacks are left abstract so you can plug in ImGui's draw list.
struct Draw {
    void (*line)(Vec2, Vec2);
    void (*box)(Vec2 top, Vec2 bottom);
};

void OnFrame(const Draw& d) {
    void* cam = Fn<get_main_t>(RVA_Camera_get_main)();
    if (!cam) return;
    int sw = Fn<get_int_t>(RVA_Camera_pixelWidth)(cam);
    int sh = Fn<get_int_t>(RVA_Camera_pixelHeight)(cam);

    void* localPC = nullptr;
    if (void* pmc = Fn<get_static_ptr_t>(RVA_PlayerMainCam_get)())
        localPC = Fn<get_ptr_t>(RVA_PlayerMainCam_getPC)(pmc);
    uint8_t myTeam = localPC ? Rd<uint8_t>((uintptr_t)localPC + OFF_PC_Team) : 0;

    Vec2 centre = { sw / 2.f, sh / 2.f };
    Target best{}; float bestD = S.aimFov; void* bestPC = nullptr;

    for (void* pc : GetPlayers()) {
        if (pc == localPC) continue;
        uint8_t team = Rd<uint8_t>((uintptr_t)pc + OFF_PC_Team);
        if (S.skipTeam && team == myTeam) continue;

        void* biped = (void*)Rd<uintptr_t>((uintptr_t)pc + OFF_PC_Biped);
        if (!biped) continue;
        void* head = (void*)Rd<uintptr_t>((uintptr_t)biped + OFF_BM_Head);
        if (!head) continue;

        Vec3 hp = Fn<get_pos_t>(RVA_Transform_get_pos)(head);
        Vec3 fp = { hp.x, hp.y - 1.7f, hp.z };
        Vec3 tp = { hp.x, hp.y + 0.25f, hp.z };

        Vec2 sHead, sFeet, sTop;
        if (!W2S(cam, hp, sHead, sw, sh) || !W2S(cam, fp, sFeet, sw, sh) ||
            !W2S(cam, tp, sTop, sw, sh)) continue;

        if (S.esp) {
            if (S.espBox)  d.box(sTop, sFeet);
            if (S.espLine) d.line({ centre.x, 0 }, sFeet);
        }
        float dx = sHead.x - centre.x, dy = sHead.y - centre.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < bestD) { bestD = dist; bestPC = pc; best = { sHead, sFeet, true, dist }; }
    }

    if (S.aim && bestPC && localPC) {
        // Aim: compute yaw/pitch from camera to head and feed them to AimController.
        // The exact setter/fields are obfuscated; start by hooking
        // AimController.BHAHDCEAFECAEAB (0x112E234) and logging its Vector3 to confirm
        // it takes (pitch, yaw, roll) in degrees, then write the smoothed value here.
        // delta = (best.head - centre) / S.aimSmooth  -> convert to degrees via fov.
    }
}
