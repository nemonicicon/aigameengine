/* gfx_xr.inc -- (11-11) A HEADSET, on OpenXR. Included by gfx_vk.c, which owns the Vulkan device a headset's runtime has to share; gfx.h has the calls.
 * 11-11, for an HP Reverb G2.
 *
 * HOW IT SITS IN THE RENDERER. Nothing of the frame's passes changed. REVA_XR=1 at start-up, and:
 *   - before the Vulkan instance is made, the OpenXR loader is found (openxr_loader.dll or libopenxr_loader.dll, opened by name, so the engine needs nothing of
 *     OpenXR to start), an OpenXR instance is made and the system asked for: a headset that's plugged in and on. The runtime says which instance and device
 *     extensions it needs (XR_KHR_vulkan_enable), and which GPU the headset is on, and the renderer makes its device so;
 *   - the picture is the two eyes side by side, each at the runtime's own size for it (REVA_XRSCALE takes a share of that: 0.8 unless it's said), so the engine
 *     draws them as two views of one frame, as it draws a split screen (engine/main.inc), and every pass after (bloom, the grade, the tonemap) is the same as ever;
 *   - at the frame's end each eye's half of the finished picture is copied into that eye's image of the runtime's (a swapchain each), and the frame is handed over.
 *     The window shows both eyes, for whoever is watching.
 * The runtime is whatever the PC has set: Windows Mixed Reality's own (Windows 10, or Windows 11 before 24H2), or SteamVR (with the Oasis driver for an HP Reverb
 * G2 on Windows 11 24H2 and later, where Mixed Reality was taken out). Seated: the runtime's LOCAL space, whose origin is where the head was.
 * THE CONTROLLERS: one action set, bound for the HP Reverb G2's own controllers (XR_EXT_hp_mixed_reality_controller) and for Mixed Reality's, Touch, Index and
 * the simple profile, so the runtime has a binding for whatever's in the player's hands.
 * ⛔ UNCHECKED ON A HEADSET (11-11): built and run here with no runtime (REVA_XR=fake draws the two eyes). No headset was here: a G2 is the first
 * real test. A runtime's own complaint is printed as [xr], with its name for the result. */
#if defined(REVA_SDL) && !defined(__ANDROID__) && defined(__has_include)
#if __has_include(<openxr/openxr.h>)
#define GFX_XR 1
#endif
#endif

#ifdef GFX_XR
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static float g_xrScale; static int g_xrRecW, g_xrRecH; static int g_xrWant, g_xrFake, g_xrOk, g_xrRun, g_xrFocus, g_xrInFrame, g_xrDrawn, g_xrQuitAsk, g_xrEyeW, g_xrEyeH, g_xrHP, g_xrRGBA;
static XrInstance g_xrI = XR_NULL_HANDLE; static XrSystemId g_xrSys; static XrSession g_xrS = XR_NULL_HANDLE; static XrSpace g_xrSpace = XR_NULL_HANDLE;
static XrSwapchain g_xrSc[2]; static VkImage g_xrImg[2][8]; static uint32_t g_xrNImg[2], g_xrIdx[2]; static int64_t g_xrFmt;
static XrTime g_xrT; static XrView g_xrView[2]; static VkImage g_xrStage; static VkImageView g_xrStageV;
static XrActionSet g_xrSet; static XrAction g_aTrig, g_aSqz, g_aStick, g_aLow, g_aUp, g_aClick, g_aMenu, g_aGrip, g_aAim, g_aBuzz; static XrPath g_xrHandP[2]; static XrSpace g_xrGripS[2], g_xrAimS[2];
static char g_xrInstExt[2048], g_xrDevExt[2048];
static PFN_xrGetInstanceProcAddr p_xrGetInstanceProcAddr;
#define XR_FNS(X) X(xrEnumerateInstanceExtensionProperties) X(xrCreateInstance) X(xrDestroyInstance) X(xrGetSystem) X(xrGetSystemProperties) X(xrResultToString) \
    X(xrEnumerateViewConfigurationViews) X(xrGetVulkanInstanceExtensionsKHR) X(xrGetVulkanDeviceExtensionsKHR) X(xrGetVulkanGraphicsDeviceKHR) X(xrGetVulkanGraphicsRequirementsKHR) \
    X(xrCreateSession) X(xrCreateReferenceSpace) X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) \
    X(xrReleaseSwapchainImage) X(xrPollEvent) X(xrBeginSession) X(xrEndSession) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews) X(xrStringToPath) X(xrCreateActionSet) \
    X(xrCreateAction) X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrSyncActions) X(xrGetActionStateBoolean) X(xrGetActionStateFloat) X(xrGetActionStateVector2f) \
    X(xrCreateActionSpace) X(xrLocateSpace) X(xrApplyHapticFeedback) X(xrRequestExitSession)
#define XR_DECL(n) static PFN_##n p_##n;
XR_FNS(XR_DECL)

static void xrSay(const char *what, XrResult r) {
    char s[XR_MAX_RESULT_STRING_SIZE] = ""; if (p_xrResultToString && g_xrI) p_xrResultToString(g_xrI, r, s); else snprintf(s, sizeof s, "%d", (int)r);
    printf("[xr] %s: %s\n", what, s); fflush(stdout);
}
#define XRK(x, what) do { XrResult r_ = (x); if (XR_FAILED(r_)) { xrSay(what, r_); return 0; } } while (0)
static int xrLoad(XrInstance in) {
    int ok = 1;
#define XR_GET(n) if (XR_FAILED(p_xrGetInstanceProcAddr(in, #n, (PFN_xrVoidFunction *)&p_##n))) { if (in != XR_NULL_HANDLE) { printf("[xr] the runtime hasn't %s\n", #n); ok = 0; } }
    XR_FNS(XR_GET)
    return ok;
}

/* before the Vulkan instance: the loader, the instance, the headset, and what the runtime wants of Vulkan. 0: no headset, and the game is on the screen as ever */
static int xrPrepare(void) {
    const char *e = getenv("REVA_XR"); g_xrWant = e && e[0] && strcmp(e, "0");
    if (!g_xrWant) return 0;
    /* how big each eye is drawn: an eye's width in pixels (REVA_XREYE, or save/xr.txt's `eye`), or a share of what the runtime asks (REVA_XRSCALE, or `scale`), as a game's
     * options kept it (E->xrScale). With neither, 2160 wide: an HP Reverb G2's own panels (the target is 90 fps at the headset's native resolution), or what the runtime
     * asks if that's less. The height keeps the runtime's shape */
    float sc = -1.f; int eyeWant = 0; { FILE *f = fopen("save/xr.txt", "r"); char k[16]; float v; if (f) { while (fscanf(f, "%15s %f", k, &v) == 2) { if (!strcmp(k, "scale")) { sc = v; eyeWant = 0; } else if (!strcmp(k, "eye")) { eyeWant = (int)v; sc = -1.f; } } fclose(f); } }
    if (getenv("REVA_XRSCALE")) { sc = (float)atof(getenv("REVA_XRSCALE")); eyeWant = 0; }
    if (getenv("REVA_XREYE")) { eyeWant = atoi(getenv("REVA_XREYE")); sc = -1.f; }
    if (sc >= 0.f && !(sc >= 0.3f && sc <= 1.5f)) sc = -1.f;
    if (eyeWant && (eyeWant < 256 || eyeWant > 8192)) eyeWant = 0;
    if (!strcmp(e, "fake")) {   /* a stand-in for a desk: two eyes of a G2's shape, side by side in the window, and no runtime */
        g_xrFake = 1; g_xrOk = 1; g_xrRun = g_xrFocus = 1; g_xrRecW = g_xrRecH = 2160;   /* it asks for a G2's own, and draws 1080 unless it's told otherwise: a desk's check */
        g_xrEyeW = (eyeWant ? eyeWant : sc > 0.f ? (int)(2160.f * sc) : 1080) & ~1; g_xrEyeH = g_xrEyeW; g_xrScale = (float)g_xrEyeW / 2160.f;
        printf("[xr] a stand-in headset (REVA_XR=fake): two eyes of %d x %d side by side, no runtime\n", g_xrEyeW, g_xrEyeH); fflush(stdout); return 1; }
    static const char *const LIBS[] = {"openxr_loader.dll", "libopenxr_loader.dll", "libopenxr_loader.so.1", "libopenxr_loader.so"};
    void *lib = NULL; for (unsigned i = 0; i < sizeof LIBS / sizeof LIBS[0] && !lib; i++) lib = SDL_LoadObject(LIBS[i]);
    if (!lib) { printf("[xr] no OpenXR loader (openxr_loader.dll beside the game): playing on the screen\n"); fflush(stdout); return 0; }
    p_xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)SDL_LoadFunction(lib, "xrGetInstanceProcAddr");
    if (!p_xrGetInstanceProcAddr) { printf("[xr] the loader has no xrGetInstanceProcAddr\n"); fflush(stdout); return 0; }
    xrLoad(XR_NULL_HANDLE);
    uint32_t n = 0; if (!p_xrEnumerateInstanceExtensionProperties || XR_FAILED(p_xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL))) { printf("[xr] no OpenXR runtime is set on this PC: playing on the screen\n"); fflush(stdout); return 0; }
    XrExtensionProperties *xp = calloc(n ? n : 1, sizeof *xp); for (uint32_t i = 0; i < n; i++) xp[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    p_xrEnumerateInstanceExtensionProperties(NULL, n, &n, xp);
    int vk = 0; for (uint32_t i = 0; i < n; i++) { if (!strcmp(xp[i].extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)) vk = 1; if (!strcmp(xp[i].extensionName, "XR_EXT_hp_mixed_reality_controller")) g_xrHP = 1; }
    free(xp);
    if (!vk) { printf("[xr] this PC's OpenXR runtime doesn't draw with Vulkan (XR_KHR_vulkan_enable), and Windows Mixed Reality's own doesn't: set SteamVR as the OpenXR runtime (SteamVR, Settings, OpenXR). Playing on the screen\n"); fflush(stdout); return 0; }
    const char *ext[2] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, "XR_EXT_hp_mixed_reality_controller"};
    XrInstanceCreateInfo ci = {.type = XR_TYPE_INSTANCE_CREATE_INFO}; snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "fleetwarfare");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "aigameengine"); ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = g_xrHP ? 2 : 1; ci.enabledExtensionNames = ext;
    XRK(p_xrCreateInstance(&ci, &g_xrI), "making the OpenXR instance");
    if (!xrLoad(g_xrI)) return 0;
    XrSystemGetInfo sg = {.type = XR_TYPE_SYSTEM_GET_INFO}; sg.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    { XrResult r = p_xrGetSystem(g_xrI, &sg, &g_xrSys); if (XR_FAILED(r)) { xrSay("no headset (is it plugged in, and its runtime running?): playing on the screen", r); p_xrDestroyInstance(g_xrI); g_xrI = XR_NULL_HANDLE; return 0; } }
    XrSystemProperties sp = {.type = XR_TYPE_SYSTEM_PROPERTIES}; p_xrGetSystemProperties(g_xrI, g_xrSys, &sp);
    XrViewConfigurationView vv[2] = {{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW}, {.type = XR_TYPE_VIEW_CONFIGURATION_VIEW}}; uint32_t nv = 0;
    XRK(p_xrEnumerateViewConfigurationViews(g_xrI, g_xrSys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, vv), "the headset's eyes");
    g_xrRecW = (int)vv[0].recommendedImageRectWidth; g_xrRecH = (int)vv[0].recommendedImageRectHeight;
    { const float shape = (float)g_xrRecH / (float)(g_xrRecW > 0 ? g_xrRecW : 1);
      const int w = eyeWant ? eyeWant : sc > 0.f ? (int)((float)g_xrRecW * sc) : (g_xrRecW < 2160 ? g_xrRecW : 2160);
      g_xrEyeW = w & ~1; g_xrEyeH = ((int)((float)w * shape + 0.5f)) & ~1; }
    if (g_xrEyeW > (int)vv[0].maxImageRectWidth) g_xrEyeW = (int)vv[0].maxImageRectWidth & ~1;
    if (g_xrEyeH > (int)vv[0].maxImageRectHeight) g_xrEyeH = (int)vv[0].maxImageRectHeight & ~1;
    if (g_xrEyeW < 64 || g_xrEyeH < 64) { printf("[xr] the headset's eyes are %d x %d: too small\n", g_xrEyeW, g_xrEyeH); fflush(stdout); return 0; }
    uint32_t len = 0; XRK(p_xrGetVulkanInstanceExtensionsKHR(g_xrI, g_xrSys, sizeof g_xrInstExt, &len, g_xrInstExt), "the Vulkan instance extensions it needs");
    XRK(p_xrGetVulkanDeviceExtensionsKHR(g_xrI, g_xrSys, sizeof g_xrDevExt, &len, g_xrDevExt), "the Vulkan device extensions it needs");
    g_xrScale = (float)g_xrEyeW / (float)(g_xrRecW > 0 ? g_xrRecW : 1);
    printf("[xr] %s: the runtime asks for %u x %u an eye; drawn at %d x %d%s\n", sp.systemName, vv[0].recommendedImageRectWidth, vv[0].recommendedImageRectHeight,
           g_xrEyeW, g_xrEyeH, g_xrHP ? "; the HP Reverb G2's controllers are named" : ""); fflush(stdout);
    g_xrOk = 1; return 1;
}
/* the runtime's extensions, added to what the renderer asks for: a list of names in one string, split in place */
static void xrSplit(char *s, const char **out, uint32_t *n, uint32_t max) {
    for (char *t = strtok(s, " "); t && *n < max; t = strtok(NULL, " ")) { int have = 0; for (uint32_t i = 0; i < *n; i++) if (!strcmp(out[i], t)) have = 1; if (!have) out[(*n)++] = t; }
}
static void xrInstanceExt(const char **ex, uint32_t *n, uint32_t max) { if (g_xrOk && !g_xrFake) xrSplit(g_xrInstExt, ex, n, max); }
static void xrDeviceExt(const char **ex, uint32_t *n, uint32_t max) { if (g_xrOk && !g_xrFake) xrSplit(g_xrDevExt, ex, n, max); }
static VkPhysicalDevice xrPhysical(VkInstance inst) {
    if (!g_xrOk || g_xrFake) return VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE; XrResult r = p_xrGetVulkanGraphicsDeviceKHR(g_xrI, g_xrSys, inst, &pd); if (XR_FAILED(r)) { xrSay("which GPU the headset is on", r); return VK_NULL_HANDLE; }
    XrGraphicsRequirementsVulkanKHR gr = {.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR}; p_xrGetVulkanGraphicsRequirementsKHR(g_xrI, g_xrSys, &gr);   /* asked before the session, as OpenXR has it */
    return pd;
}
static XrPath xrPath(const char *s) { XrPath p = XR_NULL_PATH; p_xrStringToPath(g_xrI, s, &p); return p; }
static XrAction xrAction(const char *name, const char *said, XrActionType t) {
    XrActionCreateInfo ai = {.type = XR_TYPE_ACTION_CREATE_INFO}; snprintf(ai.actionName, sizeof ai.actionName, "%s", name); snprintf(ai.localizedActionName, sizeof ai.localizedActionName, "%s", said);
    ai.actionType = t; ai.countSubactionPaths = 2; ai.subactionPaths = g_xrHandP; XrAction a = XR_NULL_HANDLE; XrResult r = p_xrCreateAction(g_xrSet, &ai, &a); if (XR_FAILED(r)) xrSay(name, r); return a;
}
/* a controller's binding: each line an action and the path on both hands it's on ("" for a hand that hasn't one) */
typedef struct { XrAction *a; const char *left, *right; } XrBind;
static void xrSuggest(const char *profile, const XrBind *b, int n) {
    XrActionSuggestedBinding s[32]; uint32_t k = 0; char p[160];
    for (int i = 0; i < n && k < 30; i++) for (int h = 0; h < 2; h++) { const char *w = h ? b[i].right : b[i].left; if (!w || !w[0] || !*b[i].a) continue;
        snprintf(p, sizeof p, "/user/hand/%s/%s", h ? "right" : "left", w); s[k].action = *b[i].a; s[k].binding = xrPath(p); k++; }
    XrInteractionProfileSuggestedBinding sb = {.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING}; sb.interactionProfile = xrPath(profile); sb.countSuggestedBindings = k; sb.suggestedBindings = s;
    XrResult r = p_xrSuggestInteractionProfileBindings(g_xrI, &sb); if (XR_FAILED(r)) { char t[200]; snprintf(t, sizeof t, "the controllers' binding %s (another of the bindings will serve)", profile); xrSay(t, r); }
}
static int xrActions(void) {
    g_xrHandP[0] = xrPath("/user/hand/left"); g_xrHandP[1] = xrPath("/user/hand/right");
    XrActionSetCreateInfo si = {.type = XR_TYPE_ACTION_SET_CREATE_INFO}; snprintf(si.actionSetName, sizeof si.actionSetName, "play"); snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "Play");
    XRK(p_xrCreateActionSet(g_xrI, &si, &g_xrSet), "the controllers' actions");
    g_aTrig = xrAction("trigger", "Trigger: fire, or take hold", XR_ACTION_TYPE_FLOAT_INPUT); g_aSqz = xrAction("grip", "Grip: take hold", XR_ACTION_TYPE_FLOAT_INPUT);
    g_aStick = xrAction("stick", "Stick: walk, or turn", XR_ACTION_TYPE_VECTOR2F_INPUT); g_aLow = xrAction("use", "A or X: use", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aUp = xrAction("other", "B or Y: reload, or crouch", XR_ACTION_TYPE_BOOLEAN_INPUT); g_aClick = xrAction("stick_click", "Stick pressed: recentre", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aMenu = xrAction("menu", "Menu: recentre", XR_ACTION_TYPE_BOOLEAN_INPUT); g_aGrip = xrAction("hand_pose", "Hand", XR_ACTION_TYPE_POSE_INPUT);
    g_aAim = xrAction("aim_pose", "Aim", XR_ACTION_TYPE_POSE_INPUT); g_aBuzz = xrAction("buzz", "Buzz", XR_ACTION_TYPE_VIBRATION_OUTPUT);
    static const char *P_GRIP = "input/grip/pose", *P_AIM = "input/aim/pose", *P_BUZZ = "output/haptic";
    if (g_xrHP) { const XrBind b[] = {{&g_aTrig, "input/trigger/value", "input/trigger/value"}, {&g_aSqz, "input/squeeze/value", "input/squeeze/value"}, {&g_aStick, "input/thumbstick", "input/thumbstick"},
                                      {&g_aLow, "input/x/click", "input/a/click"}, {&g_aUp, "input/y/click", "input/b/click"}, {&g_aMenu, "input/menu/click", "input/menu/click"},
                                      {&g_aGrip, P_GRIP, P_GRIP}, {&g_aAim, P_AIM, P_AIM}, {&g_aBuzz, P_BUZZ, P_BUZZ}};
                  xrSuggest("/interaction_profiles/hp/mixed_reality_controller", b, sizeof b / sizeof b[0]); }
    { const XrBind b[] = {{&g_aTrig, "input/trigger/value", "input/trigger/value"}, {&g_aSqz, "input/squeeze/click", "input/squeeze/click"}, {&g_aStick, "input/thumbstick", "input/thumbstick"},
                          {&g_aLow, "input/trackpad/click", "input/trackpad/click"}, {&g_aClick, "input/thumbstick/click", "input/thumbstick/click"}, {&g_aMenu, "input/menu/click", "input/menu/click"},
                          {&g_aGrip, P_GRIP, P_GRIP}, {&g_aAim, P_AIM, P_AIM}, {&g_aBuzz, P_BUZZ, P_BUZZ}};
      xrSuggest("/interaction_profiles/microsoft/motion_controller", b, sizeof b / sizeof b[0]); }
    { const XrBind b[] = {{&g_aTrig, "input/trigger/value", "input/trigger/value"}, {&g_aSqz, "input/squeeze/value", "input/squeeze/value"}, {&g_aStick, "input/thumbstick", "input/thumbstick"},
                          {&g_aLow, "input/x/click", "input/a/click"}, {&g_aUp, "input/y/click", "input/b/click"}, {&g_aClick, "input/thumbstick/click", "input/thumbstick/click"},
                          {&g_aMenu, "input/menu/click", ""}, {&g_aGrip, P_GRIP, P_GRIP}, {&g_aAim, P_AIM, P_AIM}, {&g_aBuzz, P_BUZZ, P_BUZZ}};
      xrSuggest("/interaction_profiles/oculus/touch_controller", b, sizeof b / sizeof b[0]); }
    { const XrBind b[] = {{&g_aTrig, "input/trigger/value", "input/trigger/value"}, {&g_aSqz, "input/squeeze/value", "input/squeeze/value"}, {&g_aStick, "input/thumbstick", "input/thumbstick"},
                          {&g_aLow, "input/a/click", "input/a/click"}, {&g_aUp, "input/b/click", "input/b/click"}, {&g_aClick, "input/thumbstick/click", "input/thumbstick/click"},
                          {&g_aGrip, P_GRIP, P_GRIP}, {&g_aAim, P_AIM, P_AIM}, {&g_aBuzz, P_BUZZ, P_BUZZ}};
      xrSuggest("/interaction_profiles/valve/index_controller", b, sizeof b / sizeof b[0]); }
    { const XrBind b[] = {{&g_aTrig, "input/select/click", "input/select/click"}, {&g_aMenu, "input/menu/click", "input/menu/click"}, {&g_aGrip, P_GRIP, P_GRIP}, {&g_aAim, P_AIM, P_AIM}, {&g_aBuzz, P_BUZZ, P_BUZZ}};
      xrSuggest("/interaction_profiles/khr/simple_controller", b, sizeof b / sizeof b[0]); }
    XrSessionActionSetsAttachInfo at = {.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO}; at.countActionSets = 1; at.actionSets = &g_xrSet;
    XRK(p_xrAttachSessionActionSets(g_xrS, &at), "attaching the controllers' actions");
    for (int h = 0; h < 2; h++) for (int k = 0; k < 2; k++) {
        XrActionSpaceCreateInfo sc = {.type = XR_TYPE_ACTION_SPACE_CREATE_INFO}; sc.action = k ? g_aAim : g_aGrip; sc.subactionPath = g_xrHandP[h]; sc.poseInActionSpace.orientation.w = 1.f;
        XRK(p_xrCreateActionSpace(g_xrS, &sc, k ? &g_xrAimS[h] : &g_xrGripS[h]), "a controller's place"); }
    return 1;
}
/* after the device: the session, the seated space, an image chain for each eye, and the controllers */
static int xrSession(void) {
    if (!g_xrOk || g_xrFake) return g_xrFake;
    XrGraphicsBindingVulkanKHR gb = {.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR}; gb.instance = g_inst; gb.physicalDevice = g_gpu; gb.device = g_dev; gb.queueFamilyIndex = g_qf; gb.queueIndex = 0;
    XrSessionCreateInfo sc = {.type = XR_TYPE_SESSION_CREATE_INFO}; sc.next = &gb; sc.systemId = g_xrSys;
    XRK(p_xrCreateSession(g_xrI, &sc, &g_xrS), "making the headset's session");
    XrReferenceSpaceCreateInfo rs = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; rs.poseInReferenceSpace.orientation.w = 1.f;
    XRK(p_xrCreateReferenceSpace(g_xrS, &rs, &g_xrSpace), "the seated space");
    /* the eyes' format: the picture's own order of channels in sRGB, so its bytes go across as they are (the picture already holds sRGB's values). Red first if that's all there is,
     * through an image of the picture's size in that order */
    uint32_t nf = 0; p_xrEnumerateSwapchainFormats(g_xrS, 0, &nf, NULL); int64_t *ff = calloc(nf ? nf : 1, sizeof *ff); p_xrEnumerateSwapchainFormats(g_xrS, nf, &nf, ff);
    g_xrFmt = 0; for (uint32_t i = 0; i < nf && !g_xrFmt; i++) if (ff[i] == (COLOR_FMT == VK_FORMAT_B8G8R8A8_UNORM ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_R8G8B8A8_SRGB)) g_xrFmt = ff[i];
    for (uint32_t i = 0; i < nf && !g_xrFmt; i++) if (ff[i] == VK_FORMAT_R8G8B8A8_SRGB || ff[i] == VK_FORMAT_B8G8R8A8_SRGB) g_xrFmt = ff[i];
    free(ff); if (!g_xrFmt) { printf("[xr] the runtime offers no 8-bit sRGB format for the eyes\n"); fflush(stdout); return 0; }
    g_xrRGBA = (g_xrFmt == VK_FORMAT_R8G8B8A8_SRGB) != (COLOR_FMT == VK_FORMAT_R8G8B8A8_UNORM);
    for (int e = 0; e < 2; e++) {
        XrSwapchainCreateInfo ci = {.type = XR_TYPE_SWAPCHAIN_CREATE_INFO}; ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT; ci.format = g_xrFmt;
        ci.sampleCount = 1; ci.width = (uint32_t)g_xrEyeW; ci.height = (uint32_t)g_xrEyeH; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
        XRK(p_xrCreateSwapchain(g_xrS, &ci, &g_xrSc[e]), "an eye's images");
        XrSwapchainImageVulkanKHR im[8]; for (int i = 0; i < 8; i++) { memset(&im[i], 0, sizeof im[i]); im[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR; }
        uint32_t n = 0; p_xrEnumerateSwapchainImages(g_xrSc[e], 8, &n, (XrSwapchainImageBaseHeader *)im); g_xrNImg[e] = n; for (uint32_t i = 0; i < n; i++) g_xrImg[e][i] = im[i].image; }
    if (g_xrRGBA) g_xrStage = imgMake(COLOR_FMT == VK_FORMAT_B8G8R8A8_UNORM ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM, (uint32_t)g_xrEyeW, (uint32_t)g_xrEyeH, 1,
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_xrStageV);
    if (!xrActions()) return 0;
    printf("[xr] the session is made: the eyes in format %d, %u and %u images; put the headset on\n", (int)g_xrFmt, g_xrNImg[0], g_xrNImg[1]); fflush(stdout);
    return 1;
}
static void xrEvents(void) {
    XrEventDataBuffer ev = {.type = XR_TYPE_EVENT_DATA_BUFFER};
    while (p_xrPollEvent(g_xrI, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrSessionState st = ((XrEventDataSessionStateChanged *)&ev)->state;
            if (st == XR_SESSION_STATE_READY) { XrSessionBeginInfo bi = {.type = XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                XrResult r = p_xrBeginSession(g_xrS, &bi); if (XR_FAILED(r)) xrSay("beginning the session", r); else { g_xrRun = 1; printf("[xr] in the headset\n"); fflush(stdout); } }
            else if (st == XR_SESSION_STATE_STOPPING) { p_xrEndSession(g_xrS); g_xrRun = 0; printf("[xr] out of the headset\n"); fflush(stdout); }
            else if (st == XR_SESSION_STATE_EXITING || st == XR_SESSION_STATE_LOSS_PENDING) g_xrQuitAsk = 1;
            g_xrFocus = st == XR_SESSION_STATE_FOCUSED;
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) g_xrQuitAsk = 1;
        ev.type = XR_TYPE_EVENT_DATA_BUFFER; ev.next = NULL;
    }
}
static void xrPose(const XrPosef *p, float pos[3], float rot[4]) { pos[0] = p->position.x; pos[1] = p->position.y; pos[2] = p->position.z; rot[0] = p->orientation.x; rot[1] = p->orientation.y; rot[2] = p->orientation.z; rot[3] = p->orientation.w; }
static void xrHands(GfxXrHand h[2]) {
    memset(h, 0, sizeof(GfxXrHand) * 2);
    if (g_xrFocus) { XrActiveActionSet as = {.actionSet = g_xrSet, .subactionPath = XR_NULL_PATH}; XrActionsSyncInfo si = {.type = XR_TYPE_ACTIONS_SYNC_INFO}; si.countActiveActionSets = 1; si.activeActionSets = &as; p_xrSyncActions(g_xrS, &si); }
    for (int k = 0; k < 2; k++) {
        XrActionStateGetInfo gi = {.type = XR_TYPE_ACTION_STATE_GET_INFO}; gi.subactionPath = g_xrHandP[k];
        XrActionStateFloat fs = {.type = XR_TYPE_ACTION_STATE_FLOAT}; XrActionStateBoolean bs = {.type = XR_TYPE_ACTION_STATE_BOOLEAN}; XrActionStateVector2f vs = {.type = XR_TYPE_ACTION_STATE_VECTOR2F};
        gi.action = g_aTrig; if (XR_SUCCEEDED(p_xrGetActionStateFloat(g_xrS, &gi, &fs)) && fs.isActive) h[k].trigger = fs.currentState;
        fs.type = XR_TYPE_ACTION_STATE_FLOAT; gi.action = g_aSqz; if (XR_SUCCEEDED(p_xrGetActionStateFloat(g_xrS, &gi, &fs)) && fs.isActive) h[k].squeeze = fs.currentState;
        gi.action = g_aStick; if (XR_SUCCEEDED(p_xrGetActionStateVector2f(g_xrS, &gi, &vs)) && vs.isActive) { h[k].stick[0] = vs.currentState.x; h[k].stick[1] = vs.currentState.y; }
        const XrAction B[4] = {g_aLow, g_aUp, g_aClick, g_aMenu};
        for (int b = 0; b < 4; b++) { bs.type = XR_TYPE_ACTION_STATE_BOOLEAN; gi.action = B[b]; if (B[b] && XR_SUCCEEDED(p_xrGetActionStateBoolean(g_xrS, &gi, &bs)) && bs.isActive && bs.currentState) h[k].buttons |= 1u << b; }
        XrSpaceLocation l = {.type = XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(p_xrLocateSpace(g_xrGripS[k], g_xrSpace, g_xrT, &l)) && (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) { h[k].on = 1; xrPose(&l.pose, h[k].grip, h[k].gripRot); }
        l.type = XR_TYPE_SPACE_LOCATION; l.next = NULL;
        if (XR_SUCCEEDED(p_xrLocateSpace(g_xrAimS[k], g_xrSpace, g_xrT, &l)) && (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) xrPose(&l.pose, h[k].aim, h[k].aimRot);
        else { memcpy(h[k].aim, h[k].grip, 12); memcpy(h[k].aimRot, h[k].gripRot, 16); }
    }
}
/* the stand-in: a head looking a little about and a little down, and hands held out in front of it, the right one drifting, as a desk test wants */
static void xrFakeFrame(GfxXrEye e[2], GfxXrHand h[2]) {
    const float t = (float)SDL_GetTicks() / 1000.f, yaw = 0.25f * sinf(t * 0.4f), pitch = -0.12f, cy = cosf(yaw * 0.5f), sy = sinf(yaw * 0.5f), cp = cosf(pitch * 0.5f), sp = sinf(pitch * 0.5f);
    const float q[4] = {cy * sp, sy * cp, -sy * sp, cy * cp};   /* yaw, then pitch */
    for (int k = 0; k < 2; k++) { const float s = k ? 1.f : -1.f; e[k].pos[0] = s * 0.032f * cosf(yaw); e[k].pos[1] = 0.f; e[k].pos[2] = s * 0.032f * sinf(yaw); memcpy(e[k].rot, q, 16);
        e[k].fov[0] = k ? -0.78f : -0.92f; e[k].fov[1] = k ? 0.92f : 0.78f; e[k].fov[2] = 0.86f; e[k].fov[3] = -0.92f; }
    memset(h, 0, sizeof(GfxXrHand) * 2);
    for (int k = 0; k < 2; k++) { h[k].on = 1; h[k].grip[0] = (k ? 0.18f : -0.18f) + (k ? 0.08f * sinf(t * 0.7f) : 0.f); h[k].grip[1] = -0.38f; h[k].grip[2] = -0.42f + (k ? 0.06f * cosf(t * 0.9f) : 0.f);
        h[k].gripRot[0] = -0.26f; h[k].gripRot[3] = 0.966f; memcpy(h[k].aim, h[k].grip, 12); memcpy(h[k].aimRot, h[k].gripRot, 16); }
}

static void xrOff(void) { g_xrOk = 0; }
int gfxXrOn(void) { return g_xrOk; }
void gfxXrEyeSize(int *w, int *h) { *w = g_xrOk ? g_xrEyeW : 0; *h = g_xrOk ? g_xrEyeH : 0; }
int gfxXrQuit(void) { return g_xrQuitAsk; }
float gfxXrScale(void) { return g_xrOk ? g_xrScale : 0.f; }
void gfxXrBuzz(int hand, float amount, float seconds) {
    if (!g_xrOk || g_xrFake || !g_xrS || !g_xrFocus || hand < 0 || hand > 1) return;
    XrHapticVibration v = {.type = XR_TYPE_HAPTIC_VIBRATION}; v.amplitude = amount < 0.f ? 0.f : amount > 1.f ? 1.f : amount; v.duration = (XrDuration)(seconds * 1e9f); v.frequency = XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo hi = {.type = XR_TYPE_HAPTIC_ACTION_INFO}; hi.action = g_aBuzz; hi.subactionPath = g_xrHandP[hand]; p_xrApplyHapticFeedback(g_xrS, &hi, (const XrHapticBaseHeader *)&v);
}
static void xrEnd(void);
int gfxXrFrame(GfxXrEye eye[2], GfxXrHand hand[2]) {
    if (!g_xrOk) return 0;
    if (g_xrFake) { xrFakeFrame(eye, hand); g_xrDrawn = 1; return 1; }
    if (g_xrInFrame) xrEnd();   /* a frame begun and never drawn: handed back empty */
    xrEvents(); g_xrDrawn = 0;
    if (!g_xrRun) { memset(hand, 0, sizeof(GfxXrHand) * 2); return 0; }
    XrFrameState fs = {.type = XR_TYPE_FRAME_STATE}; XrFrameWaitInfo wi = {.type = XR_TYPE_FRAME_WAIT_INFO};
    { XrResult r = p_xrWaitFrame(g_xrS, &wi, &fs); if (XR_FAILED(r)) { xrSay("waiting for the headset's frame", r); return 0; } }
    XrFrameBeginInfo bi = {.type = XR_TYPE_FRAME_BEGIN_INFO}; { XrResult r = p_xrBeginFrame(g_xrS, &bi); if (XR_FAILED(r)) { xrSay("beginning a frame", r); return 0; } }
    g_xrInFrame = 1; g_xrT = fs.predictedDisplayTime;
    xrHands(hand);
    if (!fs.shouldRender) return 0;
    XrViewLocateInfo li = {.type = XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime = g_xrT; li.space = g_xrSpace;
    XrViewState vs = {.type = XR_TYPE_VIEW_STATE}; g_xrView[0].type = g_xrView[1].type = XR_TYPE_VIEW; g_xrView[0].next = g_xrView[1].next = NULL; uint32_t n = 0;
    if (XR_FAILED(p_xrLocateViews(g_xrS, &li, &vs, 2, &n, g_xrView)) || n != 2 || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 0;
    for (int k = 0; k < 2; k++) { xrPose(&g_xrView[k].pose, eye[k].pos, eye[k].rot); eye[k].fov[0] = g_xrView[k].fov.angleLeft; eye[k].fov[1] = g_xrView[k].fov.angleRight; eye[k].fov[2] = g_xrView[k].fov.angleUp; eye[k].fov[3] = g_xrView[k].fov.angleDown; }
    g_xrDrawn = 1; return 1;
}
/* at the frame's end, with the picture finished: each eye's half into that eye's image. The picture is left as presentPass left it */
static void xrCopyEyes(VkCommandBuffer cb, VkImage pic) {
    if (!g_xrOk || g_xrFake || !g_xrInFrame || !g_xrDrawn) return;
    for (int e = 0; e < 2; e++) { XrSwapchainImageAcquireInfo ai = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO}; XrSwapchainImageWaitInfo wi = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = XR_INFINITE_DURATION;
        if (XR_FAILED(p_xrAcquireSwapchainImage(g_xrSc[e], &ai, &g_xrIdx[e])) || XR_FAILED(p_xrWaitSwapchainImage(g_xrSc[e], &wi))) { g_xrDrawn = 0; printf("[xr] an eye's image wasn't given\n"); fflush(stdout); return; } }
    barrierImg(cb, pic, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1);
    for (int e = 0; e < 2; e++) {
        VkImage dst = g_xrImg[e][g_xrIdx[e]], src = pic; int sx = e * g_xrEyeW;
        if (g_xrRGBA) {   /* the other order of channels: a blit puts them right, into an image the eye's size, and that goes across */
            barrierImg(cb, g_xrStage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
            VkImageBlit b = {0}; b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
            b.srcOffsets[0].x = sx; b.srcOffsets[1].x = sx + g_xrEyeW; b.srcOffsets[1].y = g_xrEyeH; b.srcOffsets[1].z = 1; b.dstOffsets[1].x = g_xrEyeW; b.dstOffsets[1].y = g_xrEyeH; b.dstOffsets[1].z = 1;
            vkCmdBlitImage(cb, pic, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_xrStage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
            barrierImg(cb, g_xrStage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1);
            src = g_xrStage; sx = 0; }
        barrierImg(cb, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
        VkImageCopy c = {0}; c.srcSubresource.aspectMask = c.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.srcSubresource.layerCount = c.dstSubresource.layerCount = 1;
        c.srcOffset.x = sx; c.extent.width = (uint32_t)g_xrEyeW; c.extent.height = (uint32_t)g_xrEyeH; c.extent.depth = 1;
        vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        barrierImg(cb, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
        if (g_xrRGBA && e == 0) barrierImg(cb, g_xrStage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
    }
    barrierImg(cb, pic, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
}
/* after the frame's commands are submitted: the eyes' images back to the runtime, and the frame handed over, with the eyes in it if they were drawn */
static void xrEnd(void) {
    if (!g_xrOk || g_xrFake || !g_xrInFrame) return;
    XrCompositionLayerProjectionView pv[2]; XrCompositionLayerProjection layer = {.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader *ls[1] = {(const XrCompositionLayerBaseHeader *)&layer};
    if (g_xrDrawn) {
        for (int e = 0; e < 2; e++) { XrSwapchainImageReleaseInfo ri = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; p_xrReleaseSwapchainImage(g_xrSc[e], &ri);
            memset(&pv[e], 0, sizeof pv[e]); pv[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW; pv[e].pose = g_xrView[e].pose; pv[e].fov = g_xrView[e].fov;
            pv[e].subImage.swapchain = g_xrSc[e]; pv[e].subImage.imageRect.extent.width = g_xrEyeW; pv[e].subImage.imageRect.extent.height = g_xrEyeH; }
        layer.space = g_xrSpace; layer.viewCount = 2; layer.views = pv; }
    XrFrameEndInfo fe = {.type = XR_TYPE_FRAME_END_INFO}; fe.displayTime = g_xrT; fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; fe.layerCount = g_xrDrawn ? 1 : 0; fe.layers = ls;
    XrResult r = p_xrEndFrame(g_xrS, &fe); if (XR_FAILED(r)) xrSay("handing the frame over", r);
    g_xrInFrame = 0; g_xrDrawn = 0;
}
#else   /* no OpenXR here: the calls answer as a machine with no headset does */
static int xrPrepare(void) { const char *e = getenv("REVA_XR"); if (e && e[0] && strcmp(e, "0")) { printf("[xr] this build has no OpenXR (its SDK's headers weren't there to build with): playing on the screen\n"); fflush(stdout); } return 0; }
static void xrInstanceExt(const char **ex, uint32_t *n, uint32_t max) { (void)ex; (void)n; (void)max; }
static void xrDeviceExt(const char **ex, uint32_t *n, uint32_t max) { (void)ex; (void)n; (void)max; }
static VkPhysicalDevice xrPhysical(VkInstance inst) { (void)inst; return VK_NULL_HANDLE; }
static int xrSession(void) { return 0; }
static void xrOff(void) {}
static void xrCopyEyes(VkCommandBuffer cb, VkImage pic) { (void)cb; (void)pic; }
static void xrEnd(void) {}
int gfxXrOn(void) { return 0; }
int gfxXrFrame(GfxXrEye eye[2], GfxXrHand hand[2]) { (void)eye; (void)hand; return 0; }
void gfxXrEyeSize(int *w, int *h) { *w = *h = 0; }
void gfxXrBuzz(int hand, float amount, float seconds) { (void)hand; (void)amount; (void)seconds; }
int gfxXrQuit(void) { return 0; }
float gfxXrScale(void) { return 0.f; }
#endif
