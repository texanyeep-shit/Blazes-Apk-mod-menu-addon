// ClientDaBell: a hand menu built from the starter menu (ii's Stupid Menu layout).
// The layout idea comes from ii's menu; the code is new.
//   Hold Y (left hand)      -> menu on your left hand, tipped like ii's; turn your palm to you
//                              (B and the right hand with "Right Hand Menu")
//   Touch a button with your other index finger to press it
//   < and > on the sides    -> previous / next page
//   Pages: Movement Mods, Rig Mods, Items, Settings, Enabled Mods
// Movement only changes your own game. Rig mods (dances) and Items are seen by the room,
// within the limits in AGENTS.md ("Things others see"). Read docs/GUIDE.md before adding.
#include "ymsdk.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <dirent.h>
#include <strings.h>
#include <ctime>
#include <string>
#include <vector>

using namespace BNM;
using namespace BNM::Structures::Unity;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("ClientDaBell")
#define CDB_VERSION "v.1.2.8"

// ---- small helpers ---------------------------------------------------------

static std::string TypeName(const BNM::IL2CPP::Il2CppType *t) {
    Class c(t);
    if (!c.IsValid() || !c.GetClass() || !c.GetClass()->name) return "?";
    auto *k = c.GetClass();
    return (k->namespaze && k->namespaze[0]) ? std::string(k->namespaze) + "." + k->name : k->name;
}
// Pick an overload by its exact parameter types, e.g. GetComponent(System.Type).
static MethodBase Overload(Class c, const char *name, std::vector<std::string> types) {
    for (auto &m : c.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || strcmp(mi->name, name) != 0 || mi->parameters_count != types.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < types.size() && ok; i++) ok = TypeName(mi->parameters[i]) == types[i];
        if (ok) return m;
    }
    return {};
}

// Unity objects we keep between frames must be held by a GC handle, or the
// garbage collector can free them under us.
static void *(*p_handleNew)(void *, bool);
static void *(*p_handleTarget)(void *);
static void (*p_handleFree)(void *);
struct Ref {
    void *h = nullptr;
    void set(Il2CppObject *o) { clear(); if (o && p_handleNew) h = p_handleNew(o, false); }
    Il2CppObject *get() const { return h && p_handleTarget ? (Il2CppObject *)p_handleTarget(h) : nullptr; }
    void clear() { if (h && p_handleFree) p_handleFree(h); h = nullptr; }
};

static Color Lerp(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
}
static Color Rainbow(float h) {             // hue 0..1 -> bright color
    h = h - floorf(h);
    float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
    auto c = [](float v) { return v < 0 ? 0.f : v > 1 ? 1.f : v; };
    return Color(c(r), c(g), c(b), 1);
}

// ---- game lookups, resolved once -------------------------------------------

struct XRDevice { uint64_t id; bool initialized; };
static bool g_customFont = false;       // Londrina Solid loaded from the fonts folder
static struct {
    bool ok = false;
    Method<Il2CppObject *> createPrimitive, goTransform, goGetComponent, goAddComponent, compTransform, compGameObject;
    Method<void> goSetActive, destroy, trSetParent, trSetPosition, trSetRotation, trSetLocalPosition,
                 trSetLocalRotation, trSetLocalScale, rbSetVelocity, matSetColor, matSetShader,
                 textSetFont, textSetText, textSetFontSize, textSetAlignment, graphicSetColor,
                 canvasSetRenderMode, rtSetSizeDelta, textSetFontStyle, textSetBestFit, textSetMinSize,
                 textSetMaxSize, teleportTo, rbAddForce, rbSetUseGravity, colliderSetEnabled,
                 lrSetPositionCount, lrSetPosition, lrSetStartWidth, lrSetEndWidth, lrSetUseWorldSpace;
    Method<Quaternion> trGetRotation;
    Method<Vector3> trGetPosition, trGetForward, trGetRight, trGetUp, trInverseTransformPoint, trTransformPoint, rbGetVelocity;
    Method<bool> alive, tryGetBool, haptic, raycast;
    Method<XRDevice> deviceAtNode;
    Method<Il2CppObject *> trGetParent, trGetRoot, getComponentsInChildren, netLocal;
    Method<void> pvRpc;
    Method<Il2CppObject *> audioClipCreate;           // soundboard
    Method<bool> audioClipSetData, audioClipGetData;
    Method<int> audioClipSamples, asGetTimeSamples;
    Method<float> asGetVolume;
    Method<void> asSetMute;
    Method<bool> asGetMute;
    Method<void> asGetOutputData;                      // Disable Songs: how loud another player is (if the game has it)
    Method<Il2CppObject *> asGetClip;                  // ...else read the clip Photon writes their voice into
    BNM::MonoType *tSpeaker = nullptr;                 // Photon Voice's Speaker: plays another player's voice
    Method<void> asSetClip, asSetVolume, asSetSpatial, asSetLoop, asPlay, asStop;
    Method<bool> asIsPlaying;
    BNM::MonoType *tAudioSource = nullptr;
    // Soundboard through voice chat: Photon Voice's Recorder (the thing that sends your mic)
    BNM::MonoType *tRecorder = nullptr;
    Method<void> recSetSource, recSetClip, recSetLoop, recSetTransmit, recSetVad, recRestart0, recRestart1,
                 recSetRecording, recSetOnlyWhenEnabled, recStart;
    Method<int> recGetSource;
    Method<bool> recGetTransmit, recGetVad, recGetRecording, recGetSending, recGetInit;
    Field<int> recFieldSource;                         // the backing fields, set too (some versions read these)
    Field<Il2CppObject *> recFieldClip;
    Field<bool> recFieldLoop;
    Field<Il2CppObject *> recFieldVoice;
    Method<Il2CppObject *> goFind;
    BNM::IL2CPP::Il2CppClass *floatClass = nullptr;
    BNM::IL2CPP::MethodInfo *rpcCreateItem = nullptr;     // only read for its parameter types
    BNM::IL2CPP::Il2CppClass *objectClass = nullptr, *stringClass = nullptr;
    BNM::MonoType *tPhotonView = nullptr, *tItem = nullptr;
    Method<BNM::Structures::Mono::String *> itemKey, itemId;
    Method<bool> itemHeld;
    Method<void> itemDrop, setHeldItem, rbSetKinematic;
    void *(*arrayNew)(void *, uintptr_t) = nullptr;
    void *(*valueBox)(void *, void *) = nullptr;
    Method<bool> behaviourGetEnabled;
    Method<void> behaviourSetEnabled;
    std::vector<BNM::MonoType *> trackerTypes;
    Method<Il2CppObject *> rendererGetMaterial, shaderFind, findAll, findScene, findSceneByType, builtinResource;
    Field<Il2CppObject *> leftHand, rightHand, headTransform, bodyCollider, headCollider;
    Field<float> maxJumpSpeed, jumpMultiplier;
    Method<Vector3> plGetVelocity;
    Method<void> plSetVelocity, plNoGravityFor;
    Field<int> locomotionLayers, collisionLayers;
    Method<int> goGetLayer;
    Method<bool> colliderIsTrigger, behaviourGetEnabledCollider;
    Method<void> ignoreLayers;            // a LayerMask: which layers your hands can climb on
    Method<void> goSetLayer;
    BNM::MonoType *tRigidbody = nullptr, *tCollider = nullptr, *tCanvas = nullptr, *tText = nullptr,
                  *tFont = nullptr, *tRenderer = nullptr, *tMeshCollider = nullptr, *tLine = nullptr;
    Ref shader, font, usagePrimary, usageSecondary, usageGrip, usageTrigger, usageStick, usageStickClick;
    Method<bool> tryGetVec2;            // thumbsticks
} G;

static bool Lookups() {
    if (G.ok) return true;
    auto R = ym::api()->resolve;
    p_handleNew = (decltype(p_handleNew))R("il2cpp_gchandle_new");
    p_handleTarget = (decltype(p_handleTarget))R("il2cpp_gchandle_get_target");
    p_handleFree = (decltype(p_handleFree))R("il2cpp_gchandle_free");
    Class go("UnityEngine", "GameObject"), tr("UnityEngine", "Transform"), obj("UnityEngine", "Object"),
          rb("UnityEngine", "Rigidbody"), mat("UnityEngine", "Material"), rend("UnityEngine", "Renderer"),
          sh("UnityEngine", "Shader"), res("UnityEngine", "Resources"), text("UnityEngine.UI", "Text"),
          graphic("UnityEngine.UI", "Graphic"), canvas("UnityEngine", "Canvas"), rt("UnityEngine", "RectTransform"),
          dev("UnityEngine.XR", "InputDevices"), player("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!go || !tr || !player || !dev) { ym::log("clientdabell: game classes missing"); return false; }
    G.createPrimitive = go.GetMethod("CreatePrimitive", 1);
    G.goTransform = go.GetMethod("get_transform", 0);
    G.goGetComponent = Overload(go, "GetComponent", {"System.Type"});
    G.goAddComponent = Overload(go, "AddComponent", {"System.Type"});
    G.goSetActive = go.GetMethod("SetActive", 1);
    Class comp("UnityEngine", "Component");
    G.compTransform = comp.GetMethod("get_transform", 0);
    G.compGameObject = comp.GetMethod("get_gameObject", 0);
    G.destroy = Overload(obj, "Destroy", {"UnityEngine.Object"});
    G.alive = obj.GetMethod("op_Implicit", 1);
    {
        Class aclip("UnityEngine", "AudioClip"), asrc("UnityEngine", "AudioSource"), flt("System", "Single");
        if (flt.IsValid()) G.floatClass = flt.GetClass();
        if (aclip.IsValid()) {
            G.audioClipCreate = Overload(aclip, "Create", {"System.String", "System.Int32", "System.Int32", "System.Int32", "System.Boolean"});
            G.audioClipSetData = aclip.GetMethod("SetData", 2);
            G.audioClipGetData = aclip.GetMethod("GetData", 2);           // Overlap Songs: reads a song back to mix it
            G.audioClipSamples = aclip.GetMethod("get_samples", 0);
        }
        if (asrc.IsValid()) {
            G.tAudioSource = asrc.GetMonoType();
            G.asSetClip = asrc.GetMethod("set_clip", 1);
            G.asSetVolume = asrc.GetMethod("set_volume", 1);
            G.asSetSpatial = asrc.GetMethod("set_spatialBlend", 1);
            G.asSetLoop = asrc.GetMethod("set_loop", 1);
            G.asPlay = asrc.GetMethod("Play", 0);
            G.asStop = asrc.GetMethod("Stop", 0);
            G.asGetTimeSamples = asrc.GetMethod("get_timeSamples", 0);     // where the song is now
            G.asGetVolume = asrc.GetMethod("get_volume", 0);
            G.asGetClip = asrc.GetMethod("get_clip", 0);
            G.asSetMute = asrc.GetMethod("set_mute", 1);
            G.asGetMute = asrc.GetMethod("get_mute", 0);
            for (auto &m : asrc.GetMethods(false)) {          // GetOutputData(float[], int): the array one, not (int, int)
                auto *mi = m.GetInfo();
                if (!mi || !mi->name || strcmp(mi->name, "GetOutputData") != 0 || mi->parameters_count != 2) continue;
                std::string t0 = TypeName(mi->parameters[0]);
                ym::log("clientdabell: AudioSource.GetOutputData(%s, %s)", t0.c_str(), TypeName(mi->parameters[1]).c_str());
                if (t0 != "System.Int32") G.asGetOutputData = m;
            }
            G.asIsPlaying = asrc.GetMethod("get_isPlaying", 0);
        }
        Class spk("Photon.Voice.Unity", "Speaker");
        if (spk.IsValid()) G.tSpeaker = spk.GetMonoType();
        Class rec("Photon.Voice.Unity", "Recorder");
        if (rec.IsValid()) {
            G.tRecorder = rec.GetMonoType();
            G.recSetSource = rec.GetMethod("set_SourceType", 1);       // 0 = Microphone, 1 = AudioClip
            G.recGetSource = rec.GetMethod("get_SourceType", 0);
            G.recSetClip = rec.GetMethod("set_AudioClip", 1);
            G.recSetLoop = rec.GetMethod("set_LoopAudioClip", 1);
            G.recSetTransmit = rec.GetMethod("set_TransmitEnabled", 1);
            G.recGetTransmit = rec.GetMethod("get_TransmitEnabled", 0);
            G.recSetVad = rec.GetMethod("set_VoiceDetection", 1);       // off while a song plays, or quiet parts get cut
            G.recGetVad = rec.GetMethod("get_VoiceDetection", 0);
            G.recSetRecording = rec.GetMethod("set_IsRecording", 1);
            G.recGetRecording = rec.GetMethod("get_IsRecording", 0);
            G.recGetSending = rec.GetMethod("get_IsCurrentlyTransmitting", 0);   // true only while audio really goes out
            G.recGetInit = rec.GetMethod("get_IsInitialized", 0);               // true when it's hooked up to the voice connection
            G.recSetOnlyWhenEnabled = rec.GetMethod("set_RecordOnlyWhenEnabled", 1);
            G.recStart = rec.GetMethod("StartRecording", 0);
            G.recFieldSource = rec.GetField("sourceType");
            G.recFieldClip = rec.GetField("audioClip");
            G.recFieldLoop = rec.GetField("loopAudioClip");
            G.recFieldVoice = rec.GetField("voice");            // its LocalVoice: a real one only when hooked up to voice chat
            for (auto &m : rec.GetMethods(false)) {                     // RestartRecording() or RestartRecording(bool)
                auto *mi = m.GetInfo();
                if (!mi || !mi->name || strcmp(mi->name, "RestartRecording") != 0) continue;
                if (mi->parameters_count == 0) G.recRestart0 = m;
                else if (mi->parameters_count == 1) G.recRestart1 = m;
            }
        }
        ym::log("clientdabell: voice recorder=%d source=%d clip=%d restart=%d/%d", G.tRecorder != nullptr, G.recSetSource.IsValid(),
                G.recSetClip.IsValid(), G.recRestart0.IsValid(), G.recRestart1.IsValid());
    }
    G.goFind = go.GetMethod("Find", 1);
    G.trSetParent = Overload(tr, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    G.trSetPosition = tr.GetMethod("set_position", 1);
    G.trSetRotation = tr.GetMethod("set_rotation", 1);
    G.trSetLocalPosition = tr.GetMethod("set_localPosition", 1);
    G.trSetLocalRotation = tr.GetMethod("set_localRotation", 1);
    G.trSetLocalScale = tr.GetMethod("set_localScale", 1);
    G.trGetPosition = tr.GetMethod("get_position", 0);
    G.trGetRotation = tr.GetMethod("get_rotation", 0);
    G.trGetForward = tr.GetMethod("get_forward", 0);
    G.trGetRight = tr.GetMethod("get_right", 0);
    G.trGetUp = tr.GetMethod("get_up", 0);
    G.trInverseTransformPoint = Overload(tr, "InverseTransformPoint", {"UnityEngine.Vector3"});
    G.trTransformPoint = Overload(tr, "TransformPoint", {"UnityEngine.Vector3"});
    G.rbSetVelocity = rb.GetMethod("set_velocity", 1);
    G.rbGetVelocity = rb.GetMethod("get_velocity", 0);
    G.rbSetKinematic = rb.GetMethod("set_isKinematic", 1);
    G.rendererGetMaterial = rend.GetMethod("get_material", 0);
    G.matSetColor = Overload(mat, "set_color", {"UnityEngine.Color"});
    G.matSetShader = mat.GetMethod("set_shader", 1);
    G.shaderFind = sh.GetMethod("Find", 1);
    G.findAll = Overload(res, "FindObjectsOfTypeAll", {"System.Type"});
    G.textSetFont = text.GetMethod("set_font", 1);
    G.textSetFontStyle = text.GetMethod("set_fontStyle", 1);
    G.textSetBestFit = text.GetMethod("set_resizeTextForBestFit", 1);
    G.textSetMinSize = text.GetMethod("set_resizeTextMinSize", 1);
    G.textSetMaxSize = text.GetMethod("set_resizeTextMaxSize", 1);
    G.textSetText = text.GetMethod("set_text", 1);
    G.textSetFontSize = text.GetMethod("set_fontSize", 1);
    G.textSetAlignment = text.GetMethod("set_alignment", 1);
    G.graphicSetColor = graphic.GetMethod("set_color", 1);
    G.canvasSetRenderMode = canvas.GetMethod("set_renderMode", 1);
    G.rtSetSizeDelta = rt.GetMethod("set_sizeDelta", 1);
    G.deviceAtNode = dev.GetMethod("GetDeviceAtXRNode", 1);
    G.tryGetBool = dev.GetMethod("TryGetFeatureValue_bool", 3);
    for (const char *n : {"TryGetFeatureValue_Vector2f", "TryGetFeatureValue_Vector2"})
        if (!G.tryGetVec2.IsValid()) G.tryGetVec2 = dev.GetMethod(n, 3);
    ym::log("clientdabell: thumbsticks %d", G.tryGetVec2.IsValid());
    G.haptic = dev.GetMethod("SendHapticImpulse", 4);    // optional: a small buzz on press
    G.leftHand = player.GetField("leftHandTransform");
    G.rightHand = player.GetField("rightHandTransform");
    G.headTransform = player.GetField("headTransform");
    G.bodyCollider = player.GetField("bodyCollider");
    G.headCollider = player.GetField("headCollider");
    G.maxJumpSpeed = player.GetField("maxJumpSpeed");
    G.locomotionLayers = player.GetField("locomotionEnabledLayers");
    G.collisionLayers = player.GetField("collisionEnabledLayers");
    G.goGetLayer = go.GetMethod("get_layer", 0);
    G.ignoreLayers = Class("UnityEngine", "Physics").GetMethod("IgnoreLayerCollision", 3);
    // Yeeps moves your body through these, not through a Rigidbody on the player object
    G.plGetVelocity = player.GetMethod("GetVelocity", 0);
    G.plSetVelocity = player.GetMethod("ApplySetVelocityNoHaptic", 1);
    if (!G.plSetVelocity.IsValid()) G.plSetVelocity = player.GetMethod("ApplySetVelocity", 1);
    G.plNoGravityFor = player.GetMethod("ApplyNoGravityDuration", 1);
    ym::log("clientdabell: velocity %d/%d, no-gravity %d", G.plGetVelocity.IsValid(), G.plSetVelocity.IsValid(),
            G.plNoGravityFor.IsValid());
    G.goSetLayer = go.GetMethod("set_layer", 1);
    G.jumpMultiplier = player.GetField("jumpMultiplier");
    G.teleportTo = player.GetMethod("TeleportTo", 1);
    G.rbAddForce = Overload(rb, "AddForce", {"UnityEngine.Vector3", "UnityEngine.ForceMode"});
    G.rbSetUseGravity = rb.GetMethod("set_useGravity", 1);
    G.colliderSetEnabled = Class("UnityEngine", "Collider").GetMethod("set_enabled", 1);
    G.behaviourGetEnabledCollider = Class("UnityEngine", "Collider").GetMethod("get_enabled", 0);
    G.colliderIsTrigger = Class("UnityEngine", "Collider").GetMethod("get_isTrigger", 0);
    G.raycast = Overload(Class("UnityEngine", "Physics"), "Raycast",
                         {"UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.RaycastHit", "System.Single", "System.Int32"});
    G.findScene = Overload(obj, "FindObjectsOfType", {"System.Type"});      // only when noclip starts
    G.findSceneByType = Overload(obj, "FindObjectsByType", {"System.Type", "UnityEngine.FindObjectsSortMode"});
    G.builtinResource = Overload(res, "GetBuiltinResource", {"System.Type", "System.String"});
    Class line("UnityEngine", "LineRenderer");
    G.tLine = line.GetMonoType();
    G.lrSetPositionCount = line.GetMethod("set_positionCount", 1);
    G.lrSetPosition = line.GetMethod("SetPosition", 2);
    G.lrSetStartWidth = line.GetMethod("set_startWidth", 1);
    G.lrSetEndWidth = line.GetMethod("set_endWidth", 1);
    G.lrSetUseWorldSpace = line.GetMethod("set_useWorldSpace", 1);
    G.tMeshCollider = Class("UnityEngine", "MeshCollider").GetMonoType();
    G.trGetParent = tr.GetMethod("get_parent", 0);
    G.trGetRoot = tr.GetMethod("get_root", 0);
    G.getComponentsInChildren = Overload(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    Class net("", "NetworkMessenger", Image("Assembly-CSharp.dll"));
    if (net.IsValid()) {
        G.netLocal = net.GetMethod("get_local", 0);
        for (auto &m : net.GetMethods(false))
            if (m.GetInfo() && m.GetInfo()->name && strcmp(m.GetInfo()->name, "RpcCreateItem") == 0) G.rpcCreateItem = m.GetInfo();
    }
    {
        Class pv("Photon.Pun", "PhotonView");
        G.tPhotonView = pv.IsValid() ? pv.GetMonoType() : nullptr;
        if (pv.IsValid()) G.pvRpc = Overload(pv, "RPC", {"System.String", "Photon.Pun.RpcTarget", "System.Object[]"});
        Class item("", "Item", Image("Assembly-CSharp.dll"));
        if (item.IsValid()) {
            G.tItem = item.GetMonoType();
            G.itemKey = item.GetMethod("get_itemKey", 0);
            G.itemHeld = item.GetMethod("IsHeldOrAnchoredToAnyPlayer", 0);
            G.itemId = item.GetMethod("get_itemID", 0);
            G.itemDrop = item.GetMethod("TrySendDrop", 0);
            G.setHeldItem = Class("", "MasterPlayer", Image("Assembly-CSharp.dll")).GetMethod("SetHeldItem", 2);
        }
        G.objectClass = Class("System", "Object").GetClass();
        G.stringClass = Class("System", "String").GetClass();
        G.arrayNew = (decltype(G.arrayNew))ym::api()->resolve("il2cpp_array_new");
        G.valueBox = (decltype(G.valueBox))ym::api()->resolve("il2cpp_value_box");
        ym::log("clientdabell: items rpc=%d pv=%d item=%d box=%d", G.rpcCreateItem != nullptr, G.pvRpc.IsValid(),
                G.itemKey.IsValid(), G.arrayNew && G.valueBox);
    }
    Class behaviour("UnityEngine", "Behaviour");
    G.behaviourGetEnabled = behaviour.GetMethod("get_enabled", 0);
    G.behaviourSetEnabled = behaviour.GetMethod("set_enabled", 1);
    // What moves your hands with the controllers (paused while you dance)
    for (auto nc : std::vector<std::pair<const char *, const char *>>{
             {"UnityEngine.XR.Interaction.Toolkit", "XRBaseController"},
             {"UnityEngine.InputSystem.XR", "TrackedPoseDriver"},
             {"UnityEngine.SpatialTracking", "TrackedPoseDriver"}}) {
        Class c(nc.first, nc.second);
        if (c.IsValid()) G.trackerTypes.push_back(c.GetMonoType());
    }
    G.tRigidbody = rb.GetMonoType();
    G.tCollider = Class("UnityEngine", "Collider").GetMonoType();
    G.tRenderer = rend.GetMonoType();
    G.tCanvas = canvas.GetMonoType();
    G.tText = text.GetMonoType();
    G.tFont = Class("UnityEngine", "Font").GetMonoType();
    G.usagePrimary.set((Il2CppObject *)CreateMonoString("PrimaryButton"));
    G.usageSecondary.set((Il2CppObject *)CreateMonoString("SecondaryButton"));
    G.usageGrip.set((Il2CppObject *)CreateMonoString("GripButton"));
    G.usageTrigger.set((Il2CppObject *)CreateMonoString("TriggerButton"));
    G.usageStick.set((Il2CppObject *)CreateMonoString("Primary2DAxis"));
    G.usageStickClick.set((Il2CppObject *)CreateMonoString("Primary2DAxisClick"));
    for (const char *s : {"Universal Render Pipeline/Unlit", "Universal Render Pipeline/Lit"})
        if (!G.shader.get() && G.shaderFind.IsValid()) G.shader.set(G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(s)));
    // Londrina Solid: copy LondrinaSolid-Regular.ttf into a "fonts" folder next to the .so
    // files (mods/fonts/). If it is missing or won't load, the game's own font is used.
    {
        std::string fpath = std::string(ym::api()->modsDir ? ym::api()->modsDir : "") + "/fonts/LondrinaSolid-Regular.ttf";
        FILE *ff = fopen(fpath.c_str(), "rb");
        if (ff) {
            fclose(ff);
            try {
                Class fc("UnityEngine", "Font");
                Il2CppObject *fo = fc.IsValid() ? fc.CreateNewObjectParameters(CreateMonoString(fpath.c_str())) : nullptr;
                bool good = fo && G.alive((Il2CppObject *)fo);
                ym::log("clientdabell: font ctor(path): object=%d alive=%d", fo ? 1 : 0, good ? 1 : 0);
                if (!good && fc.IsValid()) {                       // second route: pick the constructor that takes one string
                    Il2CppObject *f2 = fc.CreateNewObjectTypes({"System.String"}, CreateMonoString(fpath.c_str()));
                    if (f2 && G.alive((Il2CppObject *)f2)) { fo = f2; good = true; }
                    ym::log("clientdabell: font second route (string ctor): object=%d alive=%d", f2 ? 1 : 0, good ? 1 : 0);
                }
                MethodBase hasChar = fc.IsValid() ? Overload(fc, "HasCharacter", {"System.Char"}) : MethodBase();
                if (good && hasChar.IsValid()) {
                    Method<bool> hc = hasChar;
                    bool hasA = hc[fo]((uint16_t)'A');
                    ym::log("clientdabell: Londrina Solid object created, HasCharacter(A)=%d", (int)hasA);   // not used as a gate: it can read false before first use
                }
                if (good) { G.font.set(fo); g_customFont = true; }
                ym::log("clientdabell: Londrina Solid font %s", good ? "loaded" : "failed, using the game's font");
            } catch (...) { ym::log("clientdabell: Londrina Solid font error, using the game's font"); }
        } else ym::log("clientdabell: no font file at %s, using the game's font", fpath.c_str());
    }
    // Arial, like ii's menu (Unity 6 calls it LegacyRuntime.ttf)
    for (const char *f : {"LegacyRuntime.ttf", "Arial.ttf"})
        if (!G.font.get() && G.builtinResource.IsValid() && G.tFont)
            G.font.set(G.builtinResource((Il2CppObject *)G.tFont, CreateMonoString(f)));
    if (!G.font.get() && G.findAll.IsValid() && G.tFont) {   // else any font the game loaded (one time)
        auto *arr = (uint8_t *)G.findAll((Il2CppObject *)G.tFont);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) G.font.set(((Il2CppObject **)(arr + 0x20))[0]);
    }
    G.ok = G.createPrimitive.IsValid() && G.goTransform.IsValid() && G.trSetPosition.IsValid() &&
           G.deviceAtNode.IsValid() && G.tryGetBool.IsValid() && G.leftHand.IsValid();
    if (!G.ok) ym::log("clientdabell: some game methods are missing, menu off");
    return G.ok;
}

static bool Alive(Il2CppObject *o) { return o && G.alive((Il2CppObject *)o); }
static Il2CppObject *Tr(Il2CppObject *gameObject) { return G.goTransform[gameObject](); }
static void Show(Ref &r, bool on) { if (Alive(r.get())) G.goSetActive[r.get()](on); }
static void Kill(Ref &r) { if (Alive(r.get())) G.destroy(r.get()); r.clear(); }

// A primitive with a flat color. collider=false removes its collider so it can't push you.
// Always give new objects the URP shader: Unity's default material only draws in one
// eye on Quest.
static Il2CppObject *Prim(int type, Color c, bool collider) {
    Il2CppObject *o = G.createPrimitive(type);   // 0 = Sphere, 3 = Cube
    if (!o) return nullptr;
    if (!collider) if (auto *col = G.goGetComponent[o](G.tCollider)) G.destroy(col);
    if (auto *r = G.goGetComponent[o](G.tRenderer)) {
        Il2CppObject *m = G.rendererGetMaterial[r]();
        if (m && G.shader.get()) G.matSetShader[m](G.shader.get());
        if (m) G.matSetColor[m](c);
    }
    return o;
}
static Il2CppObject *Cube(Color c, bool collider) { return Prim(3, c, collider); }
static void SetColor(Il2CppObject *o, Color c) {
    if (!Alive(o)) return;
    if (auto *r = G.goGetComponent[o](G.tRenderer)) if (Il2CppObject *m = G.rendererGetMaterial[r]()) G.matSetColor[m](c);
}
static void SetText(Ref &text, const char *s) {
    if (Alive(text.get())) G.textSetText[text.get()](CreateMonoString(s));
}

// ---- input -------------------------------------------------------------------

struct Buttons { bool a = false, b = false, x = false, y = false, lgrip = false, rgrip = false, ltrig = false, rtrig = false,
                 lclick = false, rclick = false; Vector2 lstick = Vector2(0, 0), rstick = Vector2(0, 0); };
static bool Pressed(int node, Ref &usage) {
    XRDevice d = G.deviceAtNode(node);
    bool v = false;
    if (d.initialized) G.tryGetBool(d.id, (BNM::Structures::Mono::String *)usage.get(), &v);
    return v;
}
static Vector2 Stick(int node) {
    Vector2 v(0, 0);
    if (!G.tryGetVec2.IsValid()) return v;
    XRDevice d = G.deviceAtNode(node);
    if (d.initialized) G.tryGetVec2(d.id, (BNM::Structures::Mono::String *)G.usageStick.get(), &v);
    if (v.x * v.x + v.y * v.y < 0.15f * 0.15f) v = Vector2(0, 0);   // dead zone
    return v;
}
static Buttons ReadButtons() {
    const int left = 4, right = 5;              // XRNode.LeftHand / RightHand
    Buttons b;
    b.x = Pressed(left, G.usagePrimary);  b.y = Pressed(left, G.usageSecondary);
    b.a = Pressed(right, G.usagePrimary); b.b = Pressed(right, G.usageSecondary);
    b.lgrip = Pressed(left, G.usageGrip); b.rgrip = Pressed(right, G.usageGrip);
    b.ltrig = Pressed(left, G.usageTrigger); b.rtrig = Pressed(right, G.usageTrigger);
    b.lclick = Pressed(left, G.usageStickClick); b.rclick = Pressed(right, G.usageStickClick);
    b.lstick = Stick(left); b.rstick = Stick(right);
    return b;
}

// ---- menu contents -------------------------------------------------------------

// Feature switches. Every feature only changes your own game.
static bool f_velocityFly, f_ironMan, f_tpGun, f_noclip, f_speedBoost, f_noGravity;
static bool f_itemGun, f_spawnOnHand, f_car, f_soundLoop, f_stickFly, f_dash, f_itemOrbit;
static bool f_soundMic = true;                  // soundboard goes out through your voice chat
static bool f_overlap = false;                  // Play while a song plays mixes the two together
static bool f_disableSongs = false;             // mute other players while they play music
static bool f_colorBlind = false;               // Settings: on-buttons say [ON] and use colors everyone can tell apart
static bool f_barkMode = false;                 // Settings: the menu is a panel in front of you instead of on your hand
static bool g_rebuildMenu = false;              // Bark Mode switched: build the menu again next frame
static bool g_barkPlaced = false;               // the Bark Mode panel has been put in front of you for this opening
static bool g_rebuiltNote = false;              // say how to open the menu once it's built again
static bool g_barkOpen = false;                 // Bark Mode: the panel is open (a press opens it, another closes it)
static bool f_longArms, f_superSpeed, f_megaJump, f_superFly, f_rocket, f_grapple, f_groundPound, f_flingGun, f_propGun, f_bounce;   // Overpowered
static char g_soundLabel[64] = "Play: (open to scan)";   // text of the soundboard play button
static char g_itemKey[64] = "";                // the last item you held (learned on grab)
struct PendingItem { Vector3 pos, vel; bool drop = false, orbit = false; };   // once it appears: drop it (Item Rain) / add it to Item Orbit
static std::vector<PendingItem> g_itemQueue;
static double g_nextItem = 0, g_nextBatch = 0;
static int g_dupeArmed = 0;

static constexpr int kDances = 18;                 // 0-7 Rig Mods dances, 8-17 Emotes
static bool g_danceSel[kDances];
static bool g_wxSel[5];             // Effects: Snow, Rain, Thunderstorm, Disco, Bundle Disco (pick one, like the dances)
static int g_wx = -1;
static bool g_todSel[4];            // Effects: Morning, Afternoon, Sunset, Midnight (pick one, only on your game)
static int g_tod = -1;
static int g_dance = -1;            // the picked dance, -1 = none
static bool f_fly, f_fastFly, f_platforms, f_lowGravity, f_rainbow, f_fps, f_clock, f_rightHand,
            f_notify = true;
static Ref g_player, g_rigidbody, g_leftHand, g_rightHand, g_head;
static Vector3 g_spot;
static bool g_haveSpot = false;

static void Notify(const char *fmt, ...);
static void SaveSpot();
static void GoToSpot();
static void NextTheme();
static void AllOff();
static void PointerToHand();
static void NextPointer();
static void NextIronSpeed();
static void NextCarSpeed();
static void ApplySpeedBoost();
static void ApplyGravity();
static void DancePicked();
static void ArmDupe9();
static void ArmDupe27();
static void ArmDupe9x3();
static void ItemRain();
static void ItemCircle();
static void ItemOrbitToggled();
static bool LeftHandItem();
static void ScanSongs();
static void PlaySong();
static void NextSong();
static void PrevSong();
static void StopSound();
static void ApplyLoop();
static void NextSoundVolume();
static void DisableSongsToggled();
static void BarkModeToggled();
static void SoundMicToggled();
static void WeatherPicked();
static void LongArmsToggled();
static void NextArmLength();
static void NextJumpPower();
static void OverpoweredOff();
static void NextWeatherIntensity();
static void TodPicked();

enum Cat { MAIN, MOVEMENT, RIG, ITEMS, SOUNDBOARD, EFFECTS, OVERPOWERED, SETTINGS, ENABLED, EMOTES, CREDITS, BETA, SPAWNER, NCAT };
static const char *kCatName[NCAT] = {"Main", "Movement Mods", "Rig Mods", "Items", "Soundboard", "Effects", "Overpowered", "Settings", "Enabled Mods", "Emotes", "Credits", "Beta Testers", "Item Spawner"};
enum Kind { OPEN, TOGGLE, ACTION };
struct Item {
    Cat cat;
    const char *label;
    Kind kind;
    Cat target = MAIN;            // OPEN: the page it opens
    bool *flag = nullptr;         // TOGGLE: the switch
    void (*fn)() = nullptr;       // ACTION, or called after a TOGGLE changes
};
// The owner's layout (2026-10-31). Keep this order.
static Item g_items[] = {
    {MAIN, "Settings", OPEN, SETTINGS},
    {MAIN, "Movement Mods", OPEN, MOVEMENT},
    {MAIN, "Rig Mods", OPEN, RIG},
    {MAIN, "Items", OPEN, ITEMS},
    {MAIN, "Soundboard", OPEN, SOUNDBOARD},
    {MAIN, "Effects", OPEN, EFFECTS},
    {MAIN, "Overpowered", OPEN, OVERPOWERED},
    {MAIN, "Enabled Mods", OPEN, ENABLED},
    {MAIN, "Emotes", OPEN, EMOTES},
    {MAIN, "Credits", OPEN, CREDITS},
    {MAIN, "Beta Testers", OPEN, BETA},

    {MOVEMENT, "Exit Movement Mods", OPEN, MAIN},
    {MOVEMENT, "Platforms", TOGGLE, MAIN, &f_platforms},
    {MOVEMENT, "Fly", TOGGLE, MAIN, &f_fly},
    {MOVEMENT, "Fast Fly", TOGGLE, MAIN, &f_fastFly},
    {MOVEMENT, "Velocity Fly", TOGGLE, MAIN, &f_velocityFly},
    {MOVEMENT, "Car", TOGGLE, MAIN, &f_car},
    {MOVEMENT, "Teleport Gun", TOGGLE, MAIN, &f_tpGun},
    {MOVEMENT, "Iron Man", TOGGLE, MAIN, &f_ironMan},
    {MOVEMENT, "Noclip", TOGGLE, MAIN, &f_noclip},
    {MOVEMENT, "Speed Boost", TOGGLE, MAIN, &f_speedBoost, ApplySpeedBoost},
    {MOVEMENT, "No Gravity", TOGGLE, MAIN, &f_noGravity, ApplyGravity},
    {MOVEMENT, "Low Gravity", TOGGLE, MAIN, &f_lowGravity},
    {MOVEMENT, "Save Spot", ACTION, MAIN, nullptr, SaveSpot},
    {MOVEMENT, "Go To Spot", ACTION, MAIN, nullptr, GoToSpot},
    {MOVEMENT, "Bounce", TOGGLE, MAIN, &f_bounce},
    {MOVEMENT, "Thumbstick Fly", TOGGLE, MAIN, &f_stickFly},
    {MOVEMENT, "Dash", TOGGLE, MAIN, &f_dash},


    {EMOTES, "Exit Emotes", OPEN, MAIN},
    {EMOTES, "Floss", TOGGLE, MAIN, &g_danceSel[8], DancePicked},
    {EMOTES, "Dab", TOGGLE, MAIN, &g_danceSel[9], DancePicked},
    {EMOTES, "Disco", TOGGLE, MAIN, &g_danceSel[10], DancePicked},
    {EMOTES, "Clap", TOGGLE, MAIN, &g_danceSel[11], DancePicked},
    {EMOTES, "Hype", TOGGLE, MAIN, &g_danceSel[12], DancePicked},
    {EMOTES, "Robot", TOGGLE, MAIN, &g_danceSel[13], DancePicked},
    {EMOTES, "Cheer", TOGGLE, MAIN, &g_danceSel[14], DancePicked},
    {EMOTES, "Salute", TOGGLE, MAIN, &g_danceSel[15], DancePicked},
    {EMOTES, "Flex", TOGGLE, MAIN, &g_danceSel[16], DancePicked},
    {EMOTES, "Chicken Wings", TOGGLE, MAIN, &g_danceSel[17], DancePicked},
    {RIG, "Exit Rig Mods", OPEN, MAIN},
    {RIG, "Wave", TOGGLE, MAIN, &g_danceSel[0], DancePicked},
    {RIG, "Helicopter", TOGGLE, MAIN, &g_danceSel[1], DancePicked},
    {RIG, "T-Pose", TOGGLE, MAIN, &g_danceSel[2], DancePicked},
    {RIG, "Windmill", TOGGLE, MAIN, &g_danceSel[3], DancePicked},
    {RIG, "Swim", TOGGLE, MAIN, &g_danceSel[4], DancePicked},
    {RIG, "Jumping Jacks", TOGGLE, MAIN, &g_danceSel[5], DancePicked},
    {RIG, "Spaz", TOGGLE, MAIN, &g_danceSel[6], DancePicked},
    {RIG, "Beyblade", TOGGLE, MAIN, &g_danceSel[7], DancePicked},

    {ITEMS, "Exit Items", OPEN, MAIN},
    {ITEMS, "Item Spawner", OPEN, SPAWNER},
    {ITEMS, "Spawn Item Gun", TOGGLE, MAIN, &f_itemGun},
    {ITEMS, "Duplicate Items (3x3)", ACTION, MAIN, nullptr, ArmDupe9},
    {ITEMS, "Duplicate Items (3x3x3)", ACTION, MAIN, nullptr, ArmDupe27},
    {ITEMS, "Duplicate Items (9x3)", ACTION, MAIN, nullptr, ArmDupe9x3},
    {ITEMS, "Spawn On Hand", TOGGLE, MAIN, &f_spawnOnHand},
    {ITEMS, "Item Rain", ACTION, MAIN, nullptr, ItemRain},
    {ITEMS, "Item Circle", ACTION, MAIN, nullptr, ItemCircle},
    {ITEMS, "Item Orbit", TOGGLE, MAIN, &f_itemOrbit, ItemOrbitToggled},

    {SOUNDBOARD, "Exit Soundboard", OPEN, MAIN},
    {SOUNDBOARD, g_soundLabel, ACTION, MAIN, nullptr, PlaySong},
    {SOUNDBOARD, "Next Song", ACTION, MAIN, nullptr, NextSong},
    {SOUNDBOARD, "Previous Song", ACTION, MAIN, nullptr, PrevSong},
    {SOUNDBOARD, "Stop Sound", ACTION, MAIN, nullptr, StopSound},
    {SOUNDBOARD, "Others Hear It", TOGGLE, MAIN, &f_soundMic, SoundMicToggled},
    {SOUNDBOARD, "Loop", TOGGLE, MAIN, &f_soundLoop, ApplyLoop},
    {SOUNDBOARD, "Overlap Songs", TOGGLE, MAIN, &f_overlap},
    {SOUNDBOARD, "Disable Songs", TOGGLE, MAIN, &f_disableSongs, DisableSongsToggled},
    {SOUNDBOARD, "Sound Volume", ACTION, MAIN, nullptr, NextSoundVolume},
    {SOUNDBOARD, "Rescan Songs", ACTION, MAIN, nullptr, ScanSongs},

    {EFFECTS, "Exit Effects", OPEN, MAIN},
    {EFFECTS, "Snow", TOGGLE, MAIN, &g_wxSel[0], WeatherPicked},
    {EFFECTS, "Rain", TOGGLE, MAIN, &g_wxSel[1], WeatherPicked},
    {EFFECTS, "Thunderstorm", TOGGLE, MAIN, &g_wxSel[2], WeatherPicked},
    {EFFECTS, "Disco", TOGGLE, MAIN, &g_wxSel[3], WeatherPicked},
    {EFFECTS, "Bundle Disco", TOGGLE, MAIN, &g_wxSel[4], WeatherPicked},
    {EFFECTS, "Effect Intensity", ACTION, MAIN, nullptr, NextWeatherIntensity},
    {EFFECTS, "Morning", TOGGLE, MAIN, &g_todSel[0], TodPicked},
    {EFFECTS, "Afternoon", TOGGLE, MAIN, &g_todSel[1], TodPicked},
    {EFFECTS, "Sunset", TOGGLE, MAIN, &g_todSel[2], TodPicked},
    {EFFECTS, "Midnight", TOGGLE, MAIN, &g_todSel[3], TodPicked},

    {OVERPOWERED, "Exit Overpowered", OPEN, MAIN},
    {OVERPOWERED, "Long Arms", TOGGLE, MAIN, &f_longArms, LongArmsToggled},
    {OVERPOWERED, "Arm Length", ACTION, MAIN, nullptr, NextArmLength},
    {OVERPOWERED, "Super Speed", TOGGLE, MAIN, &f_superSpeed, ApplySpeedBoost},
    {OVERPOWERED, "Mega Jump", TOGGLE, MAIN, &f_megaJump},
    {OVERPOWERED, "Jump Power", ACTION, MAIN, nullptr, NextJumpPower},
    {OVERPOWERED, "Super Fly", TOGGLE, MAIN, &f_superFly},
    {OVERPOWERED, "Rocket Boots", TOGGLE, MAIN, &f_rocket},
    {OVERPOWERED, "Grapple Hook", TOGGLE, MAIN, &f_grapple},
    {OVERPOWERED, "Ground Pound", TOGGLE, MAIN, &f_groundPound},
    {OVERPOWERED, "Fling Gun", TOGGLE, MAIN, &f_flingGun},
    {OVERPOWERED, "Launch Items Gun", TOGGLE, MAIN, &f_propGun},
    {OVERPOWERED, "Turn Overpowered Off", ACTION, MAIN, nullptr, OverpoweredOff},

    {SETTINGS, "Exit Settings", OPEN, MAIN},
    {SETTINGS, "Change Pointer Position", ACTION, MAIN, nullptr, NextPointer},
    {SETTINGS, "Iron Man Speed", ACTION, MAIN, nullptr, NextIronSpeed},
    {SETTINGS, "Car Speed", ACTION, MAIN, nullptr, NextCarSpeed},
    {SETTINGS, "Right Hand Menu", TOGGLE, MAIN, &f_rightHand, PointerToHand},
    {SETTINGS, "Bark Mode", TOGGLE, MAIN, &f_barkMode, BarkModeToggled},
    {SETTINGS, "Change Menu Theme", ACTION, MAIN, nullptr, NextTheme},
    {SETTINGS, "Rainbow Menu", TOGGLE, MAIN, &f_rainbow},
    {SETTINGS, "Color Blind Mode", TOGGLE, MAIN, &f_colorBlind},
    {SETTINGS, "Show FPS", TOGGLE, MAIN, &f_fps},
    {SETTINGS, "Show Clock", TOGGLE, MAIN, &f_clock},
    {SETTINGS, "Notifications", TOGGLE, MAIN, &f_notify},
    {SETTINGS, "Turn Everything Off", ACTION, MAIN, nullptr, AllOff},

    {CREDITS, "Exit Credits", OPEN, MAIN},
    {CREDITS, "@CampDaBell", ACTION, MAIN, nullptr, nullptr},

    {BETA, "Exit Beta Testers", OPEN, MAIN},
    {BETA, "@CampDaBell", ACTION, MAIN, nullptr, nullptr},
    {BETA, "@cart3rrrrr", ACTION, MAIN, nullptr, nullptr},
    {BETA, "@mrbeast1479", ACTION, MAIN, nullptr, nullptr},
    // Add a tester: {BETA, "Their Name", ACTION, MAIN, nullptr, nullptr},
};
static constexpr int kItems = sizeof(g_items) / sizeof(g_items[0]);
static Item g_returnItem = {ENABLED, "Exit Enabled Mods", OPEN, MAIN};
static void DisableAllMods();
// Item Spawner: a list built from the game's own items (every item kind it has loaded).
static std::vector<std::string> g_catalog;              // item kinds (their keys), sorted
static std::vector<Item> g_spawnView;                    // one button per kind (labels point into g_catalog)
static void SpawnPicked();
static Item g_spawnExit = {SPAWNER, "Exit Item Spawner", OPEN, ITEMS};
static Item *g_lastPressed = nullptr;                   // the button being pressed right now (for SpawnPicked)
static bool f_modsPaused = false;               // Enabled Mods -> Disable All Mods is on: the mods that were on are saved here
static std::vector<Item *> g_pausedMods;
static Item g_disableAllItem = {ENABLED, "Disable All Mods", TOGGLE, MAIN, &f_modsPaused, DisableAllMods};
// What the Enabled Mods page lists: switches that are on, except settings that aren't mods.
static bool ListedAsMod(const Item &it) {
    return it.kind == TOGGLE && *it.flag && it.flag != &f_notify && it.flag != &f_soundMic && it.flag != &f_colorBlind && it.flag != &f_barkMode;
}

// Tooltips, shown under the menu while your finger is on a button (like ii's).
static const struct { const char *label, *tip; } kTips[] = {
    {"Settings", "Opens the settings."},
    {"Movement Mods", "Opens the movement mods."},
    {"Enabled Mods", "Shows every mod you have on."},
    {"Disable All Mods", "On: turns off every mod on this page. Off again: turns the same mods back on."},
    {"Credits", "Who made this menu."},
    {"Beta Testers", "The people who test ClientDaBell."},
    {"@cart3rrrrr", "Beta tester."},
    {"@mrbeast1479", "Beta tester."},
    {"@CampDaBell", "The menu is made by @CampDaBell."},
    {"Platforms", "Hold a grip to stand on a platform."},
    {"Fly", "Hold A to fly where you look."},
    {"Fast Fly", "Makes Fly a lot faster."},
    {"Velocity Fly", "Hold A to shoot where your hand points."},
    {"Car", "Hold A to drive where you look, floating just above the ground."},
    {"Car Speed", "Slow, normal or fast."},
    {"Teleport Gun", "Hold right grip to aim, trigger to teleport."},
    {"Iron Man", "Hold X and A to fly with your hands."},
    {"Noclip", "Hold right trigger to go through walls."},
    {"Speed Boost", "Faster and higher jumps."},
    {"No Gravity", "Turns your gravity off."},
    {"Low Gravity", "Jump higher, fall slower."},
    {"Save Spot", "Remembers where you are."},
    {"Go To Spot", "Teleports you to your saved spot."},
    {"Change Menu Theme", "Plush Blue, Orange, Blue, Green, Purple, Dark."},
    {"Rainbow Menu", "The menu cycles through colors."},
    {"Color Blind Mode", "Buttons that are on say [ON] and turn orange with dark text; pressed buttons flash white."},
    {"Show FPS", "Frames per second in the title."},
    {"Show Clock", "The time in the title."},
    {"Rig Mods", "Pick a dance, then hold left trigger. Everyone sees it."},
    {"Items", "Spawn copies of the item in your left hand. Everyone sees them."},
    {"Spawn Item Gun", "Hold an item in your left hand. Right grip aims, trigger spawns it."},
    {"Duplicate Items (3x3)", "9 of the item in your left hand appear in front of you."},
    {"Duplicate Items (3x3x3)", "27 of the item in your left hand appear in front of you."},
    {"Duplicate Items (9x3)", "27 of the item in your left hand appear in a row of 9, three deep."},
    {"Spawn On Hand", "Each right trigger spawns the item from your left hand at your right hand."},
    {"Item Orbit", "Hold an item in your left hand, then turn this on: it and 31 copies circle around you until you turn it off."},
    {"Item Spawner", "Every item in the game, as a list: tap one to spawn it in front of you."},
    {"Item Circle", "32 of the item in your left hand appear in a circle around you. Everyone sees them."},
    {"Item Rain", "32 of the item in your left hand fall from the sky around you. Everyone sees them."},
    {"Wave", "Waves one hand, the other on your hip. Hold left trigger."},
    {"Helicopter", "Both arms spin around you. Hold left trigger."},
    {"T-Pose", "The classic T-pose. Hold left trigger."},
    {"Windmill", "Arms spin like a windmill. Hold left trigger."},
    {"Swim", "Swims through the air. Hold left trigger."},
    {"Jumping Jacks", "Arms go up and down at your sides. Hold left trigger."},
    {"Spaz", "Hands shake all over the place. Hold left trigger."},
    {"Beyblade", "Hands spin fast around your waist. Hold left trigger."},
    {"Soundboard", "Plays .wav songs from the sounds folder, through your voice chat."},
    {"Others Hear It", "On: the room hears the song through your mic. Off: only you hear it. Turning it back on mid-song restarts the song for the room."},
    {"Next Song", "Picks the next song in the list."},
    {"Previous Song", "Picks the previous song in the list."},
    {"Stop Sound", "Stops the song (and cancels a loading one)."},
    {"Loop", "Repeats the song until you stop it."},
    {"Overlap Songs", "On: Play while a song plays adds the new song on top. Both go through voice chat."},
    {"Disable Songs", "Mutes a player for you while they play music, unmutes them when it stops. A guess, by how steady the sound is."},
    {"Sound Volume", "Quiet, medium or loud (for you; others hear it at full)."},
    {"Rescan Songs", "Looks in the sounds folder again."},
    {"Effects", "Weather, discos and time of day. Only you see them."},
    {"Morning", "Low sun, warm light and a little mist. Only you see it."},
    {"Afternoon", "High sun and a bright blue sky. Only you see it."},
    {"Sunset", "An orange sky with the sun on the horizon. Only you see it."},
    {"Midnight", "Dark sky, moon and stars. Only you see it."},
    {"Disco", "A spinning disco ball with colored beams, lights and sparkles."},
    {"Bundle Disco", "A party like buying a bundle: the ball drops in, flash, confetti, flashing lights."},
    {"Snow", "Snow falls around you."},
    {"Rain", "Rain falls around you."},
    {"Thunderstorm", "Rain with lightning and thunder."},
    {"Effect Intensity", "Light, medium or heavy."},
    {"Overpowered", "The overpowered mods. Two pages."},
    {"Long Arms", "Your arms reach farther. Hands go normal while the menu is open."},
    {"Arm Length", "How long the arms are: 1.5x, 2x, 3x or 4x."},
    {"Super Speed", "A lot faster and higher jumps than Speed Boost."},
    {"Mega Jump", "Press B for a huge jump, even in the air. (Y with the right hand menu.)"},
    {"Jump Power", "Small, medium, big or huge jump."},
    {"Super Fly", "Hold A to rocket where you look."},
    {"Rocket Boots", "Hold both triggers to shoot upward."},
    {"Grapple Hook", "Hold either trigger (or both) to swing toward what that hand points at."},
    {"Ground Pound", "Both grips: mace smash with a shockwave."},
    {"Launch Items Gun", "Free-hand grip shoots local-only props."},
    {"Emotes", "Poses and moves for your yeep: pick one, then hold left trigger."},
    {"Floss", "Hold left trigger to floss."},
    {"Dab", "Hold left trigger to dab."},
    {"Bounce", "Bounce back up every time you land."},
    {"Thumbstick Fly", "Left stick flies where you look, right stick goes up and down."},
    {"Dash", "Click the right thumbstick to dash where you look."},
    {"Fling Gun", "Press A (X) to fling yourself where you point."},
    {"Turn Overpowered Off", "Switches every Overpowered mod off."},
    {"Change Pointer Position", "Moves the ball on your finger."},
    {"Iron Man Speed", "Changes how strong Iron Man is."},
    {"Right Hand Menu", "Hold B, menu on your right hand."},
    {"Bark Mode", "Press Y to open a big panel in front of you (like the bark menu); press again to close it."},
    {"Notifications", "Messages when you switch something."},
    {"Turn Everything Off", "Switches every mod off."},
};
static const char *TipFor(const char *label) {
    if (strncmp(label, "Exit ", 5) == 0) return "Returns you back to the main category.";
    for (auto &t : kTips) if (strcmp(t.label, label) == 0) return t.tip;
    return "";
}

// Colors in the ii menu style: an animated background and darker buttons when on.
struct Theme { const char *name; Color bgA, bgB, button, buttonOn, title; };
static const Theme kThemes[] = {
    {"Plush Blue", Color(0.25f, 0.55f, 0.95f, 1), Color(0.42f, 0.72f, 1.0f, 1), Color(0.13f, 0.33f, 0.70f, 1), Color(0.05f, 0.15f, 0.42f, 1), Color(0.82f, 0.93f, 1.0f, 1)},
    {"Orange", Color(1.0f, 0.50f, 0.0f, 1), Color(1.0f, 0.40f, 0.0f, 1), Color(0.67f, 0.33f, 0.0f, 1), Color(0.33f, 0.16f, 0.0f, 1), Color(1.0f, 0.75f, 0.49f, 1)},
    {"Blue",   Color(0.20f, 0.50f, 1.0f, 1), Color(0.10f, 0.30f, 0.85f, 1), Color(0.10f, 0.28f, 0.65f, 1), Color(0.04f, 0.12f, 0.32f, 1), Color(0.70f, 0.85f, 1.0f, 1)},
    {"Green",  Color(0.30f, 0.85f, 0.30f, 1), Color(0.15f, 0.65f, 0.20f, 1), Color(0.12f, 0.45f, 0.12f, 1), Color(0.05f, 0.22f, 0.05f, 1), Color(0.75f, 1.0f, 0.75f, 1)},
    {"Purple", Color(0.65f, 0.30f, 1.0f, 1), Color(0.45f, 0.15f, 0.85f, 1), Color(0.38f, 0.15f, 0.60f, 1), Color(0.18f, 0.06f, 0.30f, 1), Color(0.88f, 0.75f, 1.0f, 1)},
    {"Dark",   Color(0.12f, 0.12f, 0.14f, 1), Color(0.05f, 0.05f, 0.06f, 1), Color(0.22f, 0.22f, 0.25f, 1), Color(0.45f, 0.45f, 0.50f, 1), Color(0.85f, 0.85f, 0.90f, 1)},
};
static constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
static int g_theme = 0;
static const Theme &T() { return kThemes[g_theme]; }

// ---- menu objects ----------------------------------------------------------------

// Layout in meters, matching ii's menu as it was ported to Yeeps. Everything is drawn
// on a "board" object: +x goes across, +y goes up, the player looks at it from -z.
static constexpr int kRows = 8;                                   // buttons per page
// These change with Bark Mode (SetLayout); the hand menu values are the defaults.
static float kBoardW = 0.30f, kBoardH = 0.3825f, kBtnW = 0.27f, kBtnH = 0.0306f;
static float kHomeY = -0.2175f, kHomeW = 0.12f, kHomeH = 0.032f;   // Home button just under the board
static float kTitleY = 0.235f, kSideX = 0.2f, kSideW = 0.06f, kSideH = 0.344f;
static int g_btnFont = 19, g_titleFont = 32;
static bool g_barkLayout = false;                   // the menu is built as the Bark Mode panel
// Bark Mode: a wide panel that floats in front of you (like Gorilla Tag's bark menu): the
// title at the top, the version in the top-right corner, 8 buttons in 2 columns of 4, tall
// < and > buttons inside the left and right edges, Home at the bottom.
static void SetLayout(bool bark) {
    g_barkLayout = bark;
    if (bark) {
        kBoardW = 0.62f; kBoardH = 0.42f; kBtnW = 0.20f; kBtnH = 0.058f;
        kHomeY = -0.175f; kHomeW = 0.14f; kHomeH = 0.04f;
        kTitleY = 0.165f; kSideX = 0.25f; kSideW = 0.05f; kSideH = 0.27f;
        g_btnFont = 26; g_titleFont = 44;
    } else {
        kBoardW = 0.30f; kBoardH = 0.3825f; kBtnW = 0.27f; kBtnH = 0.0306f;
        kHomeY = -0.2175f; kHomeW = 0.12f; kHomeH = 0.032f;
        kTitleY = 0.235f; kSideX = 0.2f; kSideW = 0.06f; kSideH = 0.344f;
        g_btnFont = 19; g_titleFont = 32;
    }
}
// Where button r (0..7) sits: one column on the hand menu; in Bark Mode two columns,
// left to right then down.
static float RowY(int r) { return g_barkLayout ? 0.095f - (r / 2) * 0.072f : 0.13f - r * 0.04f; }
static float RowX(int r) { return g_barkLayout ? (r % 2 ? 1.0f : -1.0f) * 0.112f : 0.0f; }

static Ref g_menu, g_board, g_bg, g_outline, g_pointer, g_title, g_titleBar;
static Ref g_rowCube[kRows], g_rowLabel[kRows], g_rowText[kRows];
static Ref g_prev, g_next, g_notifyGo, g_notifyText, g_tipText, g_leftPlatform, g_rightPlatform;
static double g_openTime = 0, g_flashUntil = 0;
static int g_flashRow = -2, g_hover = -2;
static bool g_built = false, g_menuShown = false, g_dirty = true;
static Cat g_cat = MAIN;
static int g_page = 0;
static std::vector<Item *> g_view;                                // what the current page list shows
static double g_time = 0, g_nextPress = 0, g_notifyUntil = 0, g_nextTitle = 0;
static int g_touching = -2, g_frames = 0;
static Ref g_homeCube, g_homeLabel;
static float g_fps = 0;

static int PageCount() { return g_view.empty() ? 1 : (int)(g_view.size() + kRows - 1) / kRows; }

// Enabled Mods -> Disable All Mods (a switch). On: every mod on that page is switched off the
// normal way (the switch goes off, then its own off code runs) and remembered. Off: the
// remembered ones are switched back on the same way.
static void DisableAllMods() {
    int n = 0;
    if (f_modsPaused) {
        g_pausedMods.clear();
        for (auto &it : g_items) {
            if (!ListedAsMod(it)) continue;
            g_pausedMods.push_back(&it);
            *it.flag = false;
            if (it.fn) { try { it.fn(); } catch (...) {} }
            n++;
        }
        Notify(n ? "Disabled %d mods" : "Nothing to disable", n);
    } else {
        for (Item *it : g_pausedMods) {
            if (*it->flag) continue;                     // you turned it back on yourself already
            *it->flag = true;
            if (it->fn) { try { it->fn(); } catch (...) {} }
            n++;
        }
        g_pausedMods.clear();
        Notify(n ? "Turned %d mods back on" : "Nothing to turn back on", n);
    }
    g_dirty = true;
}

static void BuildView() {
    g_view.clear();
    if (g_cat == SPAWNER) {
        g_view.push_back(&g_spawnExit);
        for (auto &it : g_spawnView) g_view.push_back(&it);
    } else if (g_cat == ENABLED) {
        g_view.push_back(&g_returnItem);
        bool any = f_modsPaused;                          // the switch stays while mods are saved
        for (auto &it : g_items) any |= ListedAsMod(it);
        if (any) g_view.push_back(&g_disableAllItem);
        for (auto &it : g_items) if (ListedAsMod(it)) g_view.push_back(&it);
    } else {
        for (auto &it : g_items) if (it.cat == g_cat) g_view.push_back(&it);
    }
    if (g_page >= PageCount()) g_page = 0;
}

static Il2CppObject *Label(Il2CppObject *canvasT, const char *s, Vector3 pos, float w, float h, int size, Ref *textOut) {
    if (!G.font.get() || !G.tText) return nullptr;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("Label"));
    Il2CppObject *text = go ? G.goAddComponent[go](G.tText) : nullptr;
    if (!text) return nullptr;
    Il2CppObject *t = Tr(go);
    G.trSetParent[t](canvasT, false);
    G.rtSetSizeDelta[t](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[t](pos * 1000);
    G.trSetLocalRotation[t](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[t](Vector3(1, 1, 1));
    G.textSetFont[text](G.font.get());
    G.textSetText[text](CreateMonoString(s));
    G.textSetFontSize[text](size);
    G.textSetAlignment[text](4);                 // MiddleCenter
    if (G.textSetFontStyle.IsValid()) G.textSetFontStyle[text](g_customFont ? 0 : 2);   // Londrina is already chunky; the fallback font is italic
    if (G.textSetBestFit.IsValid()) {            // shrink long labels to fit their button
        G.textSetBestFit[text](true);
        G.textSetMinSize[text](1);
        G.textSetMaxSize[text](size);
    }
    G.graphicSetColor[text](Color(1, 1, 1, 1));
    if (textOut) textOut->set(text);
    return go;
}

// A world-space canvas (1 unit = 1 mm) under parent.
static Il2CppObject *Canvas(Il2CppObject *parentT, Vector3 pos, float w, float h) {
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("StarterMenuText"));
    Il2CppObject *canvas = go && G.tCanvas ? G.goAddComponent[go](G.tCanvas) : nullptr;
    if (!canvas) return nullptr;
    G.canvasSetRenderMode[canvas](2);            // WorldSpace
    Il2CppObject *ct = Tr(go);
    G.trSetParent[ct](parentT, false);
    G.rtSetSizeDelta[ct](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[ct](pos);
    G.trSetLocalRotation[ct](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[ct](Vector3(0.001f, 0.001f, 0.001f));
    return go;
}

static Il2CppObject *Part(Il2CppObject *parentT, Color c, Vector3 pos, Vector3 scale) {
    Il2CppObject *o = Cube(c, false);
    if (!o) return nullptr;
    Il2CppObject *t = Tr(o);
    G.trSetParent[t](parentT, false);
    G.trSetLocalPosition[t](pos);
    G.trSetLocalScale[t](scale);
    return o;
}

// Where the ball sits on your hand (ii's four pointer positions).
static const Vector3 kPointerSpots[] = {Vector3(0.01f, -0.017f, 0.19f), Vector3(0, -0.1f, 0.05f),
                                        Vector3(0, 0.1f, -0.05f), Vector3(0, 0.0666f, 0.1f)};
static int g_pointerSpot = 0;

static void PointerToHand() {
    Il2CppObject *p = g_pointer.get(), *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get();
    if (!Alive(p) || !hand) return;
    Il2CppObject *t = Tr(p);
    Vector3 at = kPointerSpots[g_pointerSpot];
    if (f_rightHand) at.x = -at.x;
    G.trSetParent[t](hand, false);
    G.trSetLocalPosition[t](at);
}
static void NextPointer() { g_pointerSpot = (g_pointerSpot + 1) % 4; PointerToHand(); Notify("Pointer position %d", g_pointerSpot + 1); }

// ---- Yeeps decorations: candy-cane border, candy canes, pumpkins ------------------------
// Plain primitives with no colliders, parented to the board, built once with the menu and
// destroyed with it. A hard cap (kMaxDecor) keeps the object count fixed.
static constexpr int kMaxDecor = 72;
static int g_decorN = 0;
static Quaternion RotZ(float rad) { return Quaternion(0, 0, sinf(rad * 0.5f), cosf(rad * 0.5f)); }
// prim: 0 sphere, 2 cylinder, 3 cube. Positions are in the board's own space.
static void Decor(Il2CppObject *boardT, int prim, Color c, Vector3 pos, Vector3 scale, float roll = 0) {
    if (g_decorN >= kMaxDecor) return;
    Il2CppObject *o = Prim(prim, c, false);
    if (!o) return;
    g_decorN++;
    Il2CppObject *t = Tr(o);
    G.trSetParent[t](boardT, false);
    G.trSetLocalPosition[t](pos);
    G.trSetLocalRotation[t](RotZ(roll));
    G.trSetLocalScale[t](scale);
}
static void CandyCane(Il2CppObject *bt, float x, float y, float dir) {      // dir +1: the hook bends toward +x
    const Color white(0.97f, 0.97f, 1.0f, 1), red(0.85f, 0.10f, 0.12f, 1);
    const float z = -0.02f, h = 0.08f, d = 0.012f, R = 0.014f;
    Decor(bt, 2, white, Vector3(x, y, z), Vector3(d, h / 2, d));
    for (int i = 0; i < 3; i++)
        Decor(bt, 2, red, Vector3(x, y - 0.024f + i * 0.024f, z), Vector3(d * 1.07f, 0.005f, d * 1.07f));
    const float top = y + h / 2;
    for (int k = 1; k <= 4; k++) {                                         // the hook: half a circle of balls
        float a = 3.14159265f * 0.25f * k;
        Decor(bt, 0, k % 2 ? red : white, Vector3(x + dir * (R - R * cosf(a)), top + R * sinf(a), z), Vector3(d, d, d));
    }
}
static void Pumpkin(Il2CppObject *bt, float x, float y) {
    const Color orange(1.0f, 0.45f, 0.05f, 1), dark(0.78f, 0.28f, 0.02f, 1), green(0.2f, 0.6f, 0.15f, 1), face(0.08f, 0.04f, 0.0f, 1);
    const float z = -0.02f, front = z - 0.0245f;
    Decor(bt, 0, dark, Vector3(x - 0.012f, y, z + 0.006f), Vector3(0.034f, 0.040f, 0.04f));      // ribs behind
    Decor(bt, 0, dark, Vector3(x + 0.012f, y, z + 0.006f), Vector3(0.034f, 0.040f, 0.04f));
    Decor(bt, 0, orange, Vector3(x, y, z), Vector3(0.05f, 0.042f, 0.05f));                       // body
    Decor(bt, 3, green, Vector3(x + 0.002f, y + 0.025f, z), Vector3(0.008f, 0.014f, 0.008f), 0.3f);   // stem
    Decor(bt, 3, face, Vector3(x - 0.010f, y + 0.005f, front), Vector3(0.008f, 0.008f, 0.004f), 0.785f);   // eyes
    Decor(bt, 3, face, Vector3(x + 0.010f, y + 0.005f, front), Vector3(0.008f, 0.008f, 0.004f), 0.785f);
    Decor(bt, 3, face, Vector3(x, y - 0.009f, front), Vector3(0.022f, 0.006f, 0.004f));          // mouth
}
static void BuildDecor(Il2CppObject *bt) {
    g_decorN = 0;
    const Color white(0.97f, 0.97f, 1.0f, 1), red(0.85f, 0.10f, 0.12f, 1);
    const float hw = kBoardW / 2 + 0.004f, hh = kBoardH / 2 + 0.004f, bar = 0.008f, z = -0.002f;
    // Candy-cane border: four white bars with red stripes across them
    Decor(bt, 3, white, Vector3(0, hh, z), Vector3(kBoardW + 2 * bar, bar, 0.012f));
    Decor(bt, 3, white, Vector3(0, -hh, z), Vector3(kBoardW + 2 * bar, bar, 0.012f));
    Decor(bt, 3, white, Vector3(-hw, 0, z), Vector3(bar, kBoardH, 0.012f));
    Decor(bt, 3, white, Vector3(hw, 0, z), Vector3(bar, kBoardH, 0.012f));
    for (int i = 0; i < 6; i++) {
        float x = -kBoardW / 2 + (i + 0.5f) * (kBoardW / 6);
        Decor(bt, 3, red, Vector3(x, hh, z), Vector3(0.022f, bar * 1.05f, 0.0125f), 0.0f);
        Decor(bt, 3, red, Vector3(x, -hh, z), Vector3(0.022f, bar * 1.05f, 0.0125f), 0.0f);
    }
    for (int i = 0; i < 8; i++) {
        float y = -kBoardH / 2 + (i + 0.5f) * (kBoardH / 8);
        Decor(bt, 3, red, Vector3(-hw, y, z), Vector3(bar * 1.05f, 0.024f, 0.0125f), 0.0f);
        Decor(bt, 3, red, Vector3(hw, y, z), Vector3(bar * 1.05f, 0.024f, 0.0125f), 0.0f);
    }
    float caneX = g_barkLayout ? kBoardW / 2 - 0.035f : kSideX, caneY = g_barkLayout ? kBoardH / 2 + 0.03f : kTitleY;
    CandyCane(bt, -caneX, caneY, 1);
    CandyCane(bt, caneX, caneY, -1);
    Pumpkin(bt, -caneX, -kBoardH / 2 - 0.024f);
    Pumpkin(bt, caneX, -kBoardH / 2 - 0.024f);
    ym::log("clientdabell: %d decorations", g_decorN);
}

// Built once per room; afterwards only shown/hidden, moved and relabeled.
static void BuildMenu() {
    auto NewObject = [](const char *name) {
        return Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString(name));
    };
    Il2CppObject *menu = NewObject("ClientDaBellMenu"), *board = NewObject("ClientDaBellMenuBoard");
    if (!menu || !board) return;
    g_menu.set(menu);
    g_board.set(board);
    Il2CppObject *bt = Tr(board);
    G.trSetParent[bt](Tr(menu), false);
    // Like ii's menu: the board stands 10 cm out from the hand on its +x side, facing +x.
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));

    g_outline.set(Part(bt, T().title, Vector3(0, 0, 0.004f), Vector3(kBoardW + 0.008f, kBoardH + 0.008f, 0.004f)));
    g_bg.set(Part(bt, T().bgA, Vector3(0, 0, 0), Vector3(kBoardW, kBoardH, 0.01f)));
    for (int r = 0; r < kRows; r++)
        g_rowCube[r].set(Part(bt, T().button, Vector3(RowX(r), RowY(r), -0.005f), Vector3(kBtnW, kBtnH, 0.009f)));
    g_prev.set(Part(bt, T().button, Vector3(-kSideX, 0, 0), Vector3(kSideW, kSideH, 0.009f)));
    // The title sits on its own dark bar above the board (where ii's Disconnect button was)
    // (Bark Mode: the title is written right on the panel, no bar)
    if (!g_barkLayout) g_titleBar.set(Part(bt, T().buttonOn, Vector3(0, kTitleY, -0.002f), Vector3(kBtnW, 0.045f, 0.009f)));
    g_next.set(Part(bt, T().button, Vector3(kSideX, 0, 0), Vector3(kSideW, kSideH, 0.009f)));
    g_homeCube.set(Part(bt, T().button, Vector3(0, kHomeY, -0.005f), Vector3(kHomeW, kHomeH, 0.009f)));

    if (Il2CppObject *canvas = Canvas(bt, Vector3(0, 0, -0.0105f), std::max(kBoardW, 2 * kSideX + kSideW), kBoardH + 0.2f)) {
        Il2CppObject *ct = Tr(canvas);
        Label(ct, "ClientDaBell", Vector3(0, kTitleY, 0), g_barkLayout ? 0.4f : kBtnW, g_barkLayout ? 0.06f : 0.045f, g_titleFont, &g_title);
        for (int r = 0; r < kRows; r++)
            g_rowLabel[r].set(Label(ct, "", Vector3(RowX(r), RowY(r), 0), kBtnW - 0.01f, kBtnH, g_btnFont, &g_rowText[r]));
        Label(ct, "<", Vector3(-kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr);
        Label(ct, ">", Vector3(kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr);
        g_homeLabel.set(Label(ct, "Home", Vector3(0, kHomeY, 0), kHomeW, kHomeH, g_btnFont, nullptr));
        Label(ct, "", Vector3(0, -kBoardH / 2 - (g_barkLayout ? 0.04f : 0.09f), 0), kBoardW, 0.03f, 17, &g_tipText);
        if (g_barkLayout) Label(ct, CDB_VERSION, Vector3(kBoardW / 2 - 0.07f, kBoardH / 2 - 0.025f, 0), 0.12f, 0.025f, 16, nullptr);   // top-right corner
        else Label(ct, CDB_VERSION, Vector3(0, -kBoardH / 2 + 0.012f, 0), kBoardW, 0.02f, 11, nullptr);
    }
    BuildDecor(bt);
    // Pointer: a small ball on the index finger of the other hand
    if (Il2CppObject *p = Prim(0, T().bgB, false)) {
        G.trSetLocalScale[Tr(p)](Vector3(0.01f, 0.01f, 0.01f));
        g_pointer.set(p);
        PointerToHand();
    }
    // Notifications: a line of text low in your view, like ii's notifications
    if (Il2CppObject *n = Canvas(g_head.get(), Vector3(0, -0.12f, 0.6f), 0.5f, 0.05f)) {
        g_notifyGo.set(n);
        Label(Tr(n), "", Vector3(0, 0, 0), 0.5f, 0.05f, 22, &g_notifyText);
        G.goSetActive[n](false);
    }
    G.goSetActive[menu](false);
    Show(g_pointer, false);
    g_built = true;
    g_dirty = true;
}

static void Notify(const char *fmt, ...) {
    if (!f_notify || !Alive(g_notifyGo.get())) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SetText(g_notifyText, buf);
    G.goSetActive[g_notifyGo.get()](true);
    g_notifyUntil = g_time + 2.0;
}

// Relabels and recolors the rows. Only runs after something changed.
static void RenderPage() {
    BuildView();
    for (int r = 0; r < kRows; r++) {
        int i = g_page * kRows + r;
        bool used = i < (int)g_view.size();
        Show(g_rowCube[r], used);
        Show(g_rowLabel[r], used);
        if (!used) continue;
        Item *it = g_view[i];
        bool on = it->kind == TOGGLE && *it->flag;
        if (f_colorBlind) {
            // Color Blind Mode: "on" doesn't depend on telling two shades apart. On buttons
            // say [ON] and are orange with near-black text (orange against the blue themes is
            // the pair the common kinds of color blindness all still tell apart, and the
            // brightness differs a lot too); a press flashes white.
            char lab[96];
            snprintf(lab, sizeof lab, on ? "[ON] %s" : "%s", it->label);
            SetText(g_rowText[r], lab);
            SetColor(g_rowCube[r].get(), r == g_flashRow ? Color(1, 1, 1, 1) : on ? Color(0.90f, 0.62f, 0.0f, 1) : T().button);
            if (Alive(g_rowText[r].get()))
                G.graphicSetColor[g_rowText[r].get()](r == g_flashRow || on ? Color(0.08f, 0.08f, 0.08f, 1) : Color(1, 1, 1, 1));
            continue;
        }
        SetText(g_rowText[r], it->label);
        SetColor(g_rowCube[r].get(), r == g_flashRow ? Color(1, 0.9f, 0.6f, 1) : on ? T().buttonOn : T().button);
        if (Alive(g_rowText[r].get())) G.graphicSetColor[g_rowText[r].get()](on ? Color(1, 1, 1, 1) : T().title);
    }
    bool pages = PageCount() > 1;
    Show(g_prev, pages);
    Show(g_next, pages);
    SetColor(g_prev.get(), T().button);
    SetColor(g_next.get(), T().button);
    Show(g_homeCube, g_cat != MAIN);
    Show(g_homeLabel, g_cat != MAIN);
    SetColor(g_homeCube.get(), g_flashRow == -3 ? (f_colorBlind ? Color(1, 1, 1, 1) : Color(1, 0.9f, 0.6f, 1)) : T().button);
    SetColor(g_outline.get(), T().title);
    SetColor(g_titleBar.get(), T().buttonOn);
    SetColor(g_pointer.get(), T().bgB);
    if (Alive(g_title.get())) G.graphicSetColor[g_title.get()](Color(1, 1, 1, 1));
    g_nextTitle = 0;                     // refresh the title now
    g_dirty = false;
}

static int Hex(float v) { return (int)(v < 0 ? 0 : v > 1 ? 255 : v * 255); }

// The title is "ClientDaBell" with a moving plush-blue gradient, one color per letter
// (rich text). Rebuilt ten times a second, not every frame.
static void UpdateTitle() {
    if (g_time < g_nextTitle) return;
    g_nextTitle = g_time + 0.1;
    static const Color kFire[] = {Color(0.20f, 0.50f, 1.0f, 1), Color(0.45f, 0.75f, 1.0f, 1), Color(0.85f, 0.95f, 1.0f, 1), Color(0.45f, 0.75f, 1.0f, 1)};   // plush blue shimmer
    char buf[1024];
    int n = 0;
    const char *word = "ClientDaBell";
    for (int i = 0; word[i]; i++) {
        float t = (float)g_time * 1.5f - i * 0.35f;
        Color c;
        if (f_rainbow) c = Rainbow(t * 0.2f);
        else {
            float f = t - floorf(t / 4) * 4;          // 0..4 along the fire colors
            int k = (int)f;
            c = Lerp(kFire[k], kFire[(k + 1) % 4], f - k);
        }
        n += snprintf(buf + n, sizeof buf - n, "<b><color=#%02X%02X%02X>%c</color></b>", Hex(c.r), Hex(c.g), Hex(c.b), word[i]);
    }
    n += snprintf(buf + n, sizeof buf - n, " <color=grey>[</color><color=white>%d</color><color=grey>]</color>", g_page + 1);
    if (f_fps) n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d fps</size>", (int)(g_fps + 0.5f));
    if (f_clock) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d:%02d</size>", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    }
    SetText(g_title, buf);
}

static void ShowMenu(bool show) {
    if (show == g_menuShown) return;
    g_menuShown = show;
    if (show) { g_openTime = g_time; g_dirty = true; g_hover = -2; SetText(g_tipText, ""); g_barkPlaced = false; }
    Show(g_menu, show);
    Show(g_pointer, show);
    g_touching = -2;
}

static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Like ii's menu: the menu follows your hand and is tipped 45 degrees, so you read it
// by turning your palm toward your face. "Floating Menu" instead keeps the board above
// your hand, always facing you.
static void PlaceMenu() {
    Il2CppObject *menu = g_menu.get(), *board = g_board.get(), *head = g_head.get();
    Il2CppObject *hand = f_rightHand ? g_rightHand.get() : g_leftHand.get();
    if (!Alive(menu) || !Alive(board) || !hand || !head) return;
    Il2CppObject *t = Tr(menu), *bt = Tr(board);
    if (g_barkLayout) {                                  // Bark Mode: put once in front of you when it opens, then it stays
        if (g_barkPlaced) return;
        Vector3 hp = G.trGetPosition[head](), f = G.trGetForward[head]();
        f.y = 0;
        float n = sqrtf(f.x * f.x + f.z * f.z);
        if (n < 1e-4f) return;
        f = f * (1.0f / n);
        Vector3 center = hp + f * 0.55f + Vector3(0, -0.18f, 0);
        Vector3 tilt = f + Vector3(0, -0.35f, 0);           // tipped back a little, toward your eyes
        // Move the menu object itself and keep the panel at its center (local values only:
        // the menu object is scaled down to nothing for the pop-open, and setting a world
        // position under a zero scale sends the panel off to nowhere).
        G.trSetLocalScale[t](Vector3(1, 1, 1));
        G.trSetPosition[t](center);
        G.trSetRotation[t](Quaternion::LookRotation(tilt));
        G.trSetLocalPosition[bt](Vector3(0, 0, 0));
        G.trSetLocalRotation[bt](Quaternion(0, 0, 0, 1));
        g_barkPlaced = true;
        return;
    }
    Vector3 handPos = G.trGetPosition[hand]();
    if (!G.trGetRotation.IsValid()) {                  // fallback: float above the hand, facing you
        Vector3 center = handPos + Vector3(0, 0.25f, 0), away = center - G.trGetPosition[head]();
        if (away.x * away.x + away.y * away.y + away.z * away.z < 1e-6f) return;
        G.trSetPosition[t](handPos);
        G.trSetPosition[bt](center);
        G.trSetRotation[bt](Quaternion::LookRotation(away));
        return;
    }
    const float s = 0.38268343f, c = 0.92387953f;                  // sin/cos of 22.5 degrees
    Quaternion rot = G.trGetRotation[hand]() * Quaternion(-s, 0, 0, c);   // tip -45 degrees on x
    if (f_rightHand) rot = rot * Quaternion(0, 0, 1, 0);           // mirrored: 180 degrees on z
    G.trSetPosition[t](handPos);
    G.trSetRotation[t](rot);
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));
}

// Bark Mode switched: the menu is torn down here and built again (with the other layout)
// at the start of the next frame, so nothing is destroyed while it's being pressed.
static void BarkModeToggled() { g_rebuildMenu = true; }
static void RebuildMenuNow() {
    g_rebuildMenu = false;
    g_rebuiltNote = true;
    for (int r = 0; r < kRows; r++) { g_rowCube[r].clear(); g_rowLabel[r].clear(); g_rowText[r].clear(); }
    for (Ref *r : {&g_board, &g_bg, &g_outline, &g_title, &g_titleBar, &g_prev, &g_next, &g_notifyText, &g_tipText, &g_homeCube, &g_homeLabel}) r->clear();
    for (Ref *r : {&g_menu, &g_pointer, &g_notifyGo}) Kill(*r);
    g_built = false; g_menuShown = false; g_touching = -2; g_hover = -2;
    g_barkOpen = false;
    SetLayout(f_barkMode);
}

static void ScanCatalog();
static void Press(Item *it) {
    g_lastPressed = it;
    switch (it->kind) {
    // A button's own code is run inside a safety net: if it fails, that switch goes back off and
    // you get a message, instead of the error stopping the whole menu.
    case OPEN:
        g_cat = it->target;
        g_page = 0;
        if (it->target == SOUNDBOARD) { try { ScanSongs(); } catch (...) { ym::log("clientdabell: song scan error"); } }
        if (it->target == SPAWNER) { try { ScanCatalog(); } catch (...) { ym::log("clientdabell: item list error"); } }
        break;
    case TOGGLE:
        *it->flag = !*it->flag;
        try { if (it->fn) it->fn(); }
        catch (...) {
            *it->flag = false;
            try { if (it->fn) it->fn(); } catch (...) {}
            ym::log("clientdabell: %s error, turned off", it->label);
            Notify("%s had an error, turned off", it->label);
            break;
        }
        Notify("[%s] %s", *it->flag ? "ON" : "OFF", it->label);
        break;
    case ACTION:
        try { if (it->fn) it->fn(); }
        catch (...) { ym::log("clientdabell: %s error", it->label); Notify("%s had an error", it->label); }
        break;
    }
    g_dirty = true;
    ym::log("clientdabell: %s", it->label);
}

// Which part of the menu the pointer touches: row 0..7, -1 = <, kRows = >, -2 = nothing.
static int Touching() {
    Il2CppObject *board = g_board.get(), *pointer = g_pointer.get();
    if (!Alive(board) || !Alive(pointer)) return -2;
    Vector3 p = G.trInverseTransformPoint[Tr(board)](G.trGetPosition[Tr(pointer)]());
    if (p.z < -0.035f || p.z > 0.015f) return -2;
    if (g_cat != MAIN && fabsf(p.x) < kHomeW / 2 && fabsf(p.y - kHomeY) < kHomeH / 2) return -3;
    if (fabsf(fabsf(p.x) - kSideX) < kSideW / 2 && fabsf(p.y) < kSideH / 2 && PageCount() > 1)
        return p.x < 0 ? -1 : kRows;
    for (int r = 0; r < kRows; r++)
        if (fabsf(p.x - RowX(r)) < kBtnW / 2 && fabsf(p.y - RowY(r)) < kBtnH / 2) return r;
    return -2;
}

static void Buzz() {
    if (!G.haptic.IsValid()) return;
    XRDevice d = G.deviceAtNode(f_rightHand ? 4 : 5);    // the hand that presses
    if (d.initialized) G.haptic(d.id, 0u, 0.35f, 0.04f);
}

static void CheckPress() {
    int hit = Touching();
    if (hit != g_hover) {                                 // tooltip for the button under your finger
        g_hover = hit;
        int i = g_page * kRows + hit;
        if (hit >= 0 && hit < kRows && i < (int)g_view.size()) SetText(g_tipText, TipFor(g_view[i]->label));
    }
    if (hit != -2 && hit != g_touching && g_time >= g_nextPress) {   // one press per touch
        g_nextPress = g_time + 0.25;
        Buzz();
        if (hit >= 0 && hit < kRows) { g_flashRow = hit; g_flashUntil = g_time + 0.12; }
        if (hit == -3) {                                  // Home: back to the main page
            g_cat = MAIN;
            g_page = 0;
            g_flashRow = -3; g_flashUntil = g_time + 0.12;
            g_dirty = true;
        } else if (hit == -1 || hit == kRows) {
            int n = PageCount();
            g_page = (g_page + (hit == kRows ? 1 : n - 1)) % n;
            g_dirty = true;
        } else {
            int i = g_page * kRows + hit;
            if (i < (int)g_view.size()) Press(g_view[i]);
        }
    }
    g_touching = hit;
}

static void AnimateMenu() {
    float open = (float)(g_time - g_openTime) / 0.15f;     // pop open like ii's menu
    if (open <= 1.05f && Alive(g_menu.get())) {
        float k = open >= 1 ? 1 : 1 - (1 - open) * (1 - open);
        G.trSetLocalScale[Tr(g_menu.get())](Vector3(k, k, k));
    }
    if (g_flashRow != -2 && g_time >= g_flashUntil) { g_flashRow = -2; g_dirty = true; }
    float t = 0.5f + 0.5f * sinf((float)g_time * 2.0f);
    Color c = f_rainbow ? Rainbow((float)g_time * 0.15f) : Lerp(T().bgA, T().bgB, t);
    SetColor(g_bg.get(), c);
}

// ---- features (local only) -----------------------------------------------------

static Il2CppObject *PlayerTransform() {
    Il2CppObject *player = g_player.get();
    return player ? G.compTransform[player]() : nullptr;
}

// Your body's velocity, through the game's own player functions (Rigidbody as backup).
static bool GetVel(Vector3 &v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plGetVelocity.IsValid()) { v = G.plGetVelocity[player](); return true; }
    if (rb && G.rbGetVelocity.IsValid()) { v = G.rbGetVelocity[rb](); return true; }
    return false;
}
static void SetVel(Vector3 v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plSetVelocity.IsValid()) G.plSetVelocity[player](v);
    else if (rb && G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](v);
}

// Where your body is (the body collider), which is what teleports aim for.
static bool BodyPos(Vector3 &out) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    if (!player || !t) return false;
    Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
    out = body ? G.trGetPosition[G.compTransform[body]()]() : G.trGetPosition[t]();
    return true;
}
static void StopMoving() { SetVel(Vector3(0, 0, 0)); }
// Moves your body to pos, using the game's own TeleportTo when it has one.
static void TeleportPlayer(Vector3 pos) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    Vector3 body;
    if (!player || !t || !BodyPos(body)) return;
    Vector3 target = pos - body + G.trGetPosition[t]();
    if (G.teleportTo.IsValid()) G.teleportTo[player](target);
    else G.trSetPosition[t](target);
    StopMoving();
}

static void SaveSpot() {
    if (BodyPos(g_spot)) { g_haveSpot = true; Notify("Spot saved"); }
}
static void GoToSpot() {
    if (!g_haveSpot) { Notify("Save a spot first"); return; }
    TeleportPlayer(g_spot);
    Notify("Teleported to your spot");
}

static const float kIronSpeeds[] = {15, 12, 8, 4};
static int g_ironSpeed = 0;
static void NextIronSpeed() { g_ironSpeed = (g_ironSpeed + 1) % 4; Notify("Iron Man speed: %d", (int)kIronSpeeds[g_ironSpeed]); }
static void NextTheme() { g_theme = (g_theme + 1) % kThemeCount; Notify("Theme: %s", T().name); }
static void AllOff() {
    for (auto &it : g_items) if (it.kind == TOGGLE && it.flag != &f_notify && it.flag != &f_soundMic && it.flag != &f_colorBlind && it.flag != &f_barkMode) *it.flag = false;   // ticks undo each mod
    g_dance = -1;
    g_dupeArmed = 0;
    f_modsPaused = false;                           // nothing to bring back after this
    g_pausedMods.clear();
    g_wx = -1;
    g_tod = -1;
    StopSound();
    g_itemQueue.clear();
    PointerToHand();
    Notify("Everything off");
}

static void FlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *t = PlayerTransform();
    if (!f_fly || !in.a || !head || !t) return;
    Vector3 pos = G.trGetPosition[t]() + G.trGetForward[head]() * ((f_fastFly ? 20.0f : 8.0f) * dt);
    G.trSetPosition[t](pos);
    StopMoving();
}

// Thumbstick Fly: you hover, and the sticks move you (left: forward/back and sideways where
// you look, right: up/down). Moves your body like Fly does, so the room sees you fly.
static void StickFlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *t = PlayerTransform();
    if (!f_stickFly || !head || !t) return;
    float speed = f_fastFly ? 20.0f : 8.0f;
    Vector3 move = G.trGetForward[head]() * in.lstick.y + G.trGetRight[head]() * in.lstick.x + Vector3(0, in.rstick.y, 0);
    G.trSetPosition[t](G.trGetPosition[t]() + move * (speed * dt));
    StopMoving();
    Il2CppObject *player = g_player.get();
    if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.1f);
}

// Dash: one quick burst where you look on each right-thumbstick click.
static bool g_dashPrev = false;
static double g_nextDash = 0;
static void DashTick(const Buttons &in) {
    bool press = in.rclick && !g_dashPrev;
    g_dashPrev = in.rclick;
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    if (!f_dash || !press || !head || g_time < g_nextDash) return;
    g_nextDash = g_time + 0.5;
    SetVel(G.trGetForward[head]() * 16.0f);
    if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.15f);
}

static void LowGravityTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_lowGravity || f_noGravity || f_stickFly || (f_fly && in.a) || !GetVel(v)) return;
    SetVel(v + Vector3(0, 9.81f * 0.7f * dt, 0));       // cancels 70% of gravity
}

static void PlatformFor(bool held, Ref &platform, Ref &hand) {
    if (!held || !f_platforms) {
        Kill(platform);
        return;
    }
    if (platform.get()) return;                             // already standing on one
    if (!hand.get()) return;
    Il2CppObject *p = Cube(T().button, true);               // keeps its collider: you stand on it
    if (!p) return;
    // Hands only collide with the layers in the player's locomotionEnabledLayers (like
    // Gorilla Tag), so the platform goes on the first of those layers.
    static int layer = -2;
    if (layer == -2) {
        layer = -1;
        int mask = G.locomotionLayers.IsValid() && g_player.get() ? G.locomotionLayers[g_player.get()]() : 0;
        for (int i = 0; i < 32 && layer < 0; i++) if (mask & (1 << i)) layer = i;
        ym::log("clientdabell: hand layers 0x%x, platforms on layer %d", mask, layer);
    }
    if (layer >= 0 && G.goSetLayer.IsValid()) G.goSetLayer[p](layer);
    Il2CppObject *t = Tr(p);
    G.trSetPosition[t](G.trGetPosition[hand.get()]() - Vector3(0, 0.08f, 0));
    G.trSetLocalScale[t](Vector3(0.28f, 0.02f, 0.28f));
    platform.set(p);
}

static void VelocityFlyTick(const Buttons &in) {
    Il2CppObject *hand = g_rightHand.get();
    if (!f_velocityFly || !in.a || !hand) return;
    SetVel(G.trGetForward[hand]() * 20.0f);
}

static Vector3 Flat(Vector3 v);

// Car: like Fly, but along the ground. Holding A drives you the way you look, flat, and
// you hover a little above the ground (a raycast down finds it) so the bumps on blocks
// don't catch you. Off a ledge, with no ground close below, you fall normally.
static const float kCarSpeeds[] = {4.0f, 6.0f, 9.0f};
static const char *kCarNames[] = {"slow", "normal", "fast"};
static int g_carSpeed = 1;
static float g_carHeight = -1;               // your head's height above the ground, measured once
static void NextCarSpeed() { g_carSpeed = (g_carSpeed + 1) % 3; Notify("Car: %s", kCarNames[g_carSpeed]); }
static void CarTick(const Buttons &in) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    Vector3 v;
    if (!f_car) { g_carHeight = -1; return; }
    if (!in.a || !head || !player || !GetVel(v) || !G.raycast.IsValid()) return;
    int mask = G.locomotionLayers.IsValid() ? G.locomotionLayers[player]() : -5;
    Vector3 hp = G.trGetPosition[head]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool ground = G.raycast(hp, Vector3(0, -1, 0), &hit, 3.0f, mask ? mask : -5);
    Vector3 f = Flat(G.trGetForward[head]()) * kCarSpeeds[g_carSpeed];
    float vy = fminf(v.y, 1.0f);
    if (ground) {
        if (g_carHeight < 0) g_carHeight = hp.y - hit.point.y;      // your standing height
        float target = hit.point.y + g_carHeight + 0.15f;            // float 15 cm higher
        vy = (target - hp.y) * 8.0f;
        if (G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.1f);
    }
    SetVel(Vector3(f.x, vy, f.z));
}

static void IronManTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_ironMan || !G.trGetRight.IsValid() || (!in.x && !in.a) || !GetVel(v)) return;
    float speed = kIronSpeeds[g_ironSpeed] * dt;
    if (in.x && g_leftHand.get()) v = v + G.trGetRight[g_leftHand.get()]() * -speed;
    if (in.a && g_rightHand.get()) v = v + G.trGetRight[g_rightHand.get()]() * speed;
    SetVel(v);
}

// Speed Boost and No Gravity change your own player; turning them off puts the old
// values back. Checked every frame so they also come back after a room change.
static float g_oldJumpSpeed = 0, g_oldJumpMultiplier = 0;
static Il2CppObject *g_boosted = nullptr;      // which player has the boost (compared only)
static bool g_gravityOff = false;

static int g_boostLevel = 0;                   // 1 = Speed Boost, 2 = Super Speed
static void ApplySpeedBoost() {
    Il2CppObject *player = g_player.get();
    bool ok = G.maxJumpSpeed.IsValid() && G.jumpMultiplier.IsValid();
    int level = f_superSpeed ? 2 : f_speedBoost ? 1 : 0;
    if (level && ok && player) {
        if (g_boosted != player) {                 // first time on this player: remember the normal values
            g_oldJumpSpeed = G.maxJumpSpeed[player]();
            g_oldJumpMultiplier = G.jumpMultiplier[player]();
            g_boosted = player;
            g_boostLevel = 0;
        }
        if (g_boostLevel != level) {
            G.maxJumpSpeed[player] = level == 2 ? 20.0f : 9.0f;
            G.jumpMultiplier[player] = level == 2 ? 3.0f : 1.5f;
            g_boostLevel = level;
        }
    } else if (!level && g_boosted) {
        if (ok && player == g_boosted) {
            G.maxJumpSpeed[player] = g_oldJumpSpeed;
            G.jumpMultiplier[player] = g_oldJumpMultiplier;
        }
        g_boosted = nullptr;
        g_boostLevel = 0;
    }
}

static void ApplyGravity() {
    // The game's own "no gravity for a while" (what its levitation items use), renewed
    // every frame while the mod is on; it wears off by itself once you turn it off.
    Il2CppObject *player = g_player.get();
    if (f_noGravity && player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
    g_gravityOff = f_noGravity;
}

// Noclip: while you hold the right trigger you pass through everything. Yeeps moves
// your body with its own layer lists (locomotionEnabledLayers for your hands,
// collisionEnabledLayers for your body), so those are emptied, physics ignores your
// body against those layers, and your own colliders are off. Gravity stays normal, like
// Gorilla Tag noclip. All of it goes back when you let go. Only your own player changes; nothing is sent.
static bool g_noclipOn = false;
static Ref g_noclipOff[24];                    // your own colliders switched off during noclip
static int g_noclipOffN = 0;
// Every solid collider on your own player (body, head, hands...), switched off while
// noclip is held so nothing of you hits the level.
static void PlayerColliders(bool off) {
    if (!off) {
        for (int i = 0; i < g_noclipOffN; i++) {
            if (Alive(g_noclipOff[i].get())) G.colliderSetEnabled[g_noclipOff[i].get()](true);
            g_noclipOff[i].clear();
        }
        g_noclipOffN = 0;
        return;
    }
    Il2CppObject *player = g_player.get();
    if (!player || !G.colliderSetEnabled.IsValid()) return;
    auto offOne = [](Il2CppObject *c) {
        if (!Alive(c) || g_noclipOffN >= 24 || !G.behaviourGetEnabledCollider[c]()) return;
        if (G.colliderIsTrigger.IsValid() && G.colliderIsTrigger[c]()) return;
        for (int i = 0; i < g_noclipOffN; i++) if (g_noclipOff[i].get() == c) return;
        G.colliderSetEnabled[c](false);
        g_noclipOff[g_noclipOffN++].set(c);
    };
    // Your body and head colliders (fields on the player), then every solid collider in
    // the rig they belong to.
    Il2CppObject *own[2] = {G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr,
                            G.headCollider.IsValid() ? G.headCollider[player]() : nullptr};
    for (Il2CppObject *c : own) {
        if (!c) continue;
        offOne(c);
        Il2CppObject *rig = G.compGameObject[G.trGetRoot[G.compTransform[c]()]()]();
        auto *arr = rig && G.getComponentsInChildren.IsValid()
                        ? (uint8_t *)G.getComponentsInChildren[rig]((Il2CppObject *)G.tCollider, false) : nullptr;
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (size_t i = 0; i < n; i++) offOne(((Il2CppObject **)(arr + 0x20))[i]);
    }
}
static int g_savedLoco = 0, g_savedColl = 0, g_bodyLayer = -1;
static void NoclipTick(const Buttons &in) {
    Il2CppObject *player = g_player.get();
    bool want = f_noclip && in.rtrig && player;
    if (want == g_noclipOn) return;
    g_noclipOn = want;
    if (!player || !G.locomotionLayers.IsValid()) return;
    if (want) {
        g_savedLoco = G.locomotionLayers[player]();
        g_savedColl = G.collisionLayers.IsValid() ? G.collisionLayers[player]() : 0;
        Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
        Il2CppObject *bodyGo = body ? G.compGameObject[body]() : nullptr;
        g_bodyLayer = bodyGo && G.goGetLayer.IsValid() ? G.goGetLayer[bodyGo]() : -1;
    }
    int mask = g_savedLoco | g_savedColl;
    G.locomotionLayers[player] = want ? 0 : g_savedLoco;
    if (G.collisionLayers.IsValid()) G.collisionLayers[player] = want ? 0 : g_savedColl;
    if (g_bodyLayer >= 0 && G.ignoreLayers.IsValid())
        for (int l = 0; l < 32; l++)
            if (mask & (1 << l)) G.ignoreLayers(g_bodyLayer, l, want);
    PlayerColliders(want);
    if (want) ym::log("clientdabell: noclip on (layers 0x%x, body layer %d, %d colliders off)", mask, g_bodyLayer, g_noclipOffN);
}

// Teleport Gun: hold right grip to aim (a line and a ball show where you land), press
// the right trigger to go there.
static Ref g_gunLine, g_gunLineGo, g_gunBall;
static bool g_gunTrigger = false;
static void TeleportGunTick(const Buttons &in) {
    bool aiming = (f_tpGun || f_itemGun) && in.rgrip;
    if (!aiming) {
        Show(g_gunLineGo, false);
        Show(g_gunBall, false);
        g_gunTrigger = in.rtrig;
        return;
    }
    Il2CppObject *hand = g_rightHand.get();
    if (!hand || !G.raycast.IsValid() || !G.lrSetPosition.IsValid()) return;
    if (!g_gunBall.get()) {                              // made once per room
        Il2CppObject *ball = Prim(0, T().bgB, false);
        Il2CppObject *lineGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellGunLine"));
        Il2CppObject *line = lineGo && G.tLine ? G.goAddComponent[lineGo](G.tLine) : nullptr;
        if (!ball || !line) return;
        G.trSetLocalScale[Tr(ball)](Vector3(0.1f, 0.1f, 0.1f));
        if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
            if (G.shader.get()) G.matSetShader[m](G.shader.get());
            G.matSetColor[m](T().bgB);
        }
        G.lrSetStartWidth[line](0.025f);
        G.lrSetEndWidth[line](0.025f);
        G.lrSetPositionCount[line](2);
        G.lrSetUseWorldSpace[line](true);
        g_gunBall.set(ball);
        g_gunLine.set(line);
        g_gunLineGo.set(lineGo);
    }
    Vector3 start = G.trGetPosition[hand](), dir = G.trGetForward[hand]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool found = G.raycast(start + dir * 0.25f, dir, &hit, 512.0f, -5);   // Physics.DefaultRaycastLayers
    Vector3 end = found ? hit.point : start + dir * 512.0f;
    Show(g_gunLineGo, true);
    Show(g_gunBall, true);
    G.trSetPosition[Tr(g_gunBall.get())](end);
    G.lrSetPosition[g_gunLine.get()](0, start);
    G.lrSetPosition[g_gunLine.get()](1, end);
    SetColor(g_gunBall.get(), in.rtrig ? T().buttonOn : T().bgB);
    if (in.rtrig && !g_gunTrigger && found) {
        if (f_itemGun) {                                  // Spawn Item Gun: an item at that spot
            if (g_time >= g_nextItem && LeftHandItem()) {
                g_nextItem = g_time + 0.5;
                g_itemQueue.push_back({end + Vector3(0, 0.3f, 0), Vector3(0, 0, 0)});
            }
        } else TeleportPlayer(end + Vector3(0, 1.0f, 0));
    }
    g_gunTrigger = in.rtrig;
}

// ---- vector helpers ----------------------------------------------------------------

static Vector3 Cross(Vector3 a, Vector3 b) { return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static Vector3 Flat(Vector3 v) {                      // v without its up/down part, length 1
    v.y = 0;
    float l = sqrtf(v.x * v.x + v.z * v.z);
    return l > 1e-4f ? v * (1.0f / l) : Vector3(0, 0, 1);
}

// ---- rig mods: dances -----------------------------------------------------------
// While you hold the left trigger, your hands are moved through a dance instead of
// following your controllers. The game sends your hand positions to the room as
// usual, so other players see you dance. Nothing extra is sent.

// The dance buttons work like radio buttons: picking one turns the others off,
// pressing the picked one again turns dancing off.
static void DancePicked() {
    int picked = -1;
    for (int i = 0; i < kDances; i++) if (g_danceSel[i] && i != g_dance) picked = i;
    for (int i = 0; i < kDances; i++) g_danceSel[i] = (i == picked);
    g_dance = picked;
    if (picked >= 0) { Notify("Hold left trigger to do it"); ym::log("clientdabell: pose %d picked, hold left trigger", picked); }
}
static float Rand01() { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (x & 0xffff) / 65535.0f; }

// The controller trackers would put your hands back every frame, so they are paused
// while you dance (on the hand and the two objects above it) and turned back on after.
static Ref g_paused[12];
static int g_pausedN = 0;
static bool g_dancing = false;
static void PauseTrackers(bool pause) {
    if (!pause) {
        for (int i = 0; i < g_pausedN; i++) {
            if (Alive(g_paused[i].get())) G.behaviourSetEnabled[g_paused[i].get()](true);
            g_paused[i].clear();
        }
        g_pausedN = 0;
        return;
    }
    if (!G.behaviourSetEnabled.IsValid() || !G.trGetParent.IsValid()) return;
    for (Il2CppObject *t : {g_leftHand.get(), g_rightHand.get()})
        for (int up = 0; up < 3 && t; up++, t = G.trGetParent[t]())
            for (BNM::MonoType *type : G.trackerTypes) {
                Il2CppObject *go = G.compGameObject[t]();
                Il2CppObject *c = go ? G.goGetComponent[go]((Il2CppObject *)type) : nullptr;
                if (!c || !G.behaviourGetEnabled[c]() || g_pausedN >= 12) continue;
                G.behaviourSetEnabled[c](false);
                g_paused[g_pausedN++].set(c);
            }
}

static void DanceTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    bool want = g_dance >= 0 && in.ltrig && head && lh && rh;
    if (want != g_dancing) {
        PauseTrackers(want);
        g_dancing = want;
        if (want) ym::log("clientdabell: dancing, paused %d trackers", g_pausedN);
    }
    if (!want) return;
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), up(0, 1, 0);
    Vector3 r = Cross(up, f);                            // your right
    Vector3 shoulder = hp + up * -0.25f;
    float t = (float)g_time, s = sinf(t * 6), c = cosf(t * 6);
    Vector3 L, R;                                        // hand positions
    switch (g_dance) {
    case 0:   // Wave: right hand up and waving, left hand on the hip
        R = shoulder + r * (0.35f + 0.12f * s) + up * 0.45f + f * 0.1f;
        L = shoulder + r * -0.25f + up * -0.25f + f * 0.05f;
        break;
    case 1:   // Helicopter: both hands spin around you
        R = shoulder + r * (0.6f * cosf(t * 8)) + f * (0.6f * sinf(t * 8)) + up * 0.15f;
        L = shoulder - r * (0.6f * cosf(t * 8)) - f * (0.6f * sinf(t * 8)) + up * 0.15f;
        break;
    case 2:   // T-Pose
        R = shoulder + r * 0.7f + up * 0.1f;
        L = shoulder - r * 0.7f + up * 0.1f;
        break;
    case 3:   // Windmill: big circles at your sides
        R = shoulder + r * 0.4f + up * (0.5f * sinf(-t * 5)) + f * (0.5f * cosf(-t * 5));
        L = shoulder - r * 0.4f - up * (0.5f * sinf(-t * 5)) - f * (0.5f * cosf(-t * 5));
        break;
    case 4: {  // Swim: breaststroke
        float p = t * 3;
        R = shoulder + f * (0.3f + 0.25f * cosf(p)) + r * (0.08f + 0.35f * fabsf(sinf(p)));
        L = shoulder + f * (0.3f + 0.25f * cosf(p)) - r * (0.08f + 0.35f * fabsf(sinf(p)));
        break;
    }
    case 5: {  // Jumping jacks: arms from down by your sides to up over your head
        float a = (sinf(t * 6) * 0.5f + 0.5f) * 3.1f - 1.55f;
        R = shoulder + r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        L = shoulder - r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        break;
    }
    case 6:   // Spaz: hands in front, shaking everywhere
        R = shoulder + f * 0.35f + r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        L = shoulder + f * 0.35f - r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        break;
    case 8: {   // Floss: both arms swing side to side at your hips, one forward while the other goes back
        float sw = sinf(t * 9);
        R = shoulder - up * 0.38f + r * (0.38f * sw) + f * (0.08f + 0.22f * sw);
        L = shoulder - up * 0.38f + r * (0.38f * sw) - f * (0.08f + 0.22f * sw);
        break;
    }
    case 9:   // Dab: one arm bent across your face, the other stretched out and up
        R = hp + f * 0.2f + r * -0.05f + up * -0.08f;
        L = shoulder - r * 0.62f + up * 0.32f + f * 0.1f;
        break;
    case 10: { // Disco: point up, then point at the floor, to the beat
        bool beat = fmodf(t * 3.0f, 2.0f) < 1.0f;
        R = shoulder + r * (beat ? 0.3f : 0.4f) + up * (beat ? 0.55f : -0.45f);
        L = shoulder - r * 0.25f - up * 0.25f;
        break;
    }
    case 11: { // Clap
        float sp = 0.03f + 0.17f * (0.5f + 0.5f * sinf(t * 14));
        R = shoulder + f * 0.35f + r * sp;
        L = shoulder + f * 0.35f - r * sp;
        break;
    }
    case 12:   // Hype: fist pumps over your head
        R = shoulder + r * 0.3f + up * (0.3f + 0.35f * (0.5f + 0.5f * sinf(t * 9)));
        L = shoulder - r * 0.25f - up * 0.25f;
        break;
    case 13: { // Robot: stiff jumps from pose to pose
        int step = (int)(t * 2.5f) % 4;
        const float rx[4] = {0.45f, 0.2f, 0.45f, 0.1f}, ry[4] = {0.0f, 0.4f, -0.3f, 0.2f};
        R = shoulder + r * rx[step] + up * ry[step] + f * 0.15f;
        L = shoulder - r * rx[(step + 2) % 4] + up * ry[(step + 2) % 4] + f * 0.15f;
        break;
    }
    case 14:   // Cheer: both arms up and waving
        R = shoulder + r * (0.35f + 0.1f * s) + up * 0.55f;
        L = shoulder - r * (0.35f - 0.1f * s) + up * 0.55f;
        break;
    case 15:   // Salute
        R = hp + f * 0.14f + r * 0.13f + up * 0.04f;
        L = shoulder - r * 0.2f - up * 0.35f;
        break;
    case 16: { // Flex: both fists up by your head, pulsing
        float pu = 0.04f * sinf(t * 10);
        R = shoulder + r * (0.42f + pu) + up * (0.38f + pu);
        L = shoulder - r * (0.42f + pu) + up * (0.38f + pu);
        break;
    }
    case 17:   // Chicken Wings: elbows out, flapping
        R = shoulder + r * 0.4f + up * (0.12f + 0.12f * s);
        L = shoulder - r * 0.4f + up * (0.12f - 0.12f * s);
        break;
    default:  // Beyblade: hands spin fast around your waist
        R = shoulder - up * 0.3f + r * (0.5f * cosf(t * 20)) + f * (0.5f * sinf(t * 20));
        L = shoulder - up * 0.3f - r * (0.5f * cosf(t * 20)) - f * (0.5f * sinf(t * 20));
        break;
    }
    // Keep each hand within arm's reach of its shoulder, then glide there instead of
    // jumping, so the arms move smoothly for you and for everyone watching.
    auto reach = [&](Vector3 hand, Vector3 sh) {
        Vector3 d = hand - sh;
        float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        return len > 0.7f ? sh + d * (0.7f / len) : hand;
    };
    R = reach(R, shoulder + r * 0.18f);
    L = reach(L, shoulder - r * 0.18f);
    float k = g_dance == 6 ? 1.0f : fminf(1.0f, dt * 14.0f);     // Spaz stays twitchy
    G.trSetPosition[lh](G.trGetPosition[lh]() + (L - G.trGetPosition[lh]()) * k);
    G.trSetPosition[rh](G.trGetPosition[rh]() + (R - G.trGetPosition[rh]()) * k);
    Quaternion look = Quaternion::LookRotation(f, up);
    G.trSetRotation[lh](look);
    G.trSetRotation[rh](look);
}

// ---- items (everyone sees these) ------------------------------------------------
// Items are made with the game's own item message (RpcCreateItem on the room's
// NetworkMessenger), the same one the game sends when an item appears. Owner-approved
// 2026-10-31. Every way of spawning has a cooldown, and big batches are sent a few per
// frame, so the room can't be flooded.


static void Put(Il2CppObject *arr, size_t i, void *v) { ((void **)((uint8_t *)arr + 0x20))[i] = v; }

// Builds RpcCreateItem's arguments from its own parameter list, in the order the game
// uses: item id, item key, spawn kind 1, no owner, position, rotation, velocity, spin,
// properties, 1, 1.
static bool SendItem(const char *key, Vector3 pos, Quaternion rot, Vector3 vel, char *idOut = nullptr) {
    auto *mi = G.rpcCreateItem;
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!mi || !nm || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.tPhotonView) return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    // The last parameter (PhotonMessageInfo) is filled in by Photon on the receiving
    // side; it is never sent.
    int count = mi->parameters_count;
    if (count > 0 && TypeName(mi->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;
    char id[8];
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 4; i++) id[i] = kChars[(int)(Rand01() * 35.99f)];
    id[4] = 0;
    if (idOut) memcpy(idOut, id, 5);
    int strings = 0, vectors = 0, others = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(mi->parameters[i]);
        Class pc(mi->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;
        if (tn == "System.String") {
            if (strings == 0) v = CreateMonoString(id);
            else if (strings == 1) v = CreateMonoString(key);
            strings++;                                   // later strings stay null
        } else if (tn == "UnityEngine.Vector3") {
            Vector3 vec = vectors == 0 ? pos : vectors == 1 ? vel : Vector3(0, 0, 0);
            vectors++;
            v = G.valueBox(k, &vec);
        } else if (tn == "UnityEngine.Quaternion") {
            v = G.valueBox(k, &rot);
        } else if (tn == "System.String[]") {
            v = G.arrayNew(G.stringClass, 0);
        } else if (k && k->byval_arg.valuetype) {
            uint8_t raw[64] = {};
            raw[0] = 1;                                 // spawn kind 1 / the two bytes = 1
            // Photon can't send the game's enum types, only plain numbers (like the kit's
            // Int32 1), so an enum goes as its underlying number type.
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
            others++;
        }
        Put(args, i, v);
    }
    static bool loggedTypes = false;
    if (!loggedTypes) {                                   // once: what the game expects, for fixing
        loggedTypes = true;
        std::string sig;
        for (int i = 0; i < mi->parameters_count; i++) {
            Class pc(mi->parameters[i]);
            auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
            std::string tn = TypeName(mi->parameters[i]);
            if (tn.size() > 30) tn = k && k->byval_arg.valuetype ? (k->enumtype ? "enum" : "struct" + std::to_string(k->instance_size - 16)) : "class";
            sig += (i ? "," : "") + tn;
        }
        ym::log("clientdabell: item message takes %s", sig.c_str());
    }
    try {
        G.pvRpc[pv](CreateMonoString("RpcCreateItem"), 0, args);      // 0 = everyone in the room
    } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {   // the game said no: log why, don't crash the menu
        auto *ex = w.ex;
        auto *msg = ex ? (BNM::Structures::Mono::String *)ex->message : nullptr;
        auto *cls = ex ? (BNM::Structures::Mono::String *)ex->className : nullptr;
        ym::log("clientdabell: item message error %s: %s", cls ? cls->str().c_str() : "?", msg ? msg->str().c_str() : "?");
        return false;
    }
    return true;
}

static void QueueItems(const std::vector<PendingItem> &list) {
    if (!g_itemKey[0]) return;
    if (g_time < g_nextBatch) { Notify("Wait a moment"); return; }
    g_nextBatch = g_time + 3.0;
    for (auto &p : list) if (g_itemQueue.size() < 32) g_itemQueue.push_back(p);
}

// Item Rain: the game spawns items floating where they appear, so each rained item is found
// once it exists (by its ID) and dropped: the game's own drop call, then gravity on and a
// push down. The scene search runs 4 times a second, only while items are waiting.
struct DropWatch { char id[8]; double until; bool orbit; Vector3 pos; };
static void OrbitAdd(Il2CppObject *it);
static bool OrbitHas(Il2CppObject *it);
static std::vector<DropWatch> g_dropWatch;
static double g_nextDropScan = 0;
static void DropWatchAdd(const char *id, bool orbit = false, Vector3 pos = Vector3(0, 0, 0)) {
    if (g_dropWatch.size() >= 64) return;
    DropWatch w{};
    snprintf(w.id, sizeof w.id, "%s", id);
    w.until = g_time + 4.0;
    w.orbit = orbit;
    w.pos = pos;
    g_dropWatch.push_back(w);
}
static void DropItem(Il2CppObject *it) {
    try { if (G.itemDrop.IsValid()) G.itemDrop[it](); } catch (...) {}
    Il2CppObject *go = G.compGameObject[it]();
    Il2CppObject *rb = go && G.tRigidbody ? G.goGetComponent[go](G.tRigidbody) : nullptr;
    if (!rb && go && G.getComponentsInChildren.IsValid()) {
        auto *arr = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tRigidbody, true);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) rb = ((Il2CppObject **)(arr + 0x20))[0];
    }
    if (!Alive(rb)) return;
    if (G.rbSetKinematic.IsValid()) G.rbSetKinematic[rb](false);
    if (G.rbSetUseGravity.IsValid()) G.rbSetUseGravity[rb](true);
    G.rbSetVelocity[rb](Vector3(0, -1.5f, 0));
}
static void DropWatchTick() {
    if (g_dropWatch.empty() || g_time < g_nextDropScan) return;
    g_nextDropScan = g_time + 0.25;
    g_dropWatch.erase(std::remove_if(g_dropWatch.begin(), g_dropWatch.end(), [](const DropWatch &w) { return g_time > w.until; }),
                      g_dropWatch.end());
    if (g_dropWatch.empty() || !G.tItem || !G.itemId.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n && !g_dropWatch.empty(); i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it) || OrbitHas(it)) continue;
        auto *s = G.itemId[it]();
        std::string id = s ? s->str() : "";
        // Matched by its ID; or, if the game gave it a different ID, by being the same kind of
        // item sitting where one was just spawned (within 40 cm).
        bool sameKind = false;
        Vector3 ip;
        bool havePos = false;
        for (size_t k = 0; k < g_dropWatch.size(); k++) {
            bool match = !id.empty() && id == g_dropWatch[k].id;
            if (!match) {
                if (!havePos) {
                    havePos = true;
                    auto *ks = G.itemKey[it]();
                    sameKind = ks && ks->str() == g_itemKey;
                    ip = G.trGetPosition[G.compTransform[it]()]();
                }
                Vector3 d = ip - g_dropWatch[k].pos;
                match = sameKind && d.x * d.x + d.y * d.y + d.z * d.z < 0.4f * 0.4f;
            }
            if (!match) continue;
            if (g_dropWatch[k].orbit) OrbitAdd(it);
            else DropItem(it);
            g_dropWatch.erase(g_dropWatch.begin() + k);
            break;
        }
    }
}
static void ItemQueueTick() {
    DropWatchTick();
    for (int sent = 0; sent < 2 && !g_itemQueue.empty(); sent++) {   // 2 a frame at most
        PendingItem p = g_itemQueue.front();
        g_itemQueue.erase(g_itemQueue.begin());
        char id[8] = {};
        bool ok = SendItem(g_itemKey, p.pos, Quaternion(0, 0, 0, 1), p.vel, id);
        if (ok && (p.drop || p.orbit)) DropWatchAdd(id, p.orbit, p.pos);
        static int logged = 0;
        if (logged++ < 3) ym::log("clientdabell: spawn %s ok=%d", g_itemKey, ok ? 1 : 0);
        if (!ok) { g_itemQueue.clear(); Notify("Item spawning isn't ready in this room"); }
    }
}

// A grid in front of you: w across, d deep, h high. Never more than 27 in one batch (AGENTS.md),
// and QueueItems adds the 3 second cooldown and the cap on how many wait.
static void SpawnGridDims(int w, int d, int h) {
    Il2CppObject *head = g_head.get();
    if (!head || w < 1 || d < 1 || h < 1 || w * d * h > 27) return;
    Vector3 f = Flat(G.trGetForward[head]()), r = Cross(Vector3(0, 1, 0), f);
    Vector3 base = G.trGetPosition[head]() + f * 1.2f + Vector3(0, -0.4f, 0);
    std::vector<PendingItem> list;
    for (int y = 0; y < h; y++)
        for (int z = 0; z < d; z++)
            for (int x = 0; x < w; x++)
                list.push_back({base + r * ((x - (w - 1) * 0.5f) * 0.45f) + f * (z * 0.45f) + Vector3(0, y * 0.45f, 0), Vector3(0, 0, 0)});
    QueueItems(list);
}
static void SpawnGrid(int count) { SpawnGridDims(3, 3, count > 9 ? 3 : 1); }   // 3x3 or 3x3x3
// The item you're holding in your LEFT hand right now (owner 2026-10-31: items spawn as
// whatever is in your left hand when you press). One scene search per press.
static Il2CppObject *g_handItemObj = nullptr;   // the item LeftHandItem just found (use right away only)
static void SeeKey(const std::string &key);
static bool LeftHandItem() {
    g_handItemObj = nullptr;
    Il2CppObject *hand = g_leftHand.get();
    if (!hand || !G.tItem || !G.itemKey.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return false;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Vector3 hp = G.trGetPosition[hand]();
    Il2CppObject *best = nullptr;
    float bestD = 0.4f * 0.4f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it) || (G.itemHeld.IsValid() && !G.itemHeld[it]())) continue;
        Vector3 d = G.trGetPosition[G.compTransform[it]()]() - hp;
        float dd = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dd < bestD) { bestD = dd; best = it; }
    }
    auto *key = best ? G.itemKey[best]() : nullptr;
    if (!key) { Notify("Hold an item in your left hand"); return false; }
    snprintf(g_itemKey, sizeof g_itemKey, "%s", key->str().c_str());
    SeeKey(g_itemKey);
    g_handItemObj = best;
    return true;
}
static void ArmDupe9() { if (LeftHandItem()) SpawnGrid(9); }
static void ArmDupe27() { if (LeftHandItem()) SpawnGrid(27); }
static void ArmDupe9x3() { if (LeftHandItem()) SpawnGridDims(9, 3, 1); }   // 27 in a row of 9, three deep
// Item Rain: 32 copies of your left-hand item appear high above you, spread around, and fall.
// One shower per press, through the same queue as the duplicates (3 s between showers, at
// most 32 waiting, 2 sent per frame), so it never floods the room.
static void ItemRain() {
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) return;
    Vector3 hp = G.trGetPosition[head]();
    std::vector<PendingItem> list;
    for (int i = 0; i < 32; i++) {
        float a = Rand01() * 6.2831853f, r = 0.5f + 4.5f * sqrtf(Rand01());
        list.push_back({Vector3(hp.x + cosf(a) * r, hp.y + 5.0f + Rand01() * 4.0f, hp.z + sinf(a) * r), Vector3(0, -1.0f, 0), true});
    }
    double before = g_nextBatch;
    QueueItems(list);
    if (g_nextBatch != before) Notify("Item Rain!");
}

// Item Circle: 32 copies of your left-hand item in a ring around you at waist height, about
// 0.45 m apart (so 2.3 m out). 32 is as many as the queue holds; same limits as the duplicates.
static void ItemCircle() {
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) return;
    Vector3 hp = G.trGetPosition[head]();
    const int count = 32;
    const float radius = count * 0.45f / 6.2831853f;
    std::vector<PendingItem> list;
    for (int i = 0; i < count; i++) {
        float a = i * 6.2831853f / count;
        list.push_back({Vector3(hp.x + cosf(a) * radius, hp.y - 0.6f, hp.z + sinf(a) * radius), Vector3(0, 0, 0)});
    }
    double before = g_nextBatch;
    QueueItems(list);
    if (g_nextBatch != before) Notify("Item Circle!");
}

// Item Orbit: as many items as possible circle around you. The item in your left hand is let
// go (the game's own drop) and 31 copies of it are spawned (32 is as many as the item queue
// holds, with the same limits as the duplicates); each copy joins the orbit as soon as it
// appears. They're spread evenly around a circle at chest height that gets bigger with more
// items, bobbing and spinning. Their physics is paused while they orbit; turning Item Orbit off
// gives it back, so they drop. Grabbing one takes just that one out of the orbit.
static constexpr int kOrbitMax = 32;
static Ref g_orbitItem[kOrbitMax], g_orbitRb[kOrbitMax];
static int g_orbitN = 0;
static float g_orbitAng = 0;
static double g_orbitAdded[kOrbitMax];
static Il2CppObject *ItemRigidbody(Il2CppObject *it) {
    Il2CppObject *go = Alive(it) ? G.compGameObject[it]() : nullptr;
    Il2CppObject *rb = go && G.tRigidbody ? G.goGetComponent[go](G.tRigidbody) : nullptr;
    if (!rb && go && G.getComponentsInChildren.IsValid()) {
        auto *arr = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tRigidbody, true);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) rb = ((Il2CppObject **)(arr + 0x20))[0];
    }
    return Alive(rb) ? rb : nullptr;
}
// Collision off while an item orbits (so dragging it through walls, the floor and you doesn't
// make the game push it around), back on when it's let go.
static void ItemCollision(Il2CppObject *it, bool on) {
    Il2CppObject *go = Alive(it) ? G.compGameObject[it]() : nullptr;
    if (!go || !G.tCollider || !G.colliderSetEnabled.IsValid() || !G.getComponentsInChildren.IsValid()) return;
    auto *arr = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tCollider, true);
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n && i < 16; i++) {
        Il2CppObject *c = ((Il2CppObject **)(arr + 0x20))[i];
        if (Alive(c)) G.colliderSetEnabled[c](on);
    }
}
static void OrbitReleaseOne(int k) {                    // give item k its physics back and take it out
    try { ItemCollision(g_orbitItem[k].get(), true); } catch (...) {}
    Il2CppObject *rb = g_orbitRb[k].get();
    if (Alive(rb)) {
        if (G.rbSetKinematic.IsValid()) G.rbSetKinematic[rb](false);
        if (G.rbSetUseGravity.IsValid()) G.rbSetUseGravity[rb](true);
    }
    g_orbitItem[k].clear(); g_orbitRb[k].clear();
    for (int m = k; m + 1 < g_orbitN; m++) {            // close the gap (move the handles down)
        g_orbitItem[m].h = g_orbitItem[m + 1].h; g_orbitRb[m].h = g_orbitRb[m + 1].h; g_orbitAdded[m] = g_orbitAdded[m + 1];
    }
    if (g_orbitN > 0) { g_orbitItem[g_orbitN - 1].h = nullptr; g_orbitRb[g_orbitN - 1].h = nullptr; g_orbitN--; }
}
static void OrbitRelease() {
    while (g_orbitN > 0) { try { OrbitReleaseOne(g_orbitN - 1); } catch (...) { g_orbitItem[g_orbitN - 1].h = g_orbitRb[g_orbitN - 1].h = nullptr; g_orbitN--; } }
    for (auto &w : g_dropWatch) w.until = 0;           // copies still on their way won't join
}
static void OrbitAdd(Il2CppObject *it) {
    if (!f_itemOrbit || !Alive(it) || g_orbitN >= kOrbitMax) return;
    for (int k = 0; k < g_orbitN; k++) if (g_orbitItem[k].get() == it) return;
    int k = g_orbitN++;
    g_orbitItem[k].set(it);
    Il2CppObject *rb = ItemRigidbody(it);
    if (rb) {
        g_orbitRb[k].set(rb);
        if (G.rbSetKinematic.IsValid()) G.rbSetKinematic[rb](true);   // we move it, not physics
        G.rbSetVelocity[rb](Vector3(0, 0, 0));
    }
    try { ItemCollision(it, false); } catch (...) {}
    g_orbitAdded[k] = g_time;
}
static bool OrbitHas(Il2CppObject *it) {
    for (int k = 0; k < g_orbitN; k++) if (g_orbitItem[k].get() == it) return true;
    return false;
}
static float OrbitRadius() { return std::max(1.0f, g_orbitN * 0.35f / 6.2831853f); }   // about 0.35 m between items
static void ItemOrbitToggled() {
    if (!f_itemOrbit) { OrbitRelease(); Notify("Item Orbit off"); return; }
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem() || !Alive(g_handItemObj)) { f_itemOrbit = false; g_dirty = true; return; }   // LeftHandItem said why
    Il2CppObject *it = g_handItemObj;
    try { if (G.itemDrop.IsValid()) G.itemDrop[it](); } catch (...) {}   // let go of it
    OrbitAdd(it);
    g_orbitAng = 0;
    // 31 copies, spawned on the ring they'll orbit on; each joins once it appears
    Vector3 hp = G.trGetPosition[head]();
    float r = std::max(1.0f, kOrbitMax * 0.35f / 6.2831853f);
    std::vector<PendingItem> list;
    for (int i = 1; i < kOrbitMax; i++) {
        float a = i * 6.2831853f / kOrbitMax;
        PendingItem p{Vector3(hp.x + cosf(a) * r, hp.y - 0.35f, hp.z + sinf(a) * r), Vector3(0, 0, 0)};
        p.orbit = true;
        list.push_back(p);
    }
    double before = g_nextBatch;
    QueueItems(list);                                     // (says "Wait a moment" if you spawned items just now)
    Notify(g_nextBatch != before ? "Item Orbit on (adding copies...)" : "Item Orbit on (1 item, wait to add copies)");
}
static void ItemOrbitTick(float dt) {
    if (!f_itemOrbit) { if (g_orbitN) OrbitRelease(); return; }   // switched off some other way
    Il2CppObject *head = g_head.get();
    if (!head) { f_itemOrbit = false; g_dirty = true; OrbitRelease(); return; }
    // They keep spinning until you switch Item Orbit off. An item that's gone (despawned) is
    // taken out. Nothing is sent to the room while they spin (only the one "let go" at the start).
    for (int k = g_orbitN - 1; k >= 0; k--)
        if (!Alive(g_orbitItem[k].get())) OrbitReleaseOne(k);
    if (g_orbitN == 0) return;                          // nothing to spin right now (stays on until you switch it off)
    g_orbitAng += 1.6f * dt;                             // about one lap every 4 seconds
    if (g_orbitAng > 6.2831853f) g_orbitAng -= 6.2831853f;
    Vector3 hp = G.trGetPosition[head]();
    float r = OrbitRadius(), spin = (float)g_time * 3.0f;
    Quaternion rot(0, sinf(spin * 0.5f), 0, cosf(spin * 0.5f));
    for (int k = 0; k < g_orbitN; k++) {
        float a = g_orbitAng + k * 6.2831853f / g_orbitN;
        Vector3 pos(hp.x + cosf(a) * r, hp.y - 0.35f + 0.12f * sinf(a * 2.0f + (float)g_time), hp.z + sinf(a) * r);
        Il2CppObject *t = G.compTransform[g_orbitItem[k].get()]();
        G.trSetPosition[t](pos);
        G.trSetRotation[t](rot);
    }
    static double nextRecheck = 0;                       // twice a second: keep physics paused if the game turned it back on
    if (g_time >= nextRecheck) {
        nextRecheck = g_time + 0.5;
        for (int k = 0; k < g_orbitN; k++) {
            Il2CppObject *rb = g_orbitRb[k].get();
            if (Alive(rb) && G.rbSetKinematic.IsValid()) G.rbSetKinematic[rb](true);
        }
    }
}

// Spawn On Hand: each right-trigger press (grip open) spawns the item at your hand.
static bool g_onHandTrigger = false;
// Item Spawner: every Item the game has loaded (Resources.FindObjectsOfTypeAll also finds the
// item prefabs that aren't in the room), one entry per item kind. Built when the page opens.
static std::vector<std::string> g_seenKeys;               // every item kind seen this session (grows as you play)
static void SeeKey(const std::string &key) {
    if (key.empty() || key.size() >= 60 || g_seenKeys.size() >= 400) return;
    for (auto &k : g_seenKeys) if (k == key) return;
    g_seenKeys.push_back(key);
}
// Adds the kinds of all Items in an array; returns how many Items there were. A bad one
// (e.g. a template that has no key until it's spawned) is skipped, not the whole list.
static int CollectKeys(uint8_t *arr, int &errors) {
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it)) continue;
        try {
            auto *k = G.itemKey[it]();
            if (k) SeeKey(k->str());
        } catch (...) { errors++; }
    }
    return (int)n;
}
// The game's full item list. A real item in the room is used to find where the game keeps item
// names: each object the item points to (its item definition, usually) is checked for a text
// field that holds exactly this item's name. When one matches, that kind of object and that
// field are the game's item list: every loaded object of that kind gives one item name. Worked
// out once (when there's an item around to check); read every time the page opens.
static Ref g_defType;                                      // System.Type of the item definitions
static int g_defOffset = -1, g_defCount = 0;
static bool g_defFound = false;
static std::string g_defName;
struct Refl {
    bool ok = false;
    const char *(*className)(void *); void *(*getFields)(void *, void **); const char *(*fieldName)(void *);
    const void *(*fieldType)(void *); int (*fieldFlags)(void *); size_t (*fieldOffset)(void *); int (*typeKind)(const void *);
    void *(*getParent)(void *); const void *(*classType)(void *); Il2CppObject *(*typeObject)(const void *);
    bool (*isSub)(void *, void *, bool);
};
static Refl &Reflection() {
    static Refl r;
    static bool tried = false;
    if (tried) return r;
    tried = true;
    auto R = ym::api()->resolve;
    r.className = (decltype(r.className))R("il2cpp_class_get_name");
    r.getFields = (decltype(r.getFields))R("il2cpp_class_get_fields");
    r.fieldName = (decltype(r.fieldName))R("il2cpp_field_get_name");
    r.fieldType = (decltype(r.fieldType))R("il2cpp_field_get_type");
    r.fieldFlags = (decltype(r.fieldFlags))R("il2cpp_field_get_flags");
    r.fieldOffset = (decltype(r.fieldOffset))R("il2cpp_field_get_offset");
    r.typeKind = (decltype(r.typeKind))R("il2cpp_type_get_type");
    r.getParent = (decltype(r.getParent))R("il2cpp_class_get_parent");
    r.classType = (decltype(r.classType))R("il2cpp_class_get_type");
    r.typeObject = (decltype(r.typeObject))R("il2cpp_type_get_object");
    r.isSub = (decltype(r.isSub))R("il2cpp_class_is_subclass_of");
    r.ok = r.className && r.getFields && r.fieldName && r.fieldType && r.fieldFlags && r.fieldOffset && r.typeKind &&
           r.getParent && r.classType && r.typeObject && r.isSub;
    return r;
}
// Calls fn(offset) for every non-static field of kind `kind` on class k and its parents (up to stop).
template <typename F> static void EachField(Refl &r, void *k, void *stop, int kind, F fn) {
    for (int depth = 0; k && k != stop && depth < 8; depth++, k = r.getParent(k)) {
        void *iter = nullptr;
        while (void *f = r.getFields(k, &iter)) {
            if (r.fieldFlags(f) & 0x10) continue;                          // static
            if (r.typeKind(r.fieldType(f)) != kind) continue;
            fn((int)r.fieldOffset(f));
        }
    }
}
static void FindItemDefinitions() {
    if (g_defFound || !G.tItem || !G.itemKey.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    Refl &r = Reflection();
    Class uobj("UnityEngine", "Object"), mono("UnityEngine", "MonoBehaviour");
    if (!r.ok || !uobj.IsValid()) return;
    void *uobjClass = uobj.GetClass(), *monoClass = mono.IsValid() ? (void *)mono.GetClass() : uobjClass;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem) : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n && i < 30 && !g_defFound; i++) {             // a few real items to check against
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it)) continue;
        std::string key;
        try { auto *k = G.itemKey[it](); key = k ? k->str() : ""; } catch (...) {}
        if (key.empty()) continue;
        // the objects this item points to (0x12 = a class reference)
        EachField(r, it->klass, monoClass, 0x12, [&](int off) {
            if (g_defFound) return;
            Il2CppObject *def = *(Il2CppObject **)((uint8_t *)it + off);
            if (!def || !def->klass || !r.isSub(def->klass, uobjClass, false)) return;   // only loaded Unity objects (assets)
            // a text field on it (0x0e = string) holding exactly this item's name
            EachField(r, def->klass, uobjClass, 0x0e, [&](int soff) {
                if (g_defFound) return;
                auto *str = *(BNM::Structures::Mono::String **)((uint8_t *)def + soff);
                if (!str || str->str() != key) return;
                Il2CppObject *t = r.typeObject(r.classType(def->klass));
                if (!t) return;
                g_defType.set(t);
                g_defOffset = soff;
                g_defName = r.className(def->klass) ? r.className(def->klass) : "?";
                g_defFound = true;
            });
        });
    }
    if (g_defFound) ym::log("clientdabell: item list: the game's item names are in %s (field at %d)", g_defName.c_str(), g_defOffset);
}
static int ReadItemDefinitions() {
    Il2CppObject *t = g_defType.get();
    if (!g_defFound || !t || !G.findAll.IsValid()) return 0;
    auto *arr = (uint8_t *)G.findAll(t);
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    int got = 0;
    for (size_t i = 0; i < n && i < 2000; i++) {
        Il2CppObject *d = ((Il2CppObject **)(arr + 0x20))[i];
        if (!d) continue;
        auto *str = *(BNM::Structures::Mono::String **)((uint8_t *)d + g_defOffset);
        if (str) { SeeKey(str->str()); got++; }
    }
    g_defCount = got;
    return got;
}
static void ScanCatalog() {
    if (!G.tItem || !G.itemKey.IsValid()) { Notify("Item list isn't available in this game"); return; }
    int errors = 0, all = 0, room = 0;
    if (G.findAll.IsValid()) {                            // everything loaded, templates included
        try { all = CollectKeys((uint8_t *)G.findAll((Il2CppObject *)G.tItem), errors); } catch (...) { errors++; }
    }
    if (G.findScene.IsValid() || G.findSceneByType.IsValid()) {   // the items in this room
        try {
            room = CollectKeys((uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                                 : G.findSceneByType((Il2CppObject *)G.tItem, 0)), errors);
        } catch (...) { errors++; }
    }
    if (g_itemKey[0]) SeeKey(g_itemKey);                 // the last item you held
    int defs = 0;
    try { FindItemDefinitions(); defs = ReadItemDefinitions(); } catch (...) { errors++; }
    std::vector<std::string> keys = g_seenKeys;
    std::sort(keys.begin(), keys.end());
    g_spawnView.clear();                                 // the buttons point into g_catalog, so rebuild both together
    g_catalog = std::move(keys);
    g_spawnView.reserve(g_catalog.size());
    for (auto &k : g_catalog) g_spawnView.push_back({SPAWNER, k.c_str(), ACTION, MAIN, nullptr, SpawnPicked});
    g_page = 0;
    ym::log("clientdabell: item spawner: %d kinds (loaded=%d room=%d defs=%d in %s, errors=%d)", (int)g_catalog.size(), all, room,
            defs, g_defFound ? g_defName.c_str() : "-", errors);
    if (!g_defFound) Notify("%d items (reopen when there are items in the room for the full list)", (int)g_catalog.size());
    else Notify("%d items", (int)g_catalog.size());
}
// Tapping an item in the list: one of it appears 80 cm in front of you. Same limits as
// Spawn On Hand: one every 0.3 s, through the item queue (2 sent per frame, 32 waiting at most).
static void SpawnPicked() {
    Item *it = g_lastPressed;
    Il2CppObject *head = g_head.get();
    if (!it || !head || g_spawnView.empty() || it < &g_spawnView[0] || it > &g_spawnView.back()) return;
    if (g_time < g_nextItem) return;
    if (g_itemQueue.size() >= 32) { Notify("Wait a moment"); return; }
    g_nextItem = g_time + 0.3;
    snprintf(g_itemKey, sizeof g_itemKey, "%s", g_catalog[it - &g_spawnView[0]].c_str());
    Vector3 f = G.trGetForward[head]();
    g_itemQueue.push_back({G.trGetPosition[head]() + f * 0.8f + Vector3(0, -0.2f, 0), Vector3(0, 0, 0)});
    Notify("Spawned %.40s", g_itemKey);
}

static void SpawnOnHandTick(const Buttons &in) {
    bool press = in.rtrig && !g_onHandTrigger;
    g_onHandTrigger = in.rtrig;
    Il2CppObject *hand = g_rightHand.get();
    if (!f_spawnOnHand || !press || in.rgrip || g_menuShown || !hand || g_time < g_nextItem) return;
    if (!LeftHandItem()) return;
    g_nextItem = g_time + 0.3;
    g_itemQueue.push_back({G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.1f, Vector3(0, 0, 0)});
}

// ---- soundboard: songs you copy onto the headset ----------------------------------------
// Put .wav files in a folder named "sounds" next to the .so files in the mods folder
// (Android/data/com.TrassGames.Yeeps/files/mods/sounds/). Open Soundboard, pick a song, press
// Play. You hear it on an audio source on your head. With "Others Hear It" on, the song is
// also sent through Photon Voice: the Recorder (your mic) is switched to the song, and put
// back to your mic when the song ends, you press Stop, or you leave the room.
// WAV only (PCM 8/16/24/32-bit or 32-bit float). The song is read a little each frame, so
// the game never stalls, and mixed down to mono to save memory. Every song is converted to
// 48 kHz while it loads: Photon Voice only sends 8/12/16/24/48 kHz, so a 44.1 kHz song
// (most of them) would sometimes not be heard by the room at all.
static constexpr int kMaxSongs = 64;
static constexpr double kMaxSeconds = 272;             // about 4.5 minutes
static constexpr uint32_t kChunkFrames = 32768;        // 48 kHz frames made per game frame
static constexpr uint32_t kVoiceRate = 48000;
static std::vector<std::string> g_songs;
static int g_songSel = 0, g_soundVol = 1, g_clipSong = -1;
static const float kSoundVols[] = {0.3f, 0.6f, 1.0f};
static const char *kSoundVolNames[] = {"quiet", "medium", "loud"};
static Ref g_audioGo, g_audioSrc, g_clip, g_chunk;
static bool g_overlapPending = false, g_clipMixed = false;   // Overlap Songs: waiting to mix / the clip is a mix

struct Loader {
    FILE *f = nullptr;
    int channels = 0, bits = 0, fmt = 0;               // fmt 1 = whole numbers, 3 = floating point
    uint32_t rate = 0, block = 0;
    uint64_t frames = 0, done = 0;                     // frames in the file, frames read so far
    uint64_t outFrames = 0, outDone = 0;               // 48 kHz frames in the clip, written so far
    double step = 1;                                   // file frames per 48 kHz frame
    float prevA = 0, prevB = 0;                        // the last two file samples of the previous slice
    int song = -1, lastTen = -1;
    bool playWhenDone = false;
    std::vector<uint8_t> buf;
    std::vector<float> mono;
} g_load;

static uint32_t U32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t U16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static std::string SongsDir() {
    const char *base = ym::api()->modsDir;
    return std::string(base ? base : "") + "/sounds";
}
static std::string SongTitle(int i) {
    if (i < 0 || i >= (int)g_songs.size()) return "";
    std::string n = g_songs[i];
    return n.size() > 4 ? n.substr(0, n.size() - 4) : n;
}
static void UpdateSoundLabel() {
    if (g_songs.empty()) snprintf(g_soundLabel, sizeof g_soundLabel, "Play: (no songs)");
    else snprintf(g_soundLabel, sizeof g_soundLabel, "Play %d/%d: %.14s", g_songSel + 1, (int)g_songs.size(), SongTitle(g_songSel).c_str());
    g_dirty = true;
}
static void ScanSongs() {
    g_songs.clear();
    g_songSel = 0;
    std::string dir = SongsDir();
    DIR *d = opendir(dir.c_str());
    if (!d) {
        ym::log("clientdabell: no sounds folder at %s", dir.c_str());
        UpdateSoundLabel();
        Notify("Make a folder called sounds in your mods folder");
        return;
    }
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 4 && strcasecmp(n.c_str() + n.size() - 4, ".wav") == 0 && (int)g_songs.size() < kMaxSongs) g_songs.push_back(n);
    }
    closedir(d);
    std::sort(g_songs.begin(), g_songs.end());
    UpdateSoundLabel();
    ym::log("clientdabell: found %d songs", (int)g_songs.size());
    Notify(g_songs.empty() ? "No .wav files in the sounds folder" : "%d songs found", (int)g_songs.size());
}

// Reads the WAV header and leaves the file positioned at the first sample.
static bool OpenWav(const std::string &path, Loader &L, const char **err) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { *err = "Couldn't open that file"; return false; }
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
        fclose(f); *err = "That isn't a WAV file"; return false;
    }
    bool haveFmt = false, haveData = false;
    uint32_t dataBytes = 0;
    long dataStart = 0;
    uint8_t ch[8];
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t size = U32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fm[40] = {};
            size_t n = size < 40 ? size : 40;
            if (fread(fm, 1, n, f) != n || n < 16) break;
            L.fmt = U16(fm);
            L.channels = U16(fm + 2);
            L.rate = U32(fm + 4);
            L.bits = U16(fm + 14);
            if (L.fmt == 0xFFFE && n >= 26) L.fmt = U16(fm + 24);     // "extensible": real format inside
            haveFmt = true;
            if (size > n || (size & 1)) fseek(f, (long)(size - n) + (long)(size & 1), SEEK_CUR);
        } else if (memcmp(ch, "data", 4) == 0) {
            dataBytes = size;
            dataStart = ftell(f);
            haveData = true;
            break;
        } else {
            fseek(f, (long)size + (long)(size & 1), SEEK_CUR);
        }
    }
    if (!haveFmt || !haveData) { fclose(f); *err = "Couldn't read that WAV"; return false; }
    bool okFmt = (L.fmt == 1 && (L.bits == 8 || L.bits == 16 || L.bits == 24 || L.bits == 32)) || (L.fmt == 3 && L.bits == 32);
    if (!okFmt || L.channels < 1 || L.channels > 8 || L.rate < 8000 || L.rate > 192000) {
        fclose(f); *err = "Unsupported WAV (use 16-bit PCM)"; return false;
    }
    L.block = (uint32_t)L.channels * (uint32_t)(L.bits / 8);
    fseek(f, 0, SEEK_END);
    long fileEnd = ftell(f);
    if (dataBytes == 0 || dataBytes == 0xFFFFFFFFu || dataStart + (long)dataBytes > fileEnd) dataBytes = (uint32_t)(fileEnd - dataStart);
    fseek(f, dataStart, SEEK_SET);
    L.frames = dataBytes / L.block;
    L.f = f;
    return true;
}

static void AbortLoad() {
    if (g_load.f) fclose(g_load.f);
    g_load.f = nullptr;
    g_load.buf.clear();
    g_load.buf.shrink_to_fit();
    g_chunk.clear();
}

static bool EnsureAudioSource() {
    if (Alive(g_audioGo.get()) && Alive(g_audioSrc.get())) return true;
    if (!G.tAudioSource || !G.asPlay.IsValid() || !G.asSetClip.IsValid() || !g_head.get()) return false;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellAudio"));
    Il2CppObject *src = go ? G.goAddComponent[go](G.tAudioSource) : nullptr;
    if (!src) return false;
    Il2CppObject *t = Tr(go);
    G.trSetParent[t](g_head.get(), false);
    G.trSetLocalPosition[t](Vector3(0, 0, 0));
    g_audioGo.set(go);
    g_audioSrc.set(src);
    return true;
}

// ---- soundboard through voice chat (Photon Voice Recorder) ----------------------------
// The Recorder is what sends your mic to the room. While a song plays it is switched to
// "AudioClip" input with our song, so everyone near you hears it like your voice. Your
// own settings (push to talk, voice detection) are saved first and put back after.
static Ref g_recorder;
static bool g_micOn = false, g_micPrevTransmit = true, g_micPrevVad = false;
static double g_micUntil = 0, g_clipLen = 0;      // when the song ends; the song's length in seconds
static double g_micCheckAt = 0;                  // when to check the room is getting it
static int g_micTries = 0;

// The game can have several Recorders (your game has 4) and only the one hooked up to the
// voice connection really sends. They are put in a list, best guess first; if the one
// used doesn't start sending, the next one is tried (see SoundTick).
static constexpr int kMaxRecs = 8;
static Ref g_recCand[kMaxRecs], g_goodRec;       // g_goodRec: the one that worked last time
static int g_recCandN = 0, g_recCandIdx = 0;
static int g_recCount = -1;                    // how many voice recorders the last search found
// 1: hooked up to voice chat, 0: not, -1: can't tell
static int RecLive(Il2CppObject *r) {
    if (!Alive(r)) return 0;
    if (G.recGetInit.IsValid()) return G.recGetInit[r]() ? 1 : 0;
    if (!G.recFieldVoice.IsValid()) return -1;
    Il2CppObject *v = G.recFieldVoice[r]();
    if (!v || !v->klass || !v->klass->name) return 0;
    return strstr(v->klass->name, "Dummy") ? 0 : 1;
}
static int RecorderScore(Il2CppObject *r) {
    if (!Alive(r)) return -1;
    int s = 0;
    if (RecLive(r) == 1) s += 8;
    if (!G.behaviourGetEnabled.IsValid() || G.behaviourGetEnabled[r]()) s += 2;
    if (G.recGetTransmit.IsValid() && G.recGetTransmit[r]()) s += 1;
    if (G.recGetRecording.IsValid() && G.recGetRecording[r]()) s += 1;
    return s;
}
static void LogRecorder(const char *what, Il2CppObject *r) {
    if (!Alive(r)) { ym::log("clientdabell: %s: none", what); return; }
    ym::log("clientdabell: %s: live=%d enabled=%d transmit=%d recording=%d sending=%d source=%d", what, RecLive(r),
            G.behaviourGetEnabled.IsValid() ? (int)G.behaviourGetEnabled[r]() : -1,
            G.recGetTransmit.IsValid() ? (int)G.recGetTransmit[r]() : -1,
            G.recGetRecording.IsValid() ? (int)G.recGetRecording[r]() : -1,
            G.recGetSending.IsValid() ? (int)G.recGetSending[r]() : -1,
            G.recGetSource.IsValid() ? G.recGetSource[r]() : -1);
}
static void BuildRecCandidates() {
    for (auto &c : g_recCand) c.clear();
    g_recCandN = 0; g_recCandIdx = 0;
    if (!G.tRecorder) return;
    Il2CppObject *found[kMaxRecs]; int score[kMaxRecs]; int n = 0;
    auto Add = [&](Il2CppObject *r) {
        if (!Alive(r) || n >= kMaxRecs) return;
        for (int k = 0; k < n; k++) if (found[k] == r) return;
        found[n] = r;
        score[n] = RecorderScore(r) + (r == g_goodRec.get() ? 100 : 0);   // last time's working one first
        n++;
    };
    if (G.goFind.IsValid()) {                       // the voice object most Photon VR games use
        Il2CppObject *go = G.goFind(CreateMonoString("PhotonVRVoice"));
        if (Alive(go)) Add(G.goGetComponent[go]((Il2CppObject *)G.tRecorder));
    }
    if (G.findScene.IsValid() || G.findSceneByType.IsValid()) {
        auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tRecorder)
                                                      : G.findSceneByType((Il2CppObject *)G.tRecorder, 0));
        uintptr_t cnt = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        auto **el = arr ? (Il2CppObject **)(arr + 0x20) : nullptr;
        for (uintptr_t i = 0; i < cnt && i < 32; i++) Add(el[i]);
    }
    for (int a = 1; a < n; a++)                      // best score first
        for (int b = a; b > 0 && score[b] > score[b - 1]; b--) { std::swap(score[b], score[b - 1]); std::swap(found[b], found[b - 1]); }
    for (int k = 0; k < n; k++) {
        g_recCand[k].set(found[k]);
        char what[40];
        snprintf(what, sizeof what, "voice recorder %d of %d", k + 1, n);
        LogRecorder(what, found[k]);
    }
    g_recCandN = n;
    g_recCount = n;
}
static Il2CppObject *FindRecorder() {
    Il2CppObject *r = g_recorder.get();
    if (Alive(r)) return r;
    g_recorder.clear();
    BuildRecCandidates();
    r = g_recCandN ? g_recCand[0].get() : nullptr;
    if (!Alive(r)) return nullptr;
    LogRecorder("using voice recorder", r);
    g_recorder.set(r);
    return r;
}

static void RecSetInput(Il2CppObject *r, int type, Il2CppObject *clip, bool loop) {
    // The clip first, so the Recorder never restarts on "AudioClip" with no clip
    if (type == 1) {
        if (G.recSetClip.IsValid()) G.recSetClip[r](clip);
        else if (G.recFieldClip.IsValid()) G.recFieldClip[r] = clip;
    }
    if (G.recSetLoop.IsValid()) G.recSetLoop[r](loop);
    if (G.recFieldLoop.IsValid()) G.recFieldLoop[r] = loop;
    if (G.recSetSource.IsValid()) G.recSetSource[r](type);
    if (G.recFieldSource.IsValid()) G.recFieldSource[r] = type;
    if (type == 0) {
        if (G.recSetClip.IsValid()) G.recSetClip[r]((Il2CppObject *)nullptr);
        else if (G.recFieldClip.IsValid()) G.recFieldClip[r] = (Il2CppObject *)nullptr;
    }
}
static void RecRestart(Il2CppObject *r) {
    if (G.recRestart1.IsValid()) G.recRestart1[r](true);
    else if (G.recRestart0.IsValid()) G.recRestart0[r]();
    try { if (G.recStart.IsValid()) G.recStart[r](); } catch (...) {}   // already recording is fine
}

static void MicStop() {
    if (!g_micOn) return;
    g_micOn = false;
    g_micUntil = 0;
    g_micCheckAt = 0;
    Il2CppObject *r = g_recorder.get();
    if (!Alive(r)) { g_recorder.clear(); return; }
    try {
        RecSetInput(r, 0, nullptr, false);
        if (G.recSetTransmit.IsValid()) G.recSetTransmit[r](g_micPrevTransmit);
        if (G.recSetVad.IsValid()) G.recSetVad[r](g_micPrevVad);
        RecRestart(r);
        ym::log("clientdabell: voice chat back to your mic");
    } catch (...) { ym::log("clientdabell: couldn't put the mic back"); }
}


// Switches the recorder to the song. Used on Play, and again if a check finds it didn't take.
static void MicApply(Il2CppObject *r, Il2CppObject *clip) {
    RecSetInput(r, 1, clip, f_soundLoop);
    if (G.recSetTransmit.IsValid()) G.recSetTransmit[r](true);     // sends even with push to talk
    if (G.recSetVad.IsValid()) G.recSetVad[r](false);              // quiet parts aren't cut off
    if (G.recSetOnlyWhenEnabled.IsValid()) G.recSetOnlyWhenEnabled[r](false);
    if (G.recSetRecording.IsValid()) G.recSetRecording[r](true);
    RecRestart(r);
}
// The recorder's settings say it's on the song (source, recording, transmit).
static bool MicSettingsOk(Il2CppObject *r) {
    if (!Alive(r)) return false;
    bool src = !G.recGetSource.IsValid() || G.recGetSource[r]() == 1;
    bool rec = !G.recGetRecording.IsValid() || G.recGetRecording[r]();
    bool tx = !G.recGetTransmit.IsValid() || G.recGetTransmit[r]();
    return src && rec && tx;
}
// Sound is really going out (not just the settings): Photon Voice's IsCurrentlyTransmitting.
static bool MicTookHold(Il2CppObject *r) {
    if (!MicSettingsOk(r)) return false;
    return !G.recGetSending.IsValid() || G.recGetSending[r]();
}

// Switches recorder r to the song (saving your settings on it first).
static bool MicBegin(Il2CppObject *r) {
    Il2CppObject *clip = g_clip.get();
    if (!Alive(r) || !Alive(clip) || !(G.recSetSource.IsValid() || G.recFieldSource.IsValid())) return false;
    g_recorder.set(r);
    try {
        if (!g_micOn) {                                // save your settings once, not on every replay
            g_micPrevTransmit = G.recGetTransmit.IsValid() ? G.recGetTransmit[r]() : true;
            g_micPrevVad = G.recGetVad.IsValid() ? G.recGetVad[r]() : false;
        }
        g_micOn = true;
        MicApply(r, clip);
    } catch (...) {
        ym::log("clientdabell: voice chat error, putting the mic back");
        g_micOn = true;
        MicStop();
        return false;
    }
    g_micUntil = g_time + g_clipLen + 0.5;
    g_micCheckAt = g_time + 0.4;                       // make sure it took (Photon can ignore it right after joining)
    g_micTries = 0;
    return true;
}
// Sends the loaded song to the room. Returns false if it only plays for you.
static bool MicStart() {
    if (!f_soundMic) return false;
    // Look the recorders up again on every Play (unless a song is already going through one):
    // the game can swap its voice objects, and an old one would send nothing.
    if (!g_micOn) g_recorder.clear();
    Il2CppObject *r = FindRecorder();
    if (!r || !Alive(g_clip.get()) || !(G.recSetSource.IsValid() || G.recFieldSource.IsValid())) {
        ym::log("clientdabell: no voice recorder (type=%d), song plays only for you", G.tRecorder != nullptr);
        return false;
    }
    return MicBegin(r);
}

// Every frame: re-check a song that was just sent, and give the mic back when it's over.
static bool g_songPlaying = false;              // a song was started and not stopped
static double g_songEnd = 0;                    // when it ends if it isn't looping
static double g_micResumeAt = 0;                // "Others Hear It" was turned back on: send the song again at this time
static double g_micRetryAt = 0;                 // no recorder sent anything: try them all once more at this time
static double g_micWatchAt = 0;                 // next check that the game hasn't switched voice back to your mic
static int g_micRound = 0;                      // 0: first try through the recorders, 1: the second
static bool SongStillPlaying() {
    Il2CppObject *src = g_audioSrc.get();
    if (!g_songPlaying || g_clipSong < 0 || g_load.f || !Alive(src) || !Alive(g_clip.get())) return false;
    if (G.asIsPlaying.IsValid()) return G.asIsPlaying[src]();
    return f_soundLoop || g_time < g_songEnd;
}

// Every frame: send the song again after "Others Hear It" comes back on, check that a song
// that was just sent really goes out, and give the mic back when it's over.
static void SoundTick() {
    if (g_micResumeAt > 0 && g_time >= g_micResumeAt) {
        g_micResumeAt = 0;
        g_micRound = 0;
        g_micRetryAt = 0;
        if (f_soundMic && SongStillPlaying()) {
            // Voice chat can only play the song from the start, so it restarts for you too
            // (you and the room hear the same part).
            G.asPlay[g_audioSrc.get()]();
            g_songEnd = g_time + g_clipLen;
            if (MicStart()) Notify("Sending the song to the room...");
            else Notify("Voice chat not found, only you hear it");
        }
    }
    if (g_micRetryAt > 0 && g_time >= g_micRetryAt) {   // second try, after voice chat had a moment to settle
        g_micRetryAt = 0;
        if (f_soundMic && SongStillPlaying() && !g_micOn) {
            g_recorder.clear();
            if (MicStart()) Notify("Trying voice chat again...");
        }
    }
    if (!g_micOn) return;
    if (!f_soundLoop && g_micUntil > 0 && g_time >= g_micUntil) { MicStop(); return; }
    if (g_micCheckAt <= 0) {                            // it worked: keep an eye on it while the song plays
        if (g_time < g_micWatchAt) return;
        g_micWatchAt = g_time + 0.5;
        Il2CppObject *wr = g_recorder.get();
        if (Alive(wr) && !MicSettingsOk(wr)) {           // the game switched voice back (push to talk, mute...)
            ym::log("clientdabell: the game switched voice chat back to the mic, putting the song back");
            try { MicApply(wr, g_clip.get()); } catch (...) {}
            g_micCheckAt = g_time + 0.6;
            g_micTries = 0;
        }
        return;
    }
    if (g_time < g_micCheckAt) return;
    Il2CppObject *r = g_recorder.get();
    if (MicTookHold(r)) {
        Notify("The room hears it");
        ym::log("clientdabell: voice check ok on recorder %d of %d (after %d retries)", g_recCandIdx + 1, g_recCandN, g_micTries);
        g_goodRec.set(r);                               // use this one first next time
        g_micCheckAt = 0;
        return;
    }
    // This recorder is on the song but nothing goes out: it isn't the one hooked up to voice
    // chat. Put it back and try the next one.
    if (G.recGetSending.IsValid() && MicSettingsOk(r) && g_micTries >= 2 && g_recCandIdx + 1 < g_recCandN) {
        ym::log("clientdabell: recorder %d of %d isn't sending, trying the next", g_recCandIdx + 1, g_recCandN);
        int next = ++g_recCandIdx;
        MicStop();                                      // gives this one its mic settings back
        Il2CppObject *nr = g_recCand[next].get();
        if (Alive(nr) && MicBegin(nr)) Notify("Trying voice %d of %d...", next + 1, g_recCandN);
        return;
    }
    if (g_micTries >= 5 && g_micRound == 0 && g_recCandN > 0) {   // none of them sent: voice chat may not be ready yet, try all again soon
        ym::log("clientdabell: no recorder sent the song, trying them all again in a moment");
        g_micRound = 1;
        g_goodRec.clear();
        MicStop();
        g_micRetryAt = g_time + 1.5;
        Notify("Voice chat not ready, trying again...");
        return;
    }
    if (g_micTries >= 5 && MicSettingsOk(r)) {         // set up right, but no sign of sound going out: keep it, say so
        ym::log("clientdabell: voice set to the song but not confirmed sending");
        LogRecorder("recorder after the song was sent", r);
        // On-screen details (the headset has no easy log): recorders found, connected, sending, source
        char live[kMaxRecs + 1] = {};                   // per recorder: 1 hooked up, 0 not, ? can't tell
        for (int k = 0; k < g_recCandN; k++) { int l = RecLive(g_recCand[k].get()); live[k] = l < 0 ? '?' : (char)('0' + l); }
        Notify("Not confirmed. tried %d/%d live=%s send=%d src=%d", g_recCandIdx + 1, g_recCandN, live,
               G.recGetSending.IsValid() ? (int)G.recGetSending[r]() : -1,
               G.recGetSource.IsValid() ? G.recGetSource[r]() : -1);
        g_micCheckAt = 0;
        return;
    }
    if (g_micTries >= 5) {
        ym::log("clientdabell: voice chat wouldn't send the song, giving the mic back");
        Notify("Voice chat didn't take it, only you hear it");
        MicStop();
        return;
    }
    g_micTries++;
    if (!Alive(r)) { g_recorder.clear(); r = FindRecorder(); }
    Il2CppObject *clip = g_clip.get();
    if (!r || !Alive(clip)) { g_micCheckAt = g_time + 0.75; return; }
    ym::log("clientdabell: voice not sending yet (source=%d recording=%d sending=%d), try %d", G.recGetSource.IsValid() ? G.recGetSource[r]() : -1,
            G.recGetRecording.IsValid() ? (int)G.recGetRecording[r]() : -1, G.recGetSending.IsValid() ? (int)G.recGetSending[r]() : -1, g_micTries);
    // Settings right but no sound out yet: give Photon a moment before restarting it again
    // (restarting over and over can keep it from ever starting).
    if (!(MicSettingsOk(r) && g_micTries <= 2)) { try { MicApply(r, clip); } catch (...) {} }
    g_micCheckAt = g_time + 0.75;
}
static void SoundMicToggled() {
    if (!f_soundMic) {
        g_micResumeAt = 0;
        g_micRetryAt = 0;
        MicStop();
        Notify("Soundboard: only you hear it");
        return;
    }
    if (SongStillPlaying()) {
        // Wait a moment: the recorder was just switched back to your mic, and switching it
        // to the song again in the same moment can leave it sending nothing.
        g_micResumeAt = g_time + 0.6;
        Notify("Soundboard: the room will hear it (song restarts)");
        return;
    }
    Notify("Soundboard: the room hears your next song");
}

static void PlayLoaded() {
    if (!EnsureAudioSource() || !Alive(g_clip.get())) { Notify("Couldn't start the sound"); return; }
    Il2CppObject *src = g_audioSrc.get();
    G.asSetClip[src](g_clip.get());
    if (G.asSetVolume.IsValid()) G.asSetVolume[src](kSoundVols[g_soundVol]);
    if (G.asSetSpatial.IsValid()) G.asSetSpatial[src](0.0f);          // 2D: heard in your head, wherever you walk
    if (G.asSetLoop.IsValid()) G.asSetLoop[src](f_soundLoop);
    G.asPlay[src]();
    g_songPlaying = true;
    g_songEnd = g_time + g_clipLen;
    g_micResumeAt = 0;
    g_micRetryAt = 0;
    g_micRound = 0;
    bool room = MicStart();
    // "room hears it" only comes once the check sees sound going out (see SoundTick)
    Notify(room ? "Playing %.30s (sending to the room...)" : f_soundMic ? "Playing %.30s (voice chat not found, only you)" : "Playing %.30s",
           g_clipMixed ? "your mix" : SongTitle(g_clipSong).c_str());
}

static void StartLoad(int song, bool playAfter) {
    if (!G.audioClipCreate.IsValid() || !G.audioClipSetData.IsValid() || !G.arrayNew || !G.floatClass) {
        ym::log("clientdabell: soundboard missing: create=%d setdata=%d array=%d", G.audioClipCreate.IsValid(), G.audioClipSetData.IsValid(), G.arrayNew != nullptr);
        Notify("Soundboard isn't available in this game");
        return;
    }
    Loader L;
    const char *err = "";
    if (!OpenWav(SongsDir() + "/" + g_songs[song], L, &err)) { Notify("%s", err); return; }
    if (L.frames == 0) { fclose(L.f); Notify("That song is empty"); return; }
    if ((double)L.frames / L.rate > kMaxSeconds) { fclose(L.f); Notify("Song too long (max about 4 minutes)"); return; }
    L.step = (double)L.rate / kVoiceRate;
    L.outFrames = (uint64_t)((double)L.frames / L.step);
    if (L.outFrames == 0) L.outFrames = 1;
    if (Alive(g_clip.get())) G.destroy(g_clip.get());               // frees the last song's memory
    g_clip.clear();
    g_clipSong = -1;
    g_clipMixed = false;
    Il2CppObject *clip = G.audioClipCreate(CreateMonoString(SongTitle(song).c_str()), (int)L.outFrames, 1, (int)kVoiceRate, false);
    Il2CppObject *chunk = (Il2CppObject *)G.arrayNew(G.floatClass, kChunkFrames);
    if (!clip || !chunk) { fclose(L.f); Notify("Not enough memory for that song"); return; }
    g_clip.set(clip);
    g_chunk.set(chunk);
    size_t srcMax = (size_t)(kChunkFrames * L.step) + 4;          // file frames one slice can need
    L.buf.resize(srcMax * L.block);
    L.mono.resize(srcMax);
    L.song = song;
    L.playWhenDone = playAfter;
    g_load = std::move(L);
    ym::log("clientdabell: loading %s (%llu frames, %d ch, %d bit, %u Hz)", g_songs[song].c_str(), (unsigned long long)g_load.frames,
            g_load.channels, g_load.bits, g_load.rate);
    Notify("Loading %.30s", SongTitle(song).c_str());
}

// ---- Overlap Songs: two songs at once ------------------------------------------------
// Voice chat can only send one sound, so two songs are mixed into one clip and that clip
// is sent (and played for you). When you press Play while a song plays (with Overlap Songs
// on), the playing song is kept, the new one loads as usual, and then both are mixed a
// slice per frame: the playing song from where it is now, the new one from its start. When
// the mix is ready it replaces the song for you and the room. A third song mixes onto the
// mix the same way. The mixed clip is at most kMaxSeconds long.
static constexpr uint32_t kMixChunk = 131072;            // samples mixed per game frame
static Ref g_baseClip, g_mixClip, g_mixA, g_mixB;
static int g_baseOffset = 0;                           // where the playing song was when Play was pressed
static struct { bool active = false; int outLen = 0, done = 0, baseLen = 0, newLen = 0; } g_mix;

static void MixAbort() {
    if (g_mix.active && Alive(g_mixClip.get())) G.destroy(g_mixClip.get());
    g_mixClip.clear(); g_mixA.clear(); g_mixB.clear();
    g_mix.active = false;
    g_overlapPending = false;
}
static bool CanOverlap() {
    return f_overlap && G.audioClipGetData.IsValid() && G.audioClipSamples.IsValid() && G.asGetTimeSamples.IsValid() &&
           G.audioClipCreate.IsValid() && G.audioClipSetData.IsValid() && G.arrayNew && G.floatClass;
}
// The new song finished loading: start mixing it with the one that is playing.
static void StartMix() {
    Il2CppObject *base = g_baseClip.get(), *add = g_clip.get();
    if (!Alive(base) || !Alive(add)) { PlayLoaded(); return; }
    g_mix.baseLen = G.audioClipSamples[base]();
    g_mix.newLen = G.audioClipSamples[add]();
    int remA = std::max(0, g_mix.baseLen - g_baseOffset);
    int out = std::min(std::max(remA, g_mix.newLen), (int)(kMaxSeconds * kVoiceRate));
    Il2CppObject *mix = out > 0 ? G.audioClipCreate(CreateMonoString("ClientDaBellMix"), out, 1, (int)kVoiceRate, false) : nullptr;
    Il2CppObject *a = (Il2CppObject *)G.arrayNew(G.floatClass, kMixChunk), *b = (Il2CppObject *)G.arrayNew(G.floatClass, kMixChunk);
    if (!mix || !a || !b) { Notify("Not enough memory to overlap, playing the new song"); g_baseClip.clear(); PlayLoaded(); return; }
    g_mixClip.set(mix); g_mixA.set(a); g_mixB.set(b);
    g_mix.outLen = out; g_mix.done = 0; g_mix.active = true;
    Notify("Mixing the songs...");
}
static void MixTick() {
    if (!g_mix.active) return;
    Il2CppObject *base = g_baseClip.get(), *add = g_clip.get(), *mix = g_mixClip.get(), *a = g_mixA.get(), *b = g_mixB.get();
    if (!Alive(base) || !Alive(add) || !Alive(mix) || !a || !b) { MixAbort(); g_baseClip.clear(); return; }
    int n = std::min((int)kMixChunk, g_mix.outLen - g_mix.done);
    float *pa = (float *)((uint8_t *)a + 0x20), *pb = (float *)((uint8_t *)b + 0x20);
    // GetData wraps around to the start past the end, so only the samples before the end are used
    int posA = g_baseOffset + g_mix.done, validA = std::max(0, std::min(n, g_mix.baseLen - posA));
    int validB = std::max(0, std::min(n, g_mix.newLen - g_mix.done));
    if (validA > 0) G.audioClipGetData[base](a, posA);
    if (validB > 0) G.audioClipGetData[add](b, g_mix.done);
    Il2CppObject *outArr = n == (int)kMixChunk ? a : (Il2CppObject *)G.arrayNew(G.floatClass, (uintptr_t)n);
    if (!outArr) { MixAbort(); g_baseClip.clear(); Notify("Not enough memory to overlap"); return; }
    float *po = (float *)((uint8_t *)outArr + 0x20);
    for (int i = 0; i < n; i++) {
        float v = (i < validA ? pa[i] : 0.0f) * 0.75f + (i < validB ? pb[i] : 0.0f) * 0.75f;   // a bit quieter so the sum doesn't clip
        po[i] = v > 1 ? 1.f : v < -1 ? -1.f : v;
    }
    G.audioClipSetData[mix](outArr, g_mix.done);
    g_mix.done += n;
    if (g_mix.done < g_mix.outLen) return;
    // Done: switch you and the room to the mix, then free the two old clips.
    Ref oldBase, oldAdd;
    oldBase.set(base); oldAdd.set(add);
    g_clip.set(mix);
    g_clipLen = (double)g_mix.outLen / kVoiceRate;
    g_clipMixed = true;
    g_mix.active = false;
    g_mixClip.clear(); g_mixA.clear(); g_mixB.clear(); g_baseClip.clear();
    PlayLoaded();                                       // plays the mix and sends it (replaces the recorder's clip)
    if (Alive(oldBase.get())) G.destroy(oldBase.get());
    if (Alive(oldAdd.get())) G.destroy(oldAdd.get());
    oldBase.clear(); oldAdd.clear();
    ym::log("clientdabell: overlap mix ready, %.1f s", g_clipLen);
}

// Called every frame: reads the next slice of the song, mixes it to mono, converts it to
// 48 kHz (straight-line between samples) and writes 32k frames into the clip.
static void LoadTick() {
    MixTick();
    Loader &L = g_load;
    if (!L.f) return;
    Il2CppObject *clip = g_clip.get();
    if (!Alive(clip)) { AbortLoad(); return; }
    uint64_t wantOut = L.outFrames - L.outDone < kChunkFrames ? L.outFrames - L.outDone : kChunkFrames;
    // the file frames this slice needs: up to the one after the last output's position
    uint64_t needEnd = (uint64_t)((double)(L.outDone + wantOut - 1) * L.step) + 2;
    if (needEnd > L.frames) needEnd = L.frames;
    uint64_t readN = needEnd > L.done ? needEnd - L.done : 0;
    if (readN > L.mono.size()) readN = L.mono.size();
    uint64_t n = readN ? fread(L.buf.data(), 1, (size_t)(readN * L.block), L.f) / L.block : 0;
    const int bytes = L.bits / 8;
    const float inv = 1.0f / (float)L.channels;
    for (uint64_t i = 0; i < n; i++) {
        const uint8_t *p = L.buf.data() + i * L.block;
        float sum = 0;
        for (int c = 0; c < L.channels; c++) {
            const uint8_t *q = p + c * bytes;
            float v;
            if (L.fmt == 3) { uint32_t u = U32(q); memcpy(&v, &u, 4); }
            else if (L.bits == 16) v = (float)(int16_t)U16(q) * (1.0f / 32768.0f);
            else if (L.bits == 24) v = (float)(int32_t)(((uint32_t)q[0] << 8) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 24)) * (1.0f / 2147483648.0f);
            else if (L.bits == 32) v = (float)(int32_t)U32(q) * (1.0f / 2147483648.0f);
            else v = ((float)q[0] - 128.0f) * (1.0f / 128.0f);
            sum += v;
        }
        float m = sum * inv;
        L.mono[i] = m > 1 ? 1.f : m < -1 ? -1.f : m;
    }
    const uint64_t base = L.done;
    L.done += n;
    bool fileEnded = n < readN;
    if (fileEnded && n == 0) wantOut = 0;            // nothing left to read: stop here
    if (wantOut > 0) {
        Il2CppObject *arr = wantOut == kChunkFrames ? g_chunk.get() : (Il2CppObject *)G.arrayNew(G.floatClass, (uintptr_t)wantOut);
        if (!arr) { AbortLoad(); Notify("Not enough memory for that song"); return; }
        float *dst = (float *)((uint8_t *)arr + 0x20);
        auto S = [&](uint64_t k) -> float {                           // file sample k, from this slice or the last
            if (k + 2 == base) return L.prevA;
            if (k + 1 == base) return L.prevB;
            if (k < base) return L.prevA;
            uint64_t j = k - base;
            return j < n ? L.mono[j] : (n ? L.mono[n - 1] : L.prevB);
        };
        for (uint64_t i = 0; i < wantOut; i++) {
            double pos = (double)(L.outDone + i) * L.step;
            uint64_t k = (uint64_t)pos;
            float f = (float)(pos - (double)k), s0 = S(k), s1 = S(k + 1);
            dst[i] = s0 + (s1 - s0) * f;
        }
        G.audioClipSetData[clip](arr, (int)L.outDone);
        L.outDone += wantOut;
    }
    if (n >= 2) { L.prevA = L.mono[n - 2]; L.prevB = L.mono[n - 1]; }
    else if (n == 1) { L.prevA = L.prevB; L.prevB = L.mono[0]; }
    int ten = (int)(L.outDone * 10 / L.outFrames);
    if (ten != L.lastTen && ten < 10) { L.lastTen = ten; Notify("Loading %d%%", ten * 10); }
    if (L.outDone >= L.outFrames || wantOut == 0) {                   // finished (or the file ended early)
        bool play = L.playWhenDone;
        int song = L.song;
        g_clipLen = (double)L.outDone / kVoiceRate;
        ym::log("clientdabell: loaded %s, %u Hz -> 48000 Hz, %.1f s", g_songs[song].c_str(), L.rate, g_clipLen);
        AbortLoad();
        g_clipSong = song;
        if (play) {
            if (g_overlapPending && Alive(g_baseClip.get())) StartMix();
            else PlayLoaded();
        }
        g_overlapPending = false;
    }
}

static void PlaySong() {
    if (g_songs.empty()) { Notify("Put .wav files in the sounds folder, then Rescan"); return; }
    if (g_load.f || g_mix.active) { Notify("Still loading, one moment"); return; }
    if (CanOverlap() && SongStillPlaying()) {           // Overlap Songs: keep this one playing, add the new one on top
        Il2CppObject *src = g_audioSrc.get(), *base = g_clip.get();
        g_baseOffset = std::max(0, G.asGetTimeSamples[src]());
        g_baseClip.set(base);
        g_clip.clear();                                  // so loading the new song doesn't free the playing one
        g_clipSong = -1;
        StartLoad(g_songSel, true);
        if (g_load.f) { g_overlapPending = true; return; }
        g_clip.set(base);                                // couldn't load the new song: keep the old one going
        g_baseClip.clear();
        return;
    }
    if (g_clipSong == g_songSel && Alive(g_clip.get()) && !g_clipMixed) PlayLoaded();
    else StartLoad(g_songSel, true);
}
static void NextSong() { if (g_songs.empty()) return; g_songSel = (g_songSel + 1) % (int)g_songs.size(); UpdateSoundLabel(); }
static void PrevSong() { if (g_songs.empty()) return; g_songSel = (g_songSel + (int)g_songs.size() - 1) % (int)g_songs.size(); UpdateSoundLabel(); }
static void StopSound() {
    AbortLoad();
    if (g_clipSong < 0 && Alive(g_clip.get())) { G.destroy(g_clip.get()); g_clip.clear(); }   // a half-loaded clip is useless
    Il2CppObject *src = g_audioSrc.get();
    if (Alive(src) && G.asStop.IsValid()) G.asStop[src]();
    g_songPlaying = false;
    g_micResumeAt = 0;
    g_micRetryAt = 0;
    MicStop();
    MixAbort();                                         // after the recorder let go of the clips
    if (Alive(g_baseClip.get()) && g_baseClip.get() != g_clip.get()) G.destroy(g_baseClip.get());
    g_baseClip.clear();
}
static void ApplyLoop() {
    Il2CppObject *src = g_audioSrc.get();
    if (Alive(src) && G.asSetLoop.IsValid()) G.asSetLoop[src](f_soundLoop);
    Il2CppObject *r = g_recorder.get();
    if (g_micOn && Alive(r)) {
        if (G.recSetLoop.IsValid()) G.recSetLoop[r](f_soundLoop);
        if (G.recFieldLoop.IsValid()) G.recFieldLoop[r] = f_soundLoop;
        if (!f_soundLoop) g_micUntil = g_time + g_clipLen + 0.5;     // at most one more play-through
    }
}
// ---- Disable Songs ------------------------------------------------------------------
// Other players' songs come through their voice chat, so a song can't be told apart from
// talking for sure. This guesses from how the sound behaves: talking has short pauses all the
// time, music keeps going. Each other player's voice (Photon Voice's Speaker, and the
// AudioSource it plays through) is measured 10 times a second. If it stays loud with no pause
// for kSongSecs, they're muted for you (volume turned almost all the way down, so it can
// still be measured). When it's quiet for kQuietSecs (the music stopped), their volume goes
// back. Only your own game changes; nothing is sent.
static constexpr int kMaxSpeakers = 16;
static constexpr float kSongSecs = 5.0f, kQuietSecs = 1.0f, kPauseSecs = 0.3f, kLoud = 0.015f, kMutedVol = 0.02f;
static struct SpeakerWatch { Ref src, go; float loudRun = 0, quietRun = 0, volBefore = 1; bool muted = false, byMute = false, muteBefore = false; } g_spk[kMaxSpeakers];
// Mute or unmute every sound player on that player's voice object (the game can play a voice
// through more than one, and keeps setting their volume, so the mute switch is used).
static void SetVoiceMute(Il2CppObject *go, Il2CppObject *src, bool on) {
    if (Alive(src)) G.asSetMute[src](on);
    if (!go || !G.getComponentsInChildren.IsValid()) return;
    auto *ca = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tAudioSource, true);
    size_t n = ca ? *(uintptr_t *)(ca + 0x18) : 0;
    for (size_t i = 0; i < n && i < 8; i++) {
        Il2CppObject *a = ((Il2CppObject **)(ca + 0x20))[i];
        if (Alive(a) && a != g_audioSrc.get()) G.asSetMute[a](on);
    }
}
static int g_spkN = 0, g_spkFound = -1;           // voices being watched; Speakers in the scene
static float g_spkMaxLevel = 0;                        // loudest voice since the last on-screen report
static double g_nextSpkReport = 0;
static Ref g_spkBuf;
static double g_nextSpkFind = 0, g_nextSpkMeasure = 0;

static void SpeakerUnmute(SpeakerWatch &w) {
    Il2CppObject *src = w.src.get();
    if (w.muted && w.byMute) SetVoiceMute(w.go.get(), src, w.muteBefore);   // back to how it was (you may have muted them yourself)
    else if (w.muted && Alive(src) && G.asSetVolume.IsValid()) G.asSetVolume[src](w.volBefore);
    w.muted = false;
}
static void DisableSongsReset() {                       // everyone back to normal
    for (int i = 0; i < g_spkN; i++) { try { SpeakerUnmute(g_spk[i]); } catch (...) {} g_spk[i].src.clear(); g_spk[i].go.clear(); g_spk[i] = SpeakerWatch(); }
    g_spkN = 0;
    g_nextSpkFind = g_nextSpkMeasure = 0;
}
static void DisableSongsToggled() {
    if (!f_disableSongs) DisableSongsReset();
}
// Look for other players' voices now and then (they come and go as players join and leave).
static void FindSpeakers() {
    if (!G.findScene.IsValid() && !G.findSceneByType.IsValid()) return;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tSpeaker)
                                                  : G.findSceneByType((Il2CppObject *)G.tSpeaker, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    g_spkFound = (int)n;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *sp = ((Il2CppObject **)(arr + 0x20))[i];
        Il2CppObject *go = Alive(sp) ? G.compGameObject[sp]() : nullptr;
        Il2CppObject *src = go ? G.goGetComponent[go](G.tAudioSource) : nullptr;
        if (!Alive(src) && go && G.getComponentsInChildren.IsValid()) {       // on a child object
            auto *ca = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tAudioSource, true);
            if (ca && *(uintptr_t *)(ca + 0x18) > 0) src = ((Il2CppObject **)(ca + 0x20))[0];
        }
        if (!Alive(src) && go && G.trGetParent.IsValid()) {                   // or on the parent
            Il2CppObject *pt = G.trGetParent[Tr(go)]();
            Il2CppObject *pgo = pt ? G.compGameObject[pt]() : nullptr;
            if (pgo) src = G.goGetComponent[pgo](G.tAudioSource);
        }
        if (!Alive(src) || src == g_audioSrc.get()) continue;
        bool known = false;
        for (int k = 0; k < g_spkN && !known; k++) known = g_spk[k].src.get() == src;
        if (known || g_spkN >= kMaxSpeakers) continue;
        g_spk[g_spkN] = SpeakerWatch();
        g_spk[g_spkN].src.set(src);
        g_spk[g_spkN].go.set(go);
        g_spkN++;
    }
    for (int k = 0; k < g_spkN; ) {                    // forget players who left
        if (Alive(g_spk[k].src.get())) { k++; continue; }
        g_spk[k].src.clear(); g_spk[k].go.clear();      // free this one's handles, then move the last one here
        g_spk[k] = g_spk[g_spkN - 1];
        g_spk[g_spkN - 1] = SpeakerWatch();
        g_spkN--;
    }
}
static void DisableSongsTick() {
    if (!f_disableSongs && g_spkN > 0) DisableSongsReset();   // switched off some other way (Turn Everything Off): unmute everyone
    if (!f_disableSongs) return;
    bool canClip = G.asGetClip.IsValid() && G.audioClipGetData.IsValid() && G.asGetTimeSamples.IsValid() && G.audioClipSamples.IsValid();
    if (!G.tSpeaker || !(G.asGetOutputData.IsValid() || canClip) || !G.asSetVolume.IsValid() || !G.arrayNew || !G.floatClass) {
        if (g_time >= g_nextSpkReport) {                 // log what's missing (once a minute)
            g_nextSpkReport = g_time + 60.0;
            ym::log("clientdabell: disable songs missing speaker=%d data=%d clip=%d vol=%d buf=%d", G.tSpeaker ? 1 : 0,
                    G.asGetOutputData.IsValid() ? 1 : 0, canClip ? 1 : 0, G.asSetVolume.IsValid() ? 1 : 0, G.arrayNew && G.floatClass ? 1 : 0);
        }
        return;
    }
    if (g_time >= g_nextSpkFind) { g_nextSpkFind = g_time + 2.0; FindSpeakers(); }
    if (g_time < g_nextSpkMeasure) return;
    const float step = 0.1f;
    g_nextSpkMeasure = g_time + step;
    if (!g_spkBuf.get()) g_spkBuf.set((Il2CppObject *)G.arrayNew(G.floatClass, 1024));
    Il2CppObject *buf = g_spkBuf.get();
    if (!buf) return;
    float *d = (float *)((uint8_t *)buf + 0x20);
    for (int k = 0; k < g_spkN; k++) {
        SpeakerWatch &w = g_spk[k];
        Il2CppObject *src = w.src.get();
        if (!Alive(src)) continue;
        // How loud they are: the last 1024 samples of the clip Photon writes their voice into
        // (before the volume, so muting doesn't change it), or else what the AudioSource plays.
        bool fromClip = false;
        if (G.asIsPlaying.IsValid() && !G.asIsPlaying[src]()) {   // their voice isn't playing at all: silent
            w.quietRun += step;
            if (w.quietRun >= kPauseSecs) w.loudRun = 0;
            if (w.muted && w.quietRun >= kQuietSecs) { SpeakerUnmute(w); w.loudRun = 0; }
            continue;
        }
        Il2CppObject *clip = canClip ? G.asGetClip[src]() : nullptr;
        if (Alive(clip)) {
            int len = G.audioClipSamples[clip]();
            if (len > 2048) {
                int pos = G.asGetTimeSamples[src]() - 1024;
                if (pos < 0) pos += len;                 // GetData wraps past the end by itself
                fromClip = G.audioClipGetData[clip](buf, pos);
            }
        }
        if (!fromClip) {
            if (!G.asGetOutputData.IsValid()) continue;
            G.asGetOutputData[src](buf, 0);
        }
        double sum = 0;
        for (int i = 0; i < 1024; i++) sum += d[i] * d[i];
        float rms = sqrtf((float)(sum / 1024));
        // AudioSource output is after the volume: while muted, scale back up to how loud they really are
        float level = (w.muted && !w.byMute && !fromClip) ? rms / kMutedVol : rms;
        if (level > g_spkMaxLevel) g_spkMaxLevel = level;
        if (level > kLoud) { w.loudRun += step; w.quietRun = 0; }
        else {
            w.quietRun += step;
            if (w.quietRun >= kPauseSecs) w.loudRun = 0;   // a pause: that's talking, not a song
        }
        if (!w.muted && w.loudRun >= kSongSecs) {
            // The mute switch when we can still measure from the clip (it isn't affected by
            // muting); else turn the volume down so it can still be measured.
            w.byMute = fromClip && G.asSetMute.IsValid();
            if (w.byMute) {
                w.muteBefore = G.asGetMute.IsValid() && G.asGetMute[src]();
                SetVoiceMute(w.go.get(), src, true);
            } else {
                w.volBefore = G.asGetVolume.IsValid() ? G.asGetVolume[src]() : 1.0f;
                if (w.volBefore < kMutedVol * 2) w.volBefore = 1.0f;
                G.asSetVolume[src](kMutedVol);
            }
            w.muted = true;
            Notify("Muted a song");
            ym::log("clientdabell: disable songs: muted speaker %d (level %.3f)", k, level);
        } else if (w.muted) {
            if (w.byMute) SetVoiceMute(w.go.get(), src, true);   // keep it muted if the game turns it back on
            else G.asSetVolume[src](kMutedVol);                   // keep it down if the game turns it back up
            if (w.quietRun >= kQuietSecs) { SpeakerUnmute(w); w.loudRun = 0; }
        }
    }
}

static void NextSoundVolume() {
    g_soundVol = (g_soundVol + 1) % 3;
    Il2CppObject *src = g_audioSrc.get();
    if (Alive(src) && G.asSetVolume.IsValid()) G.asSetVolume[src](kSoundVols[g_soundVol]);
    Notify("Sound volume: %s", kSoundVolNames[g_soundVol]);
}

// ---- effects: snow, rain, thunderstorm (only on your own game) ------------------------
// The effect is drawn only on your own game; nothing is sent to the room. A fixed pool of small cubes (kMaxFx) falls around your
// head and is reused: a piece that lands, or drifts too far away, restarts above you. The
// pool is made a few pieces per frame, once, and is hidden (not destroyed) when you switch
// the effect off. A thunderstorm adds a lightning bolt far away in the sky, a brief
// dim flash of light, and a thunder rumble that is made from noise in code, once.
static constexpr int kMaxFx = 100;
static const int kFxCounts[5][3] = {{40, 70, 100}, {50, 80, 100}, {50, 80, 100}, {30, 55, 80}, {50, 80, 100}};   // [effect][intensity]
static const char *kFxIntensityNames[] = {"light", "medium", "heavy"};
static int g_wxIntensity = 1;
static Ref g_wxRoot, g_fx[kMaxFx], g_fxT[kMaxFx], g_bolt[7], g_wxLight, g_wxAudioGo, g_wxAudioSrc, g_wxClip;
static Vector3 g_fxPos[kMaxFx], g_fxVel[kMaxFx];      // velocity: only the bundle disco's confetti uses it
static float g_fxPhase[kMaxFx];
static int g_fxMade = 0, g_fxMode = -1, g_fxShown = -1;
static float g_groundY = 0;
static double g_nextGround = 0, g_nextBolt = 0, g_boltHideAt = 0, g_flashAt = 0, g_thunderAt = 0;
static bool g_wxOn = false;
static double g_bundleStart = 0;                       // when the bundle disco started (the ball drops in)
static bool g_bundleBurst = false;                     // the confetti burst went off
static Method<void> g_lightType, g_lightIntensity, g_lightRange, g_lightColor;

static void WeatherPicked() {
    int picked = -1;
    for (int i = 0; i < 5; i++) if (g_wxSel[i] && i != g_wx) picked = i;
    for (int i = 0; i < 5; i++) g_wxSel[i] = (i == picked);
    g_wx = picked;
}
static void NextWeatherIntensity() { g_wxIntensity = (g_wxIntensity + 1) % 3; Notify("Effect intensity: %s", kFxIntensityNames[g_wxIntensity]); }

static bool EnsureWxRoot() {
    if (Alive(g_wxRoot.get())) return true;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellWeather"));
    if (!go) return false;
    g_wxRoot.set(go);
    return true;
}
static void RespawnFx(int i, Vector3 head, bool anywhere) {
    float a = Rand01() * 6.2831853f, r = 4.0f * sqrtf(Rand01());
    float top = head.y + 3.0f;
    float y = anywhere ? g_groundY + Rand01() * (top - g_groundY) : head.y + 2.0f + Rand01() * 1.5f;
    g_fxPos[i] = Vector3(head.x + cosf(a) * r, y, head.z + sinf(a) * r);
    g_fxPhase[i] = Rand01() * 6.2831853f;
    g_fxVel[i] = Vector3(0, -0.5f, 0);
}
static void StyleFx(int i, int mode) {
    Il2CppObject *o = g_fx[i].get(), *t = g_fxT[i].get();
    if (!Alive(o) || !t) return;
    if (mode == 0) { SetColor(o, Color(0.95f, 0.97f, 1.0f, 1)); G.trSetLocalScale[t](Vector3(0.02f, 0.02f, 0.02f)); }
    else if (mode == 3) { SetColor(o, Rainbow(i / 17.0f)); G.trSetLocalScale[t](Vector3(0.025f, 0.025f, 0.025f)); }
    else if (mode == 4) { SetColor(o, Rainbow(Rand01())); G.trSetLocalScale[t](Vector3(0.045f, 0.004f, 0.03f)); }   // confetti
    else           { SetColor(o, Color(0.65f, 0.78f, 1.0f, 1)); G.trSetLocalScale[t](Vector3(0.004f, 0.07f, 0.004f)); }
}

// Thunder: low-passed noise with a quick attack and a long tail, plus a low thump.
static void BuildThunderClip() {
    if (Alive(g_wxClip.get()) || !G.audioClipCreate.IsValid() || !G.audioClipSetData.IsValid() || !G.arrayNew || !G.floatClass) return;
    const int rate = 22050, n = rate * 3;
    Il2CppObject *clip = G.audioClipCreate(CreateMonoString("ClientDaBellThunder"), n, 1, rate, false);
    Il2CppObject *arr = (Il2CppObject *)G.arrayNew(G.floatClass, n);
    if (!clip || !arr) return;
    float *d = (float *)((uint8_t *)arr + 0x20);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / rate, noise = Rand01() * 2 - 1;
        lp += (noise - lp) * 0.04f;
        lp2 += (lp - lp2) * 0.15f;
        float env = t < 0.04f ? t / 0.04f : expf(-(t - 0.04f) * 1.3f);
        float crack = t < 0.5f ? noise * 0.25f * expf(-t * 9.0f) : 0.0f;
        float thump = sinf(6.2831853f * 48.0f * t) * 0.35f * expf(-t * 1.8f);
        float v = (lp2 * 4.0f + thump * 0.7f + crack * 0.7f) * env;
        d[i] = v > 1 ? 1 : v < -1 ? -1 : v;
    }
    G.audioClipSetData[clip](arr, 0);
    g_wxClip.set(clip);
}
static void PlayThunder() {
    if (!Alive(g_wxClip.get()) || !G.tAudioSource || !G.asPlay.IsValid() || !g_head.get()) return;
    if (!Alive(g_wxAudioGo.get()) || !Alive(g_wxAudioSrc.get())) {
        Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellThunderAudio"));
        Il2CppObject *src = go ? G.goAddComponent[go](G.tAudioSource) : nullptr;
        if (!src) return;
        G.trSetParent[Tr(go)](g_head.get(), false);
        G.trSetLocalPosition[Tr(go)](Vector3(0, 0, 0));
        g_wxAudioGo.set(go);
        g_wxAudioSrc.set(src);
    }
    Il2CppObject *src = g_wxAudioSrc.get();
    G.asSetClip[src](g_wxClip.get());
    if (G.asSetVolume.IsValid()) G.asSetVolume[src](kSoundVols[g_soundVol]);
    if (G.asSetSpatial.IsValid()) G.asSetSpatial[src](0.0f);
    if (G.asSetLoop.IsValid()) G.asSetLoop[src](false);
    G.asPlay[src]();
}
static void EnsureStorm() {
    if (!EnsureWxRoot()) return;
    if (!Alive(g_wxLight.get())) {
        Class light("UnityEngine", "Light");
        Il2CppObject *go = light.IsValid() ? Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellLightning")) : nullptr;
        Il2CppObject *l = go ? G.goAddComponent[go](light.GetMonoType()) : nullptr;
        if (l) {
            g_lightType = light.GetMethod("set_type", 1);
            g_lightIntensity = light.GetMethod("set_intensity", 1);
            g_lightRange = light.GetMethod("set_range", 1);
            g_lightColor = light.GetMethod("set_color", 1);
            G.trSetParent[Tr(go)](Tr(g_wxRoot.get()), false);
            if (g_lightType.IsValid()) g_lightType[l](2);                     // a point light
            if (g_lightRange.IsValid()) g_lightRange[l](80.0f);
            if (g_lightColor.IsValid()) g_lightColor[l](Color(0.8f, 0.85f, 1.0f, 1));
            if (g_lightIntensity.IsValid()) g_lightIntensity[l](0.0f);
            G.behaviourSetEnabled[l](false);
            g_wxLight.set(l);
        }
    }
    for (int i = 0; i < 7; i++) {
        if (Alive(g_bolt[i].get())) continue;
        Il2CppObject *o = Prim(3, Color(0.92f, 0.95f, 1.0f, 1), false);
        if (!o) continue;
        G.trSetParent[Tr(o)](Tr(g_wxRoot.get()), false);
        G.goSetActive[o](false);
        g_bolt[i].set(o);
    }
    BuildThunderClip();
}
static void StrikeBolt(Vector3 head) {
    float ang = Rand01() * 6.2831853f, dist = 25.0f + Rand01() * 30.0f;
    Vector3 base(head.x + cosf(ang) * dist, g_groundY, head.z + sinf(ang) * dist), pts[8];
    float jx = 0, jz = 0;
    for (int i = 0; i < 8; i++) {                       // a jagged line from the sky to the ground
        float k = i / 7.0f;
        if (i > 0 && i < 7) { jx += (Rand01() - 0.5f) * 7.0f; jz += (Rand01() - 0.5f) * 7.0f; }
        pts[i] = Vector3(base.x + jx * (1 - k), base.y + (1 - k) * 45.0f, base.z + jz * (1 - k));
    }
    for (int i = 0; i < 7; i++) {
        Il2CppObject *o = g_bolt[i].get();
        if (!Alive(o)) continue;
        Vector3 dv = pts[i + 1] - pts[i], mid = (pts[i] + pts[i + 1]) * 0.5f;
        float len = sqrtf(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
        if (len < 0.01f) continue;
        Il2CppObject *t = Tr(o);
        G.trSetPosition[t](mid);
        G.trSetRotation[t](Quaternion::LookRotation(dv));
        G.trSetLocalScale[t](Vector3(0.8f, 0.8f, len));
        G.goSetActive[o](true);
    }
    g_boltHideAt = g_time + 0.22;
    g_flashAt = g_time;
    g_thunderAt = g_time + 0.4 + dist * 0.02;
}
static void HideBolt() {
    for (int i = 0; i < 7; i++) Show(g_bolt[i], false);
}
static void StormTick(Vector3 hp) {
    EnsureStorm();
    if (g_nextBolt == 0) g_nextBolt = g_time + 2.0 + Rand01() * 3.0;
    if (g_time >= g_nextBolt && Alive(g_bolt[0].get())) {
        StrikeBolt(hp);
        g_nextBolt = g_time + 5.0 + Rand01() * 7.0;               // one strike every 5-12 seconds
    }
    if (g_boltHideAt > 0 && g_time >= g_boltHideAt) { HideBolt(); g_boltHideAt = 0; }
    Il2CppObject *l = g_wxLight.get();
    if (Alive(l) && g_lightIntensity.IsValid()) {              // a dim flash that fades, with a small flicker
        float t = (float)(g_time - g_flashAt);
        float k = g_flashAt > 0 && t < 0.4f ? 1.0f - t / 0.4f : 0.0f;
        if (t > 0.12f && t < 0.2f) k *= 0.3f;
        G.behaviourSetEnabled[l](k > 0);
        if (k > 0) {
            G.trSetPosition[Tr(G.compGameObject[l]())](hp + Vector3(0, 5.0f, 0));
            g_lightIntensity[l](9.0f * k);
        }
    }
    if (g_thunderAt > 0 && g_time >= g_thunderAt) { PlayThunder(); g_thunderAt = 0; }
}

// ---- disco ------------------------------------------------------------------------
// A silver ball above your head with colored mirror tiles, spinning; six colored beams
// sweeping down from it; three colored lights circling you, pulsing to a 120 BPM beat;
// and the effect pool used as floating rainbow sparkles. Everything is made once, parented
// to the weather root (so it hides and is destroyed with the other effects), and only
// moved and recolored after that.
static constexpr int kDiscoFacets = 14, kDiscoBeams = 6, kDiscoLights = 3;
static Ref g_discoBall, g_discoFacet[kDiscoFacets], g_discoBeam[kDiscoBeams], g_discoLight[kDiscoLights];
static bool g_discoShown = false;

static void HideDisco() {
    if (!g_discoShown) return;
    g_discoShown = false;
    Show(g_discoBall, false);
    for (auto &b : g_discoBeam) Show(b, false);
    for (auto &l : g_discoLight) if (Alive(l.get())) G.behaviourSetEnabled[l.get()](false);
}
static void DiscoClear() {                                     // the objects died with the weather root
    g_discoBall.clear();
    for (auto &f : g_discoFacet) f.clear();
    for (auto &b : g_discoBeam) b.clear();
    for (auto &l : g_discoLight) l.clear();
    g_discoShown = false;
}
static void LightMethods(Class &light) {
    if (!g_lightType.IsValid()) g_lightType = light.GetMethod("set_type", 1);
    if (!g_lightIntensity.IsValid()) g_lightIntensity = light.GetMethod("set_intensity", 1);
    if (!g_lightRange.IsValid()) g_lightRange = light.GetMethod("set_range", 1);
    if (!g_lightColor.IsValid()) g_lightColor = light.GetMethod("set_color", 1);
}
static void EnsureDisco() {
    if (!EnsureWxRoot()) return;
    Il2CppObject *root = Tr(g_wxRoot.get());
    if (!Alive(g_discoBall.get())) {
        Il2CppObject *ball = Prim(0, Color(0.78f, 0.80f, 0.86f, 1), false);
        if (!ball) return;
        Il2CppObject *bt = Tr(ball);
        G.trSetParent[bt](root, false);
        G.trSetLocalScale[bt](Vector3(0.5f, 0.5f, 0.5f));
        g_discoBall.set(ball);
        for (int i = 0; i < kDiscoFacets; i++) {               // tiles spread over the ball (a golden-angle spiral)
            float y = 1 - 2 * (i + 0.5f) / kDiscoFacets, r = sqrtf(1 - y * y), a = i * 2.39996f;
            Vector3 d(cosf(a) * r, y, sinf(a) * r);
            Il2CppObject *f = Cube(Rainbow(i / (float)kDiscoFacets), false);
            if (!f) continue;
            Il2CppObject *ft = Tr(f);
            G.trSetParent[ft](bt, false);
            G.trSetLocalPosition[ft](d * 0.5f);
            G.trSetLocalRotation[ft](Quaternion::LookRotation(d));
            G.trSetLocalScale[ft](Vector3(0.2f, 0.2f, 0.04f));
            g_discoFacet[i].set(f);
        }
    }
    for (int i = 0; i < kDiscoBeams; i++) {
        if (Alive(g_discoBeam[i].get())) continue;
        Il2CppObject *b = Cube(Rainbow(i / (float)kDiscoBeams), false);
        if (!b) continue;
        G.trSetParent[Tr(b)](root, false);
        g_discoBeam[i].set(b);
    }
    Class light("UnityEngine", "Light");
    if (!light.IsValid()) return;
    LightMethods(light);
    for (int i = 0; i < kDiscoLights; i++) {
        if (Alive(g_discoLight[i].get())) continue;
        Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellDiscoLight"));
        Il2CppObject *l = go ? G.goAddComponent[go](light.GetMonoType()) : nullptr;
        if (!l) continue;
        G.trSetParent[Tr(go)](root, false);
        if (g_lightType.IsValid()) g_lightType[l](2);              // point light
        if (g_lightRange.IsValid()) g_lightRange[l](9.0f);
        g_discoLight[i].set(l);
    }
}
// Bundle Disco (bundle = true): the party you get when you buy a bundle. The ball drops in from
// the sky, and when it lands there is a white flash and a burst of confetti; after that the ball
// is bigger and spins faster, the beams sweep faster and change color together, the lights
// flash white on every beat (about 128 BPM), and confetti keeps falling.
static constexpr float kBundleDrop = 1.2f;                     // seconds the ball takes to drop in
static void DiscoTick(Vector3 hp, int intensity, bool bundle) {
    EnsureDisco();
    float t = (float)g_time;
    float bps = bundle ? 128.0f / 60.0f : 2.0f;
    float ph = t * bps - floorf(t * bps);                           // 0..1 through each beat
    float beat = 1.0f - ph;                                         // jumps to 1 on each beat, then fades
    beat = 0.55f + 0.45f * beat * beat;
    float since = bundle ? (float)(g_time - g_bundleStart) : 99.0f;
    float drop = since >= kBundleDrop ? 1.0f : 1.0f - powf(1.0f - since / kBundleDrop, 3.0f);   // eases in
    float grow = bundle ? std::min(1.0f, std::max(0.0f, (since - kBundleDrop) / 0.4f)) : 1.0f;  // beams after landing
    if (!g_discoShown) {
        g_discoShown = true;
        Show(g_discoBall, true);
        for (auto &b : g_discoBeam) Show(b, true);
    }
    Vector3 ball = hp + Vector3(0, 2.2f + (1.0f - drop) * 10.0f, 0);
    Il2CppObject *bo = g_discoBall.get();
    if (Alive(bo)) {
        Il2CppObject *bt = Tr(bo);
        float spin = t * (bundle ? 2.6f : 1.2f);
        float size = bundle ? 0.75f : 0.5f;
        G.trSetPosition[bt](ball);
        G.trSetRotation[bt](Quaternion(0, sinf(spin * 0.5f), 0, cosf(spin * 0.5f)));
        G.trSetLocalScale[bt](Vector3(size, size, size));
    }
    if (g_frames % 3 == 0)                                          // tiles twinkle through the colors
        for (int i = 0; i < kDiscoFacets; i++) SetColor(g_discoFacet[i].get(), Rainbow(t * 0.5f + i * 0.37f));
    const float len = 6.0f, tilt = 0.9f;                            // beams: about 50 degrees down from straight up
    for (int i = 0; i < kDiscoBeams; i++) {
        Il2CppObject *b = g_discoBeam[i].get();
        if (!Alive(b)) continue;
        float a = t * (bundle ? 1.8f : 0.8f) + i * 6.2831853f / kDiscoBeams, wob = tilt + 0.25f * sinf(t * (bundle ? 2.4f : 1.3f) + i);
        Vector3 d(cosf(a) * sinf(wob), -cosf(wob), sinf(a) * sinf(wob));
        float l = len * std::max(0.01f, grow);
        Il2CppObject *bt = Tr(b);
        G.trSetPosition[bt](ball + d * (l * 0.5f + 0.25f));
        G.trSetRotation[bt](Quaternion::LookRotation(d));
        G.trSetLocalScale[bt](Vector3(bundle ? 0.045f : 0.03f, bundle ? 0.045f : 0.03f, l));
        if (bundle) { if (g_frames % 2 == 0) SetColor(b, Rainbow(floorf(t * bps) * 0.17f + (i % 2) * 0.5f)); }   // all change on the beat
        else if (g_frames % 3 == 1) SetColor(b, Rainbow(t * 0.3f + i / (float)kDiscoBeams));
    }
    if (bundle) {
        const float strengthB[] = {3.0f, 4.5f, 6.0f};
        float landed = since - kBundleDrop;
        for (int i = 0; i < kDiscoLights; i++) {
            Il2CppObject *l = g_discoLight[i].get();
            if (!Alive(l)) continue;
            G.behaviourSetEnabled[l](since >= kBundleDrop * 0.5f);
            float a = t * 2.0f + i * 6.2831853f / kDiscoLights;
            G.trSetPosition[Tr(G.compGameObject[l]())](hp + Vector3(cosf(a) * 2.2f, 1.2f, sinf(a) * 2.2f));
            bool flash = landed >= 0 && landed < 0.3f;                  // the big flash when the ball lands
            bool strobe = ph < 0.12f;                                   // a white flash on each beat
            Color c = flash || strobe ? Color(1, 1, 1, 1) : Rainbow(floorf(t * bps) * 0.23f + i / (float)kDiscoLights);
            float k = flash ? 14.0f * (1.0f - landed / 0.3f) + strengthB[intensity] : strengthB[intensity] * (strobe ? 1.6f : beat);
            if (g_lightColor.IsValid()) g_lightColor[l](c);
            if (g_lightIntensity.IsValid()) g_lightIntensity[l](k);
        }
        return;
    }
    const float strength[] = {1.5f, 2.5f, 3.5f};
    for (int i = 0; i < kDiscoLights; i++) {
        Il2CppObject *l = g_discoLight[i].get();
        if (!Alive(l)) continue;
        G.behaviourSetEnabled[l](true);
        float a = t * 1.1f + i * 6.2831853f / kDiscoLights;
        G.trSetPosition[Tr(G.compGameObject[l]())](hp + Vector3(cosf(a) * 2.0f, 1.0f, sinf(a) * 2.0f));
        if (g_lightColor.IsValid()) g_lightColor[l](Rainbow(t * 0.25f + i / (float)kDiscoLights));
        if (g_lightIntensity.IsValid()) g_lightIntensity[l](strength[intensity] * beat);
    }
}

static void WeatherOff() {
    g_wxOn = false;
    g_fxMode = -1;
    g_nextBolt = 0;
    g_thunderAt = 0;
    g_boltHideAt = 0;
    g_flashAt = 0;
    Show(g_wxRoot, false);                                     // hides every piece; they are reused next time
    if (Alive(g_wxLight.get())) G.behaviourSetEnabled[g_wxLight.get()](false);
    if (Alive(g_wxAudioSrc.get()) && G.asStop.IsValid()) G.asStop[g_wxAudioSrc.get()]();
    HideBolt();
    HideDisco();
}
static void WeatherReset() {                                   // room change: the old objects are gone
    Kill(g_wxRoot);
    Kill(g_wxAudioGo);
    Kill(g_wxClip);
    for (int i = 0; i < kMaxFx; i++) { g_fx[i].clear(); g_fxT[i].clear(); }
    for (int i = 0; i < 7; i++) g_bolt[i].clear();
    DiscoClear();
    g_wxLight.clear();
    g_wxAudioSrc.clear();
    g_fxMade = 0; g_fxMode = -1; g_fxShown = -1;
    g_nextBolt = g_thunderAt = g_boltHideAt = g_flashAt = 0;
    g_wxOn = false;
}

// Effects and time of day only show on your own game: this repo doesn't allow sending
// them over the network (see AGENTS.md "Never add").
static const int g_roomWx = -1, g_roomWxInt = 1, g_roomTod = -1;

static void WeatherTick(float dt) {
    bool own = g_wx >= 0 && g_wxSel[g_wx];
    int mode = own ? g_wx : g_roomWx;                          // yours wins; else the room's
    int intensity = own ? g_wxIntensity : g_roomWxInt;
    Il2CppObject *head = g_head.get();
    if (mode < 0 || !head) { if (g_wxOn) WeatherOff(); return; }
    if (!EnsureWxRoot()) return;
    if (!g_wxOn) { g_wxOn = true; Show(g_wxRoot, true); }
    Vector3 hp = G.trGetPosition[head]();
    if (g_time >= g_nextGround) {                              // ground height, four times a second
        g_nextGround = g_time + 0.25;
        int mask = G.locomotionLayers.IsValid() && g_player.get() ? G.locomotionLayers[g_player.get()]() : -5;
        BNM::Structures::Unity::RaycastHit hit{};
        bool found = G.raycast.IsValid() && G.raycast(hp, Vector3(0, -1, 0), &hit, 8.0f, mask ? mask : -5);
        g_groundY = found ? hit.point.y : hp.y - 2.5f;
    }
    int want = kFxCounts[mode][intensity];
    for (int n = 0; n < 25 && g_fxMade < kMaxFx; n++) {        // make the pool a few pieces per frame
        Il2CppObject *o = Prim(3, Color(1, 1, 1, 1), false);
        if (!o) break;
        int i = g_fxMade++;
        Il2CppObject *t = Tr(o);
        G.trSetParent[t](Tr(g_wxRoot.get()), false);
        g_fx[i].set(o);
        g_fxT[i].set(t);
        StyleFx(i, mode);
        RespawnFx(i, hp, true);
        G.goSetActive[o](i < want);
    }
    if (g_fxMade == 0) return;
    if (mode != g_fxMode) {                                    // switched effect: restyle and refill the air
        g_fxMode = mode;
        for (int i = 0; i < g_fxMade; i++) { StyleFx(i, mode); RespawnFx(i, hp, mode != 4); }
        g_fxShown = -1;
        if (mode == 4) { g_bundleStart = g_time; g_bundleBurst = false; }
    }
    if (mode == 4 && !g_bundleBurst && g_time - g_bundleStart >= kBundleDrop) {   // the ball landed: confetti burst
        g_bundleBurst = true;
        Vector3 from = hp + Vector3(0, 2.2f, 0);
        for (int i = 0; i < g_fxMade; i++) {
            float a = Rand01() * 6.2831853f, up = 0.2f + Rand01() * 0.9f, sp = 3.0f + Rand01() * 3.5f;
            g_fxPos[i] = from;
            g_fxVel[i] = Vector3(cosf(a) * sp * (1 - up * 0.5f), up * sp, sinf(a) * sp * (1 - up * 0.5f));
        }
    }
    if (want != g_fxShown) {
        for (int i = 0; i < g_fxMade; i++) if (Alive(g_fx[i].get())) G.goSetActive[g_fx[i].get()](i < want);
        g_fxShown = want;
    }
    int active = std::min(want, g_fxMade);
    float t = (float)g_time;
    for (int i = 0; i < active; i++) {
        Vector3 &p = g_fxPos[i];
        if (mode == 0) {                                       // snow: slow, swaying
            p.y -= 1.1f * dt;
            p.x += sinf(t * 1.1f + g_fxPhase[i]) * 0.35f * dt;
            p.z += cosf(t * 0.9f + g_fxPhase[i]) * 0.35f * dt;
        } else if (mode == 4) {                                // bundle disco: confetti flies, then flutters down
            Vector3 &v = g_fxVel[i];
            float drag = std::max(0.0f, 1.0f - 1.8f * dt);
            v = Vector3(v.x * drag, v.y - 5.0f * dt, v.z * drag);
            if (v.y < -0.9f) v.y = -0.9f;                      // paper falls slowly
            p = p + v * dt;
            p.x += sinf(t * 3.0f + g_fxPhase[i]) * 0.45f * dt;
            p.z += cosf(t * 2.6f + g_fxPhase[i]) * 0.45f * dt;
            if (Il2CppObject *tr = g_fxT[i].get()) {           // tumbling
                float ang = t * (3.0f + g_fxPhase[i]) + g_fxPhase[i], ph = g_fxPhase[i];
                Vector3 ax(cosf(ph), 0.6f, sinf(ph));
                float n = sqrtf(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z), s2 = sinf(ang * 0.5f) / n;
                G.trSetRotation[tr](Quaternion(ax.x * s2, ax.y * s2, ax.z * s2, cosf(ang * 0.5f)));
            }
        } else if (mode == 3) {                                // disco: sparkles float and circle around you
            float dx = p.x - hp.x, dz = p.z - hp.z;
            p.x += -dz * 0.25f * dt;
            p.z += dx * 0.25f * dt;
            p.y += sinf(t * 2.0f + g_fxPhase[i]) * 0.25f * dt;
            if ((g_frames + i) % 4 == 0) SetColor(g_fx[i].get(), Rainbow(t * 0.4f + g_fxPhase[i]));
        } else {                                               // rain: fast, a little wind
            p.y -= 9.0f * dt;
            p.x += 0.6f * dt;
        }
        float dx = p.x - hp.x, dz = p.z - hp.z;
        if (p.y < g_groundY || dx * dx + dz * dz > (mode == 4 ? 49.0f : 25.0f)) RespawnFx(i, hp, mode == 3);
        if (Il2CppObject *tr = g_fxT[i].get()) G.trSetPosition[tr](p);
    }
    if (mode == 2) StormTick(hp);
    else { HideBolt(); g_nextBolt = 0; }
    if (mode == 3 || mode == 4) DiscoTick(hp, intensity, mode == 4);
    else HideDisco();
}

// ---- time of day: morning, afternoon, sunset, midnight ----------------------------------
// Only changes your own game; nothing is sent to the room.
// Yeeps maps have baked lighting, so changing the scene's lights alone barely shows. So the
// time of day is drawn with objects of our own as well:
//   - a sky dome: a big sphere around you, seen from the inside, in the sky's color
//     (it covers the map's skybox; it is kept inside the camera's view distance)
//   - a sun or moon and stars inside the dome
//   - a color filter: a small sphere around your eyes that multiplies everything you see by
//     a color (darker and bluer at midnight, orange at sunset). It only draws if the shader
//     takes a blend mode, so it can never black out your view.
// The scene's light, ambient and fog are changed too where the map uses them. The game's
// own values are saved the first time and put back after.
struct TodPreset {
    float az, el;                 // where the sun (or moon) is, in degrees
    Color light; float lightI;    // the scene's directional lights
    Color ambient;
    Color sky; float exposure;    // skybox tint and brightness
    bool fog; Color fogC; float fogStart, fogEnd;
    Color ball; float ballSize; bool moon, stars;
};
static const TodPreset kTod[4] = {
    {  0, 15, Color(1.0f, 0.86f, 0.70f, 1), 1.0f,  Color(0.55f, 0.55f, 0.60f, 1), Color(0.80f, 0.82f, 0.95f, 1), 1.1f,
       true, Color(0.86f, 0.86f, 0.92f, 1), 30, 260, Color(1.0f, 0.88f, 0.60f, 1), 7, false, false},
    { 30, 65, Color(1.0f, 0.97f, 0.92f, 1), 1.25f, Color(0.70f, 0.72f, 0.76f, 1), Color(0.55f, 0.70f, 1.00f, 1), 1.3f,
       false, Color(1, 1, 1, 1), 0, 0,             Color(1.0f, 0.98f, 0.85f, 1), 6, false, false},
    {180,  4, Color(1.0f, 0.50f, 0.25f, 1), 0.9f,  Color(0.55f, 0.38f, 0.35f, 1), Color(1.00f, 0.55f, 0.35f, 1), 0.9f,
       true, Color(0.95f, 0.52f, 0.36f, 1), 40, 260, Color(1.0f, 0.45f, 0.15f, 1), 10, false, false},
    {220, 45, Color(0.55f, 0.65f, 1.0f, 1), 0.25f, Color(0.08f, 0.09f, 0.16f, 1), Color(0.08f, 0.10f, 0.25f, 1), 0.25f,
       true, Color(0.03f, 0.04f, 0.09f, 1), 15, 120, Color(0.90f, 0.92f, 1.0f, 1), 1.8f, true, true},
};
static constexpr int kStars = 60, kMaxTodLights = 8;

static struct {
    bool init = false;
    Method<int> ambMode, fogMode, lType;
    Method<Color> ambLight, fogColor, matGetColor, lColor;
    Method<float> ambInt, fogStart, fogEnd, matGetFloat, lIntensity;
    Method<bool> fog, matHas;
    Method<void> setAmbMode, setAmbLight, setAmbInt, setFog, setFogMode, setFogColor, setFogStart, setFogEnd,
                 matSetColorN, matSetFloat, matSetQueue, matKeyword;
    Method<Il2CppObject *> sky, camMain;
    Method<float> camFar;
} TD;
static void TodInit() {
    if (TD.init) return;
    TD.init = true;
    Class rs("UnityEngine", "RenderSettings"), mat("UnityEngine", "Material"), light("UnityEngine", "Light");
    if (rs.IsValid()) {
        TD.ambMode = rs.GetMethod("get_ambientMode", 0);      TD.setAmbMode = rs.GetMethod("set_ambientMode", 1);
        TD.ambLight = rs.GetMethod("get_ambientLight", 0);    TD.setAmbLight = rs.GetMethod("set_ambientLight", 1);
        TD.ambInt = rs.GetMethod("get_ambientIntensity", 0);  TD.setAmbInt = rs.GetMethod("set_ambientIntensity", 1);
        TD.fog = rs.GetMethod("get_fog", 0);                  TD.setFog = rs.GetMethod("set_fog", 1);
        TD.fogMode = rs.GetMethod("get_fogMode", 0);          TD.setFogMode = rs.GetMethod("set_fogMode", 1);
        TD.fogColor = rs.GetMethod("get_fogColor", 0);        TD.setFogColor = rs.GetMethod("set_fogColor", 1);
        TD.fogStart = rs.GetMethod("get_fogStartDistance", 0); TD.setFogStart = rs.GetMethod("set_fogStartDistance", 1);
        TD.fogEnd = rs.GetMethod("get_fogEndDistance", 0);    TD.setFogEnd = rs.GetMethod("set_fogEndDistance", 1);
        TD.sky = rs.GetMethod("get_skybox", 0);
    }
    if (mat.IsValid()) {
        TD.matHas = Overload(mat, "HasProperty", {"System.String"});
        TD.matGetColor = Overload(mat, "GetColor", {"System.String"});
        TD.matSetColorN = Overload(mat, "SetColor", {"System.String", "UnityEngine.Color"});
        TD.matGetFloat = Overload(mat, "GetFloat", {"System.String"});
        TD.matSetFloat = Overload(mat, "SetFloat", {"System.String", "System.Single"});
        TD.matSetQueue = mat.GetMethod("set_renderQueue", 1);
        TD.matKeyword = Overload(mat, "EnableKeyword", {"System.String"});
    }
    Class cam("UnityEngine", "Camera");
    if (cam.IsValid()) {
        TD.camMain = cam.GetMethod("get_main", 0);
        TD.camFar = cam.GetMethod("get_farClipPlane", 0);
    }
    if (light.IsValid()) {
        LightMethods(light);
        TD.lType = light.GetMethod("get_type", 0);
        TD.lColor = light.GetMethod("get_color", 0);
        TD.lIntensity = light.GetMethod("get_intensity", 0);
    }
    ym::log("clientdabell: time of day render=%d fog=%d sky=%d mat=%d/%d light=%d/%d", TD.setAmbLight.IsValid(),
            TD.setFog.IsValid(), TD.sky.IsValid(), TD.matHas.IsValid(), TD.matSetFloat.IsValid(),
            TD.lType.IsValid(), g_lightIntensity.IsValid());
}

static Il2CppObject *StrArg(const char *s) { return (Il2CppObject *)CreateMonoString(s); }

// What the scene had before we changed it.
static struct {
    bool saved = false;
    int ambMode = 0, fogMode = 1; Color ambLight, fogColor; float ambInt = 1, fogStart = 0, fogEnd = 300; bool fog = false;
    Ref sky; bool hasTint = false, hasSkyTint = false, hasExposure = false; Color tint, skyTint; float exposure = 1;
    Ref light[kMaxTodLights]; Color lightC[kMaxTodLights]; float lightI[kMaxTodLights]; Quaternion lightR[kMaxTodLights];
    int lights = 0;
} TS;
static Ref g_todRoot, g_todBall, g_todStar[kStars], g_todOwnLight, g_todDome, g_todTint;
static Vector3 g_starDir[kStars];
static int g_todShown = -1;
static double g_nextTodApply = 0;
static float g_todR = 250;                     // sky dome radius
static bool g_todSceneBroken = false;          // the scene's lights/fog can't be changed here: our own sky still works
// The color filter for each time of day (multiplied with everything you see). White = none.
static const Color kTodTint[4] = {Color(1.0f, 0.94f, 0.86f, 1), Color(1, 1, 1, 1), Color(1.0f, 0.70f, 0.52f, 1),
                                  Color(0.30f, 0.34f, 0.55f, 1)};
static Il2CppObject *MatOf(Il2CppObject *go) {
    Il2CppObject *r = Alive(go) ? G.goGetComponent[go](G.tRenderer) : nullptr;
    return r ? G.rendererGetMaterial[r]() : nullptr;
}
static bool MatHas(Il2CppObject *m, const char *prop) { return m && TD.matHas.IsValid() && TD.matHas[m](StrArg(prop)); }

static void TodPicked() {
    int picked = -1;
    for (int i = 0; i < 4; i++) if (g_todSel[i] && i != g_tod) picked = i;
    for (int i = 0; i < 4; i++) g_todSel[i] = (i == picked);
    g_tod = picked;
}
static Vector3 SkyDir(float azDeg, float elDeg) {
    float az = azDeg * 0.0174533f, el = elDeg * 0.0174533f;
    return Vector3(cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az));
}

static void TodSave() {
    if (TS.saved) return;
    TS.saved = true;
    if (TD.ambMode.IsValid()) TS.ambMode = TD.ambMode();
    if (TD.ambLight.IsValid()) TS.ambLight = TD.ambLight();
    if (TD.ambInt.IsValid()) TS.ambInt = TD.ambInt();
    if (TD.fog.IsValid()) TS.fog = TD.fog();
    if (TD.fogMode.IsValid()) TS.fogMode = TD.fogMode();
    if (TD.fogColor.IsValid()) TS.fogColor = TD.fogColor();
    if (TD.fogStart.IsValid()) TS.fogStart = TD.fogStart();
    if (TD.fogEnd.IsValid()) TS.fogEnd = TD.fogEnd();
    Il2CppObject *sky = TD.sky.IsValid() ? TD.sky() : nullptr;
    if (Alive(sky) && TD.matHas.IsValid()) {
        TS.sky.set(sky);
        // Unity's built-in skyboxes use _Tint (cubemap / 6-sided) or _SkyTint (procedural), plus _Exposure
        if ((TS.hasTint = TD.matHas[sky](StrArg("_Tint")))) TS.tint = TD.matGetColor[sky](StrArg("_Tint"));
        if ((TS.hasSkyTint = TD.matHas[sky](StrArg("_SkyTint")))) TS.skyTint = TD.matGetColor[sky](StrArg("_SkyTint"));
        if ((TS.hasExposure = TD.matHas[sky](StrArg("_Exposure")))) TS.exposure = TD.matGetFloat[sky](StrArg("_Exposure"));
    }
    TS.lights = 0;
    Class light("UnityEngine", "Light");
    if (light.IsValid() && TD.lType.IsValid() && (G.findScene.IsValid() || G.findSceneByType.IsValid())) {
        auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)light.GetMonoType())
                                                      : G.findSceneByType((Il2CppObject *)light.GetMonoType(), 0));
        uintptr_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (uintptr_t i = 0; i < n && TS.lights < kMaxTodLights; i++) {
            Il2CppObject *l = ((Il2CppObject **)(arr + 0x20))[i];
            if (!Alive(l) || TD.lType[l]() != 1) continue;          // 1 = Directional
            int k = TS.lights++;
            TS.light[k].set(l);
            TS.lightC[k] = TD.lColor[l]();
            TS.lightI[k] = TD.lIntensity[l]();
            TS.lightR[k] = G.trGetRotation[Tr(G.compGameObject[l]())]();
        }
    }
    ym::log("clientdabell: time of day saved: sky=%d tint=%d/%d exposure=%d, %d sun lights", Alive(sky) ? 1 : 0,
            TS.hasTint, TS.hasSkyTint, TS.hasExposure, TS.lights);
}
// sceneToo = false after a room change, when RenderSettings may already belong to a new scene
static void TodRestore(bool sceneToo) {
    if (!TS.saved) return;
    if (sceneToo) {
        if (TD.setAmbMode.IsValid()) TD.setAmbMode(TS.ambMode);
        if (TD.setAmbLight.IsValid()) TD.setAmbLight(TS.ambLight);
        if (TD.setAmbInt.IsValid()) TD.setAmbInt(TS.ambInt);
        if (TD.setFog.IsValid()) TD.setFog(TS.fog);
        if (TD.setFogMode.IsValid()) TD.setFogMode(TS.fogMode);
        if (TD.setFogColor.IsValid()) TD.setFogColor(TS.fogColor);
        if (TD.setFogStart.IsValid()) TD.setFogStart(TS.fogStart);
        if (TD.setFogEnd.IsValid()) TD.setFogEnd(TS.fogEnd);
    }
    Il2CppObject *sky = TS.sky.get();                  // the skybox is an asset: always put it back
    if (Alive(sky)) {
        if (TS.hasTint) TD.matSetColorN[sky](StrArg("_Tint"), TS.tint);
        if (TS.hasSkyTint) TD.matSetColorN[sky](StrArg("_SkyTint"), TS.skyTint);
        if (TS.hasExposure) TD.matSetFloat[sky](StrArg("_Exposure"), TS.exposure);
    }
    for (int k = 0; k < TS.lights; k++) {
        Il2CppObject *l = TS.light[k].get();
        if (Alive(l)) {
            g_lightColor[l](TS.lightC[k]);
            g_lightIntensity[l](TS.lightI[k]);
            G.trSetRotation[Tr(G.compGameObject[l]())](TS.lightR[k]);
        }
        TS.light[k].clear();
    }
    TS.lights = 0;
    TS.sky.clear();
    TS.saved = false;
}
static void EnsureTodObjects() {
    if (Alive(g_todRoot.get())) return;
    Il2CppObject *root = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellSky"));
    if (!root) return;
    g_todRoot.set(root);
    Il2CppObject *rt = Tr(root);
    if (Il2CppObject *ball = Prim(0, Color(1, 1, 1, 1), false)) { G.trSetParent[Tr(ball)](rt, false); g_todBall.set(ball); }
    for (int i = 0; i < kStars; i++) {
        Il2CppObject *st = Cube(Color(0.92f, 0.94f, 1.0f, 1), false);
        if (!st) continue;
        float az = Rand01() * 360.0f, el = 15.0f + Rand01() * 70.0f, size = 0.12f + Rand01() * 0.16f;
        g_starDir[i] = SkyDir(az, el);
        Il2CppObject *t = Tr(st);
        G.trSetParent[t](rt, false);
        G.trSetLocalPosition[t](g_starDir[i] * 34.0f);       // inside the night fog's start, so they stay visible
        G.trSetLocalScale[t](Vector3(size, size, size));
        g_todStar[i].set(st);
    }
    // How far the camera can see: the dome has to sit inside that or it isn't drawn.
    Il2CppObject *cam = TD.camMain.IsValid() ? TD.camMain() : nullptr;
    float far = Alive(cam) && TD.camFar.IsValid() ? TD.camFar[cam]() : 0.0f;
    g_todR = far > 1.0f ? std::max(60.0f, std::min(300.0f, far * 0.85f)) : 250.0f;
    // Sky dome: the URP shader's _Cull set to Front draws the inside of the sphere.
    if (Il2CppObject *d = Prim(0, Color(1, 1, 1, 1), false)) {
        Il2CppObject *m = MatOf(d);
        if (MatHas(m, "_Cull") && TD.matSetFloat.IsValid()) {
            TD.matSetFloat[m](StrArg("_Cull"), 1.0f);              // 1 = Front: we see the inside
            Il2CppObject *dt = Tr(d);
            G.trSetParent[dt](rt, false);
            G.trSetLocalPosition[dt](Vector3(0, 0, 0));
            G.trSetLocalScale[dt](Vector3(g_todR * 2, g_todR * 2, g_todR * 2));
            g_todDome.set(d);
        } else { G.destroy(d); ym::log("clientdabell: time of day: no sky dome (shader has no _Cull)"); }
    }
    // Color filter around your eyes: multiply blend (result = what's behind * our color), so
    // its alpha doesn't matter. Only made when the shader takes the blend settings.
    Il2CppObject *eyes = Alive(cam) ? G.compTransform[cam]() : g_head.get();
    if (eyes) if (Il2CppObject *f = Prim(0, Color(1, 1, 1, 1), false)) {
        Il2CppObject *m = MatOf(f);
        if (MatHas(m, "_SrcBlend") && MatHas(m, "_DstBlend") && MatHas(m, "_Cull") && MatHas(m, "_ZWrite") &&
            TD.matSetFloat.IsValid() && TD.matSetQueue.IsValid()) {
            auto F = [&](const char *n, float v) { if (MatHas(m, n)) TD.matSetFloat[m](StrArg(n), v); };
            F("_Surface", 1);       // transparent
            F("_Blend", 3);         // multiply
            F("_SrcBlend", 2);      // DstColor
            F("_DstBlend", 0);      // Zero
            F("_SrcBlendAlpha", 0);
            F("_DstBlendAlpha", 1);
            F("_ZWrite", 0);
            F("_Cull", 0);          // both sides
            if (TD.matKeyword.IsValid()) TD.matKeyword[m](StrArg("_SURFACE_TYPE_TRANSPARENT"));
            TD.matSetQueue[m](4000);   // after everything else
            Il2CppObject *ft = Tr(f);
            G.trSetParent[ft](eyes, false);
            G.trSetLocalPosition[ft](Vector3(0, 0, 0));
            G.trSetLocalScale[ft](Vector3(0.5f, 0.5f, 0.5f));
            G.goSetActive[f](false);
            g_todTint.set(f);
        } else { G.destroy(f); ym::log("clientdabell: time of day: no color filter (shader has no blend settings)"); }
    }
    ym::log("clientdabell: time of day objects: far=%.0f dome=%d (r=%.0f) filter=%d", far, Alive(g_todDome.get()) ? 1 : 0,
            g_todR, Alive(g_todTint.get()) ? 1 : 0);
    G.goSetActive[root](false);
}
static void TodApply(int tod) {
    const TodPreset &p = kTod[tod];
    Vector3 toSun = SkyDir(p.az, p.el);
    if (TD.setAmbMode.IsValid()) TD.setAmbMode(3);                 // Flat
    if (TD.setAmbLight.IsValid()) TD.setAmbLight(p.ambient);
    if (TD.setAmbInt.IsValid()) TD.setAmbInt(1.0f);
    if (TD.setFog.IsValid()) TD.setFog(p.fog || TS.fog);
    if (p.fog) {
        if (TD.setFogMode.IsValid()) TD.setFogMode(1);              // Linear
        if (TD.setFogColor.IsValid()) TD.setFogColor(p.fogC);
        if (TD.setFogStart.IsValid()) TD.setFogStart(p.fogStart);
        if (TD.setFogEnd.IsValid()) TD.setFogEnd(p.fogEnd);
    } else if (TS.fog) {                                            // keep the map's own fog, recolored to the sky
        if (TD.setFogColor.IsValid()) TD.setFogColor(p.sky);
    }
    Il2CppObject *sky = TS.sky.get();
    if (Alive(sky)) {
        if (TS.hasTint) TD.matSetColorN[sky](StrArg("_Tint"), Color(p.sky.r * 0.5f, p.sky.g * 0.5f, p.sky.b * 0.5f, 1));   // 0.5 is "no tint"
        if (TS.hasSkyTint) TD.matSetColorN[sky](StrArg("_SkyTint"), p.sky);
        if (TS.hasExposure) TD.matSetFloat[sky](StrArg("_Exposure"), p.exposure);
    }
    Quaternion rot = Quaternion::LookRotation(Vector3(0, 0, 0) - toSun);
    for (int k = 0; k < TS.lights; k++) {
        Il2CppObject *l = TS.light[k].get();
        if (!Alive(l)) continue;
        g_lightColor[l](p.light);
        g_lightIntensity[l](p.lightI);
        G.trSetRotation[Tr(G.compGameObject[l]())](rot);
    }
    if (TS.lights == 0 && Alive(g_todRoot.get())) {                // the map has no sun light: use our own
        if (!Alive(g_todOwnLight.get())) {
            Class light("UnityEngine", "Light");
            Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellSun"));
            Il2CppObject *l = go && light.IsValid() ? G.goAddComponent[go](light.GetMonoType()) : nullptr;
            if (l) { G.trSetParent[Tr(go)](Tr(g_todRoot.get()), false); if (g_lightType.IsValid()) g_lightType[l](1); g_todOwnLight.set(l); }
        }
        Il2CppObject *l = g_todOwnLight.get();
        if (Alive(l)) {
            g_lightColor[l](p.light);
            g_lightIntensity[l](p.lightI * 0.6f);
            G.trSetRotation[Tr(G.compGameObject[l]())](rot);
        }
    }
}
static void TodOff() {
    if (g_todShown < 0) return;
    g_todShown = -1;
    Show(g_todRoot, false);
    Show(g_todTint, false);
    try { TodRestore(true); } catch (...) { g_todSceneBroken = true; }
}
static void TodReset() {                                           // room change
    Il2CppObject *sky = TD.sky.IsValid() ? TD.sky() : nullptr;
    TodRestore(TS.saved && sky && sky == TS.sky.get());            // same scene still: put everything back
    Kill(g_todRoot);
    Kill(g_todTint);
    g_todBall.clear(); g_todOwnLight.clear(); g_todDome.clear();
    for (auto &s : g_todStar) s.clear();
    g_todShown = -1;
    g_nextTodApply = 0;
    g_todSceneBroken = false;                                      // new map: try its lights again
}
static void TodTick() {
    bool own = g_tod >= 0 && g_todSel[g_tod];
    int tod = own ? g_tod : g_roomTod;
    Il2CppObject *head = g_head.get();
    if (tod < 0 || !head) { TodOff(); return; }
    TodInit();
    if (!g_todSceneBroken) {
        try { TodSave(); }
        catch (...) { g_todSceneBroken = true; ym::log("clientdabell: time of day: scene lights can't be changed, using our own sky only"); }
    }
    EnsureTodObjects();
    const TodPreset &p = kTod[tod];
    if (tod != g_todShown) {                                       // new pick: restyle the sky objects
        g_todShown = tod;
        g_nextTodApply = 0;
        Show(g_todRoot, true);
        if (Alive(g_todDome.get())) SetColor(g_todDome.get(), p.fog ? p.fogC : p.sky);
        Il2CppObject *f = g_todTint.get();
        if (Alive(f)) {
            Color c = kTodTint[tod];
            bool on = c.r < 0.99f || c.g < 0.99f || c.b < 0.99f;
            if (on) SetColor(f, c);
            G.goSetActive[f](on);
        }
        Il2CppObject *ball = g_todBall.get();
        if (Alive(ball)) {
            float dist = std::min(p.moon ? 34.0f : 120.0f, g_todR * 0.6f);
            Il2CppObject *bt = Tr(ball);
            G.trSetLocalPosition[bt](SkyDir(p.az, p.el) * dist);
            G.trSetLocalScale[bt](Vector3(p.ballSize, p.ballSize, p.ballSize));
            SetColor(ball, p.ball);
        }
        for (auto &s : g_todStar) Show(s, p.stars);
    }
    Il2CppObject *root = g_todRoot.get();
    if (Alive(root)) G.trSetPosition[Tr(root)](G.trGetPosition[head]());   // the sky follows you
    if (g_time >= g_nextTodApply && !g_todSceneBroken) {          // again now and then, in case the game resets it
        g_nextTodApply = g_time + 0.5;
        try { TodApply(tod); }
        catch (...) { g_todSceneBroken = true; ym::log("clientdabell: time of day: scene lights can't be changed, using our own sky only"); }
    }
}

// ---- Overpowered (all of it changes only your own body; nothing is sent) ---------------
// Very Long Arms moves your own hands, like the dances do, so the room sees the long arms
// the way it sees any pose: the game sends your hands as usual and nothing extra goes out.
static const float kArmScales[] = {1.5f, 2.0f, 3.0f, 4.0f};
static int g_armScale = 1;
static void NextArmLength() { g_armScale = (g_armScale + 1) % 4; Notify("Arm length: %.1fx", kArmScales[g_armScale]); }

// To stretch an arm the controller trackers have to be paused (they would put the hand back),
// so the real controller position is read from the headset's input instead. That position is
// in the space of one of the hand's parents; the first frames check which one by comparing
// with where the hand really is. If nothing matches, Very Long Arms switches itself off.
static int g_laState = 0;                      // 0 starting, 1 checking, 2 working, 3 not supported
static int g_laChecks = 0, g_laSpaceL = -1, g_laSpaceR = -1;
static bool g_laOwns = false, g_laInit = false;
static double g_laNextPause = 0;
// Where the controller position comes from. This game build hides the XR feature-value reader, so
// try the older XR calls and the Oculus ones; whichever exists is used (and logged once).
static int g_laProv = 0;                       // 0 none, 1 XR InputTracking, 2 OVRInput
static Method<Vector3> g_provPos;
static Method<Quaternion> g_provRot;
static void LongArmsToggled() { g_laState = 0; g_laChecks = 0; g_laSpaceL = g_laSpaceR = -1; }

static bool LaInit() {
    if (g_laInit) return g_laProv != 0;
    g_laInit = true;
    Class it("UnityEngine.XR", "InputTracking");
    if (it.IsValid()) {
        g_provPos = it.GetMethod("GetLocalPosition", 1);
        g_provRot = it.GetMethod("GetLocalRotation", 1);
        if (g_provPos.IsValid() && g_provRot.IsValid()) g_laProv = 1;
    }
    if (!g_laProv) {
        Class ov("", "OVRInput");
        if (ov.IsValid()) {
            g_provPos = ov.GetMethod("GetLocalControllerPosition", 1);
            g_provRot = ov.GetMethod("GetLocalControllerRotation", 1);
            if (g_provPos.IsValid() && g_provRot.IsValid()) g_laProv = 2;
        }
    }
    ym::log("clientdabell: long arms controller source: %s", g_laProv == 1 ? "XR InputTracking" : g_laProv == 2 ? "OVRInput" : "none");
    return g_laProv != 0;
}
// The controller's real position and rotation in the world, as one of the hand's parents sees it.
static bool ReadController(int node, Il2CppObject *hand, int &space, Vector3 &pos, Quaternion &rot) {
    int arg = g_laProv == 1 ? node : (node == 4 ? 1 : 2);        // XRNode 4/5, or OVRInput.Controller LTouch/RTouch
    Vector3 lp = g_provPos(arg);
    Quaternion lr = g_provRot(arg);
    Il2CppObject *a = G.trGetParent[hand]();
    Vector3 handPos = G.trGetPosition[hand]();
    int best = -1;
    float bestErr = 0.10f;
    for (int k = 0; k < 3 && a; k++, a = G.trGetParent[a]()) {
        if (space >= 0 && k != space) continue;
        Vector3 w = G.trTransformPoint[a](lp), e = w - handPos;
        float err = sqrtf(Dot(e, e));
        if (space >= 0 || err < bestErr) { best = k; bestErr = err; pos = w; rot = G.trGetRotation[a]() * lr; }
        if (space >= 0) break;
    }
    if (best < 0) return false;
    if (space < 0) space = best;
    return true;
}
// Stretch mode needs the trackers to place the hands during Update (before this mod runs), not just
// before rendering, or they would move the hands back after every stretch. Done once per room.
static bool g_laTrackersSet = false;
static void SetTrackersToUpdate() {
    if (g_laTrackersSet || !G.trGetParent.IsValid()) return;
    g_laTrackersSet = true;
    int found = 0, changed = 0;
    for (auto nc : std::vector<std::tuple<const char *, const char *, const char *>>{
             {"UnityEngine.XR.Interaction.Toolkit", "XRBaseController", "set_updateTrackingType"},
             {"UnityEngine.InputSystem.XR", "TrackedPoseDriver", "set_updateType"},
             {"UnityEngine.SpatialTracking", "TrackedPoseDriver", "set_updateType"}}) {
        Class c(std::get<0>(nc), std::get<1>(nc));
        if (!c.IsValid()) continue;
        Method<void> setter = c.GetMethod(std::get<2>(nc), 1);
        for (Il2CppObject *t : {g_leftHand.get(), g_rightHand.get()})
            for (int up = 0; up < 3 && t; up++, t = G.trGetParent[t]()) {
                Il2CppObject *go = G.compGameObject[t]();
                Il2CppObject *comp = go ? G.goGetComponent[go]((Il2CppObject *)c.GetMonoType()) : nullptr;
                if (!comp) continue;
                found++;
                if (setter.IsValid()) { setter[comp](0); changed++; }
            }
    }
    ym::log("clientdabell: long arms stretch: %d trackers found, %d set to Update", found, changed);
}
static bool g_laStretch = false;               // fallback: stretch the hand from where the trackers just put it
static void LongArmsFail(const char *why) {
    g_laState = 3;
    if (g_laOwns && !g_dancing) PauseTrackers(false);
    g_laOwns = false;
    f_longArms = false;
    g_dirty = true;
    ym::log("clientdabell: long arms off: %s", why);
    Notify("Long Arms isn't supported here");
}
static void LongArmsTick() {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    bool want = f_longArms && !g_menuShown && !g_dancing && head && lh && rh;
    if (!want) {
        if (g_laOwns && !g_dancing) { PauseTrackers(false); g_laOwns = false; }   // the menu and the dances need normal hands
        if (!f_longArms) g_laState = 0;
        return;
    }
    Vector3 pl, pr;
    Quaternion ql, qr;
    if (!g_laStretch && (!LaInit() || !G.trTransformPoint.IsValid() || !G.trGetParent.IsValid() || !G.trGetRotation.IsValid())) {
        g_laStretch = true;
        ym::log("clientdabell: long arms: controller input not available, using stretch mode");
    }
    if (g_laStretch) {                                             // the trackers place the hands each frame; push them outward from the chest
        SetTrackersToUpdate();
        Vector3 chest = G.trGetPosition[head]() + Vector3(0, -0.25f, 0);
        float k = kArmScales[g_armScale];
        static int logged = 0;
        for (Il2CppObject *h : {lh, rh}) {
            Vector3 before = G.trGetPosition[h]();
            G.trSetPosition[h](chest + (before - chest) * k);
            if (logged < 4) { Vector3 after = G.trGetPosition[h](); ym::log("clientdabell: stretch hand %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f", before.x, before.y, before.z, after.x, after.y, after.z); logged++; }
        }
        return;
    }
    if (g_laState < 2) {                                           // trackers still on: find out how to read the controllers
        bool okL = ReadController(4, lh, g_laSpaceL, pl, ql), okR = ReadController(5, rh, g_laSpaceR, pr, qr);
        if (okL && okR) { g_laState = 2; ym::log("clientdabell: long arms ready (spaces %d/%d)", g_laSpaceL, g_laSpaceR); }
        else if (++g_laChecks > 60) { g_laStretch = true; ym::log("clientdabell: long arms: controller positions don't match the hands, using stretch mode"); }
        return;
    }
    if (g_pausedN == 0 && g_time >= g_laNextPause) {               // pause the trackers (retry twice a second)
        g_laNextPause = g_time + 0.5;
        PauseTrackers(true);
        g_laOwns = g_pausedN > 0;
        if (!g_laOwns) ym::log("clientdabell: long arms: couldn't pause the hand trackers");
    }
    if (!ReadController(4, lh, g_laSpaceL, pl, ql) || !ReadController(5, rh, g_laSpaceR, pr, qr)) return;
    Vector3 chest = G.trGetPosition[head]() + Vector3(0, -0.25f, 0);
    float k = kArmScales[g_armScale];
    G.trSetPosition[lh](chest + (pl - chest) * k);
    G.trSetRotation[lh](ql);
    G.trSetPosition[rh](chest + (pr - chest) * k);
    G.trSetRotation[rh](qr);
}

// Mega Jump: press B (Y if the menu is on your right hand) for a jump of any power, in the air too.
static const float kJumpPowers[] = {6.0f, 10.0f, 16.0f, 24.0f};
static const char *kJumpNames[] = {"small", "medium", "big", "huge"};
static int g_jumpPower = 1;
static bool g_megaPrev = false;
static double g_nextMega = 0;
static void NextJumpPower() { g_jumpPower = (g_jumpPower + 1) % 4; Notify("Jump power: %s", kJumpNames[g_jumpPower]); }
static void MegaJumpTick(const Buttons &in) {
    bool btn = f_rightHand ? in.y : in.b, press = btn && !g_megaPrev;
    g_megaPrev = btn;
    Vector3 v;
    if (!f_megaJump || !press || g_menuShown || g_time < g_nextMega || !GetVel(v)) return;
    g_nextMega = g_time + 0.25;
    SetVel(Vector3(v.x, kJumpPowers[g_jumpPower], v.z));
}

// Super Fly: like Velocity Fly, but along your head, and a lot faster.
static void SuperFlyTick(const Buttons &in) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    if (!f_superFly || !in.a || !head || !player) return;
    SetVel(G.trGetForward[head]() * 35.0f);
    if (G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.1f);
}

// Rocket Boots: hold both triggers and you climb, keeping your sideways speed.
static void RocketTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_rocket || !in.ltrig || !in.rtrig || !GetVel(v)) return;
    v.y += 30.0f * dt;
    if (v.y > 14.0f) v.y = 14.0f;
    SetVel(v);
    if (G.plNoGravityFor.IsValid() && g_player.get()) G.plNoGravityFor[g_player.get()](0.1f);
}

// Ground Pound (mace style): press both grips to slam straight down. The harder the fall, the
// bigger the smash: on landing a shockwave ring spreads out from your feet and you bounce back up.
// All of it is only on your own screen and your own body.
static bool g_poundPrev = false, g_pounding = false;
static double g_poundStart = 0, g_ringStart = -1;
static float g_poundTopY = 0, g_ringPower = 1;
static Vector3 g_ringAt;
static Ref g_ring[2];
static void RingTick() {
    if (g_ringStart < 0) return;
    float t = (float)(g_time - g_ringStart) / 0.55f;
    if (t >= 1) { g_ringStart = -1; for (auto &r : g_ring) Show(r, false); return; }
    for (int i = 0; i < 2; i++) {
        Il2CppObject *o = g_ring[i].get();
        if (!Alive(o)) continue;
        float tt = t - i * 0.12f;
        bool vis = tt > 0;
        Show(g_ring[i], vis);
        if (!vis) continue;
        float e = 1 - (1 - tt) * (1 - tt);
        float r = (1.0f + 6.0f * g_ringPower * e) * 2;             // diameter
        float th = 0.06f * (1 - tt) + 0.005f;
        Il2CppObject *tr = Tr(o);
        G.trSetPosition[tr](g_ringAt + Vector3(0, 0.03f + i * 0.03f, 0));
        G.trSetLocalScale[tr](Vector3(r, th, r));
        SetColor(o, i ? Color(1.0f, 0.85f, 0.4f, 1) : Color(1.0f, 1.0f, 1.0f, 1));
    }
}
static void GroundPoundTick(const Buttons &in) {
    bool both = in.lgrip && in.rgrip, press = both && !g_poundPrev;
    g_poundPrev = both;
    RingTick();
    Vector3 v, body;
    if (!f_groundPound || !GetVel(v) || !BodyPos(body)) return;
    if (press) {
        g_pounding = true;
        g_poundStart = g_time;
        g_poundTopY = body.y;
        SetVel(Vector3(0, -35.0f, 0));
        return;
    }
    if (!g_pounding) return;
    if (g_time - g_poundStart > 4.0) { g_pounding = false; return; }
    if (g_time - g_poundStart < 0.12 || v.y > -6.0f) {
        if (g_time - g_poundStart >= 0.12 && v.y > -6.0f) {        // landed
            g_pounding = false;
            float fall = g_poundTopY - body.y;
            g_ringPower = fall < 1 ? 0.5f : fall > 12 ? 2.0f : 0.5f + 1.5f * (fall - 1) / 11;
            g_ringAt = body;
            BNM::Structures::Unity::RaycastHit hit{};
            if (G.raycast.IsValid() && G.raycast(body, Vector3(0, -1, 0), &hit, 3.0f, -5)) g_ringAt = hit.point;
            for (auto &r : g_ring)
                if (!Alive(r.get())) {
                    Il2CppObject *o = Prim(2, Color(1, 1, 1, 1), false);   // flat cylinder = a ring on the ground
                    if (o) r.set(o);
                }
            g_ringStart = g_time;
            SetVel(Vector3(v.x, 5.0f + 5.0f * g_ringPower, v.z));      // the bounce
            Buzz();
            Notify("SMASH!");
        }
    }
}

// Bounce: every landing sends you back up, a little lower each time (like a rubber ball).
// A landing = you were falling fast a moment ago and now you are not.
static float g_fallPeak = 0;
static double g_fallSeen = 0, g_bounceNext = 0;
static int g_bounceLogged = 0;
static void BounceTick() {
    Vector3 v;
    if (!f_bounce || !GetVel(v)) { g_fallPeak = 0; return; }
    if (v.y < -2.0f) {
        if (v.y < g_fallPeak) g_fallPeak = v.y;
        g_fallSeen = g_time;
    } else if (g_fallPeak < -3.0f && v.y > -1.0f && g_time - g_fallSeen < 0.35 && g_time >= g_bounceNext) {
        float up = -g_fallPeak * 0.85f;
        up = up < 6.0f ? 6.0f : up > 30.0f ? 30.0f : up;
        SetVel(Vector3(v.x, up, v.z));
        g_bounceNext = g_time + 0.3;
        if (g_bounceLogged < 6) { g_bounceLogged++; ym::log("clientdabell: bounce fall %.1f -> up %.1f", g_fallPeak, up); }
        g_fallPeak = 0;
    } else if (g_time - g_fallSeen > 0.35) {
        g_fallPeak = 0;                                            // the fall ended without a landing (flying, etc.)
    }
}

// Fling Gun: press A (X if the menu is on your right hand) to launch yourself the way your free hand points.
static bool g_flingPrev = false;
static double g_flingNext = 0;
static void FlingGunTick(const Buttons &in) {
    bool btn = f_rightHand ? in.x : in.a, press = btn && !g_flingPrev;
    g_flingPrev = btn;
    Il2CppObject *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get();
    Vector3 v;
    if (!f_flingGun || !press || g_menuShown || g_dance >= 0 || !hand || g_time < g_flingNext || !GetVel(v)) return;
    g_flingNext = g_time + 0.4;
    SetVel(G.trGetForward[hand]() * 32.0f);
    Buzz();
}

// Launch Items Gun: squeeze the grip of your free hand to shoot colorful blocks and balls that fly,
// bounce and fade. They are local props that only you see; real room items are never touched.
static constexpr int kMaxShots = 24;
struct Shot { Ref go; Vector3 pos, vel; double born = -1; };
static Shot g_shots[kMaxShots];
static int g_shotNext = 0;
static bool g_shotPrev = false;
static double g_shotCool = 0;
static void ItemGunTick(const Buttons &in, float dt) {
    bool grip = f_rightHand ? in.lgrip : in.rgrip, other = f_rightHand ? in.rgrip : in.lgrip;
    bool press = grip && !other && !g_shotPrev;
    g_shotPrev = grip;
    Il2CppObject *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get();
    if (f_propGun && press && !g_menuShown && g_dance < 0 && hand && g_time >= g_shotCool && G.createPrimitive.IsValid()) {
        g_shotCool = g_time + 0.2;
        Shot &s = g_shots[g_shotNext];
        g_shotNext = (g_shotNext + 1) % kMaxShots;
        Kill(s.go);                                                // recycle the oldest
        float h = fmodf((float)g_time * 0.37f, 1.0f);
        Il2CppObject *o = Prim(g_shotNext % 2 ? 0 : 3, Rainbow(h), false);
        if (o) {
            G.trSetLocalScale[Tr(o)](Vector3(0.25f, 0.25f, 0.25f));
            s.go.set(o);
            Vector3 d = G.trGetForward[hand]();
            s.pos = G.trGetPosition[hand]() + d * 0.3f;
            s.vel = d * 22.0f;
            s.born = g_time;
            G.trSetPosition[Tr(o)](s.pos);
            Buzz();
        }
    }
    for (auto &s : g_shots) {
        Il2CppObject *o = s.go.get();
        if (!o) continue;
        if (!Alive(o) || g_time - s.born > 6.0 || !f_propGun) { Kill(s.go); continue; }
        Vector3 from = s.pos;
        s.vel.y -= 9.8f * dt;
        s.pos = s.pos + s.vel * dt;
        Vector3 seg = s.pos - from;
        float len = sqrtf(Dot(seg, seg));
        BNM::Structures::Unity::RaycastHit hit{};
        if (len > 1e-4f && G.raycast.IsValid() && G.raycast(from, seg * (1.0f / len), &hit, len + 0.12f, -5)) {
            float vn = Dot(s.vel, hit.normal);
            s.vel = (s.vel - hit.normal * (2 * vn)) * 0.55f;       // bounce, losing energy
            s.pos = hit.point + hit.normal * 0.14f;
        }
        G.trSetPosition[Tr(o)](s.pos);
    }
}

// Grapple Hook: hold a trigger (either hand, or both) and a rope pulls you toward the spot that hand points at.
struct Grapple { Ref go, line; bool on = false, prev = false; Vector3 point; };
static Grapple g_grap[2];
static void GrappleHand(Grapple &g, Il2CppObject *hand, bool trig, float dt, Vector3 &pull) {
    bool held = f_grapple && trig && g_dance < 0 && !g_menuShown && hand && G.raycast.IsValid() && G.lrSetPosition.IsValid();
    bool press = held && !g.prev;
    g.prev = trig;
    if (!held) {
        if (g.on) { g.on = false; Show(g.go, false); }
        return;
    }
    Vector3 start = G.trGetPosition[hand](), dir = G.trGetForward[hand]();
    if (press) {
        BNM::Structures::Unity::RaycastHit hit{};
        if (!G.raycast(start + dir * 0.25f, dir, &hit, 80.0f, -5)) return;
        g.point = hit.point;
        g.on = true;
        if (!g.line.get()) {                                       // each rope is made once
            Il2CppObject *lineGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("ClientDaBellGrapple"));
            Il2CppObject *line = lineGo && G.tLine ? G.goAddComponent[lineGo](G.tLine) : nullptr;
            if (!line) { g.on = false; return; }
            if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
                if (G.shader.get()) G.matSetShader[m](G.shader.get());
                G.matSetColor[m](Color(1.0f, 1.0f, 1.0f, 1));
            }
            G.lrSetStartWidth[line](0.02f);
            G.lrSetEndWidth[line](0.02f);
            G.lrSetPositionCount[line](2);
            G.lrSetUseWorldSpace[line](true);
            g.go.set(lineGo);
            g.line.set(line);
        }
        Show(g.go, true);
    }
    Vector3 body;
    if (!g.on || !BodyPos(body) || !Alive(g.line.get())) return;
    Vector3 to = g.point - body;
    float dist = sqrtf(Dot(to, to));
    if (dist < 1.5f) { g.on = false; Show(g.go, false); return; }  // arrived
    pull = pull + to * (45.0f * dt / dist);
    G.lrSetPosition[g.line.get()](0, start);
    G.lrSetPosition[g.line.get()](1, g.point);
}
static void GrappleTick(const Buttons &in, float dt) {
    Vector3 v, pull(0, 0, 0);
    GrappleHand(g_grap[0], g_leftHand.get(), in.ltrig, dt, pull);
    GrappleHand(g_grap[1], g_rightHand.get(), in.rtrig, dt, pull);
    if (!g_grap[0].on && !g_grap[1].on) return;
    if (!GetVel(v)) return;
    v = v + pull;                                                  // both ropes pull together
    float speed = sqrtf(Dot(v, v));
    if (speed > 28.0f) v = v * (28.0f / speed);
    SetVel(v);
}


static void OverpoweredOff() {
    for (auto &it : g_items) if (it.cat == OVERPOWERED && it.kind == TOGGLE) { *it.flag = false; if (it.fn) it.fn(); }
    Notify("Overpowered off");
}
static void OverpoweredTick(const Buttons &in, float dt) {
    MegaJumpTick(in);
    SuperFlyTick(in);
    RocketTick(in, dt);
    GrappleTick(in, dt);
    GroundPoundTick(in);
    FlingGunTick(in);
    BounceTick();
    ItemGunTick(in, dt);
    LongArmsTick();
}
static void OverpoweredReset() {                               // room change: the old objects are gone
    g_laTrackersSet = false;
    for (auto &g : g_grap) { Kill(g.go); g.line.clear(); g.on = g.prev = false; }
    g_laState = 0; g_laChecks = 0; g_laSpaceL = g_laSpaceR = -1; g_laOwns = false;
    g_megaPrev = g_poundPrev = g_pounding = g_flingPrev = g_shotPrev = false;
    for (auto &sh : g_shots) { Kill(sh.go); sh.born = -1; }
    g_ringStart = -1;
    for (auto &r : g_ring) Kill(r);
}

// ---- callbacks ---------------------------------------------------------------

static void OnRoomChange() {
    for (int k = 0; k < kOrbitMax; k++) { g_orbitItem[k].clear(); g_orbitRb[k].clear(); }
    g_orbitN = 0; f_itemOrbit = false;
    try { DisableSongsReset(); } catch (...) {}
    // The old room's objects are gone (or about to be): forget them, build again.
    for (int r = 0; r < kRows; r++) { g_rowCube[r].clear(); g_rowLabel[r].clear(); g_rowText[r].clear(); }
    for (Ref *r : {&g_board, &g_bg, &g_outline, &g_title, &g_titleBar, &g_prev, &g_next, &g_notifyText, &g_tipText}) r->clear();
    try { MicStop(); } catch (...) {}             // the voice object can outlive the room: give it your mic back
    g_recorder.clear();
    for (Ref *r : {&g_menu, &g_pointer, &g_notifyGo, &g_leftPlatform, &g_rightPlatform, &g_gunBall, &g_gunLineGo, &g_audioGo}) Kill(*r);
    g_audioSrc.clear();
    PauseTrackers(false);
    g_dancing = false;
    g_gunLine.clear();
    WeatherReset();
    try { TodReset(); } catch (...) {}
    OverpoweredReset();
    if (g_noclipOn && g_player.get()) {           // leaving the room mid-noclip: put things back
        Buttons none;                             // trigger "released"
        NoclipTick(none);
    }
    g_boosted = nullptr;
    g_gravityOff = false;
    for (Ref *r : {&g_player, &g_rigidbody, &g_leftHand, &g_rightHand, &g_head}) r->clear();
    g_built = false; g_menuShown = false; g_touching = -2; g_haveSpot = false;
}

static void OnUpdate(float dt) {
    g_time += dt;
    g_frames++;
    static double fpsStart = 0;
    if (g_time - fpsStart >= 1.0) { g_fps = (float)(g_frames / (g_time - fpsStart)); g_frames = 0; fpsStart = g_time; }
    if (!Lookups()) return;
    auto *player = (Il2CppObject *)ym::api()->localPlayer();
    if (!player) return;
    if (player != g_player.get()) {                 // first frame in this room
        OnRoomChange();
        g_player.set(player);
        g_leftHand.set(G.leftHand[player]());
        g_rightHand.set(G.rightHand[player]());
        g_head.set(G.headTransform[player]());
        Il2CppObject *pgo = G.compGameObject[player]();
        if (pgo) g_rigidbody.set(G.goGetComponent[pgo](G.tRigidbody));
    }
    if (!g_leftHand.get() || !g_rightHand.get() || !g_head.get()) return;
    if (g_rebuildMenu) RebuildMenuNow();
    if (!g_built) {
        SetLayout(f_barkMode);
        BuildMenu();
        if (g_rebuiltNote && g_built) {
            g_rebuiltNote = false;
            Notify(f_barkMode ? "Bark Mode on: press %s to open or close the menu" : "Bark Mode off: the menu is back on your hand", f_rightHand ? "B" : "Y");
        }
    }

    Buttons in = ReadButtons();
    {
        bool btn = f_rightHand ? in.b : in.y;
        if (g_barkLayout) {                              // Bark Mode: a press opens the panel and it stays up; press again to close
            static bool prevBtn = false;
            if (btn && !prevBtn) g_barkOpen = !g_barkOpen;
            prevBtn = btn;
            ShowMenu(g_barkOpen);
        } else {
            g_barkOpen = false;
            ShowMenu(btn);                               // hand menu: shown while you hold the button
        }
    }
    if (g_menuShown) {
        try {
            PlaceMenu();
            CheckPress();
            if (g_dirty) RenderPage();
            UpdateTitle();
            AnimateMenu();
        } catch (...) {                                  // never let one bad frame stop the menu
            g_dirty = true;
            ym::log("clientdabell: menu error (kept going)");
        }
    }
    if (g_notifyUntil > 0 && g_time >= g_notifyUntil) { Show(g_notifyGo, false); g_notifyUntil = 0; }
    FlyTick(in, dt);
    StickFlyTick(in, dt);
    DashTick(in);
    LowGravityTick(in, dt);
    PlatformFor(in.lgrip, g_leftPlatform, g_leftHand);
    PlatformFor(in.rgrip, g_rightPlatform, g_rightHand);
    VelocityFlyTick(in);
    CarTick(in);
    IronManTick(in, dt);
    ApplySpeedBoost();
    ApplyGravity();
    NoclipTick(in);
    TeleportGunTick(in);
    try { ItemOrbitTick(dt); }
    catch (...) { f_itemOrbit = false; try { OrbitRelease(); } catch (...) {} ym::log("clientdabell: item orbit error, turned off"); }
    try { SpawnOnHandTick(in); ItemQueueTick(); }
    catch (...) { g_itemQueue.clear(); ym::log("clientdabell: item code error"); }
    try { DisableSongsTick(); }
    catch (...) { f_disableSongs = false; try { DisableSongsReset(); } catch (...) {} ym::log("clientdabell: disable songs error, turned off"); }
    try { LoadTick(); SoundTick(); }
    catch (...) { AbortLoad(); ym::log("clientdabell: song loading error"); }
    try { WeatherTick(dt); }
    catch (...) { WeatherOff(); g_wx = -1; for (bool &b : g_wxSel) b = false; ym::log("clientdabell: effects error, turned off"); }
    try { TodTick(); }
    catch (...) { g_tod = -1; for (bool &b : g_todSel) b = false; try { TodOff(); } catch (...) {} ym::log("clientdabell: time of day error, turned off"); Notify("Time of day error, turned off"); }
    try { OverpoweredTick(in, dt); }
    catch (...) {
        for (auto &it : g_items) if (it.cat == OVERPOWERED && it.kind == TOGGLE) *it.flag = false;
        ym::log("clientdabell: overpowered error, turned off");
    }
    DanceTick(in, dt);              // last: it moves your hands after everything else
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    ym::log("ClientDaBell %s loaded on %s (%s)", CDB_VERSION, api->gameVersion, api->buildId);
    return 0;
}
