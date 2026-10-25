// Loader: waits for libil2cpp.so, hooks eglSwapBuffers, draws ImGui + calls OnFrame().
#include <thread>
#include <chrono>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "And64InlineHook.hpp"
#include "settings.h"
#include "touch.h"
#include <float.h>

struct Vec2 { float x, y; };
struct Draw { void (*line)(Vec2, Vec2); void (*box)(Vec2, Vec2); };
void OnFrame(const Draw& d);
void InstallHooks();

static void DLine(Vec2 a, Vec2 b) {
    ImGui::GetBackgroundDrawList()->AddLine({a.x, a.y}, {b.x, b.y}, IM_COL32(255, 60, 60, 255), 1.5f);
}
static void DBox(Vec2 top, Vec2 bot) {
    float h = bot.y - top.y, w = h * 0.45f;
    ImGui::GetBackgroundDrawList()->AddRect({top.x - w / 2, top.y}, {top.x + w / 2, bot.y},
                                            IM_COL32(60, 255, 60, 255), 0, 0, 1.5f);
}


static bool g_open = false;
static void DrawMenu(float W, float H) {
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##toggle", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button(g_open ? " X " : "MENU")) g_open = !g_open;
    ImGui::End();
    if (!g_open) return;

    ImGui::SetNextWindowSize(ImVec2(W * 0.40f, H * 0.75f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, H * 0.12f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Standrise", &g_open);
    ImGui::Checkbox("WH (ESP)", &S.esp);
    ImGui::Indent();
    ImGui::Checkbox("Box", &S.espBox);
    ImGui::Checkbox("Lines", &S.espLine);
    ImGui::Checkbox("Skip team", &S.skipTeam);
    ImGui::Unindent();
    ImGui::Separator();
    ImGui::Checkbox("Aim (not ready)", &S.aim);
    ImGui::SliderFloat("FOV", &S.aimFov, 30.f, 600.f);
    ImGui::SliderFloat("Smooth", &S.aimSmooth, 1.f, 20.f);
    ImGui::End();
}

static void FeedInput(float W, float H) {
    ImGuiIO& io = ImGui::GetIO();
    float x = 0, y = 0;
    bool d = TouchGet(W, H, x, y);
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, d);
}

static EGLBoolean (*orig_swap)(EGLDisplay, EGLSurface) = nullptr;
static bool g_init = false;

static EGLBoolean hook_swap(EGLDisplay dpy, EGLSurface surf) {
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) {
        if (!g_init) {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().FontGlobalScale = h / 540.f;   // readable on phone screens
            ImGui_ImplOpenGL3_Init("#version 300 es");
            g_init = true;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DeltaTime = 1.0f / 60.0f;
        FeedInput((float)w, (float)h);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        Draw d{DLine, DBox};
        OnFrame(d);
        DrawMenu((float)w, (float)h);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    return orig_swap(dpy, surf);
}

static void worker() {
    // wait until the game's il2cpp is loaded
    while (!dlopen("libil2cpp.so", RTLD_NOLOAD))
        std::this_thread::sleep_for(std::chrono::seconds(1));
    std::this_thread::sleep_for(std::chrono::seconds(5));
    InstallHooks();
    TouchStart();
    void* swap = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (swap) A64HookFunction(swap, (void*)hook_swap, (void**)&orig_swap);
    __android_log_print(ANDROID_LOG_INFO, "mod", "eglSwapBuffers hooked: %p", swap);
}

__attribute__((constructor)) static void entry() { std::thread(worker).detach(); }
