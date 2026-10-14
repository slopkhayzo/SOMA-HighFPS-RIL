#include "runtime.h"

#include "camera_interpolation.h"
#include "gui_mouse_mapping.h"
#include "hands_interpolation.h"
#include "hands_mapping.h"
#include "logger.h"
#include "mouse_look_preview.h"
#include "pose_interpolation.h"
#include "presentation_clock.h"
#include "world_mapping.h"
#include "world_interpolation.h"

#include <Windows.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace soma::runtime {
namespace {

using EngineRun = void (*)(void* self);
using UpdaterRunMessage = void (*)(void* self, int message, float timeStep);
using SdlGlSwapWindow = void (*)(void* window);
using EngineMicroseconds = std::uint64_t (*)();
using NewtonUpdate = void (*)(void* world, float timeStep);
using ViewportRender = void (*)(void* scene, void* viewport, float timeStep,
                                std::uint64_t flags);
using LuxEntitySetMatrix = void (*)(void* entity, const void* matrix);
using LuxEntityUpdateAttachment = void (*)(void* entity);
using MeshUpdateGraphics = void (*)(void* mesh);
using SkinningConvert = void (*)(const hands::Matrix* matrix,
                                 float* rotation, float* translation);
using SubMeshGetModelMatrix = hands::Matrix* (*)(void* subMesh,
                                                 void* frustum);
using LuxPropOnUpdate = void (*)(void* prop, float timeStep);
using LuxAgentOnUpdate = void (*)(void* agent, float timeStep);
struct Int2 {
    int x = 0;
    int y = 0;
};
struct Float2 {
    float x = 0.0F;
    float y = 0.0F;
};
struct Float3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};
using GuiMousePixelSetter = void (*)(void* gui, const Int2* position,
                                     const Int2* relative);
using GuiMouseVirtualSetter = void (*)(void* gui, const Float2* position,
                                       const Float2* relative);
using GuiCursorQueue = void (*)(void* gui);
using GuiDoMouse = void (*)(void* gui, const void* gfx,
                            const Float3* offset, const void* size);
using SdlGetRelativeMouseMode = int (*)();
using SdlGetRelativeMouseState = std::uint32_t (*)(int* x, int* y);
using SdlPumpEvents = void (*)();

enum class RenderUnlockState : unsigned int {
    dormant,
    pending,
    active,
    rejected,
    restored,
};

EngineRun g_originalEngineRun = nullptr;
UpdaterRunMessage g_originalUpdaterRunMessage = nullptr;
SdlGlSwapWindow g_originalSdlGlSwapWindow = nullptr;
NewtonUpdate g_originalNewtonUpdate = nullptr;
ViewportRender g_originalViewportRender = nullptr;
LuxEntitySetMatrix g_originalHandsSetMatrix = nullptr;
LuxEntityUpdateAttachment g_originalUpdateAttachment = nullptr;
MeshUpdateGraphics g_originalMeshUpdateGraphics = nullptr;
SubMeshGetModelMatrix g_originalSubMeshGetModelMatrix = nullptr;
LuxPropOnUpdate g_originalLuxPropOnUpdate = nullptr;
LuxAgentOnUpdate g_originalLuxAgentOnUpdate = nullptr;
GuiMousePixelSetter g_originalGuiMousePixelSetter = nullptr;
GuiMouseVirtualSetter g_originalGuiMouseVirtualSetter = nullptr;
GuiCursorQueue g_originalGuiCursorQueue = nullptr;
GuiDoMouse g_originalGuiDoMouse = nullptr;
SdlGetRelativeMouseState g_originalSdlGetRelativeMouseState = nullptr;
EngineMicroseconds g_engineMicroseconds = nullptr;
void* g_engineRunTarget = nullptr;
void* g_updaterRunMessageTarget = nullptr;
void* g_sdlGlSwapWindowTarget = nullptr;
void* g_newtonUpdateTarget = nullptr;
void* g_viewportRenderTarget = nullptr;
void* g_handsSetMatrixTarget = nullptr;
void* g_updateAttachmentTarget = nullptr;
void* g_meshUpdateGraphicsTarget = nullptr;
void* g_subMeshGetModelMatrixTarget = nullptr;
void* g_luxPropOnUpdateTarget = nullptr;
void* g_luxAgentOnUpdateTarget = nullptr;
void* g_guiMousePixelSetterTarget = nullptr;
void* g_guiMouseVirtualSetterTarget = nullptr;
void* g_guiCursorQueueTarget = nullptr;
void* g_guiDoMouseTarget = nullptr;
void* g_sdlGetRelativeMouseStateTarget = nullptr;
SdlPumpEvents g_sdlPumpEvents = nullptr;
SkinningConvert g_skinningConvert = nullptr;
SdlGetRelativeMouseMode g_sdlGetRelativeMouseMode = nullptr;
std::uintptr_t g_hostImageBegin = 0;
std::uintptr_t g_hostImageEnd = 0;
std::uint8_t g_limitFpsOffset = 0;
std::uint8_t g_engineTimerOffset = 0;
std::uint8_t g_viewportCameraOffset = 0;
bool g_renderUnlockEnabled = false;
bool g_telemetryEnabled = false;
bool g_cameraInterpolationEnabled = false;
bool g_handsRootInterpolationEnabled = false;
bool g_handsPoseInterpolationEnabled = false;
bool g_worldRootInterpolationEnabled = false;
bool g_worldPoseInterpolationEnabled = false;
bool g_agentRootInterpolationEnabled = false;
bool g_agentPoseInterpolationEnabled = false;
bool g_guiMouseOverlayEnabled = false;
bool g_mouseLookPreviewEnabled = false;
bool g_ragdollPoseInterpolationEnabled = false;
unsigned int g_reportIntervalSeconds = 5;
bool g_detailedReportsEnabled = false;
bool g_healthReportsEnabled = true;
HANDLE g_reportThread = nullptr;
HANDLE g_reportStopEvent = nullptr;
LARGE_INTEGER g_qpcFrequency{};
LARGE_INTEGER g_telemetryStart{};
LARGE_INTEGER g_previousReport{};
std::uint64_t g_previousFixedTicks = 0;
std::uint64_t g_previousDraws = 0;
std::uint64_t g_previousPostRenders = 0;
std::uint64_t g_previousPostBufferSwaps = 0;
std::uint64_t g_previousPresents = 0;
std::uint64_t g_previousNewtonSteps = 0;
double g_previousNewtonSeconds = 0.0;
std::atomic<std::uint64_t> g_fixedTicks{0};
std::atomic<std::uint64_t> g_draws{0};
std::atomic<std::uint64_t> g_postRenders{0};
std::atomic<std::uint64_t> g_postBufferSwaps{0};
std::atomic<std::uint64_t> g_presents{0};
std::atomic<std::uint64_t> g_newtonSteps{0};
std::atomic<double> g_newtonSeconds{0.0};
std::atomic<std::uint64_t> g_phaseSamples{0};
std::atomic<std::uint64_t> g_phaseInvalid{0};
std::atomic<std::uint64_t> g_phaseBelowRange{0};
std::atomic<std::uint64_t> g_phaseAboveRange{0};
std::atomic<double> g_phaseSum{0.0};
std::atomic<double> g_phaseMinimum{std::numeric_limits<double>::infinity()};
std::atomic<double> g_phaseMaximum{-std::numeric_limits<double>::infinity()};
std::atomic<std::uintptr_t> g_logicTimer{0};
std::optional<clock::PresentationClock> g_presentationClock;
std::uint64_t g_fixedSerial = 0;
std::atomic<std::uint64_t> g_clockPublishes{0};
std::atomic<std::uint64_t> g_clockPublishResets{0};
std::atomic<std::uint64_t> g_clockInterpolated{0};
std::atomic<std::uint64_t> g_clockUnavailable{0};
std::atomic<std::uint64_t> g_clockEvaluationResets{0};
std::atomic<bool> g_telemetryActive{false};
std::atomic_flag g_reportLock = ATOMIC_FLAG_INIT;
std::atomic<RenderUnlockState> g_renderUnlockState{RenderUnlockState::dormant};

struct CameraPose {
    std::array<float, 3> position{};
    float pitch = 0.0F;
    float yaw = 0.0F;
    float roll = 0.0F;
    float extendedPitch = 0.0F;
    float extendedYaw = 0.0F;
    float extendedRoll = 0.0F;
    std::uint32_t projectionType = 0;
    std::uint32_t rotateMode = 0;
    std::array<float, 16> rotation{};
};

struct CameraSlot {
    std::uintptr_t camera = 0;
    std::uint64_t samples = 0;
    std::uint64_t repeats = 0;
    std::uint64_t fixedBoundaryChanges = 0;
    std::uint64_t sameFixedSerialChanges = 0;
    std::uint64_t lastPoseSerial = 0;
    CameraPose lastPose{};
    bool hasPose = false;
};

constexpr std::size_t kMaximumCameraIdentities = 8;
SRWLOCK g_cameraLock = SRWLOCK_INIT;
std::array<CameraSlot, kMaximumCameraIdentities> g_cameraSlots{};
std::atomic<std::uint64_t> g_viewportRenders{0};
std::atomic<std::uint64_t> g_cameraPoseValid{0};
std::atomic<std::uint64_t> g_cameraPoseInvalid{0};
std::atomic<std::uint64_t> g_cameraIdentityOverflow{0};
std::atomic<std::uintptr_t> g_latestCameraIdentity{0};
std::atomic<std::uint64_t> g_lastValidCameraDraw{0};
camera::History g_cameraHistory{};
std::uintptr_t g_cameraHistoryIdentity = 0;
clock::Sample g_latestClockSample{};
bool g_hasLatestClockSample = false;
std::atomic<std::uint64_t> g_cameraCaptures{0};
std::atomic<std::uint64_t> g_cameraCaptureResets{0};
std::atomic<std::uint64_t> g_cameraOverrides{0};
std::atomic<std::uint64_t> g_cameraMatrixModeOverrides{0};
std::atomic<std::uint64_t> g_cameraRestores{0};
std::atomic<std::uint64_t> g_cameraOverrideUnavailable{0};
std::atomic<std::uint64_t> g_cameraRestoreFailures{0};
std::atomic<int> g_updaterMessageContext{-1};

struct GuiMouseSlot {
    std::uintptr_t gui = 0;
    std::uint64_t pixelCalls = 0;
    std::uint64_t virtualCalls = 0;
    std::uint64_t motionCalls = 0;
    std::array<std::uint64_t, 9> messageCounts{};
    Int2 pixelFixed{};
    Int2 pixelPresented{};
    Float2 virtualFixed{};
    Float2 virtualPresented{};
    Float2 virtualStockRelative{};
    Int2 fixedCursor{};
    float virtualScaleX = 0.0F;
    float virtualScaleY = 0.0F;
    std::uint64_t lastStockSerial = 0;
    std::uint64_t lastTouchedSerial = 0;
    std::uint64_t cursorQueueCalls = 0;
    std::uint64_t lastCursorQueueSerial = 0;
    std::size_t cursorCommandIndex = 0;
    std::uintptr_t cursorCommandIdentity = 0;
    Float2 cursorQueuedMouse{};
    Float2 cursorQueuedPosition{};
    bool usesVirtualPosition = false;
    bool positionValid = false;
    bool awaitingVirtualCalibration = false;
    bool virtualScaleXValid = false;
    bool virtualScaleYValid = false;
    bool cursorCommandValid = false;
};

constexpr std::size_t kMaximumGuiMouseIdentities = 8;
SRWLOCK g_guiMouseLock = SRWLOCK_INIT;
std::array<GuiMouseSlot, kMaximumGuiMouseIdentities> g_guiMouseSlots{};
std::atomic<std::uint64_t> g_guiMousePixelCalls{0};
std::atomic<std::uint64_t> g_guiMouseVirtualCalls{0};
std::atomic<std::uint64_t> g_guiMouseMotionCalls{0};
std::atomic<std::uint64_t> g_guiMouseRelativeModeCalls{0};
std::atomic<std::uint64_t> g_guiMouseAbsoluteModeCalls{0};
std::atomic<std::uint64_t> g_guiMouseIdentityOverflow{0};
std::atomic<std::uint64_t> g_guiMousePresentationCalls{0};
std::atomic<std::uint64_t> g_guiMousePresentationMoves{0};
std::atomic<std::uint64_t> g_guiMouseFixedCorrections{0};
std::atomic<std::uint64_t> g_guiMouseVirtualCalibrations{0};
std::atomic<std::uint64_t> g_guiMouseStaleSkips{0};
std::atomic<std::uint64_t> g_guiMouseRelativeModeSkips{0};
std::atomic<std::uint64_t> g_guiMouseInvalidSkips{0};
std::atomic<std::uint64_t> g_guiMousePreviousSyncs{0};
std::atomic<std::uint64_t> g_guiMousePreviousSyncFailures{0};
std::atomic<std::uint64_t> g_guiCursorQueueCalls{0};
std::atomic<std::uint64_t> g_guiCursorCommandCaptures{0};
std::atomic<std::uint64_t> g_guiCursorCommandApplies{0};
std::atomic<std::uint64_t> g_guiCursorCommandFailures{0};
std::atomic<std::uint64_t> g_guiDoMouseCalls{0};
std::atomic<std::uint64_t> g_guiDoMouseCaptures{0};
std::atomic<std::uint64_t> g_guiDoMouseFailures{0};
HWND g_guiMouseWindow = nullptr;

struct HandsRootSlot {
    std::uintptr_t entity = 0;
    std::uintptr_t mesh = 0;
    std::uint64_t calls = 0;
    std::uint64_t repeats = 0;
    std::uint64_t fixedBoundaryChanges = 0;
    std::uint64_t sameFixedSerialChanges = 0;
    std::uint64_t invalidMatrices = 0;
    std::uint64_t lastSerial = 0;
    std::array<std::uint64_t, 9> messageCounts{};
    std::array<float, 16> lastMatrix{};
    bool hasMatrix = false;
    std::uintptr_t paletteBuffer = 0;
    std::size_t boneCount = 0;
    std::uint64_t paletteCalls = 0;
    std::uint64_t paletteRepeats = 0;
    std::uint64_t paletteFixedBoundaryChanges = 0;
    std::uint64_t paletteSameFixedSerialChanges = 0;
    std::uint64_t paletteInvalid = 0;
    std::uint64_t paletteIdentityChanges = 0;
    std::uint64_t lastPaletteSerial = 0;
    std::uint64_t lastPaletteHash = 0;
    bool hasPalette = false;
};

constexpr std::size_t kMaximumHandsIdentities = 4;
SRWLOCK g_handsLock = SRWLOCK_INIT;
std::array<HandsRootSlot, kMaximumHandsIdentities> g_handsSlots{};
std::atomic<std::uint64_t> g_entitySetMatrixCalls{0};
std::atomic<std::uint64_t> g_handsSetMatrixCalls{0};
std::atomic<std::uint64_t> g_handsIdentityOverflow{0};
std::atomic<std::uintptr_t> g_latestHandsMesh{0};
std::atomic<std::uint64_t> g_meshUpdateGraphicsCalls{0};
std::atomic<std::uint64_t> g_handsPaletteUpdates{0};
std::atomic<std::uint64_t> g_handsPaletteMisses{0};

struct PendingHandsRoot {
    void* entity = nullptr;
    void* mesh = nullptr;
    hands::Matrix matrix{};
    std::uint64_t serial = 0;
    bool valid = false;
};

struct HandsModelSlot {
    void* subMesh = nullptr;
    hands::Matrix matrix{};
};

struct HandObjectHistorySlot {
    void* subMesh = nullptr;
    hands::History history{};
    std::uint64_t lastCapturedSerial = 0;
    bool occupied = false;
};

// Several hand-attached objects can be live in one tick (a tool plus its
// chips, or holstered tools still receiving attachment updates), so each is
// tracked independently instead of one replacing another.
struct HandObjectMeshSlot {
    void* mesh = nullptr;
    std::uint64_t lastSeenSerial = 0;
};

using SkinningVector = std::array<float, 4>;

struct SkinningVectorView {
    SkinningVector* begin = nullptr;
    std::size_t count = 0;
};

PendingHandsRoot g_pendingHandsRoot{};
hands::History g_handsRootHistory{};
pose::History g_handsPoseHistory{};
void* g_interpolatedHandsEntity = nullptr;
void* g_interpolatedHandsMesh = nullptr;
std::array<HandObjectMeshSlot, 8> g_handObjectMeshes{};
hands::Matrix g_authoritativeHandsRoot{};
hands::Matrix g_handsRootCorrection{};
std::array<HandsModelSlot, 32> g_handsModelSlots{};
std::size_t g_handsModelSlotCount = 0;
std::array<HandObjectHistorySlot, 32> g_handObjectHistories{};
std::uint64_t g_handsPoseLastSerial = 0;
void* g_handsPoseBufferIdentity = nullptr;
std::size_t g_handsPoseBoneCount = 0;
std::array<hands::Matrix, pose::kMaximumBones> g_handsPoseRestore{};
std::array<SkinningVector, pose::kMaximumBones * 2>
    g_handsSkinningRestore{};
pose::Pose g_handsPoseOverride{};
hands::Matrix* g_handsPoseOverrideBuffer = nullptr;
SkinningVector* g_handsSkinningOverrideBuffer = nullptr;
std::size_t g_handsSkinningVectorCount = 0;
bool g_handsRenderActive = false;
bool g_handsRootRenderActive = false;
bool g_handsPoseOverridden = false;
std::atomic<std::uint64_t> g_handsRootCaptures{0};
std::atomic<std::uint64_t> g_handsRootApplies{0};
std::atomic<std::uint64_t> g_handsRootUnavailable{0};
std::atomic<std::uint64_t> g_handsPoseCaptures{0};
std::atomic<std::uint64_t> g_handsPoseApplies{0};
std::atomic<std::uint64_t> g_handsPoseRestores{0};
std::atomic<std::uint64_t> g_handsPoseUnavailable{0};
std::atomic<std::uint64_t> g_handsSkinningApplies{0};
std::atomic<std::uint64_t> g_handsSkinningRestores{0};
std::atomic<std::uint64_t> g_handObjectUpdates{0};
std::atomic<std::uint64_t> g_handObjectCaptures{0};
std::atomic<std::uint64_t> g_handObjectApplies{0};

bool IsPlausiblePointer(const void* address,
                        std::size_t alignment = alignof(void*)) noexcept;
template <typename T>
bool ReadAt(const void* base, std::size_t offset, T& value) noexcept;

struct WorldMeshRegistrySlot {
    std::atomic<std::uintptr_t> mesh{0};
    std::atomic<std::uintptr_t> prop{0};
    std::atomic<std::uint64_t> lastSeenSerial{0};
    std::atomic<std::uint32_t> bodyCount{0};
    std::atomic<std::uint32_t> ragdollBodyCount{0};
    std::atomic<std::uint32_t> scriptDriven{0};
    std::atomic<std::uint64_t> ragdollPaletteLastSerial{0};
    std::atomic<std::uintptr_t> ragdollPaletteBuffer{0};
    std::atomic<std::uint32_t> ragdollBoneCount{0};
    std::atomic<std::uint64_t> ragdollPaletteHash{0};
};

constexpr std::size_t kWorldMeshRegistrySize = 8192;
constexpr std::size_t kWorldMeshRegistryProbeLimit = 32;
// An entry untouched for this many fixed updates belongs to a prop that is
// asleep or gone and may be replaced. Without this the registry only ever
// grows across a session and eventually refuses new props.
constexpr std::uint64_t kWorldMeshRegistryEvictionAge = 1800;
std::array<WorldMeshRegistrySlot, kWorldMeshRegistrySize>
    g_worldMeshRegistry{};
std::atomic<std::uint64_t> g_worldPropUpdates{0};
std::atomic<std::uint64_t> g_worldPropMessageMismatch{0};
std::atomic<std::uint64_t> g_worldPropLayoutInvalid{0};
std::atomic<std::uint64_t> g_worldPropMeshMissing{0};
std::atomic<std::uint64_t> g_worldPropBodyInvalid{0};
std::atomic<std::uint64_t> g_worldPropZeroBody{0};
std::atomic<std::uint64_t> g_worldPropOneBody{0};
std::atomic<std::uint64_t> g_worldPropMultiBody{0};
std::atomic<std::uint64_t> g_worldMeshRegistryInsertions{0};
std::atomic<std::uint64_t> g_worldMeshRegistryFull{0};
std::atomic<std::uint64_t> g_worldMeshRegistryEvictions{0};
std::atomic<std::uint64_t> g_worldModelCalls{0};
std::atomic<std::uint64_t> g_worldModelRecent{0};
std::atomic<std::uint64_t> g_worldModelStale{0};
std::array<std::atomic<std::uint64_t>, 10> g_worldPropMessageCounts{};
std::atomic<std::uint64_t> g_ragdollPropUpdates{0};
std::atomic<std::uint64_t> g_ragdollTaggedBodies{0};
std::atomic<std::uint64_t> g_ragdollBodyFlagInvalid{0};
std::atomic<std::uint64_t> g_ragdollPaletteCalls{0};
std::atomic<std::uint64_t> g_ragdollPaletteCaptures{0};
std::atomic<std::uint64_t> g_ragdollPaletteRepeats{0};
std::atomic<std::uint64_t> g_ragdollPaletteChanges{0};
std::atomic<std::uint64_t> g_ragdollPaletteInvalid{0};
std::atomic<std::uint64_t> g_ragdollPaletteIdentityChanges{0};
std::atomic<std::uint32_t> g_ragdollMinimumBones{0};
std::atomic<std::uint32_t> g_ragdollMaximumBones{0};
std::atomic<std::uint64_t> g_ragdollRootApplies{0};
std::atomic<std::uint64_t> g_ragdollPoseApplies{0};

struct AgentMeshRegistrySlot {
    std::atomic<std::uintptr_t> mesh{0};
    std::atomic<std::uintptr_t> agent{0};
    std::atomic<std::uint64_t> lastSeenSerial{0};
    std::atomic<std::uint64_t> paletteLastSerial{0};
    std::atomic<std::uintptr_t> paletteBuffer{0};
    std::atomic<std::uint32_t> boneCount{0};
    std::atomic<std::uint64_t> paletteHash{0};
};

constexpr std::size_t kAgentMeshRegistrySize = 128;
constexpr std::size_t kAgentMeshRegistryProbeLimit = 12;
constexpr std::uint64_t kAgentMeshRegistryEvictionAge = 1800;
std::array<AgentMeshRegistrySlot, kAgentMeshRegistrySize>
    g_agentMeshRegistry{};
std::atomic<std::uint64_t> g_agentUpdates{0};
std::atomic<std::uint64_t> g_agentMeshMissing{0};
std::atomic<std::uint64_t> g_agentMeshInsertions{0};
std::atomic<std::uint64_t> g_agentRegistryFull{0};
std::atomic<std::uint64_t> g_agentPaletteCalls{0};
std::atomic<std::uint64_t> g_agentPaletteCaptures{0};
std::atomic<std::uint64_t> g_agentPaletteRepeats{0};
std::atomic<std::uint64_t> g_agentPaletteChanges{0};
std::atomic<std::uint64_t> g_agentPaletteInvalid{0};
std::atomic<std::uint64_t> g_agentPaletteIdentityChanges{0};
std::atomic<std::uint32_t> g_agentMinimumBones{0};
std::atomic<std::uint32_t> g_agentMaximumBones{0};
std::array<std::atomic<std::uint64_t>, 10> g_agentMessageCounts{};

struct AgentModelSlot {
    std::uintptr_t subMesh = 0;
    std::uint64_t lastTouchedSerial = 0;
    world::ModelTrack track{};
    hands::Matrix interpolated{};
};

constexpr std::size_t kAgentModelSlotCount = 512;
constexpr std::size_t kAgentModelProbeLimit = 16;
constexpr std::uint64_t kAgentModelEvictionAge = 120;
std::array<AgentModelSlot, kAgentModelSlotCount> g_agentModelSlots{};
std::atomic<std::uint64_t> g_agentModelCalls{0};
std::atomic<std::uint64_t> g_agentModelRecent{0};
std::atomic<std::uint64_t> g_agentModelStale{0};
std::atomic<std::uint64_t> g_agentModelCaptures{0};
std::atomic<std::uint64_t> g_agentModelApplies{0};
std::atomic<std::uint64_t> g_agentModelUnavailable{0};
std::atomic<std::uint64_t> g_agentModelInvalid{0};
std::atomic<std::uint64_t> g_agentModelSameTickMutation{0};
std::atomic<std::uint64_t> g_agentModelDiscontinuity{0};
std::atomic<std::uint64_t> g_agentModelSlotFull{0};

struct AgentPoseSlot {
    std::uintptr_t mesh = 0;
    std::uintptr_t agent = 0;
    std::uint64_t lastTouchedSerial = 0;
    std::uint64_t lastCapturedSerial = 0;
    void* matrixBufferIdentity = nullptr;
    std::size_t boneCount = 0;
    pose::History history{};
    pose::Pose overridePose{};
    std::array<hands::Matrix, pose::kMaximumBones> matrixRestore{};
    std::array<SkinningVector, pose::kMaximumBones * 2> skinningRestore{};
    hands::Matrix* matrixOverrideBuffer = nullptr;
    SkinningVector* skinningOverrideBuffer = nullptr;
    std::size_t skinningVectorCount = 0;
    bool overridden = false;
};

constexpr std::size_t kAgentPoseSlotCount = 32;
constexpr std::uint64_t kAgentPoseEvictionAge = 120;
std::array<AgentPoseSlot, kAgentPoseSlotCount> g_agentPoseSlots{};
bool g_agentPoseRenderActive = false;
std::atomic<std::uint64_t> g_agentPoseCalls{0};
std::atomic<std::uint64_t> g_agentPoseEligible{0};
std::atomic<std::uint64_t> g_agentPoseCaptures{0};
std::atomic<std::uint64_t> g_agentPoseApplies{0};
std::atomic<std::uint64_t> g_agentPoseRestores{0};
std::atomic<std::uint64_t> g_agentPoseUnavailable{0};
std::atomic<std::uint64_t> g_agentPoseRejected{0};
std::atomic<std::uint64_t> g_agentPoseSlotFull{0};
std::atomic<std::uint64_t> g_agentPoseRestoreFailures{0};

struct WorldModelSlot {
    std::uintptr_t subMesh = 0;
    std::uint64_t lastTouchedSerial = 0;
    world::ModelTrack track{};
    hands::Matrix interpolated{};
};

constexpr std::size_t kWorldModelSlotCount = 4096;
constexpr std::size_t kWorldModelProbeLimit = 24;
constexpr std::uint64_t kWorldModelEvictionAge = 120;
std::array<WorldModelSlot, kWorldModelSlotCount> g_worldModelSlots{};
std::atomic<std::uint64_t> g_worldModelSlotFull{0};
std::atomic<std::uint64_t> g_worldModelCaptures{0};
std::atomic<std::uint64_t> g_worldModelApplies{0};
std::atomic<std::uint64_t> g_worldModelUnavailable{0};
std::atomic<std::uint64_t> g_worldModelInvalid{0};
std::atomic<std::uint64_t> g_worldModelSameTickMutation{0};
std::atomic<std::uint64_t> g_worldModelDiscontinuity{0};
std::atomic<std::uint64_t> g_worldArticulatedCalls{0};
std::atomic<std::uint64_t> g_worldArticulatedCaptures{0};
std::atomic<std::uint64_t> g_worldArticulatedApplies{0};
std::atomic<std::uint64_t> g_worldArticulatedUnavailable{0};
std::atomic<std::uint64_t> g_worldArticulatedRejected{0};
std::atomic<std::uint64_t> g_worldScriptDrivenSetMatrixCalls{0};
std::atomic<std::uint64_t> g_worldScriptDrivenRefreshes{0};
std::atomic<std::uint64_t> g_worldScriptDrivenRegistryMisses{0};
std::atomic<std::uint64_t> g_worldScriptDrivenCalls{0};
std::atomic<std::uint64_t> g_worldScriptDrivenCaptures{0};
std::atomic<std::uint64_t> g_worldScriptDrivenApplies{0};
std::atomic<std::uint64_t> g_worldScriptDrivenUnavailable{0};
std::atomic<std::uint64_t> g_worldScriptDrivenRejected{0};

struct WorldPoseSlot {
    std::uintptr_t mesh = 0;
    std::uintptr_t prop = 0;
    std::uint64_t lastTouchedSerial = 0;
    std::uint64_t lastCapturedSerial = 0;
    void* matrixBufferIdentity = nullptr;
    std::size_t boneCount = 0;
    pose::History history{};
    pose::Pose overridePose{};
    std::array<hands::Matrix, pose::kMaximumBones> matrixRestore{};
    std::array<SkinningVector, pose::kMaximumBones * 2> skinningRestore{};
    hands::Matrix* matrixOverrideBuffer = nullptr;
    SkinningVector* skinningOverrideBuffer = nullptr;
    std::size_t skinningVectorCount = 0;
    bool overridden = false;
};

constexpr std::size_t kWorldPoseSlotCount = 64;
constexpr std::uint64_t kWorldPoseEvictionAge = 120;
std::array<WorldPoseSlot, kWorldPoseSlotCount> g_worldPoseSlots{};
bool g_worldPoseRenderActive = false;
std::atomic<std::uint64_t> g_worldPoseCalls{0};
std::atomic<std::uint64_t> g_worldPoseEligible{0};
std::atomic<std::uint64_t> g_worldPoseCaptures{0};
std::atomic<std::uint64_t> g_worldPoseApplies{0};
std::atomic<std::uint64_t> g_worldPoseRestores{0};
std::atomic<std::uint64_t> g_worldPoseUnavailable{0};
std::atomic<std::uint64_t> g_worldPoseRejected{0};
std::atomic<std::uint64_t> g_worldPoseSlotFull{0};
std::atomic<std::uint64_t> g_worldPoseRestoreFailures{0};

std::size_t WorldMeshHash(const std::uintptr_t mesh) noexcept {
    std::uintptr_t value = mesh >> 4U;
    value ^= value >> 17U;
    value *= static_cast<std::uintptr_t>(0xed5ad4bbU);
    value ^= value >> 11U;
    return static_cast<std::size_t>(value) &
           (kWorldMeshRegistrySize - 1U);
}

void AssignWorldMesh(WorldMeshRegistrySlot& slot, void* const prop,
                     const std::size_t bodyCount,
                     const std::size_t ragdollBodyCount) noexcept {
    const std::uintptr_t propIdentity =
        reinterpret_cast<std::uintptr_t>(prop);
    if (slot.prop.load(std::memory_order_relaxed) != propIdentity) {
        slot.scriptDriven.store(0, std::memory_order_relaxed);
        slot.ragdollPaletteLastSerial.store(0, std::memory_order_relaxed);
        slot.ragdollPaletteBuffer.store(0, std::memory_order_relaxed);
        slot.ragdollBoneCount.store(0, std::memory_order_relaxed);
        slot.ragdollPaletteHash.store(0, std::memory_order_relaxed);
    }
    slot.prop.store(propIdentity, std::memory_order_relaxed);
    slot.bodyCount.store(static_cast<std::uint32_t>(bodyCount),
                         std::memory_order_relaxed);
    slot.ragdollBodyCount.store(static_cast<std::uint32_t>(ragdollBodyCount),
                                std::memory_order_relaxed);
    slot.lastSeenSerial.store(g_fixedSerial, std::memory_order_release);
}

void RegisterWorldMesh(void* const prop, void* const mesh,
                       const std::size_t bodyCount,
                       const std::size_t ragdollBodyCount) noexcept {
    const auto identity = reinterpret_cast<std::uintptr_t>(mesh);
    if (identity == 0) return;
    const std::size_t initial = WorldMeshHash(identity);
    WorldMeshRegistrySlot* stalest = nullptr;
    std::uint64_t stalestSeen = 0;
    for (std::size_t probe = 0; probe < kWorldMeshRegistryProbeLimit;
         ++probe) {
        auto& slot = g_worldMeshRegistry[
            (initial + probe) & (kWorldMeshRegistrySize - 1U)];
        std::uintptr_t observed = slot.mesh.load(std::memory_order_acquire);
        if (observed == 0) {
            if (slot.mesh.compare_exchange_strong(
                    observed, identity, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                g_worldMeshRegistryInsertions.fetch_add(
                    1, std::memory_order_relaxed);
                observed = identity;
            }
        }
        if (observed == identity) {
            AssignWorldMesh(slot, prop, bodyCount, ragdollBodyCount);
            return;
        }
        const std::uint64_t seen =
            slot.lastSeenSerial.load(std::memory_order_acquire);
        if (seen <= g_fixedSerial &&
            g_fixedSerial - seen > kWorldMeshRegistryEvictionAge &&
            (stalest == nullptr || seen < stalestSeen)) {
            stalest = &slot;
            stalestSeen = seen;
        }
    }
    if (stalest != nullptr) {
        // Replace in place rather than clearing: lookups stop at the first
        // empty slot, so a slot that has been used must stay occupied.
        stalest->prop.store(0, std::memory_order_relaxed);
        stalest->mesh.store(identity, std::memory_order_release);
        AssignWorldMesh(*stalest, prop, bodyCount, ragdollBodyCount);
        g_worldMeshRegistryEvictions.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_worldMeshRegistryFull.fetch_add(1, std::memory_order_relaxed);
}

WorldMeshRegistrySlot* FindWorldMesh(
    const std::uintptr_t identity) noexcept {
    if (identity == 0) return nullptr;
    const std::size_t initial = WorldMeshHash(identity);
    for (std::size_t probe = 0; probe < kWorldMeshRegistryProbeLimit;
         ++probe) {
        auto& slot = g_worldMeshRegistry[
            (initial + probe) & (kWorldMeshRegistrySize - 1U)];
        const std::uintptr_t observed =
            slot.mesh.load(std::memory_order_acquire);
        if (observed == identity) return &slot;
        if (observed == 0) return nullptr;
    }
    return nullptr;
}

void RecordWorldProp(void* const prop) noexcept {
    g_worldPropUpdates.fetch_add(1, std::memory_order_relaxed);
    const int message =
        g_updaterMessageContext.load(std::memory_order_relaxed);
    const std::size_t messageIndex =
        message >= 0 && message <= 8 ? static_cast<std::size_t>(message) : 9U;
    g_worldPropMessageCounts[messageIndex].fetch_add(
        1, std::memory_order_relaxed);
    if (message != 5) {
        g_worldPropMessageMismatch.fetch_add(1, std::memory_order_relaxed);
    }

    mapping::PropBodyVector bodies{};
    void* mesh = nullptr;
    if (!ReadAt(prop, mapping::kLuxPropBodiesBeginOffset, bodies.begin) ||
        !ReadAt(prop, mapping::kLuxPropBodiesEndOffset, bodies.end) ||
        !ReadAt(prop, mapping::kLuxPropBodiesCapacityOffset,
                bodies.capacity)) {
        g_worldPropLayoutInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::size_t count = 0;
    if (!mapping::ValidatePropBodyVector(bodies, count) ||
        (count != 0 && !IsPlausiblePointer(bodies.begin))) {
        g_worldPropLayoutInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::size_t ragdollBodyCount = 0;
    for (std::size_t index = 0; index < count; ++index) {
        void* body = nullptr;
        std::memcpy(&body, bodies.begin + index, sizeof(body));
        if (!IsPlausiblePointer(body)) {
            g_worldPropBodyInvalid.fetch_add(1,
                                             std::memory_order_relaxed);
            return;
        }
        std::uint8_t isRagdoll = 0;
        if (!ReadAt(body, mapping::kPhysicsBodyIsRagdollOffset,
                    isRagdoll) || isRagdoll > 1) {
            g_ragdollBodyFlagInvalid.fetch_add(1,
                                               std::memory_order_relaxed);
            return;
        }
        if (isRagdoll != 0) ++ragdollBodyCount;
    }
    if (!ReadAt(prop, mapping::kLuxPropMeshOffset, mesh) ||
        !IsPlausiblePointer(mesh)) {
        g_worldPropMeshMissing.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (count == 0) {
        g_worldPropZeroBody.fetch_add(1, std::memory_order_relaxed);
    } else if (count == 1) {
        g_worldPropOneBody.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_worldPropMultiBody.fetch_add(1, std::memory_order_relaxed);
    }
    if (ragdollBodyCount != 0) {
        g_ragdollPropUpdates.fetch_add(1, std::memory_order_relaxed);
        g_ragdollTaggedBodies.fetch_add(ragdollBodyCount,
                                        std::memory_order_relaxed);
    }
    RegisterWorldMesh(prop, mesh, count, ragdollBodyCount);
}

const WorldMeshRegistrySlot* RecordWorldModelCall(
    const void* const owner) noexcept {
    const WorldMeshRegistrySlot* const slot = FindWorldMesh(
        reinterpret_cast<std::uintptr_t>(owner));
    if (slot == nullptr) return nullptr;
    g_worldModelCalls.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t lastSeen =
        slot->lastSeenSerial.load(std::memory_order_acquire);
    if (lastSeen <= g_fixedSerial && g_fixedSerial - lastSeen <= 2) {
        g_worldModelRecent.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_worldModelStale.fetch_add(1, std::memory_order_relaxed);
    }
    return slot;
}

WorldModelSlot* FindWorldModelSlot(void* const subMesh) noexcept {
    const auto identity = reinterpret_cast<std::uintptr_t>(subMesh);
    if (identity == 0) return nullptr;
    std::uintptr_t mixed = identity >> 4U;
    mixed ^= mixed >> 19U;
    mixed *= static_cast<std::uintptr_t>(0x9e3779b1U);
    mixed ^= mixed >> 13U;
    const std::size_t initial = static_cast<std::size_t>(mixed) &
                                (kWorldModelSlotCount - 1U);
    WorldModelSlot* evictable = nullptr;
    for (std::size_t probe = 0; probe < kWorldModelProbeLimit; ++probe) {
        auto& slot = g_worldModelSlots[
            (initial + probe) & (kWorldModelSlotCount - 1U)];
        if (slot.subMesh == identity) {
            slot.lastTouchedSerial = g_fixedSerial;
            return &slot;
        }
        if (slot.subMesh == 0) {
            slot = {};
            slot.subMesh = identity;
            slot.lastTouchedSerial = g_fixedSerial;
            return &slot;
        }
        if (evictable == nullptr &&
            slot.lastTouchedSerial <= g_fixedSerial &&
            g_fixedSerial - slot.lastTouchedSerial >
                kWorldModelEvictionAge) {
            evictable = &slot;
        }
    }
    if (evictable != nullptr) {
        *evictable = {};
        evictable->subMesh = identity;
        evictable->lastTouchedSerial = g_fixedSerial;
        return evictable;
    }
    g_worldModelSlotFull.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

hands::Matrix* InterpolateWorldModel(
    void* const subMesh, const WorldMeshRegistrySlot& registry,
    hands::Matrix* const original) noexcept {
    const std::uint32_t bodyCount =
        registry.bodyCount.load(std::memory_order_relaxed);
    const std::uint64_t lastSeen =
        registry.lastSeenSerial.load(std::memory_order_acquire);
    const bool readable =
        registry.scriptDriven.load(std::memory_order_relaxed) != 0;
    const std::uint32_t ragdollBodyCount =
        registry.ragdollBodyCount.load(std::memory_order_relaxed);
    // With the ragdoll feature on, skeleton-physics props take the ordinary
    // root path here and the ordinary palette path at their graphics update,
    // both on the shared clock, so root and pose stay in agreement.
    const world::Eligibility eligibility = world::ClassifyEligibility(
        g_worldRootInterpolationEnabled, bodyCount,
        g_ragdollPoseInterpolationEnabled ? 0U : ragdollBodyCount, readable,
        g_fixedSerial, lastSeen,
        g_hasLatestClockSample && g_presentationClock.has_value());
    if (eligibility != world::Eligibility::eligible) {
        return original;
    }
    const bool articulated = bodyCount > 1;
    if (articulated) {
        g_worldArticulatedCalls.fetch_add(1, std::memory_order_relaxed);
    }
    if (readable) {
        g_worldScriptDrivenCalls.fetch_add(1, std::memory_order_relaxed);
    }
    WorldModelSlot* const slot = FindWorldModelSlot(subMesh);
    if (slot == nullptr) return original;
    const std::uintptr_t prop =
        registry.prop.load(std::memory_order_acquire);
    const hands::PublishKind published = slot->track.Capture(
        prop, g_fixedSerial, g_presentationClock->Generation(), *original);
    switch (published) {
    case hands::PublishKind::appended:
    case hands::PublishKind::resetGeneration:
        g_worldModelCaptures.fetch_add(1, std::memory_order_relaxed);
        if (articulated) {
            g_worldArticulatedCaptures.fetch_add(1,
                                                 std::memory_order_relaxed);
        }
        if (readable) {
            g_worldScriptDrivenCaptures.fetch_add(1,
                                              std::memory_order_relaxed);
        }
        break;
    case hands::PublishKind::resetSameTickMutation:
        g_worldModelSameTickMutation.fetch_add(1,
                                               std::memory_order_relaxed);
        if (articulated) {
            g_worldArticulatedRejected.fetch_add(1,
                                                 std::memory_order_relaxed);
        }
        if (readable) {
            g_worldScriptDrivenRejected.fetch_add(1,
                                              std::memory_order_relaxed);
        }
        return original;
    case hands::PublishKind::resetDiscontinuity:
    case hands::PublishKind::resetNonMonotonic:
        g_worldModelDiscontinuity.fetch_add(1,
                                            std::memory_order_relaxed);
        if (articulated) {
            g_worldArticulatedRejected.fetch_add(1,
                                                 std::memory_order_relaxed);
        }
        if (readable) {
            g_worldScriptDrivenRejected.fetch_add(1,
                                              std::memory_order_relaxed);
        }
        return original;
    case hands::PublishKind::invalid:
        g_worldModelInvalid.fetch_add(1, std::memory_order_relaxed);
        if (articulated) {
            g_worldArticulatedRejected.fetch_add(1,
                                                 std::memory_order_relaxed);
        }
        if (readable) {
            g_worldScriptDrivenRejected.fetch_add(1,
                                              std::memory_order_relaxed);
        }
        return original;
    case hands::PublishKind::unchanged:
        break;
    }
    const hands::SampleResult sampled =
        slot->track.Evaluate(g_latestClockSample);
    if (sampled.kind != hands::SampleKind::applied) {
        g_worldModelUnavailable.fetch_add(1, std::memory_order_relaxed);
        if (articulated) {
            g_worldArticulatedUnavailable.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (readable) {
            g_worldScriptDrivenUnavailable.fetch_add(1,
                                                 std::memory_order_relaxed);
        }
        return original;
    }
    slot->interpolated = sampled.matrix;
    g_worldModelApplies.fetch_add(1, std::memory_order_relaxed);
    if (ragdollBodyCount != 0 &&
        bodyCount > world::kMaximumRagdollRootBodyCount) {
        g_ragdollRootApplies.fetch_add(1, std::memory_order_relaxed);
    }
    if (articulated) {
        g_worldArticulatedApplies.fetch_add(1,
                                            std::memory_order_relaxed);
    }
    if (readable) {
        g_worldScriptDrivenApplies.fetch_add(1, std::memory_order_relaxed);
    }
    return &slot->interpolated;
}

// The plugin's only OS memory query. It is reserved for one-time identity
// validation (cEngine::Run entry/exit and a newly observed camera) and must
// never be reached from a per-object or per-presentation path.
bool QueryCommittedRange(const void* const address, const std::size_t size,
                         const bool writable) noexcept {
    if (address == nullptr || size == 0) return false;
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t end = cursor + size;
    if (end < cursor) return false;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &information,
                         sizeof(information)) == 0 ||
            information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const DWORD protection = information.Protect & 0xffU;
        if (writable && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
        const std::uintptr_t regionEnd =
            reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
            information.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = regionEnd;
    }
    return true;
}

// Hook hot paths perform no OS memory queries. Engine objects arrive as the
// `this` of a signature-validated hook or through a fixed-layout field of
// one, so a pointer is only screened structurally before a direct access.
bool IsPlausiblePointer(const void* const address,
                        const std::size_t alignment) noexcept {
    constexpr std::uintptr_t kLowestUserAddress = 0x10000;
    constexpr std::uintptr_t kHighestUserAddress = 0x00007ffffffeffff;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= kLowestUserAddress && value <= kHighestUserAddress &&
           (value & (alignment - 1U)) == 0;
}

bool IsInHostImage(const void* const address) noexcept {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= g_hostImageBegin && value < g_hostImageEnd;
}

template <typename T>
bool ReadAt(const void* const base, const std::size_t offset,
            T& value) noexcept {
    if (!IsPlausiblePointer(base)) return false;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset,
                sizeof(T));
    return true;
}

template <typename T>
bool WriteAt(void* const base, const std::size_t offset,
             const T& value) noexcept {
    if (!IsPlausiblePointer(base)) return false;
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value,
                sizeof(T));
    return true;
}

bool ReadLuxEntityName(const void* const entity, const char*& text,
                       std::size_t& size) noexcept {
    text = nullptr;
    size = 0;
    std::size_t capacity = 0;
    constexpr std::size_t kName = mapping::kLuxEntityNameOffset;
    if (!ReadAt(entity, kName + 0x10, size) ||
        !ReadAt(entity, kName + 0x18, capacity) || size == 0 || size > 128 ||
        capacity < size) {
        return false;
    }
    if (capacity < 16) {
        text = static_cast<const char*>(entity) + kName;
    } else if (!ReadAt(entity, kName, text) || text == nullptr) {
        return false;
    }
    return IsPlausiblePointer(text, 1) && text[size] == '\0';
}

bool IsPlayerHandsName(const std::string_view name) noexcept {
    constexpr std::string_view kPrefix = "PlayerHands_";
    return name.size() > kPrefix.size() && name.starts_with(kPrefix);
}

bool IsHandsAttachmentObject(const void* const entity) noexcept {
    const char* text = nullptr;
    std::size_t size = 0;
    return ReadLuxEntityName(entity, text, size) &&
           mapping::IsHandsAttachmentObjectName(
               std::string_view{text, size});
}

void* GetLuxEntityMesh(void* const entity) noexcept {
    // Every iLuxEntity class is native to the host executable, so both its
    // vtable and the mesh getter must lie inside the mapped image.
    void** vtable = nullptr;
    if (!ReadAt(entity, 0, vtable) || !IsInHostImage(vtable)) return nullptr;
    void* getterAddress = nullptr;
    if (!ReadAt(vtable, 0xd8, getterAddress) ||
        !IsInHostImage(getterAddress)) {
        return nullptr;
    }
    using GetMeshEntity = void* (*)(void*);
    return reinterpret_cast<GetMeshEntity>(getterAddress)(entity);
}

std::size_t AgentMeshHash(const std::uintptr_t mesh) noexcept {
    std::uintptr_t value = mesh >> 4U;
    value ^= value >> 17U;
    value *= static_cast<std::uintptr_t>(0xed5ad4bbU);
    value ^= value >> 11U;
    return static_cast<std::size_t>(value) &
           (kAgentMeshRegistrySize - 1U);
}

AgentMeshRegistrySlot* FindAgentMesh(const std::uintptr_t identity) noexcept {
    if (identity == 0) return nullptr;
    const std::size_t initial = AgentMeshHash(identity);
    for (std::size_t probe = 0; probe < kAgentMeshRegistryProbeLimit;
         ++probe) {
        auto& slot = g_agentMeshRegistry[
            (initial + probe) & (kAgentMeshRegistrySize - 1U)];
        const std::uintptr_t observed =
            slot.mesh.load(std::memory_order_acquire);
        if (observed == identity) return &slot;
        if (observed == 0) return nullptr;
    }
    return nullptr;
}

void RegisterAgentMesh(void* const agent) noexcept {
    g_agentUpdates.fetch_add(1, std::memory_order_relaxed);
    const int message =
        g_updaterMessageContext.load(std::memory_order_relaxed);
    const std::size_t messageIndex = message >= 0 && message <= 8
        ? static_cast<std::size_t>(message)
        : 9U;
    g_agentMessageCounts[messageIndex].fetch_add(
        1, std::memory_order_relaxed);

    void* const mesh = GetLuxEntityMesh(agent);
    if (!IsPlausiblePointer(mesh)) {
        g_agentMeshMissing.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto identity = reinterpret_cast<std::uintptr_t>(mesh);
    const auto agentIdentity = reinterpret_cast<std::uintptr_t>(agent);
    const std::size_t initial = AgentMeshHash(identity);
    const auto assign = [agentIdentity](AgentMeshRegistrySlot& slot) noexcept {
        if (slot.agent.load(std::memory_order_relaxed) != agentIdentity) {
            slot.paletteLastSerial.store(0, std::memory_order_relaxed);
            slot.paletteBuffer.store(0, std::memory_order_relaxed);
            slot.boneCount.store(0, std::memory_order_relaxed);
            slot.paletteHash.store(0, std::memory_order_relaxed);
        }
        slot.agent.store(agentIdentity, std::memory_order_relaxed);
        slot.lastSeenSerial.store(g_fixedSerial, std::memory_order_release);
    };
    AgentMeshRegistrySlot* stalest = nullptr;
    std::uint64_t stalestSeen = 0;
    for (std::size_t probe = 0; probe < kAgentMeshRegistryProbeLimit;
         ++probe) {
        auto& slot = g_agentMeshRegistry[
            (initial + probe) & (kAgentMeshRegistrySize - 1U)];
        std::uintptr_t observed = slot.mesh.load(std::memory_order_acquire);
        if (observed == 0) {
            if (slot.mesh.compare_exchange_strong(
                    observed, identity, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                g_agentMeshInsertions.fetch_add(1,
                                                std::memory_order_relaxed);
                observed = identity;
            }
        }
        if (observed == identity) {
            assign(slot);
            return;
        }
        const std::uint64_t seen =
            slot.lastSeenSerial.load(std::memory_order_acquire);
        if (seen <= g_fixedSerial &&
            g_fixedSerial - seen > kAgentMeshRegistryEvictionAge &&
            (stalest == nullptr || seen < stalestSeen)) {
            stalest = &slot;
            stalestSeen = seen;
        }
    }
    if (stalest != nullptr) {
        // Replaced in place so that lookups, which stop at the first empty
        // slot, still reach entries further along the probe sequence.
        stalest->agent.store(0, std::memory_order_relaxed);
        stalest->mesh.store(identity, std::memory_order_release);
        assign(*stalest);
        return;
    }
    g_agentRegistryFull.fetch_add(1, std::memory_order_relaxed);
}

const AgentMeshRegistrySlot* RecordAgentModelCall(
    const void* const owner) noexcept {
    const AgentMeshRegistrySlot* const slot = FindAgentMesh(
        reinterpret_cast<std::uintptr_t>(owner));
    if (slot == nullptr) return nullptr;
    g_agentModelCalls.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t lastSeen =
        slot->lastSeenSerial.load(std::memory_order_acquire);
    if (lastSeen <= g_fixedSerial && g_fixedSerial - lastSeen <= 2) {
        g_agentModelRecent.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_agentModelStale.fetch_add(1, std::memory_order_relaxed);
    }
    return slot;
}

AgentModelSlot* FindAgentModelSlot(void* const subMesh) noexcept {
    const auto identity = reinterpret_cast<std::uintptr_t>(subMesh);
    if (identity == 0) return nullptr;
    std::uintptr_t mixed = identity >> 4U;
    mixed ^= mixed >> 19U;
    mixed *= static_cast<std::uintptr_t>(0x9e3779b1U);
    mixed ^= mixed >> 13U;
    const std::size_t initial = static_cast<std::size_t>(mixed) &
                                (kAgentModelSlotCount - 1U);
    AgentModelSlot* evictable = nullptr;
    for (std::size_t probe = 0; probe < kAgentModelProbeLimit; ++probe) {
        auto& slot = g_agentModelSlots[
            (initial + probe) & (kAgentModelSlotCount - 1U)];
        if (slot.subMesh == identity) {
            slot.lastTouchedSerial = g_fixedSerial;
            return &slot;
        }
        if (slot.subMesh == 0) {
            slot = {};
            slot.subMesh = identity;
            slot.lastTouchedSerial = g_fixedSerial;
            return &slot;
        }
        if (evictable == nullptr &&
            slot.lastTouchedSerial <= g_fixedSerial &&
            g_fixedSerial - slot.lastTouchedSerial > kAgentModelEvictionAge) {
            evictable = &slot;
        }
    }
    if (evictable != nullptr) {
        *evictable = {};
        evictable->subMesh = identity;
        evictable->lastTouchedSerial = g_fixedSerial;
        return evictable;
    }
    g_agentModelSlotFull.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

hands::Matrix* InterpolateAgentModel(
    void* const subMesh, const AgentMeshRegistrySlot& registry,
    hands::Matrix* const original) noexcept {
    if (!g_agentRootInterpolationEnabled || !g_hasLatestClockSample ||
        !g_presentationClock.has_value()) {
        return original;
    }
    const std::uint64_t lastSeen =
        registry.lastSeenSerial.load(std::memory_order_acquire);
    if (lastSeen > g_fixedSerial || g_fixedSerial - lastSeen > 2) {
        return original;
    }
    AgentModelSlot* const slot = FindAgentModelSlot(subMesh);
    if (slot == nullptr) return original;
    const std::uintptr_t agent =
        registry.agent.load(std::memory_order_acquire);
    const hands::PublishKind published = slot->track.Capture(
        agent, g_fixedSerial, g_presentationClock->Generation(), *original);
    switch (published) {
    case hands::PublishKind::appended:
    case hands::PublishKind::resetGeneration:
        g_agentModelCaptures.fetch_add(1, std::memory_order_relaxed);
        break;
    case hands::PublishKind::resetSameTickMutation:
        g_agentModelSameTickMutation.fetch_add(1,
                                               std::memory_order_relaxed);
        return original;
    case hands::PublishKind::resetDiscontinuity:
    case hands::PublishKind::resetNonMonotonic:
        g_agentModelDiscontinuity.fetch_add(1, std::memory_order_relaxed);
        return original;
    case hands::PublishKind::invalid:
        g_agentModelInvalid.fetch_add(1, std::memory_order_relaxed);
        return original;
    case hands::PublishKind::unchanged:
        break;
    }
    const hands::SampleResult sampled =
        slot->track.Evaluate(g_latestClockSample);
    if (sampled.kind != hands::SampleKind::applied) {
        g_agentModelUnavailable.fetch_add(1, std::memory_order_relaxed);
        return original;
    }
    slot->interpolated = sampled.matrix;
    g_agentModelApplies.fetch_add(1, std::memory_order_relaxed);
    return &slot->interpolated;
}

void RefreshScriptDrivenProp(void* const entity) noexcept {
    g_worldScriptDrivenSetMatrixCalls.fetch_add(1, std::memory_order_relaxed);
    void* const mesh = GetLuxEntityMesh(entity);
    WorldMeshRegistrySlot* const slot = FindWorldMesh(
        reinterpret_cast<std::uintptr_t>(mesh));
    if (slot == nullptr ||
        slot->prop.load(std::memory_order_acquire) !=
            reinterpret_cast<std::uintptr_t>(entity)) {
        g_worldScriptDrivenRegistryMisses.fetch_add(1,
                                                std::memory_order_relaxed);
        return;
    }
    const int message =
        g_updaterMessageContext.load(std::memory_order_relaxed);
    const std::uint64_t serial =
        g_fixedSerial + (message == 5 ? 1U : 0U);
    slot->scriptDriven.store(1, std::memory_order_relaxed);
    slot->lastSeenSerial.store(serial, std::memory_order_release);
    g_worldScriptDrivenRefreshes.fetch_add(1, std::memory_order_relaxed);
}

void TrackHandsHudObject(void* const entity) noexcept {
    void* const mesh = GetLuxEntityMesh(entity);
    if (mesh == nullptr) return;
    g_handObjectUpdates.fetch_add(1, std::memory_order_relaxed);
    HandObjectMeshSlot* selected = nullptr;
    for (auto& slot : g_handObjectMeshes) {
        if (slot.mesh == mesh) {
            selected = &slot;
            break;
        }
        if (selected == nullptr ||
            slot.lastSeenSerial < selected->lastSeenSerial) {
            selected = &slot;
        }
    }
    selected->mesh = mesh;
    selected->lastSeenSerial = g_fixedSerial;
}

bool IsRecentHandObjectMesh(const void* const mesh) noexcept {
    for (const auto& slot : g_handObjectMeshes) {
        if (slot.mesh != mesh) continue;
        // Attachment updates run inside the tick that then advances the
        // serial, so a live object is at most a few serials behind.
        return g_fixedSerial - slot.lastSeenSerial <=
               world::kMaximumRecentSerialAge + 1U;
    }
    return false;
}

struct MatrixVectorView {
    const std::array<float, 16>* begin = nullptr;
    std::size_t count = 0;
};

bool ReadHandsPalette(void* const mesh, MatrixVectorView& view) noexcept {
    view = {};
    const std::array<float, 16>* begin = nullptr;
    const std::array<float, 16>* end = nullptr;
    const std::array<float, 16>* capacity = nullptr;
    constexpr std::size_t kVector = mapping::kMeshBoneMatricesOffset;
    if (!ReadAt(mesh, kVector, begin) ||
        !ReadAt(mesh, kVector + sizeof(void*), end) ||
        !ReadAt(mesh, kVector + 2 * sizeof(void*), capacity) ||
        !IsPlausiblePointer(begin, alignof(float))) {
        return false;
    }
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
    const auto capacityAddress = reinterpret_cast<std::uintptr_t>(capacity);
    if (endAddress < beginAddress || capacityAddress < endAddress) return false;
    const std::size_t bytes = static_cast<std::size_t>(endAddress - beginAddress);
    const std::size_t capacityBytes =
        static_cast<std::size_t>(capacityAddress - beginAddress);
    if (bytes % sizeof(*begin) != 0 ||
        capacityBytes % sizeof(*begin) != 0) {
        return false;
    }
    const std::size_t count = bytes / sizeof(*begin);
    if (count == 0 || count > 128) return false;
    view.begin = begin;
    view.count = count;
    return true;
}

bool ReadHandsSkinningVectors(void* const mesh,
                              SkinningVectorView& view) noexcept {
    view = {};
    SkinningVector* begin = nullptr;
    SkinningVector* end = nullptr;
    SkinningVector* capacity = nullptr;
    constexpr std::size_t kVector = mapping::kMeshSkinningVectorsOffset;
    if (!ReadAt(mesh, kVector, begin) ||
        !ReadAt(mesh, kVector + sizeof(void*), end) ||
        !ReadAt(mesh, kVector + 2 * sizeof(void*), capacity) ||
        !IsPlausiblePointer(begin, alignof(float))) {
        return false;
    }
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
    const auto capacityAddress = reinterpret_cast<std::uintptr_t>(capacity);
    if (endAddress < beginAddress || capacityAddress < endAddress) return false;
    const std::size_t bytes = static_cast<std::size_t>(endAddress - beginAddress);
    const std::size_t capacityBytes =
        static_cast<std::size_t>(capacityAddress - beginAddress);
    if (bytes % sizeof(*begin) != 0 ||
        capacityBytes % sizeof(*begin) != 0) {
        return false;
    }
    const std::size_t count = bytes / sizeof(*begin);
    if (count == 0 || count > pose::kMaximumBones * 2) return false;
    view.begin = begin;
    view.count = count;
    return true;
}

bool IsSaneHandsMatrix(const std::array<float, 16>& matrix) noexcept {
    for (const float value : matrix) {
        if (!std::isfinite(value) || std::abs(value) > 10'000'000.0F) {
            return false;
        }
    }
    constexpr float kAffineEpsilon = 1.0e-3F;
    if (std::abs(matrix[12]) > kAffineEpsilon ||
        std::abs(matrix[13]) > kAffineEpsilon ||
        std::abs(matrix[14]) > kAffineEpsilon ||
        std::abs(matrix[15] - 1.0F) > kAffineEpsilon) {
        return false;
    }
    return true;
}

void RecordHandsRoot(void* const entity, void* const mesh,
                     const std::array<float, 16>& matrix) noexcept {
    g_handsSetMatrixCalls.fetch_add(1, std::memory_order_relaxed);
    const auto identity = reinterpret_cast<std::uintptr_t>(entity);
    const int message = g_updaterMessageContext.load(std::memory_order_relaxed);
    const std::size_t messageIndex = message >= 0 && message < 8
        ? static_cast<std::size_t>(message)
        : 8U;
    const std::uint64_t serial =
        g_fixedSerial + (message == 5 ? 1U : 0U);

    AcquireSRWLockExclusive(&g_handsLock);
    HandsRootSlot* selected = nullptr;
    HandsRootSlot* empty = nullptr;
    for (auto& slot : g_handsSlots) {
        if (slot.entity == identity) {
            selected = &slot;
            break;
        }
        if (empty == nullptr && slot.entity == 0) empty = &slot;
    }
    if (selected == nullptr) selected = empty;
    if (selected == nullptr) {
        g_handsIdentityOverflow.fetch_add(1, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_handsLock);
        return;
    }
    if (selected->entity == 0) selected->entity = identity;
    const auto meshIdentity = reinterpret_cast<std::uintptr_t>(mesh);
    if (meshIdentity != 0 && selected->mesh != meshIdentity) {
        selected->mesh = meshIdentity;
        selected->paletteBuffer = 0;
        selected->boneCount = 0;
        selected->paletteCalls = 0;
        selected->paletteRepeats = 0;
        selected->paletteFixedBoundaryChanges = 0;
        selected->paletteSameFixedSerialChanges = 0;
        selected->paletteInvalid = 0;
        selected->paletteIdentityChanges = 0;
        selected->lastPaletteSerial = 0;
        selected->lastPaletteHash = 0;
        selected->hasPalette = false;
        g_latestHandsMesh.store(meshIdentity, std::memory_order_relaxed);
    }
    ++selected->calls;
    ++selected->messageCounts[messageIndex];
    if (!IsSaneHandsMatrix(matrix)) {
        ++selected->invalidMatrices;
        ReleaseSRWLockExclusive(&g_handsLock);
        return;
    }
    if (selected->hasMatrix) {
        if (std::memcmp(selected->lastMatrix.data(), matrix.data(),
                        sizeof(matrix)) == 0) {
            ++selected->repeats;
        } else if (selected->lastSerial == serial) {
            ++selected->sameFixedSerialChanges;
        } else {
            ++selected->fixedBoundaryChanges;
        }
    }
    selected->lastMatrix = matrix;
    selected->lastSerial = serial;
    selected->hasMatrix = true;
    ReleaseSRWLockExclusive(&g_handsLock);
}

std::uint64_t HashBytes(const void* const data,
                        const std::size_t size) noexcept {
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kPrime;
    }
    return hash;
}

void UpdateMinimum(std::atomic<std::uint32_t>& target,
                   const std::uint32_t value) noexcept {
    std::uint32_t observed = target.load(std::memory_order_relaxed);
    while ((observed == 0 || value < observed) &&
           !target.compare_exchange_weak(observed, value,
                                         std::memory_order_relaxed)) {
    }
}

void UpdateMaximum(std::atomic<std::uint32_t>& target,
                   const std::uint32_t value) noexcept {
    std::uint32_t observed = target.load(std::memory_order_relaxed);
    while (value > observed &&
           !target.compare_exchange_weak(observed, value,
                                         std::memory_order_relaxed)) {
    }
}

void RecordAgentPalette(void* const mesh) noexcept {
    AgentMeshRegistrySlot* const slot = FindAgentMesh(
        reinterpret_cast<std::uintptr_t>(mesh));
    if (slot == nullptr) return;
    const std::uint64_t lastSeen =
        slot->lastSeenSerial.load(std::memory_order_acquire);
    if (lastSeen > g_fixedSerial || g_fixedSerial - lastSeen > 2) return;
    g_agentPaletteCalls.fetch_add(1, std::memory_order_relaxed);
    if (slot->paletteLastSerial.exchange(
            g_fixedSerial, std::memory_order_relaxed) == g_fixedSerial) {
        return;
    }

    MatrixVectorView matrices{};
    SkinningVectorView vectors{};
    bool valid = ReadHandsPalette(mesh, matrices) &&
        ReadHandsSkinningVectors(mesh, vectors) &&
        vectors.count == matrices.count * 2;
    if (valid) {
        for (std::size_t index = 0; index < matrices.count; ++index) {
            if (!IsSaneHandsMatrix(matrices.begin[index])) {
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        g_agentPaletteInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto buffer = reinterpret_cast<std::uintptr_t>(matrices.begin);
    const auto count = static_cast<std::uint32_t>(matrices.count);
    const auto previousBuffer =
        slot->paletteBuffer.load(std::memory_order_relaxed);
    const auto previousCount =
        slot->boneCount.load(std::memory_order_relaxed);
    if (previousBuffer != 0 &&
        (previousBuffer != buffer || previousCount != count)) {
        g_agentPaletteIdentityChanges.fetch_add(
            1, std::memory_order_relaxed);
    }
    const std::uint64_t hash = HashBytes(
        matrices.begin, matrices.count * sizeof(*matrices.begin));
    const std::uint64_t previousHash =
        slot->paletteHash.load(std::memory_order_relaxed);
    if (previousBuffer != 0) {
        if (previousHash == hash) {
            g_agentPaletteRepeats.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_agentPaletteChanges.fetch_add(1, std::memory_order_relaxed);
        }
    }
    slot->paletteBuffer.store(buffer, std::memory_order_relaxed);
    slot->boneCount.store(count, std::memory_order_relaxed);
    slot->paletteHash.store(hash, std::memory_order_relaxed);
    UpdateMinimum(g_agentMinimumBones, count);
    UpdateMaximum(g_agentMaximumBones, count);
    g_agentPaletteCaptures.fetch_add(1, std::memory_order_relaxed);
}

void RecordRagdollPalette(void* const mesh) noexcept {
    WorldMeshRegistrySlot* const slot = FindWorldMesh(
        reinterpret_cast<std::uintptr_t>(mesh));
    if (slot == nullptr ||
        slot->bodyCount.load(std::memory_order_relaxed) < 2 ||
        slot->ragdollBodyCount.load(std::memory_order_relaxed) == 0) {
        return;
    }
    const std::uint64_t lastSeen =
        slot->lastSeenSerial.load(std::memory_order_acquire);
    if (lastSeen > g_fixedSerial || g_fixedSerial - lastSeen > 2) return;
    g_ragdollPaletteCalls.fetch_add(1, std::memory_order_relaxed);
    if (slot->ragdollPaletteLastSerial.exchange(
            g_fixedSerial, std::memory_order_relaxed) == g_fixedSerial) {
        return;
    }

    MatrixVectorView matrices{};
    SkinningVectorView vectors{};
    bool valid = ReadHandsPalette(mesh, matrices) &&
        ReadHandsSkinningVectors(mesh, vectors) &&
        vectors.count == matrices.count * 2;
    if (valid) {
        for (std::size_t index = 0; index < matrices.count; ++index) {
            if (!IsSaneHandsMatrix(matrices.begin[index])) {
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        g_ragdollPaletteInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto buffer = reinterpret_cast<std::uintptr_t>(matrices.begin);
    const auto count = static_cast<std::uint32_t>(matrices.count);
    const auto previousBuffer =
        slot->ragdollPaletteBuffer.load(std::memory_order_relaxed);
    const auto previousCount =
        slot->ragdollBoneCount.load(std::memory_order_relaxed);
    if (previousBuffer != 0 &&
        (previousBuffer != buffer || previousCount != count)) {
        g_ragdollPaletteIdentityChanges.fetch_add(
            1, std::memory_order_relaxed);
    }
    const std::uint64_t hash = HashBytes(
        matrices.begin, matrices.count * sizeof(*matrices.begin));
    const std::uint64_t previousHash =
        slot->ragdollPaletteHash.load(std::memory_order_relaxed);
    if (previousBuffer != 0) {
        if (previousHash == hash) {
            g_ragdollPaletteRepeats.fetch_add(1,
                                              std::memory_order_relaxed);
        } else {
            g_ragdollPaletteChanges.fetch_add(1,
                                              std::memory_order_relaxed);
        }
    }
    slot->ragdollPaletteBuffer.store(buffer, std::memory_order_relaxed);
    slot->ragdollBoneCount.store(count, std::memory_order_relaxed);
    slot->ragdollPaletteHash.store(hash, std::memory_order_relaxed);
    UpdateMinimum(g_ragdollMinimumBones, count);
    UpdateMaximum(g_ragdollMaximumBones, count);
    g_ragdollPaletteCaptures.fetch_add(1, std::memory_order_relaxed);
}

void RecordHandsPalette(void* const mesh) noexcept {
    g_handsPaletteUpdates.fetch_add(1, std::memory_order_relaxed);
    MatrixVectorView view{};
    const bool readable = ReadHandsPalette(mesh, view);
    std::uint64_t hash = 0;
    bool valid = readable;
    if (valid) {
        for (std::size_t index = 0; index < view.count; ++index) {
            if (!IsSaneHandsMatrix(view.begin[index])) {
                valid = false;
                break;
            }
        }
    }
    if (valid) {
        hash = HashBytes(view.begin, view.count * sizeof(*view.begin));
    }

    const auto meshIdentity = reinterpret_cast<std::uintptr_t>(mesh);
    const std::uint64_t serial = g_fixedSerial;
    AcquireSRWLockExclusive(&g_handsLock);
    HandsRootSlot* selected = nullptr;
    for (auto& slot : g_handsSlots) {
        if (slot.mesh == meshIdentity) {
            selected = &slot;
            break;
        }
    }
    if (selected == nullptr) {
        g_handsPaletteMisses.fetch_add(1, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_handsLock);
        return;
    }

    ++selected->paletteCalls;
    if (!valid) {
        ++selected->paletteInvalid;
        ReleaseSRWLockExclusive(&g_handsLock);
        return;
    }
    const auto buffer = reinterpret_cast<std::uintptr_t>(view.begin);
    if (selected->hasPalette &&
        (selected->paletteBuffer != buffer ||
         selected->boneCount != view.count)) {
        ++selected->paletteIdentityChanges;
        selected->hasPalette = false;
    }
    if (selected->hasPalette) {
        if (selected->lastPaletteHash == hash) {
            ++selected->paletteRepeats;
        } else if (selected->lastPaletteSerial == serial) {
            ++selected->paletteSameFixedSerialChanges;
        } else {
            ++selected->paletteFixedBoundaryChanges;
        }
    }
    selected->paletteBuffer = buffer;
    selected->boneCount = view.count;
    selected->lastPaletteSerial = serial;
    selected->lastPaletteHash = hash;
    selected->hasPalette = true;
    ReleaseSRWLockExclusive(&g_handsLock);
}

void ResetHandsInterpolation() noexcept {
    g_handsRootHistory.Reset();
    g_handsPoseHistory.Reset();
    g_pendingHandsRoot = {};
    g_interpolatedHandsEntity = nullptr;
    g_interpolatedHandsMesh = nullptr;
    g_handObjectMeshes = {};
    g_authoritativeHandsRoot = {};
    g_handsRootCorrection = hands::Identity();
    g_handsModelSlots = {};
    g_handsModelSlotCount = 0;
    g_handObjectHistories = {};
    g_handsPoseLastSerial = 0;
    g_handsPoseBufferIdentity = nullptr;
    g_handsPoseBoneCount = 0;
    g_handsPoseOverride = {};
    g_handsPoseOverrideBuffer = nullptr;
    g_handsSkinningOverrideBuffer = nullptr;
    g_handsSkinningVectorCount = 0;
    g_handsRenderActive = false;
    g_handsRootRenderActive = false;
    g_handsPoseOverridden = false;
}

void StageHandsInterpolation(void* const entity, void* const mesh,
                             const std::array<float, 16>& matrix) noexcept {
    if ((!g_handsRootInterpolationEnabled &&
         !g_handsPoseInterpolationEnabled) ||
        g_updaterMessageContext.load(std::memory_order_relaxed) != 5 ||
        mesh == nullptr) {
        return;
    }
    hands::Matrix captured{};
    std::memcpy(captured.values.data(), matrix.data(), sizeof(captured));
    if (!hands::IsRigid(captured)) {
        g_pendingHandsRoot = {};
        return;
    }
    g_pendingHandsRoot = {
        entity, mesh, captured, g_fixedSerial + 1U, true};
}

void CommitHandsInterpolation() noexcept {
    if (!g_presentationClock.has_value() ||
        (!g_handsRootInterpolationEnabled &&
         !g_handsPoseInterpolationEnabled)) {
        return;
    }
    if (!g_pendingHandsRoot.valid ||
        g_pendingHandsRoot.serial != g_fixedSerial) {
        ResetHandsInterpolation();
        return;
    }
    const bool identityChanged =
        g_interpolatedHandsEntity != g_pendingHandsRoot.entity ||
        g_interpolatedHandsMesh != g_pendingHandsRoot.mesh;
    if (identityChanged) {
        g_handsRootHistory.Reset();
        g_handsPoseHistory.Reset();
        g_handObjectMeshes = {};
        g_handObjectHistories = {};
        g_handsPoseLastSerial = 0;
        g_handsPoseBufferIdentity = nullptr;
        g_handsPoseBoneCount = 0;
    }
    g_interpolatedHandsEntity = g_pendingHandsRoot.entity;
    g_interpolatedHandsMesh = g_pendingHandsRoot.mesh;
    g_authoritativeHandsRoot = g_pendingHandsRoot.matrix;
    if (g_handsRootInterpolationEnabled) {
        const hands::PublishKind result = g_handsRootHistory.Publish(
            g_fixedSerial, g_presentationClock->Generation(),
            g_authoritativeHandsRoot);
        if (result == hands::PublishKind::appended ||
            result == hands::PublishKind::unchanged) {
            g_handsRootCaptures.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_pendingHandsRoot = {};
}

bool BuildSkinningVectors(const pose::Pose& sampled,
                          SkinningVectorView& vectors) noexcept {
    if (g_skinningConvert == nullptr ||
        vectors.count != sampled.boneCount * 2) {
        return false;
    }
    for (std::size_t index = 0; index < sampled.boneCount; ++index) {
        g_skinningConvert(&sampled.bones[index], vectors.begin[index * 2].data(),
                          vectors.begin[index * 2 + 1].data());
    }
    return true;
}

void ResetWorldPoseSlot(WorldPoseSlot& slot) noexcept {
    slot.mesh = 0;
    slot.prop = 0;
    slot.lastTouchedSerial = 0;
    slot.lastCapturedSerial = 0;
    slot.matrixBufferIdentity = nullptr;
    slot.boneCount = 0;
    slot.history.Reset();
    slot.overridePose = {};
    slot.matrixOverrideBuffer = nullptr;
    slot.skinningOverrideBuffer = nullptr;
    slot.skinningVectorCount = 0;
    slot.overridden = false;
}

WorldPoseSlot* FindWorldPoseSlot(const std::uintptr_t mesh,
                                 const std::uintptr_t prop,
                                 const bool create) noexcept {
    WorldPoseSlot* empty = nullptr;
    WorldPoseSlot* evictable = nullptr;
    for (auto& slot : g_worldPoseSlots) {
        if (slot.mesh == mesh) {
            if (prop != 0 && slot.prop != prop) {
                if (slot.overridden) return nullptr;
                ResetWorldPoseSlot(slot);
                slot.mesh = mesh;
                slot.prop = prop;
            }
            slot.lastTouchedSerial = g_fixedSerial;
            return &slot;
        }
        if (empty == nullptr && slot.mesh == 0) empty = &slot;
        if (evictable == nullptr && !slot.overridden &&
            slot.lastTouchedSerial <= g_fixedSerial &&
            g_fixedSerial - slot.lastTouchedSerial > kWorldPoseEvictionAge) {
            evictable = &slot;
        }
    }
    if (!create) return nullptr;
    WorldPoseSlot* const selected = empty != nullptr ? empty : evictable;
    if (selected == nullptr) {
        g_worldPoseSlotFull.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    ResetWorldPoseSlot(*selected);
    selected->mesh = mesh;
    selected->prop = prop;
    selected->lastTouchedSerial = g_fixedSerial;
    return selected;
}

bool RestoreWorldPoseSlot(WorldPoseSlot& slot) noexcept {
    if (!slot.overridden || slot.matrixOverrideBuffer == nullptr ||
        slot.skinningOverrideBuffer == nullptr ||
        slot.overridePose.boneCount == 0 ||
        slot.skinningVectorCount != slot.overridePose.boneCount * 2) {
        return false;
    }
    const std::size_t matrixBytes =
        slot.overridePose.boneCount * sizeof(hands::Matrix);
    const std::size_t vectorBytes =
        slot.skinningVectorCount * sizeof(SkinningVector);
    std::memcpy(slot.matrixOverrideBuffer, slot.matrixRestore.data(),
                matrixBytes);
    std::memcpy(slot.skinningOverrideBuffer, slot.skinningRestore.data(),
                vectorBytes);
    slot.matrixOverrideBuffer = nullptr;
    slot.skinningOverrideBuffer = nullptr;
    slot.skinningVectorCount = 0;
    slot.overridePose = {};
    slot.overridden = false;
    g_worldPoseRestores.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void DisableWorldPoseInterpolation(const wchar_t* const reason) noexcept {
    g_worldPoseInterpolationEnabled = false;
    g_worldPoseRenderActive = false;
    g_worldPoseRestoreFailures.fetch_add(1, std::memory_order_relaxed);
    log::Write(log::Level::error,
               std::wstring(L"World pose interpolation disabled: ") + reason);
}

void RestoreWorldPoseBeforeUpdate(void* const mesh) noexcept {
    if (!g_worldPoseInterpolationEnabled || mesh == nullptr) return;
    WorldPoseSlot* const slot = FindWorldPoseSlot(
        reinterpret_cast<std::uintptr_t>(mesh), 0, false);
    if (slot == nullptr || !slot->overridden) return;
    if (!RestoreWorldPoseSlot(*slot)) {
        DisableWorldPoseInterpolation(
            L"a source/GPU palette could not be restored before a graphics "
            L"update.");
    }
}

void CaptureAndApplyWorldPose(void* const mesh) noexcept {
    if (!g_worldPoseInterpolationEnabled || !g_worldPoseRenderActive ||
        !g_hasLatestClockSample || !g_presentationClock.has_value() ||
        mesh == nullptr) {
        return;
    }
    g_worldPoseCalls.fetch_add(1, std::memory_order_relaxed);
    WorldMeshRegistrySlot* const registry = FindWorldMesh(
        reinterpret_cast<std::uintptr_t>(mesh));
    if (registry == nullptr) return;
    // Same class-wide gate as rigid world roots: any recently updated or
    // script-driven prop qualifies. Skeleton-physics props beyond a single
    // body wait for the ragdoll path.
    const std::uint32_t bodyCount =
        registry->bodyCount.load(std::memory_order_relaxed);
    const std::uint32_t ragdollBodyCount =
        registry->ragdollBodyCount.load(std::memory_order_relaxed);
    const bool ragdoll = bodyCount > 1 && ragdollBodyCount != 0;
    if ((ragdoll && !g_ragdollPoseInterpolationEnabled) ||
        world::ClassifyEligibility(
            true, bodyCount, ragdoll ? 0U : ragdollBodyCount,
            registry->scriptDriven.load(std::memory_order_relaxed) != 0,
            g_fixedSerial,
            registry->lastSeenSerial.load(std::memory_order_acquire),
            true) != world::Eligibility::eligible) {
        return;
    }
    g_worldPoseEligible.fetch_add(1, std::memory_order_relaxed);

    MatrixVectorView matrices{};
    SkinningVectorView vectors{};
    if (!ReadHandsPalette(mesh, matrices) ||
        !ReadHandsSkinningVectors(mesh, vectors) ||
        vectors.count != matrices.count * 2) {
        g_worldPoseUnavailable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const std::size_t matrixBytes = matrices.count * sizeof(*matrices.begin);
    const std::size_t vectorBytes = vectors.count * sizeof(*vectors.begin);
    auto* const matrixBuffer =
        reinterpret_cast<hands::Matrix*>(
            const_cast<std::array<float, 16>*>(matrices.begin));

    const std::uintptr_t prop =
        registry->prop.load(std::memory_order_acquire);
    WorldPoseSlot* const slot = FindWorldPoseSlot(
        reinterpret_cast<std::uintptr_t>(mesh), prop, true);
    if (slot == nullptr) return;
    void* const bufferIdentity = matrixBuffer;
    if (slot->matrixBufferIdentity != bufferIdentity ||
        slot->boneCount != matrices.count) {
        slot->history.Reset();
        slot->lastCapturedSerial = 0;
        slot->matrixBufferIdentity = bufferIdentity;
        slot->boneCount = matrices.count;
    }

    if (slot->lastCapturedSerial != g_fixedSerial) {
        pose::Pose authoritative{};
        authoritative.boneCount = matrices.count;
        std::memcpy(authoritative.bones.data(), matrices.begin, matrixBytes);
        const pose::PublishKind published = slot->history.Publish(
            g_fixedSerial, g_presentationClock->Generation(), authoritative);
        slot->lastCapturedSerial = g_fixedSerial;
        switch (published) {
        case pose::PublishKind::appended:
        case pose::PublishKind::unchanged:
        case pose::PublishKind::resetGeneration:
            g_worldPoseCaptures.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            g_worldPoseRejected.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    const pose::SampleResult sampled =
        slot->history.Evaluate(g_latestClockSample);
    if (sampled.kind != pose::SampleKind::applied ||
        sampled.pose.boneCount != matrices.count) {
        g_worldPoseUnavailable.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::memcpy(slot->matrixRestore.data(), matrices.begin, matrixBytes);
    std::memcpy(slot->skinningRestore.data(), vectors.begin, vectorBytes);
    std::memcpy(matrixBuffer, sampled.pose.bones.data(), matrixBytes);
    if (!BuildSkinningVectors(sampled.pose, vectors)) {
        std::memcpy(matrixBuffer, slot->matrixRestore.data(), matrixBytes);
        std::memcpy(vectors.begin, slot->skinningRestore.data(), vectorBytes);
        g_worldPoseRejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    slot->overridePose = sampled.pose;
    slot->matrixOverrideBuffer = matrixBuffer;
    slot->skinningOverrideBuffer = vectors.begin;
    slot->skinningVectorCount = vectors.count;
    slot->overridden = true;
    g_worldPoseApplies.fetch_add(1, std::memory_order_relaxed);
    if (ragdoll) {
        g_ragdollPoseApplies.fetch_add(1, std::memory_order_relaxed);
    }
}

void BeginWorldPoseRender() noexcept {
    g_worldPoseRenderActive = g_worldPoseInterpolationEnabled &&
        g_hasLatestClockSample && g_presentationClock.has_value();
}

void EndWorldPoseRender() noexcept {
    g_worldPoseRenderActive = false;
    if (!g_worldPoseInterpolationEnabled) return;
    for (auto& slot : g_worldPoseSlots) {
        if (!slot.overridden) continue;
        if (!RestoreWorldPoseSlot(slot)) {
            DisableWorldPoseInterpolation(
                L"an authoritative palette could not be restored after "
                L"rendering.");
            return;
        }
    }
}

void ResetAgentPoseSlot(AgentPoseSlot& slot) noexcept {
    slot.mesh = 0;
    slot.agent = 0;
    slot.lastTouchedSerial = 0;
    slot.lastCapturedSerial = 0;
    slot.matrixBufferIdentity = nullptr;
    slot.boneCount = 0;
    slot.history.Reset();
    slot.overridePose = {};
    slot.matrixOverrideBuffer = nullptr;
    slot.skinningOverrideBuffer = nullptr;
    slot.skinningVectorCount = 0;
    slot.overridden = false;
}

AgentPoseSlot* FindAgentPoseSlot(const std::uintptr_t mesh,
                                 const std::uintptr_t agent,
                                 const bool create) noexcept {
    AgentPoseSlot* empty = nullptr;
    AgentPoseSlot* evictable = nullptr;
    for (auto& slot : g_agentPoseSlots) {
        if (slot.mesh == mesh) {
            if (agent != 0 && slot.agent != agent) {
                if (slot.overridden) return nullptr;
                ResetAgentPoseSlot(slot);
                slot.mesh = mesh;
                slot.agent = agent;
            }
            slot.lastTouchedSerial = g_fixedSerial;
            return &slot;
        }
        if (empty == nullptr && slot.mesh == 0) empty = &slot;
        if (evictable == nullptr && !slot.overridden &&
            slot.lastTouchedSerial <= g_fixedSerial &&
            g_fixedSerial - slot.lastTouchedSerial > kAgentPoseEvictionAge) {
            evictable = &slot;
        }
    }
    if (!create) return nullptr;
    AgentPoseSlot* const selected = empty != nullptr ? empty : evictable;
    if (selected == nullptr) {
        g_agentPoseSlotFull.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    ResetAgentPoseSlot(*selected);
    selected->mesh = mesh;
    selected->agent = agent;
    selected->lastTouchedSerial = g_fixedSerial;
    return selected;
}

bool RestoreAgentPoseSlot(AgentPoseSlot& slot) noexcept {
    if (!slot.overridden || slot.matrixOverrideBuffer == nullptr ||
        slot.skinningOverrideBuffer == nullptr ||
        slot.overridePose.boneCount == 0 ||
        slot.skinningVectorCount != slot.overridePose.boneCount * 2) {
        return false;
    }
    const std::size_t matrixBytes =
        slot.overridePose.boneCount * sizeof(hands::Matrix);
    const std::size_t vectorBytes =
        slot.skinningVectorCount * sizeof(SkinningVector);
    std::memcpy(slot.matrixOverrideBuffer, slot.matrixRestore.data(),
                matrixBytes);
    std::memcpy(slot.skinningOverrideBuffer, slot.skinningRestore.data(),
                vectorBytes);
    slot.matrixOverrideBuffer = nullptr;
    slot.skinningOverrideBuffer = nullptr;
    slot.skinningVectorCount = 0;
    slot.overridePose = {};
    slot.overridden = false;
    g_agentPoseRestores.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void DisableAgentPoseInterpolation(const wchar_t* const reason) noexcept {
    g_agentPoseInterpolationEnabled = false;
    g_agentPoseRenderActive = false;
    g_agentPoseRestoreFailures.fetch_add(1, std::memory_order_relaxed);
    log::Write(log::Level::error,
               std::wstring(L"Agent pose interpolation disabled: ") + reason);
}

void RestoreAgentPoseBeforeUpdate(void* const mesh) noexcept {
    if (!g_agentPoseInterpolationEnabled || mesh == nullptr) return;
    AgentPoseSlot* const slot = FindAgentPoseSlot(
        reinterpret_cast<std::uintptr_t>(mesh), 0, false);
    if (slot == nullptr || !slot->overridden) return;
    if (!RestoreAgentPoseSlot(*slot)) {
        DisableAgentPoseInterpolation(
            L"a source/GPU palette could not be restored before a graphics "
            L"update.");
    }
}

void CaptureAndApplyAgentPose(void* const mesh) noexcept {
    if (!g_agentPoseInterpolationEnabled || !g_agentPoseRenderActive ||
        !g_hasLatestClockSample || !g_presentationClock.has_value() ||
        mesh == nullptr) {
        return;
    }
    g_agentPoseCalls.fetch_add(1, std::memory_order_relaxed);
    AgentMeshRegistrySlot* const registry = FindAgentMesh(
        reinterpret_cast<std::uintptr_t>(mesh));
    if (registry == nullptr) return;
    const std::uint64_t lastSeen =
        registry->lastSeenSerial.load(std::memory_order_acquire);
    if (lastSeen > g_fixedSerial || g_fixedSerial - lastSeen > 2) return;
    g_agentPoseEligible.fetch_add(1, std::memory_order_relaxed);

    MatrixVectorView matrices{};
    SkinningVectorView vectors{};
    if (!ReadHandsPalette(mesh, matrices) ||
        !ReadHandsSkinningVectors(mesh, vectors) ||
        vectors.count != matrices.count * 2) {
        g_agentPoseUnavailable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const std::size_t matrixBytes = matrices.count * sizeof(*matrices.begin);
    const std::size_t vectorBytes = vectors.count * sizeof(*vectors.begin);
    auto* const matrixBuffer = reinterpret_cast<hands::Matrix*>(
        const_cast<std::array<float, 16>*>(matrices.begin));

    const std::uintptr_t agent =
        registry->agent.load(std::memory_order_acquire);
    AgentPoseSlot* const slot = FindAgentPoseSlot(
        reinterpret_cast<std::uintptr_t>(mesh), agent, true);
    if (slot == nullptr) return;
    void* const bufferIdentity = matrixBuffer;
    if (slot->matrixBufferIdentity != bufferIdentity ||
        slot->boneCount != matrices.count) {
        slot->history.Reset();
        slot->lastCapturedSerial = 0;
        slot->matrixBufferIdentity = bufferIdentity;
        slot->boneCount = matrices.count;
    }

    if (slot->lastCapturedSerial != g_fixedSerial) {
        pose::Pose authoritative{};
        authoritative.boneCount = matrices.count;
        std::memcpy(authoritative.bones.data(), matrices.begin, matrixBytes);
        const pose::PublishKind published = slot->history.Publish(
            g_fixedSerial, g_presentationClock->Generation(), authoritative);
        slot->lastCapturedSerial = g_fixedSerial;
        switch (published) {
        case pose::PublishKind::appended:
        case pose::PublishKind::unchanged:
        case pose::PublishKind::resetGeneration:
            g_agentPoseCaptures.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            g_agentPoseRejected.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    const pose::SampleResult sampled =
        slot->history.Evaluate(g_latestClockSample);
    if (sampled.kind != pose::SampleKind::applied ||
        sampled.pose.boneCount != matrices.count) {
        g_agentPoseUnavailable.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::memcpy(slot->matrixRestore.data(), matrices.begin, matrixBytes);
    std::memcpy(slot->skinningRestore.data(), vectors.begin, vectorBytes);
    std::memcpy(matrixBuffer, sampled.pose.bones.data(), matrixBytes);
    if (!BuildSkinningVectors(sampled.pose, vectors)) {
        std::memcpy(matrixBuffer, slot->matrixRestore.data(), matrixBytes);
        std::memcpy(vectors.begin, slot->skinningRestore.data(), vectorBytes);
        g_agentPoseRejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    slot->overridePose = sampled.pose;
    slot->matrixOverrideBuffer = matrixBuffer;
    slot->skinningOverrideBuffer = vectors.begin;
    slot->skinningVectorCount = vectors.count;
    slot->overridden = true;
    g_agentPoseApplies.fetch_add(1, std::memory_order_relaxed);
}

void BeginAgentPoseRender() noexcept {
    g_agentPoseRenderActive = g_agentPoseInterpolationEnabled &&
        g_hasLatestClockSample && g_presentationClock.has_value();
}

void EndAgentPoseRender() noexcept {
    g_agentPoseRenderActive = false;
    bool restoreFailed = false;
    for (auto& slot : g_agentPoseSlots) {
        if (!slot.overridden) continue;
        if (!RestoreAgentPoseSlot(slot)) {
            restoreFailed = true;
        }
    }
    if (restoreFailed && g_agentPoseInterpolationEnabled) {
        DisableAgentPoseInterpolation(
            L"an authoritative palette could not be restored after "
            L"rendering.");
    }
}

bool ApplyHandsPoseBuffers(const pose::Pose& sampled,
                           const bool saveRestore) noexcept {
    MatrixVectorView matrices{};
    SkinningVectorView vectors{};
    if (!ReadHandsPalette(g_interpolatedHandsMesh, matrices) ||
        !ReadHandsSkinningVectors(g_interpolatedHandsMesh, vectors) ||
        matrices.count != sampled.boneCount ||
        vectors.count != sampled.boneCount * 2) {
        return false;
    }
    const std::size_t matrixBytes =
        matrices.count * sizeof(*matrices.begin);
    const std::size_t vectorBytes = vectors.count * sizeof(*vectors.begin);
    auto* const matrixBuffer =
        const_cast<std::array<float, 16>*>(matrices.begin);
    if (saveRestore) {
        std::memcpy(g_handsPoseRestore.data(), matrices.begin, matrixBytes);
        std::memcpy(g_handsSkinningRestore.data(), vectors.begin, vectorBytes);
    }
    std::memcpy(matrixBuffer, sampled.bones.data(), matrixBytes);
    if (!BuildSkinningVectors(sampled, vectors)) {
        if (saveRestore) {
            std::memcpy(matrixBuffer, g_handsPoseRestore.data(), matrixBytes);
            std::memcpy(vectors.begin, g_handsSkinningRestore.data(),
                        vectorBytes);
        }
        return false;
    }
    g_handsPoseOverrideBuffer =
        reinterpret_cast<hands::Matrix*>(matrixBuffer);
    g_handsSkinningOverrideBuffer = vectors.begin;
    g_handsSkinningVectorCount = vectors.count;
    g_handsSkinningApplies.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool RestoreHandsPoseBuffers() noexcept {
    if (g_handsPoseOverrideBuffer == nullptr ||
        g_handsSkinningOverrideBuffer == nullptr ||
        g_handsPoseOverride.boneCount == 0 ||
        g_handsSkinningVectorCount != g_handsPoseOverride.boneCount * 2) {
        return false;
    }
    const std::size_t matrixBytes =
        g_handsPoseOverride.boneCount * sizeof(hands::Matrix);
    const std::size_t vectorBytes =
        g_handsSkinningVectorCount * sizeof(SkinningVector);
    std::memcpy(g_handsPoseOverrideBuffer, g_handsPoseRestore.data(),
                matrixBytes);
    std::memcpy(g_handsSkinningOverrideBuffer,
                g_handsSkinningRestore.data(), vectorBytes);
    g_handsSkinningRestores.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool CaptureAndApplyHandsPose() noexcept {
    if (!g_handsPoseInterpolationEnabled ||
        g_interpolatedHandsMesh == nullptr ||
        g_originalMeshUpdateGraphics == nullptr ||
        !g_presentationClock.has_value()) {
        return false;
    }

    // Use the trampoline so the authoritative fixed-tick palette is current
    // before it is sampled and temporarily replaced for this presentation.
    g_originalMeshUpdateGraphics(g_interpolatedHandsMesh);
    MatrixVectorView view{};
    if (!ReadHandsPalette(g_interpolatedHandsMesh, view)) return false;
    const std::size_t bytes = view.count * sizeof(*view.begin);

    void* const bufferIdentity =
        const_cast<std::array<float, 16>*>(view.begin);
    if (g_handsPoseBufferIdentity != bufferIdentity ||
        g_handsPoseBoneCount != view.count) {
        g_handsPoseHistory.Reset();
        g_handsPoseLastSerial = 0;
        g_handsPoseBufferIdentity = bufferIdentity;
        g_handsPoseBoneCount = view.count;
    }

    if (g_handsPoseLastSerial != g_fixedSerial) {
        pose::Pose authoritative{};
        authoritative.boneCount = view.count;
        std::memcpy(authoritative.bones.data(), view.begin, bytes);
        const pose::PublishKind published = g_handsPoseHistory.Publish(
            g_fixedSerial, g_presentationClock->Generation(), authoritative);
        g_handsPoseLastSerial = g_fixedSerial;
        if (published == pose::PublishKind::appended ||
            published == pose::PublishKind::unchanged) {
            g_handsPoseCaptures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const pose::SampleResult sampled =
        g_handsPoseHistory.Evaluate(g_latestClockSample);
    if (sampled.kind != pose::SampleKind::applied ||
        sampled.pose.boneCount != view.count) {
        return false;
    }
    g_handsPoseOverride = sampled.pose;
    if (!ApplyHandsPoseBuffers(sampled.pose, true)) {
        g_handsPoseOverride = {};
        return false;
    }
    g_handsPoseOverridden = true;
    g_handsPoseApplies.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void BeginHandsRender() noexcept {
    g_handsRenderActive = false;
    g_handsRootRenderActive = false;
    g_handsModelSlotCount = 0;
    if ((!g_handsRootInterpolationEnabled &&
         !g_handsPoseInterpolationEnabled) ||
        !g_hasLatestClockSample || g_interpolatedHandsMesh == nullptr) {
        if (g_handsRootInterpolationEnabled ||
            g_handsPoseInterpolationEnabled) {
            g_handsRootUnavailable.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    g_handsRenderActive = true;

    if (g_handsRootInterpolationEnabled) {
        const hands::SampleResult sampled =
            g_handsRootHistory.Evaluate(g_latestClockSample);
        hands::Matrix inverse{};
        if (sampled.kind == hands::SampleKind::applied &&
            hands::InverseRigid(g_authoritativeHandsRoot, inverse) &&
            hands::Multiply(sampled.matrix, inverse,
                            g_handsRootCorrection)) {
            g_handsRootRenderActive = true;
        } else {
            g_handsRootUnavailable.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (g_handsPoseInterpolationEnabled && !CaptureAndApplyHandsPose()) {
        g_handsPoseUnavailable.fetch_add(1, std::memory_order_relaxed);
    }
}

void EndHandsRender() noexcept {
    g_handsRootRenderActive = false;
    g_handsRenderActive = false;
    g_handsModelSlotCount = 0;
    if (!g_handsPoseOverridden) return;
    if (RestoreHandsPoseBuffers()) {
        g_handsPoseRestores.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_handsPoseInterpolationEnabled = false;
        log::Write(log::Level::error,
                   L"Hands pose interpolation disabled: the authoritative "
                   L"palette could not be restored after rendering.");
    }
    g_handsPoseOverrideBuffer = nullptr;
    g_handsSkinningOverrideBuffer = nullptr;
    g_handsSkinningVectorCount = 0;
    g_handsPoseOverride = {};
    g_handsPoseOverridden = false;
}

// cCamera::SetRotationMatrix stores the view rotation here; the view-matrix
// rebuild consumes it instead of the Euler angles when the rotate mode is 1.
constexpr std::size_t kCameraRotationMatrixOffset = 0x1f4;

bool IsSaneCameraPose(const CameraPose& pose) noexcept {
    constexpr float kMaximumPosition = 10'000'000.0F;
    constexpr float kMaximumAngle = 1000.0F;
    for (const float component : pose.position) {
        if (!std::isfinite(component) ||
            std::abs(component) > kMaximumPosition) {
            return false;
        }
    }
    const std::array<float, 6> angles{
        pose.pitch, pose.yaw, pose.roll, pose.extendedPitch,
        pose.extendedYaw, pose.extendedRoll};
    for (const float angle : angles) {
        if (!std::isfinite(angle) || std::abs(angle) > kMaximumAngle) {
            return false;
        }
    }
    return pose.projectionType <= 1U && pose.rotateMode <= 1U;
}

bool ReadCameraPose(const void* const camera, CameraPose& pose) noexcept {
    pose = {};
    return ReadAt(camera, 0x10, pose.position) &&
           ReadAt(camera, 0x40, pose.projectionType) &&
           ReadAt(camera, 0x44, pose.pitch) &&
           ReadAt(camera, 0x48, pose.yaw) &&
           ReadAt(camera, 0x4c, pose.roll) &&
           ReadAt(camera, 0x60, pose.extendedPitch) &&
           ReadAt(camera, 0x64, pose.extendedYaw) &&
           ReadAt(camera, 0x68, pose.extendedRoll) &&
           ReadAt(camera, 0x6c, pose.rotateMode) &&
           (pose.rotateMode != camera::kRotateModeMatrix ||
            ReadAt(camera, kCameraRotationMatrixOffset, pose.rotation)) &&
           IsSaneCameraPose(pose);
}

bool SamePose(const CameraPose& left, const CameraPose& right) noexcept {
    return left.position == right.position && left.pitch == right.pitch &&
           left.yaw == right.yaw && left.roll == right.roll &&
           left.extendedPitch == right.extendedPitch &&
           left.extendedYaw == right.extendedYaw &&
           left.extendedRoll == right.extendedRoll &&
           left.projectionType == right.projectionType &&
           left.rotateMode == right.rotateMode &&
           left.rotation == right.rotation;
}

constexpr std::size_t kCameraSize = 0x710;
constexpr std::size_t kCameraFrustumSize = 0x308;

// A camera or extended-frustum address costs one OS memory query when it is
// first observed; every later presentation reuses the remembered verdict.
struct CameraMemorySlot {
    std::uintptr_t camera = 0;
    std::uintptr_t extendedFrustum = 0;
    std::uintptr_t rejectedExtendedFrustum = 0;
};

constexpr std::uint32_t kRejectedCameraRetryCalls = 256;
std::array<CameraMemorySlot, kMaximumCameraIdentities> g_cameraMemory{};
std::size_t g_cameraMemoryNext = 0;
std::uintptr_t g_rejectedCamera = 0;
std::uint32_t g_rejectedCameraRetryIn = 0;

void ResetCameraMemory() noexcept {
    g_cameraMemory = {};
    g_cameraMemoryNext = 0;
    g_rejectedCamera = 0;
    g_rejectedCameraRetryIn = 0;
}

CameraMemorySlot* FindValidatedCamera(void* const camera) noexcept {
    if (!IsPlausiblePointer(camera)) return nullptr;
    const auto identity = reinterpret_cast<std::uintptr_t>(camera);
    for (auto& slot : g_cameraMemory) {
        if (slot.camera == identity) return &slot;
    }
    if (g_rejectedCamera == identity && g_rejectedCameraRetryIn != 0) {
        --g_rejectedCameraRetryIn;
        return nullptr;
    }
    if (!QueryCommittedRange(camera, kCameraSize, true)) {
        g_rejectedCamera = identity;
        g_rejectedCameraRetryIn = kRejectedCameraRetryCalls;
        return nullptr;
    }
    CameraMemorySlot& slot = g_cameraMemory[g_cameraMemoryNext];
    g_cameraMemoryNext = (g_cameraMemoryNext + 1U) % g_cameraMemory.size();
    slot = {};
    slot.camera = identity;
    return &slot;
}

bool IsValidatedExtendedFrustum(CameraMemorySlot& slot,
                                const std::uintptr_t extended) noexcept {
    if (extended == slot.extendedFrustum) return true;
    const auto* const address = reinterpret_cast<const void*>(extended);
    if (extended == slot.rejectedExtendedFrustum ||
        !IsPlausiblePointer(address)) {
        return false;
    }
    if (!QueryCommittedRange(address, kCameraFrustumSize, true)) {
        slot.rejectedExtendedFrustum = extended;
        return false;
    }
    slot.extendedFrustum = extended;
    return true;
}

void* ResolveViewportCamera(void* const viewport) noexcept {
    void* camera = nullptr;
    if (g_viewportCameraOffset == 0 ||
        !ReadAt(viewport, g_viewportCameraOffset, camera) ||
        FindValidatedCamera(camera) == nullptr) {
        return nullptr;
    }
    return camera;
}

void RecordCameraTelemetry(void* const camera) noexcept {
    g_viewportRenders.fetch_add(1, std::memory_order_relaxed);
    if (camera == nullptr) {
        g_cameraPoseInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    CameraPose pose{};
    if (!ReadCameraPose(camera, pose)) {
        g_cameraPoseInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_cameraPoseValid.fetch_add(1, std::memory_order_relaxed);
    g_latestCameraIdentity.store(reinterpret_cast<std::uintptr_t>(camera),
                                 std::memory_order_release);
    g_lastValidCameraDraw.store(
        g_draws.load(std::memory_order_relaxed), std::memory_order_release);

    AcquireSRWLockExclusive(&g_cameraLock);
    CameraSlot* selected = nullptr;
    CameraSlot* empty = nullptr;
    const auto identity = reinterpret_cast<std::uintptr_t>(camera);
    for (auto& slot : g_cameraSlots) {
        if (slot.camera == identity) {
            selected = &slot;
            break;
        }
        if (empty == nullptr && slot.camera == 0) empty = &slot;
    }
    if (selected == nullptr) selected = empty;
    if (selected == nullptr) {
        g_cameraIdentityOverflow.fetch_add(1, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_cameraLock);
        return;
    }
    if (selected->camera == 0) selected->camera = identity;
    ++selected->samples;
    if (selected->hasPose) {
        if (SamePose(selected->lastPose, pose)) {
            ++selected->repeats;
        } else if (selected->lastPoseSerial == g_fixedSerial) {
            ++selected->sameFixedSerialChanges;
        } else {
            ++selected->fixedBoundaryChanges;
        }
    }
    selected->lastPose = pose;
    selected->lastPoseSerial = g_fixedSerial;
    selected->hasPose = true;
    ReleaseSRWLockExclusive(&g_cameraLock);
}

camera::Pose ToInterpolationPose(const CameraPose& source) noexcept {
    camera::Pose result{};
    result.position = {source.position[0], source.position[1],
                       source.position[2]};
    result.pitch = source.pitch;
    result.yaw = source.yaw;
    result.roll = source.roll;
    result.extendedPitch = source.extendedPitch;
    result.extendedYaw = source.extendedYaw;
    result.extendedRoll = source.extendedRoll;
    result.projectionType = source.projectionType;
    result.rotateMode = source.rotateMode;
    result.rotation.values = source.rotation;
    return result;
}

bool ReadInterpolationPose(const void* const camera,
                           camera::Pose& pose) noexcept {
    CameraPose raw{};
    if (!ReadCameraPose(camera, raw)) return false;
    pose = ToInterpolationPose(raw);
    return camera::IsFinite(pose);
}

struct CameraSavedState {
    camera::Pose pose{};
    std::array<std::uint8_t, 0x100> matrices{};
    std::array<std::uint8_t, kCameraFrustumSize> embeddedFrustum{};
    std::array<std::uint8_t, kCameraFrustumSize> extendedFrustum{};
    std::array<std::uint8_t, 6> flags{};
    std::uintptr_t extendedFrustumPointer = 0;
    bool hasExtendedFrustum = false;
};

bool SaveCameraState(void* const camera, CameraSavedState& state) noexcept {
    CameraMemorySlot* const memory = FindValidatedCamera(camera);
    if (memory == nullptr || !ReadInterpolationPose(camera, state.pose) ||
        !ReadAt(camera, 0x700, state.extendedFrustumPointer)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(camera);
    std::memcpy(state.matrices.data(), bytes + 0x74, state.matrices.size());
    std::memcpy(state.embeddedFrustum.data(), bytes + 0x3f8,
                state.embeddedFrustum.size());
    std::memcpy(state.flags.data(), bytes + 0x708, state.flags.size());
    state.hasExtendedFrustum = state.extendedFrustumPointer != 0;
    if (state.hasExtendedFrustum) {
        if (!IsValidatedExtendedFrustum(*memory,
                                        state.extendedFrustumPointer)) {
            return false;
        }
        std::memcpy(state.extendedFrustum.data(),
                    reinterpret_cast<const void*>(
                        state.extendedFrustumPointer),
                    state.extendedFrustum.size());
    }
    return true;
}

bool HasExtendedRotation(const camera::Pose& pose) noexcept {
    constexpr float kEpsilon = 1.0e-7F;
    return std::abs(pose.extendedPitch) > kEpsilon ||
           std::abs(pose.extendedYaw) > kEpsilon ||
           std::abs(pose.extendedRoll) > kEpsilon;
}

bool ApplyCameraPose(void* const camera, const camera::Pose& pose,
                     const CameraSavedState& saved) noexcept {
    if (!camera::IsFinite(pose) ||
        (!saved.hasExtendedFrustum && HasExtendedRotation(pose))) {
        return false;
    }
    const std::array<float, 3> position{
        pose.position.x, pose.position.y, pose.position.z};
    if (!WriteAt(camera, 0x10, position) ||
        !WriteAt(camera, 0x44, pose.pitch) ||
        !WriteAt(camera, 0x48, pose.yaw) ||
        !WriteAt(camera, 0x4c, pose.roll) ||
        !WriteAt(camera, 0x60, pose.extendedPitch) ||
        !WriteAt(camera, 0x64, pose.extendedYaw) ||
        !WriteAt(camera, 0x68, pose.extendedRoll) ||
        (pose.rotateMode == camera::kRotateModeMatrix &&
         !WriteAt(camera, kCameraRotationMatrixOffset,
                  pose.rotation.values))) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(camera);
    bytes[0x709] = 1U;
    bytes[0x70b] = 1U;
    bytes[0x70c] = 1U;
    bytes[0x70d] = 1U;
    camera::Pose applied{};
    return ReadInterpolationPose(camera, applied) &&
           camera::NearlyEqual(applied, pose, 0.0F);
}

bool RestoreCameraState(void* const camera,
                        const CameraSavedState& state) noexcept {
    std::uintptr_t currentExtended = 0;
    if (!ReadAt(camera, 0x700, currentExtended) ||
        currentExtended != state.extendedFrustumPointer) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(camera);
    const std::array<float, 3> position{
        state.pose.position.x, state.pose.position.y, state.pose.position.z};
    std::memcpy(bytes + 0x10, position.data(), sizeof(position));
    std::memcpy(bytes + 0x44, &state.pose.pitch, sizeof(state.pose.pitch));
    std::memcpy(bytes + 0x48, &state.pose.yaw, sizeof(state.pose.yaw));
    std::memcpy(bytes + 0x4c, &state.pose.roll, sizeof(state.pose.roll));
    std::memcpy(bytes + 0x60, &state.pose.extendedPitch,
                sizeof(state.pose.extendedPitch));
    std::memcpy(bytes + 0x64, &state.pose.extendedYaw,
                sizeof(state.pose.extendedYaw));
    std::memcpy(bytes + 0x68, &state.pose.extendedRoll,
                sizeof(state.pose.extendedRoll));
    if (state.pose.rotateMode == camera::kRotateModeMatrix) {
        std::memcpy(bytes + kCameraRotationMatrixOffset,
                    state.pose.rotation.values.data(),
                    sizeof(state.pose.rotation.values));
    }
    std::memcpy(bytes + 0x74, state.matrices.data(), state.matrices.size());
    std::memcpy(bytes + 0x3f8, state.embeddedFrustum.data(),
                state.embeddedFrustum.size());
    if (state.hasExtendedFrustum) {
        std::memcpy(reinterpret_cast<void*>(state.extendedFrustumPointer),
                    state.extendedFrustum.data(),
                    state.extendedFrustum.size());
    }
    std::memcpy(bytes + 0x708, state.flags.data(), state.flags.size());

    camera::Pose restored{};
    if (!ReadInterpolationPose(camera, restored) ||
        !camera::NearlyEqual(restored, state.pose, 0.0F) ||
        std::memcmp(bytes + 0x74, state.matrices.data(),
                    state.matrices.size()) != 0 ||
        std::memcmp(bytes + 0x3f8, state.embeddedFrustum.data(),
                    state.embeddedFrustum.size()) != 0 ||
        std::memcmp(bytes + 0x708, state.flags.data(), state.flags.size()) != 0) {
        return false;
    }
    return !state.hasExtendedFrustum ||
           std::memcmp(reinterpret_cast<const void*>(
                           state.extendedFrustumPointer),
                       state.extendedFrustum.data(),
                       state.extendedFrustum.size()) == 0;
}

void CaptureCameraSnapshot() noexcept {
    if (!g_cameraInterpolationEnabled || !g_presentationClock.has_value()) {
        return;
    }
    const std::uint64_t draws = g_draws.load(std::memory_order_acquire);
    const std::uint64_t lastValid =
        g_lastValidCameraDraw.load(std::memory_order_acquire);
    const std::uintptr_t identity =
        g_latestCameraIdentity.load(std::memory_order_acquire);
    if (identity == 0 || draws > lastValid + 2) {
        g_cameraHistory.Reset();
        g_cameraHistoryIdentity = 0;
        g_cameraCaptureResets.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    camera::Pose pose{};
    if (!ReadInterpolationPose(reinterpret_cast<const void*>(identity), pose)) {
        g_cameraHistory.Reset();
        g_cameraHistoryIdentity = 0;
        g_cameraCaptureResets.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_cameraHistoryIdentity != identity) {
        g_cameraHistory.Reset();
        g_cameraHistoryIdentity = identity;
        g_cameraCaptureResets.fetch_add(1, std::memory_order_relaxed);
    }
    const camera::PublishResult published = g_cameraHistory.Publish(
        g_fixedSerial, g_presentationClock->Generation(), pose);
    if (published.kind == camera::PublishKind::appended ||
        published.kind == camera::PublishKind::unchanged) {
        g_cameraCaptures.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_cameraCaptureResets.fetch_add(1, std::memory_order_relaxed);
    }
}

// Gameplay mouse look. The engine reads SDL's relative mouse state once per
// fixed update, so camera rotation is otherwise both stepped and a tick
// late. Presentation reads that state early through the original function
// and the detour hands the carried counts to the engine's next read, which
// keeps every count delivered exactly once.
mouse::LookPreview g_lookPreview{};
DWORD g_lookPreviewThread = 0;
hands::Matrix g_lookCorrection{};
bool g_lookCorrectionActive = false;
float g_lookBlendYaw = 0.0F;
float g_lookBlendPitch = 0.0F;
std::atomic<std::uint64_t> g_lookEngineReads{0};
std::atomic<std::uint64_t> g_lookEngineDiscards{0};
std::atomic<std::uint64_t> g_lookPresentationReads{0};
std::atomic<std::uint64_t> g_lookForeignThreadReads{0};
std::atomic<std::uint64_t> g_lookYawApplies{0};
std::atomic<std::uint64_t> g_lookPitchApplies{0};
std::atomic<std::uint64_t> g_lookUnavailable{0};
std::atomic<std::uint64_t> g_lookPumpCalls{0};
std::atomic<std::uint64_t> g_lookPumpLimitHits{0};
std::atomic<std::uint64_t> g_lookCapturedMismatchTicks{0};
std::atomic<bool> g_lookYawDirect{false};
std::atomic<bool> g_lookPitchDirect{false};

// Frames that run a fixed update reach the mouse read later than other
// frames, yet a frame limiter shows them on the same schedule, so their
// input is younger when displayed. The age from read to present is measured
// per frame kind and the difference is held back on the younger kind.
struct LookFrameAge {
    double seconds = 0.0;
    std::uint32_t samples = 0;
};
constexpr std::uint32_t kLookAgeWarmup = 32;
LookFrameAge g_lookAgeNormal{};
LookFrameAge g_lookAgeTick{};
double g_lookSampleSeconds = -1.0;
bool g_lookSampleWasTick = false;
bool g_lookTickSinceSample = false;

struct LookTraceRow {
    std::uint32_t microseconds = 0;
    std::int16_t readX = 0;
    std::int16_t readY = 0;
    std::uint16_t lateMicroseconds = 0;
    std::uint8_t tickFrame = 0;
    std::int32_t yawMicroRadians = 0;
};
// Recent frames in which the preview drew mouse motion; written to the log
// once at shutdown, never from a hook hot path.
constexpr std::size_t kLookTraceRows = 600;
std::array<LookTraceRow, kLookTraceRows> g_lookTrace{};
std::size_t g_lookTraceNext = 0;
std::size_t g_lookTraceCount = 0;
double g_lookTraceLastSeconds = 0.0;
int g_lookLastReadX = 0;
int g_lookLastReadY = 0;
double g_lookLastLateSeconds = 0.0;

void ResetLookPreview() noexcept {
    g_lookAgeNormal = {};
    g_lookAgeTick = {};
    g_lookSampleSeconds = -1.0;
    g_lookSampleWasTick = false;
    g_lookTickSinceSample = false;
    g_lookTraceNext = 0;
    g_lookTraceCount = 0;
    g_lookTraceLastSeconds = 0.0;
    g_lookLastReadX = 0;
    g_lookLastReadY = 0;
    g_lookLastLateSeconds = 0.0;
    g_lookPreview.Reset();
    g_lookCorrection = {};
    g_lookCorrectionActive = false;
    g_lookBlendYaw = 0.0F;
    g_lookBlendPitch = 0.0F;
    g_lookEngineReads.store(0, std::memory_order_relaxed);
    g_lookEngineDiscards.store(0, std::memory_order_relaxed);
    g_lookPresentationReads.store(0, std::memory_order_relaxed);
    g_lookForeignThreadReads.store(0, std::memory_order_relaxed);
    g_lookYawApplies.store(0, std::memory_order_relaxed);
    g_lookPitchApplies.store(0, std::memory_order_relaxed);
    g_lookUnavailable.store(0, std::memory_order_relaxed);
    g_lookPumpCalls.store(0, std::memory_order_relaxed);
    g_lookPumpLimitHits.store(0, std::memory_order_relaxed);
    g_lookCapturedMismatchTicks.store(0, std::memory_order_relaxed);
}

double LookPreviewSeconds() noexcept {
    LARGE_INTEGER now{};
    (void)QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_telemetryStart.QuadPart) /
           static_cast<double>(g_qpcFrequency.QuadPart);
}

int SaturateToInt(const std::int64_t value) noexcept {
    return static_cast<int>((std::clamp)(
        value,
        static_cast<std::int64_t>((std::numeric_limits<int>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<int>::max)())));
}

std::uint32_t SdlGetRelativeMouseStateDetour(int* const x, int* const y) {
    if (!g_mouseLookPreviewEnabled ||
        !g_telemetryActive.load(std::memory_order_relaxed)) {
        return g_originalSdlGetRelativeMouseState(x, y);
    }
    if (GetCurrentThreadId() != g_lookPreviewThread) {
        g_lookForeignThreadReads.fetch_add(1, std::memory_order_relaxed);
        return g_originalSdlGetRelativeMouseState(x, y);
    }
    if (x == nullptr && y == nullptr) {
        // The engine flushes stale motion this way when it changes mouse
        // mode; anything read early belongs to the same discarded motion.
        g_lookPreview.OnDiscarded();
        g_lookEngineDiscards.fetch_add(1, std::memory_order_relaxed);
        return g_originalSdlGetRelativeMouseState(x, y);
    }
    int readX = 0;
    int readY = 0;
    const std::uint32_t buttons =
        g_originalSdlGetRelativeMouseState(&readX, &readY);
    const mouse::Counts total =
        g_lookPreview.OnDelivered(readX, readY, LookPreviewSeconds());
    if (x != nullptr) *x = SaturateToInt(total.x);
    if (y != nullptr) *y = SaturateToInt(total.y);
    g_lookEngineReads.fetch_add(1, std::memory_order_relaxed);
    return buttons;
}

void AcquireLookPreview() noexcept {
    if (!g_mouseLookPreviewEnabled || g_sdlPumpEvents == nullptr ||
        g_originalSdlGetRelativeMouseState == nullptr ||
        GetCurrentThreadId() != g_lookPreviewThread) {
        return;
    }
    // Pumping only moves OS input into SDL's queue and relative state; the
    // engine still polls every event itself, in order.
    //
    // The bundled SDL pump returns after the first message stamped inside
    // the current GetTickCount period (about 15.6 ms), so one call consumes
    // a single mouse report and the rest wait for the timer to advance.
    // Pump until the thread's queue is empty, as the engine's own
    // SDL_PollEvent loop does once per fixed update.
    constexpr std::uint32_t kMaximumPumps = 256;
    std::uint32_t pumps = 0;
    MSG queued{};
    do {
        g_sdlPumpEvents();
        ++pumps;
    } while (pumps < kMaximumPumps &&
             PeekMessageW(&queued, nullptr, 0, 0, PM_NOREMOVE) != FALSE);
    g_lookPumpCalls.fetch_add(pumps, std::memory_order_relaxed);
    if (pumps == kMaximumPumps) {
        g_lookPumpLimitHits.fetch_add(1, std::memory_order_relaxed);
    }
    int readX = 0;
    int readY = 0;
    (void)g_originalSdlGetRelativeMouseState(&readX, &readY);
    const double now = LookPreviewSeconds();
    const bool tickFrame = g_lookTickSinceSample;
    g_lookTickSinceSample = false;
    double lateSeconds = 0.0;
    if (g_lookAgeNormal.samples >= kLookAgeWarmup &&
        g_lookAgeTick.samples >= kLookAgeWarmup) {
        const LookFrameAge& mine = tickFrame ? g_lookAgeTick : g_lookAgeNormal;
        const double oldest =
            (std::max)(g_lookAgeNormal.seconds, g_lookAgeTick.seconds);
        lateSeconds = (std::max)(0.0, oldest - mine.seconds);
    }
    g_lookPreview.OnAcquired(readX, readY, now, lateSeconds);
    g_lookSampleSeconds = now;
    g_lookSampleWasTick = tickFrame;
    g_lookLastReadX = readX;
    g_lookLastReadY = readY;
    g_lookLastLateSeconds = lateSeconds;
    g_lookPresentationReads.fetch_add(1, std::memory_order_relaxed);
}

void NoteLookPresent() noexcept {
    if (!g_mouseLookPreviewEnabled || g_lookSampleSeconds < 0.0) return;
    const double age = LookPreviewSeconds() - g_lookSampleSeconds;
    g_lookSampleSeconds = -1.0;
    constexpr double kMaximumAge = 0.050;
    if (age <= 0.0 || age > kMaximumAge) return;
    LookFrameAge& target =
        g_lookSampleWasTick ? g_lookAgeTick : g_lookAgeNormal;
    target.seconds = target.samples == 0
        ? age
        : target.seconds + 0.05 * (age - target.seconds);
    if (target.samples < kLookAgeWarmup) ++target.samples;
}

void RecordLookTrace(const float renderedYaw) noexcept {
    if (g_lookLastReadX == 0 && g_lookLastReadY == 0) return;
    const double now = g_lookSampleSeconds >= 0.0
        ? g_lookSampleSeconds
        : LookPreviewSeconds();
    LookTraceRow& row = g_lookTrace[g_lookTraceNext];
    g_lookTraceNext = (g_lookTraceNext + 1) % kLookTraceRows;
    if (g_lookTraceCount < kLookTraceRows) ++g_lookTraceCount;
    row.microseconds = static_cast<std::uint32_t>((std::clamp)(
        (now - g_lookTraceLastSeconds) * 1.0e6, 0.0, 4.0e9));
    g_lookTraceLastSeconds = now;
    row.readX = static_cast<std::int16_t>(
        (std::clamp)(g_lookLastReadX, -32768, 32767));
    row.readY = static_cast<std::int16_t>(
        (std::clamp)(g_lookLastReadY, -32768, 32767));
    row.lateMicroseconds = static_cast<std::uint16_t>(
        (std::clamp)(g_lookLastLateSeconds * 1.0e6, 0.0, 65535.0));
    row.tickFrame = g_lookSampleWasTick ? 1U : 0U;
    row.yawMicroRadians = static_cast<std::int32_t>(
        (std::clamp)(static_cast<double>(renderedYaw) * 1.0e6, -2.0e9,
                     2.0e9));
}

void ReportLookTrace() {
    // The per-frame trace is diagnostic detail, not routine logging.
    if (!g_mouseLookPreviewEnabled || !g_detailedReportsEnabled ||
        g_lookTraceCount == 0) {
        return;
    }
    log::Write(log::Level::info,
               L"Mouse look trace, oldest first; each entry is "
               L"dtMicroseconds/tickFrame/readX/readY/lateMicroseconds/"
               L"renderedYawMicroRadians.");
    constexpr std::size_t kRowsPerLine = 12;
    const std::size_t first =
        (g_lookTraceNext + kLookTraceRows - g_lookTraceCount) %
        kLookTraceRows;
    std::wostringstream line;
    for (std::size_t index = 0; index < g_lookTraceCount; ++index) {
        const LookTraceRow& row =
            g_lookTrace[(first + index) % kLookTraceRows];
        line << row.microseconds << L'/' << static_cast<int>(row.tickFrame)
             << L'/' << row.readX << L'/' << row.readY << L'/'
             << row.lateMicroseconds << L'/' << row.yawMicroRadians << L' ';
        if ((index + 1) % kRowsPerLine == 0 ||
            index + 1 == g_lookTraceCount) {
            log::Write(log::Level::info, L"LookTrace " + line.str());
            line.str(L"");
        }
    }
}

void ObserveLookPreviewTick() noexcept {
    if (!g_mouseLookPreviewEnabled) return;
    camera::Pose pose{};
    const bool valid = g_cameraHistoryIdentity != 0 &&
        ReadInterpolationPose(
            reinterpret_cast<const void*>(g_cameraHistoryIdentity), pose) &&
        pose.rotateMode == camera::kRotateModeEuler;
    const std::uint64_t before = g_lookPreview.Yaw(0).Mismatches() +
                                 g_lookPreview.Pitch(0).Mismatches();
    g_lookPreview.OnTickBoundary(valid, pose.yaw, pose.pitch);
    // Mismatches while the mouse is captured for look are the ones that can
    // interrupt a preview; menus and terminals release the capture.
    if (g_lookPreview.Yaw(0).Mismatches() +
                g_lookPreview.Pitch(0).Mismatches() != before &&
        g_sdlGetRelativeMouseMode != nullptr &&
        g_sdlGetRelativeMouseMode() != 0) {
        g_lookCapturedMismatchTicks.fetch_add(1, std::memory_order_relaxed);
    }
    g_lookYawDirect.store(
        g_lookPreview.Yaw(0).Direct() || g_lookPreview.Yaw(1).Direct(),
        std::memory_order_relaxed);
    g_lookPitchDirect.store(
        g_lookPreview.Pitch(0).Direct() || g_lookPreview.Pitch(1).Direct(),
        std::memory_order_relaxed);
}

float StepLookBlend(const float blend, const bool engaged) noexcept {
    // A short ramp hides the one-tick offset between the interpolated and
    // the previewed orientation whenever the preview engages or releases.
    constexpr float kStep = 0.125F;
    return (std::clamp)(blend + (engaged ? kStep : -kStep), 0.0F, 1.0F);
}

// Replaces the delayed, interpolated yaw/pitch with the newest authoritative
// orientation plus the mouse motion the fixed update has not applied yet.
void ApplyLookPreview(const camera::Pose& authoritative,
                      const camera::Pose& interpolated,
                      camera::Pose& rendered) noexcept {
    g_lookCorrectionActive = false;
    if (!g_mouseLookPreviewEnabled) return;
    const bool eligible =
        authoritative.rotateMode == camera::kRotateModeEuler &&
        interpolated.rotateMode == camera::kRotateModeEuler &&
        g_sdlGetRelativeMouseMode != nullptr &&
        g_sdlGetRelativeMouseMode() != 0;
    const mouse::Offset offset =
        eligible ? g_lookPreview.PendingOffset(LookPreviewSeconds())
                 : mouse::Offset{};
    g_lookBlendYaw = StepLookBlend(g_lookBlendYaw, offset.yawValid);
    g_lookBlendPitch = StepLookBlend(g_lookBlendPitch, offset.pitchValid);
    if (authoritative.rotateMode != camera::kRotateModeEuler ||
        interpolated.rotateMode != camera::kRotateModeEuler) {
        g_lookBlendYaw = 0.0F;
        g_lookBlendPitch = 0.0F;
    }
    if (g_lookBlendYaw <= 0.0F && g_lookBlendPitch <= 0.0F) {
        g_lookUnavailable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    constexpr float kMaximumPitch = 1.55F;
    if (g_lookBlendYaw > 0.0F) {
        const double target = static_cast<double>(authoritative.yaw) +
                              (offset.yawValid ? offset.yaw : 0.0);
        rendered.yaw = interpolated.yaw + g_lookBlendYaw *
            static_cast<float>(mouse::WrapAngle(target - interpolated.yaw));
        g_lookYawApplies.fetch_add(1, std::memory_order_relaxed);
        if (offset.yawValid) RecordLookTrace(rendered.yaw);
    }
    if (g_lookBlendPitch > 0.0F) {
        const float target = (std::clamp)(
            authoritative.pitch +
                static_cast<float>(offset.pitchValid ? offset.pitch : 0.0),
            -kMaximumPitch, kMaximumPitch);
        rendered.pitch = interpolated.pitch +
                         g_lookBlendPitch * (target - interpolated.pitch);
        g_lookPitchApplies.fetch_add(1, std::memory_order_relaxed);
    }
    g_lookCorrectionActive =
        camera::LookCorrection(rendered, interpolated, g_lookCorrection);
}

// Keeps hands and held objects locked to the previewed camera rotation.
void ApplyLookCorrection(hands::Matrix& matrix) noexcept {
    if (!g_lookCorrectionActive) return;
    hands::Matrix corrected{};
    if (hands::Multiply(g_lookCorrection, matrix, corrected)) {
        matrix = corrected;
    }
}

void AtomicMinimum(std::atomic<double>& destination,
                   const double value) noexcept {
    double current = destination.load(std::memory_order_relaxed);
    while (value < current &&
           !destination.compare_exchange_weak(
               current, value, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void AtomicMaximum(std::atomic<double>& destination,
                   const double value) noexcept {
    double current = destination.load(std::memory_order_relaxed);
    while (value > current &&
           !destination.compare_exchange_weak(
               current, value, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

bool CaptureLogicTimer(void* const engine) noexcept {
    if (engine == nullptr || g_engineTimerOffset == 0) return false;
    const auto* const slot = reinterpret_cast<const std::uintptr_t*>(
        static_cast<const std::uint8_t*>(engine) + g_engineTimerOffset);
    if (!QueryCommittedRange(slot, sizeof(*slot), false)) return false;
    const std::uintptr_t timer = *slot;
    constexpr std::size_t kRequiredTimerBytes = 0x48;
    if (!QueryCommittedRange(reinterpret_cast<const void*>(timer),
                             kRequiredTimerBytes, false)) {
        return false;
    }
    g_logicTimer.store(timer, std::memory_order_release);
    double stepMicroseconds = 0.0;
    std::memcpy(&stepMicroseconds,
                reinterpret_cast<const void*>(timer + 0x8),
                sizeof(stepMicroseconds));
    if (!std::isfinite(stepMicroseconds) || stepMicroseconds < 1000.0 ||
        stepMicroseconds > 1'000'000.0) {
        return false;
    }
    g_presentationClock.emplace(clock::Configuration{
        stepMicroseconds / 1'000'000.0, 0.250, 1});
    return g_presentationClock->IsConfigured();
}

template <typename T>
T ReadTimerField(const std::uintptr_t timer,
                 const std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(timer + offset),
                sizeof(value));
    return value;
}

bool RecordSchedulerPhase(double& alpha, double& wallTimeSeconds) noexcept {
    alpha = 0.0;
    wallTimeSeconds = 0.0;
    const std::uintptr_t timer =
        g_logicTimer.load(std::memory_order_acquire);
    if (timer == 0 || g_engineMicroseconds == nullptr) {
        g_phaseInvalid.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const std::uint64_t deadline =
        ReadTimerField<std::uint64_t>(timer, 0x0);
    const double stepMicroseconds = ReadTimerField<double>(timer, 0x8);
    const double speedMultiplier = ReadTimerField<double>(timer, 0x38);
    const std::uint64_t now = g_engineMicroseconds();
    double rawAlpha = 0.0;
    if (!clock::LogicTimerAlpha(
            static_cast<double>(deadline) / 1000.0,
            stepMicroseconds / 1000.0, speedMultiplier,
            static_cast<double>(now) / 1000.0, 0.0, alpha, &rawAlpha)) {
        g_phaseInvalid.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (rawAlpha < 0.0) {
        g_phaseBelowRange.fetch_add(1, std::memory_order_relaxed);
    } else if (rawAlpha > 1.0) {
        g_phaseAboveRange.fetch_add(1, std::memory_order_relaxed);
    }
    g_phaseSum.fetch_add(rawAlpha, std::memory_order_relaxed);
    AtomicMinimum(g_phaseMinimum, rawAlpha);
    AtomicMaximum(g_phaseMaximum, rawAlpha);
    g_phaseSamples.fetch_add(1, std::memory_order_relaxed);
    wallTimeSeconds = static_cast<double>(now) / 1'000'000.0;
    return true;
}

std::wstring HookError(const wchar_t* const operation,
                       const MH_STATUS status) {
    const char* text = MH_StatusToString(status);
    std::wstring wide;
    if (text != nullptr) {
        while (*text != '\0') {
            wide.push_back(static_cast<unsigned char>(*text));
            ++text;
        }
    }
    return std::wstring(operation) + L" failed: " + wide;
}

void ResetTelemetry() noexcept {
    g_fixedTicks.store(0, std::memory_order_relaxed);
    g_draws.store(0, std::memory_order_relaxed);
    g_postRenders.store(0, std::memory_order_relaxed);
    g_postBufferSwaps.store(0, std::memory_order_relaxed);
    g_presents.store(0, std::memory_order_relaxed);
    g_newtonSteps.store(0, std::memory_order_relaxed);
    g_newtonSeconds.store(0.0, std::memory_order_relaxed);
    g_phaseSamples.store(0, std::memory_order_relaxed);
    g_phaseInvalid.store(0, std::memory_order_relaxed);
    g_phaseBelowRange.store(0, std::memory_order_relaxed);
    g_phaseAboveRange.store(0, std::memory_order_relaxed);
    g_phaseSum.store(0.0, std::memory_order_relaxed);
    g_phaseMinimum.store(std::numeric_limits<double>::infinity(),
                         std::memory_order_relaxed);
    g_phaseMaximum.store(-std::numeric_limits<double>::infinity(),
                         std::memory_order_relaxed);
    g_clockPublishes.store(0, std::memory_order_relaxed);
    g_clockPublishResets.store(0, std::memory_order_relaxed);
    g_clockInterpolated.store(0, std::memory_order_relaxed);
    g_clockUnavailable.store(0, std::memory_order_relaxed);
    g_clockEvaluationResets.store(0, std::memory_order_relaxed);
    g_viewportRenders.store(0, std::memory_order_relaxed);
    g_cameraPoseValid.store(0, std::memory_order_relaxed);
    g_cameraPoseInvalid.store(0, std::memory_order_relaxed);
    g_cameraIdentityOverflow.store(0, std::memory_order_relaxed);
    g_latestCameraIdentity.store(0, std::memory_order_relaxed);
    g_lastValidCameraDraw.store(0, std::memory_order_relaxed);
    g_cameraCaptures.store(0, std::memory_order_relaxed);
    g_cameraCaptureResets.store(0, std::memory_order_relaxed);
    g_cameraOverrides.store(0, std::memory_order_relaxed);
    g_cameraMatrixModeOverrides.store(0, std::memory_order_relaxed);
    g_cameraRestores.store(0, std::memory_order_relaxed);
    g_cameraOverrideUnavailable.store(0, std::memory_order_relaxed);
    g_cameraRestoreFailures.store(0, std::memory_order_relaxed);
    g_updaterMessageContext.store(-1, std::memory_order_relaxed);
    g_guiMousePixelCalls.store(0, std::memory_order_relaxed);
    g_guiMouseVirtualCalls.store(0, std::memory_order_relaxed);
    g_guiMouseMotionCalls.store(0, std::memory_order_relaxed);
    g_guiMouseRelativeModeCalls.store(0, std::memory_order_relaxed);
    g_guiMouseAbsoluteModeCalls.store(0, std::memory_order_relaxed);
    g_guiMouseIdentityOverflow.store(0, std::memory_order_relaxed);
    g_guiMousePresentationCalls.store(0, std::memory_order_relaxed);
    g_guiMousePresentationMoves.store(0, std::memory_order_relaxed);
    g_guiMouseFixedCorrections.store(0, std::memory_order_relaxed);
    g_guiMouseVirtualCalibrations.store(0, std::memory_order_relaxed);
    g_guiMouseStaleSkips.store(0, std::memory_order_relaxed);
    g_guiMouseRelativeModeSkips.store(0, std::memory_order_relaxed);
    g_guiMouseInvalidSkips.store(0, std::memory_order_relaxed);
    g_guiMousePreviousSyncs.store(0, std::memory_order_relaxed);
    g_guiMousePreviousSyncFailures.store(0,
                                         std::memory_order_relaxed);
    g_guiCursorQueueCalls.store(0, std::memory_order_relaxed);
    g_guiCursorCommandCaptures.store(0, std::memory_order_relaxed);
    g_guiCursorCommandApplies.store(0, std::memory_order_relaxed);
    g_guiCursorCommandFailures.store(0, std::memory_order_relaxed);
    g_guiDoMouseCalls.store(0, std::memory_order_relaxed);
    g_guiDoMouseCaptures.store(0, std::memory_order_relaxed);
    g_guiDoMouseFailures.store(0, std::memory_order_relaxed);
    g_guiMouseWindow = nullptr;
    AcquireSRWLockExclusive(&g_guiMouseLock);
    g_guiMouseSlots = {};
    ReleaseSRWLockExclusive(&g_guiMouseLock);
    g_entitySetMatrixCalls.store(0, std::memory_order_relaxed);
    g_handsSetMatrixCalls.store(0, std::memory_order_relaxed);
    g_handsIdentityOverflow.store(0, std::memory_order_relaxed);
    g_latestHandsMesh.store(0, std::memory_order_relaxed);
    g_meshUpdateGraphicsCalls.store(0, std::memory_order_relaxed);
    g_handsPaletteUpdates.store(0, std::memory_order_relaxed);
    g_handsPaletteMisses.store(0, std::memory_order_relaxed);
    g_handsRootCaptures.store(0, std::memory_order_relaxed);
    g_handsRootApplies.store(0, std::memory_order_relaxed);
    g_handsRootUnavailable.store(0, std::memory_order_relaxed);
    g_handsPoseCaptures.store(0, std::memory_order_relaxed);
    g_handsPoseApplies.store(0, std::memory_order_relaxed);
    g_handsPoseRestores.store(0, std::memory_order_relaxed);
    g_handsPoseUnavailable.store(0, std::memory_order_relaxed);
    g_handsSkinningApplies.store(0, std::memory_order_relaxed);
    g_handsSkinningRestores.store(0, std::memory_order_relaxed);
    g_handObjectUpdates.store(0, std::memory_order_relaxed);
    g_handObjectCaptures.store(0, std::memory_order_relaxed);
    g_handObjectApplies.store(0, std::memory_order_relaxed);
    g_agentUpdates.store(0, std::memory_order_relaxed);
    g_agentMeshMissing.store(0, std::memory_order_relaxed);
    g_agentMeshInsertions.store(0, std::memory_order_relaxed);
    g_agentRegistryFull.store(0, std::memory_order_relaxed);
    g_agentPaletteCalls.store(0, std::memory_order_relaxed);
    g_agentPaletteCaptures.store(0, std::memory_order_relaxed);
    g_agentPaletteRepeats.store(0, std::memory_order_relaxed);
    g_agentPaletteChanges.store(0, std::memory_order_relaxed);
    g_agentPaletteInvalid.store(0, std::memory_order_relaxed);
    g_agentPaletteIdentityChanges.store(0, std::memory_order_relaxed);
    g_agentMinimumBones.store(0, std::memory_order_relaxed);
    g_agentMaximumBones.store(0, std::memory_order_relaxed);
    for (auto& count : g_agentMessageCounts) {
        count.store(0, std::memory_order_relaxed);
    }
    g_agentModelCalls.store(0, std::memory_order_relaxed);
    g_agentModelRecent.store(0, std::memory_order_relaxed);
    g_agentModelStale.store(0, std::memory_order_relaxed);
    g_agentModelCaptures.store(0, std::memory_order_relaxed);
    g_agentModelApplies.store(0, std::memory_order_relaxed);
    g_agentModelUnavailable.store(0, std::memory_order_relaxed);
    g_agentModelInvalid.store(0, std::memory_order_relaxed);
    g_agentModelSameTickMutation.store(0, std::memory_order_relaxed);
    g_agentModelDiscontinuity.store(0, std::memory_order_relaxed);
    g_agentModelSlotFull.store(0, std::memory_order_relaxed);
    g_agentPoseCalls.store(0, std::memory_order_relaxed);
    g_agentPoseEligible.store(0, std::memory_order_relaxed);
    g_agentPoseCaptures.store(0, std::memory_order_relaxed);
    g_agentPoseApplies.store(0, std::memory_order_relaxed);
    g_agentPoseRestores.store(0, std::memory_order_relaxed);
    g_agentPoseUnavailable.store(0, std::memory_order_relaxed);
    g_agentPoseRejected.store(0, std::memory_order_relaxed);
    g_agentPoseSlotFull.store(0, std::memory_order_relaxed);
    g_agentPoseRestoreFailures.store(0, std::memory_order_relaxed);
    g_worldPropUpdates.store(0, std::memory_order_relaxed);
    g_worldPropMessageMismatch.store(0, std::memory_order_relaxed);
    g_worldPropLayoutInvalid.store(0, std::memory_order_relaxed);
    g_worldPropMeshMissing.store(0, std::memory_order_relaxed);
    g_worldPropBodyInvalid.store(0, std::memory_order_relaxed);
    g_worldPropZeroBody.store(0, std::memory_order_relaxed);
    g_worldPropOneBody.store(0, std::memory_order_relaxed);
    g_worldPropMultiBody.store(0, std::memory_order_relaxed);
    g_worldMeshRegistryInsertions.store(0, std::memory_order_relaxed);
    g_worldMeshRegistryFull.store(0, std::memory_order_relaxed);
    g_worldMeshRegistryEvictions.store(0, std::memory_order_relaxed);
    g_ragdollPropUpdates.store(0, std::memory_order_relaxed);
    g_ragdollTaggedBodies.store(0, std::memory_order_relaxed);
    g_ragdollBodyFlagInvalid.store(0, std::memory_order_relaxed);
    g_ragdollPaletteCalls.store(0, std::memory_order_relaxed);
    g_ragdollPaletteCaptures.store(0, std::memory_order_relaxed);
    g_ragdollPaletteRepeats.store(0, std::memory_order_relaxed);
    g_ragdollPaletteChanges.store(0, std::memory_order_relaxed);
    g_ragdollPaletteInvalid.store(0, std::memory_order_relaxed);
    g_ragdollPaletteIdentityChanges.store(0, std::memory_order_relaxed);
    g_ragdollMinimumBones.store(0, std::memory_order_relaxed);
    g_ragdollMaximumBones.store(0, std::memory_order_relaxed);
    g_ragdollRootApplies.store(0, std::memory_order_relaxed);
    g_ragdollPoseApplies.store(0, std::memory_order_relaxed);
    g_worldModelCalls.store(0, std::memory_order_relaxed);
    g_worldModelRecent.store(0, std::memory_order_relaxed);
    g_worldModelStale.store(0, std::memory_order_relaxed);
    g_worldModelSlotFull.store(0, std::memory_order_relaxed);
    g_worldModelCaptures.store(0, std::memory_order_relaxed);
    g_worldModelApplies.store(0, std::memory_order_relaxed);
    g_worldModelUnavailable.store(0, std::memory_order_relaxed);
    g_worldModelInvalid.store(0, std::memory_order_relaxed);
    g_worldModelSameTickMutation.store(0, std::memory_order_relaxed);
    g_worldModelDiscontinuity.store(0, std::memory_order_relaxed);
    g_worldArticulatedCalls.store(0, std::memory_order_relaxed);
    g_worldArticulatedCaptures.store(0, std::memory_order_relaxed);
    g_worldArticulatedApplies.store(0, std::memory_order_relaxed);
    g_worldArticulatedUnavailable.store(0, std::memory_order_relaxed);
    g_worldArticulatedRejected.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenSetMatrixCalls.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenRefreshes.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenRegistryMisses.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenCalls.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenCaptures.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenApplies.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenUnavailable.store(0, std::memory_order_relaxed);
    g_worldScriptDrivenRejected.store(0, std::memory_order_relaxed);
    g_worldPoseCalls.store(0, std::memory_order_relaxed);
    g_worldPoseEligible.store(0, std::memory_order_relaxed);
    g_worldPoseCaptures.store(0, std::memory_order_relaxed);
    g_worldPoseApplies.store(0, std::memory_order_relaxed);
    g_worldPoseRestores.store(0, std::memory_order_relaxed);
    g_worldPoseUnavailable.store(0, std::memory_order_relaxed);
    g_worldPoseRejected.store(0, std::memory_order_relaxed);
    g_worldPoseSlotFull.store(0, std::memory_order_relaxed);
    g_worldPoseRestoreFailures.store(0, std::memory_order_relaxed);
    for (auto& count : g_worldPropMessageCounts) {
        count.store(0, std::memory_order_relaxed);
    }
    for (auto& slot : g_worldMeshRegistry) {
        slot.ragdollPaletteHash.store(0, std::memory_order_relaxed);
        slot.ragdollBoneCount.store(0, std::memory_order_relaxed);
        slot.ragdollPaletteBuffer.store(0, std::memory_order_relaxed);
        slot.ragdollPaletteLastSerial.store(0,
                                            std::memory_order_relaxed);
        slot.lastSeenSerial.store(0, std::memory_order_relaxed);
        slot.bodyCount.store(0, std::memory_order_relaxed);
        slot.ragdollBodyCount.store(0, std::memory_order_relaxed);
        slot.scriptDriven.store(0, std::memory_order_relaxed);
        slot.prop.store(0, std::memory_order_relaxed);
        slot.mesh.store(0, std::memory_order_relaxed);
    }
    for (auto& slot : g_agentMeshRegistry) {
        slot.paletteHash.store(0, std::memory_order_relaxed);
        slot.boneCount.store(0, std::memory_order_relaxed);
        slot.paletteBuffer.store(0, std::memory_order_relaxed);
        slot.paletteLastSerial.store(0, std::memory_order_relaxed);
        slot.lastSeenSerial.store(0, std::memory_order_relaxed);
        slot.agent.store(0, std::memory_order_relaxed);
        slot.mesh.store(0, std::memory_order_relaxed);
    }
    for (auto& slot : g_agentPoseSlots) {
        ResetAgentPoseSlot(slot);
    }
    for (auto& slot : g_agentModelSlots) {
        slot = {};
    }
    g_agentPoseRenderActive = false;
    // Do not aggregate-assign this large table. MSVC may materialize the
    // value-initialized temporary on SOMA's main-thread stack before copying
    // it, which is large enough to overflow the stock stack at cEngine::Run
    // entry. Clear each static-storage slot in place instead.
    for (auto& slot : g_worldModelSlots) {
        slot.subMesh = 0;
        slot.lastTouchedSerial = 0;
        slot.track.Reset();
        slot.interpolated = {};
    }
    // These slots are also intentionally cleared in place: their fixed-size
    // pose and restore buffers make an aggregate temporary too large for the
    // game's stock main-thread stack.
    for (auto& slot : g_worldPoseSlots) {
        ResetWorldPoseSlot(slot);
    }
    g_worldPoseRenderActive = false;
    g_cameraHistory.Reset();
    g_cameraHistoryIdentity = 0;
    ResetCameraMemory();
    ResetLookPreview();
    g_latestClockSample = {};
    g_hasLatestClockSample = false;
    AcquireSRWLockExclusive(&g_cameraLock);
    g_cameraSlots = {};
    ReleaseSRWLockExclusive(&g_cameraLock);
    AcquireSRWLockExclusive(&g_handsLock);
    g_handsSlots = {};
    ReleaseSRWLockExclusive(&g_handsLock);
    ResetHandsInterpolation();
    g_presentationClock.reset();
    g_fixedSerial = 0;
    g_previousFixedTicks = 0;
    g_previousDraws = 0;
    g_previousPostRenders = 0;
    g_previousPostBufferSwaps = 0;
    g_previousPresents = 0;
    g_previousNewtonSteps = 0;
    g_previousNewtonSeconds = 0.0;
    (void)QueryPerformanceCounter(&g_telemetryStart);
    g_previousReport = g_telemetryStart;
}

GuiMouseSlot* FindGuiMouseSlotLocked(const std::uintptr_t identity,
                                     const bool create) noexcept {
    // The game creates new GUI objects throughout a session, so a slot whose
    // GUI has not been drawn or updated for a while is handed to the next
    // one instead of leaving later GUIs without a smoothed cursor.
    constexpr std::uint64_t kIdleTicksBeforeReuse = 120;
    GuiMouseSlot* empty = nullptr;
    GuiMouseSlot* idle = nullptr;
    for (auto& candidate : g_guiMouseSlots) {
        if (candidate.gui == identity && identity != 0) {
            if (create) candidate.lastTouchedSerial = g_fixedSerial;
            return &candidate;
        }
        if (candidate.gui == 0) {
            if (empty == nullptr) empty = &candidate;
        } else if (candidate.lastTouchedSerial <= g_fixedSerial &&
                   g_fixedSerial - candidate.lastTouchedSerial >
                       kIdleTicksBeforeReuse &&
                   (idle == nullptr ||
                    candidate.lastTouchedSerial < idle->lastTouchedSerial)) {
            idle = &candidate;
        }
    }
    if (!create || identity == 0) return nullptr;
    GuiMouseSlot* const selected = empty != nullptr ? empty : idle;
    if (selected == nullptr) return nullptr;
    *selected = {};
    selected->gui = identity;
    selected->lastTouchedSerial = g_fixedSerial;
    return selected;
}

struct ProcessWindowSearch {
    DWORD processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindProcessWindow(const HWND window,
                                const LPARAM parameter) noexcept {
    auto* const search =
        reinterpret_cast<ProcessWindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    search->window = window;
    return FALSE;
}

bool EnsureGuiMouseWindow() noexcept {
    if (g_guiMouseWindow != nullptr && IsWindow(g_guiMouseWindow)) return true;
    const DWORD processId = GetCurrentProcessId();
    HWND window = GetForegroundWindow();
    DWORD windowProcessId = 0;
    if (window != nullptr) {
        GetWindowThreadProcessId(window, &windowProcessId);
    }
    if (window == nullptr || windowProcessId != processId) {
        ProcessWindowSearch search{processId, nullptr};
        EnumWindows(&FindProcessWindow, reinterpret_cast<LPARAM>(&search));
        window = search.window;
    }
    g_guiMouseWindow = window;
    return window != nullptr;
}

bool ReadClientCursor(Int2& cursor,
                      Int2* const clientSize = nullptr) noexcept {
    cursor = {};
    if (!EnsureGuiMouseWindow()) return false;
    RECT client{};
    POINT point{};
    if (!GetClientRect(g_guiMouseWindow, &client) ||
        client.right <= client.left || client.bottom <= client.top ||
        !GetCursorPos(&point) ||
        !ScreenToClient(g_guiMouseWindow, &point)) {
        return false;
    }
    cursor.x = (std::clamp)(point.x, 0L, client.right - client.left - 1L);
    cursor.y = (std::clamp)(point.y, 0L, client.bottom - client.top - 1L);
    if (clientSize != nullptr) {
        clientSize->x = client.right - client.left;
        clientSize->y = client.bottom - client.top;
    }
    return true;
}

int SaturatingDifference(const int current, const int previous) noexcept {
    const std::int64_t difference =
        static_cast<std::int64_t>(current) - previous;
    return static_cast<int>((std::clamp)(
        difference,
        static_cast<std::int64_t>((std::numeric_limits<int>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<int>::max)())));
}

struct GuiDrawVectorView {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    std::uintptr_t capacity = 0;
    std::size_t count = 0;
};

bool ReadGuiDrawVector(void* const gui,
                       GuiDrawVectorView& view) noexcept {
    view = {};
    if (gui == nullptr ||
        !ReadAt(gui, mapping::kGuiDrawCommandsOffset, view.begin) ||
        !ReadAt(gui, mapping::kGuiDrawCommandsOffset + sizeof(void*),
                view.end) ||
        !ReadAt(gui, mapping::kGuiDrawCommandsOffset + 2 * sizeof(void*),
                view.capacity)) {
        return false;
    }
    if (view.begin == 0 || view.end == 0 || view.capacity == 0) {
        return view.begin == 0 && view.end == 0 && view.capacity == 0;
    }
    if (view.begin > view.end || view.end > view.capacity) return false;
    const std::uintptr_t used = view.end - view.begin;
    if (used % mapping::kGuiDrawCommandSize != 0) return false;
    view.count = static_cast<std::size_t>(
        used / mapping::kGuiDrawCommandSize);
    constexpr std::size_t kMaximumDrawCommands = 65'536;
    if (view.count > kMaximumDrawCommands) return false;
    return IsPlausiblePointer(reinterpret_cast<const void*>(view.begin));
}

void GuiDoMouseDetour(void* const gui, const void* const gfx,
                     const Float3* const offset, const void* const size) {
    GuiDrawVectorView before{};
    Float2 queuedMouse{};
    std::uint8_t movementEnabled = 0;
    const bool candidate = gui != nullptr && gfx != nullptr &&
        offset != nullptr && ReadGuiDrawVector(gui, before) &&
        ReadAt(gui, mapping::kGuiMousePreviousPositionOffset,
               queuedMouse) &&
        std::isfinite(queuedMouse.x) && std::isfinite(queuedMouse.y) &&
        ReadAt(gui, mapping::kGuiMouseMovementEnabledOffset,
               movementEnabled) && movementEnabled != 0;

    g_originalGuiDoMouse(gui, gfx, offset, size);
    if (!g_telemetryActive.load(std::memory_order_relaxed) ||
        !g_guiMouseOverlayEnabled) {
        return;
    }
    g_guiDoMouseCalls.fetch_add(1, std::memory_order_relaxed);

    GuiDrawVectorView after{};
    Float2 queuedPosition{};
    std::uintptr_t commandIdentity = 0;
    bool captured = false;
    if (candidate && ReadGuiDrawVector(gui, after) &&
        after.count == before.count + 1) {
        const std::uintptr_t command = after.begin +
            before.count * mapping::kGuiDrawCommandSize;
        captured =
            ReadAt(reinterpret_cast<const void*>(command), 0,
                   commandIdentity) && commandIdentity != 0 &&
            ReadAt(reinterpret_cast<const void*>(command),
                   mapping::kGuiDrawCommandPositionOffset,
                   queuedPosition) &&
            std::isfinite(queuedPosition.x) &&
            std::isfinite(queuedPosition.y);
    }

    AcquireSRWLockExclusive(&g_guiMouseLock);
    GuiMouseSlot* const slot = FindGuiMouseSlotLocked(
        reinterpret_cast<std::uintptr_t>(gui), true);
    if (slot != nullptr) {
        if (candidate) slot->cursorCommandValid = captured;
        if (captured) {
            slot->cursorCommandIndex = before.count;
            slot->cursorCommandIdentity = commandIdentity;
            slot->cursorQueuedMouse = queuedMouse;
            slot->cursorQueuedPosition = queuedPosition;
            slot->lastCursorQueueSerial = g_fixedSerial;
            g_guiDoMouseCaptures.fetch_add(1,
                                           std::memory_order_relaxed);
        } else if (candidate) {
            g_guiDoMouseFailures.fetch_add(1,
                                           std::memory_order_relaxed);
        }
    } else {
        g_guiMouseIdentityOverflow.fetch_add(1,
                                             std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_guiMouseLock);
}

void GuiCursorQueueDetour(void* const gui) {
    GuiDrawVectorView before{};
    Float2 queuedMouse{};
    std::uint8_t drawMouse = 0;
    std::uint8_t movementEnabled = 0;
    std::uint8_t alreadyQueued = 0;
    const bool candidate =
        gui != nullptr && ReadGuiDrawVector(gui, before) &&
        ReadAt(gui, mapping::kGuiMousePreviousPositionOffset,
               queuedMouse) &&
        std::isfinite(queuedMouse.x) && std::isfinite(queuedMouse.y) &&
        ReadAt(gui, mapping::kGuiDrawMouseOffset, drawMouse) &&
        ReadAt(gui, mapping::kGuiMouseMovementEnabledOffset,
               movementEnabled) &&
        ReadAt(gui, mapping::kGuiCursorAlreadyQueuedOffset,
               alreadyQueued) &&
        drawMouse != 0 && movementEnabled != 0 && alreadyQueued == 0;

    g_originalGuiCursorQueue(gui);
    g_guiCursorQueueCalls.fetch_add(1, std::memory_order_relaxed);

    GuiDrawVectorView after{};
    Float2 queuedPosition{};
    std::uintptr_t commandIdentity = 0;
    bool captured = false;
    if (candidate && ReadGuiDrawVector(gui, after) &&
        after.count > before.count) {
        const std::uintptr_t command = after.begin +
            before.count * mapping::kGuiDrawCommandSize;
        captured =
            ReadAt(reinterpret_cast<const void*>(command), 0,
                   commandIdentity) && commandIdentity != 0 &&
            ReadAt(reinterpret_cast<const void*>(command),
                   mapping::kGuiDrawCommandPositionOffset,
                   queuedPosition) &&
            std::isfinite(queuedPosition.x) &&
            std::isfinite(queuedPosition.y);
    }

    AcquireSRWLockExclusive(&g_guiMouseLock);
    GuiMouseSlot* const slot = FindGuiMouseSlotLocked(
        reinterpret_cast<std::uintptr_t>(gui), true);
    if (slot != nullptr) {
        ++slot->cursorQueueCalls;
        if (candidate) slot->cursorCommandValid = captured;
        if (captured) {
            slot->cursorCommandIndex = before.count;
            slot->cursorCommandIdentity = commandIdentity;
            slot->cursorQueuedMouse = queuedMouse;
            slot->cursorQueuedPosition = queuedPosition;
            slot->lastCursorQueueSerial = g_fixedSerial;
            g_guiCursorCommandCaptures.fetch_add(
                1, std::memory_order_relaxed);
        }
    } else {
        g_guiMouseIdentityOverflow.fetch_add(1,
                                             std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_guiMouseLock);
}

bool ApplyGuiCursorCommand(GuiMouseSlot& slot,
                           const Float2& mousePosition) noexcept {
    if (!slot.cursorCommandValid ||
        slot.lastCursorQueueSerial > g_fixedSerial ||
        g_fixedSerial - slot.lastCursorQueueSerial > 2 ||
        !std::isfinite(mousePosition.x) ||
        !std::isfinite(mousePosition.y)) {
        return false;
    }

    GuiDrawVectorView vector{};
    if (!ReadGuiDrawVector(reinterpret_cast<void*>(slot.gui), vector) ||
        slot.cursorCommandIndex >= vector.count) {
        slot.cursorCommandValid = false;
        g_guiCursorCommandFailures.fetch_add(1,
                                              std::memory_order_relaxed);
        return false;
    }
    const std::uintptr_t command = vector.begin +
        slot.cursorCommandIndex * mapping::kGuiDrawCommandSize;
    std::uintptr_t identity = 0;
    if (!ReadAt(reinterpret_cast<const void*>(command), 0, identity) ||
        identity != slot.cursorCommandIdentity) {
        slot.cursorCommandValid = false;
        g_guiCursorCommandFailures.fetch_add(1,
                                              std::memory_order_relaxed);
        return false;
    }

    const Float2 position{
        slot.cursorQueuedPosition.x +
            (mousePosition.x - slot.cursorQueuedMouse.x),
        slot.cursorQueuedPosition.y +
            (mousePosition.y - slot.cursorQueuedMouse.y)};
    constexpr float kMaximumCoordinateMagnitude = 1.0e6F;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        std::abs(position.x) > kMaximumCoordinateMagnitude ||
        std::abs(position.y) > kMaximumCoordinateMagnitude) {
        slot.cursorCommandValid = false;
        g_guiCursorCommandFailures.fetch_add(1,
                                              std::memory_order_relaxed);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(
                    command + mapping::kGuiDrawCommandPositionOffset),
                &position, sizeof(position));
    g_guiCursorCommandApplies.fetch_add(1,
                                        std::memory_order_relaxed);
    return true;
}

void RecordGuiMouseCall(void* const gui, const bool virtualPosition,
                        const bool motion) noexcept {
    if (!g_telemetryActive.load(std::memory_order_relaxed) ||
        !g_guiMouseOverlayEnabled) {
        return;
    }
    if (virtualPosition) {
        g_guiMouseVirtualCalls.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_guiMousePixelCalls.fetch_add(1, std::memory_order_relaxed);
    }
    if (motion) {
        g_guiMouseMotionCalls.fetch_add(1, std::memory_order_relaxed);
    }
    if (g_sdlGetRelativeMouseMode != nullptr &&
        g_sdlGetRelativeMouseMode() != 0) {
        g_guiMouseRelativeModeCalls.fetch_add(1,
                                               std::memory_order_relaxed);
    } else {
        g_guiMouseAbsoluteModeCalls.fetch_add(1,
                                               std::memory_order_relaxed);
    }

    const auto identity = reinterpret_cast<std::uintptr_t>(gui);
    const int message = g_updaterMessageContext.load(std::memory_order_relaxed);
    const std::size_t messageIndex =
        message >= 0 && message <= 7 ? static_cast<std::size_t>(message) : 8U;
    AcquireSRWLockExclusive(&g_guiMouseLock);
    GuiMouseSlot* const slot = FindGuiMouseSlotLocked(identity, true);
    if (slot != nullptr) {
        if (virtualPosition) {
            ++slot->virtualCalls;
        } else {
            ++slot->pixelCalls;
        }
        if (motion) ++slot->motionCalls;
        ++slot->messageCounts[messageIndex];
    } else {
        g_guiMouseIdentityOverflow.fetch_add(1,
                                             std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_guiMouseLock);
}

void GuiMousePixelSetterDetour(void* const gui, const Int2* const position,
                               const Int2* const relative) {
    Int2 corrected{};
    const Int2* delivered = relative;
    bool fixedCorrection = false;
    if (gui != nullptr && position != nullptr && relative != nullptr) {
        corrected = *relative;
        AcquireSRWLockShared(&g_guiMouseLock);
        const GuiMouseSlot* const slot = FindGuiMouseSlotLocked(
            reinterpret_cast<std::uintptr_t>(gui), false);
        if (slot != nullptr && slot->positionValid &&
            !slot->usesVirtualPosition) {
            corrected.x = SaturatingDifference(position->x,
                                                slot->pixelPresented.x);
            corrected.y = SaturatingDifference(position->y,
                                                slot->pixelPresented.y);
            fixedCorrection = corrected.x != relative->x ||
                              corrected.y != relative->y;
        }
        ReleaseSRWLockShared(&g_guiMouseLock);
        delivered = &corrected;
    }
    g_originalGuiMousePixelSetter(gui, position, delivered);
    if (fixedCorrection) {
        g_guiMouseFixedCorrections.fetch_add(1,
                                              std::memory_order_relaxed);
    }
    if (gui != nullptr && position != nullptr && relative != nullptr) {
        AcquireSRWLockExclusive(&g_guiMouseLock);
        GuiMouseSlot* const slot = FindGuiMouseSlotLocked(
            reinterpret_cast<std::uintptr_t>(gui), true);
        if (slot != nullptr) {
            slot->usesVirtualPosition = false;
            slot->positionValid = true;
            slot->pixelFixed = *position;
            slot->pixelPresented = *position;
            slot->lastStockSerial = g_fixedSerial;
        }
        for (auto& virtualSlot : g_guiMouseSlots) {
            if (!virtualSlot.awaitingVirtualCalibration ||
                virtualSlot.lastStockSerial != g_fixedSerial) {
                continue;
            }
            virtualSlot.fixedCursor = *position;
            bool calibrated = false;
            if (relative->x != 0) {
                const float scale = virtualSlot.virtualStockRelative.x /
                                    static_cast<float>(relative->x);
                if (std::isfinite(scale) && std::abs(scale) > 1.0e-8F &&
                    std::abs(scale) <= 10'000.0F) {
                    virtualSlot.virtualScaleX = scale;
                    virtualSlot.virtualScaleXValid = true;
                    calibrated = true;
                }
            }
            if (relative->y != 0) {
                const float scale = virtualSlot.virtualStockRelative.y /
                                    static_cast<float>(relative->y);
                if (std::isfinite(scale) && std::abs(scale) > 1.0e-8F &&
                    std::abs(scale) <= 10'000.0F) {
                    virtualSlot.virtualScaleY = scale;
                    virtualSlot.virtualScaleYValid = true;
                    calibrated = true;
                }
            }
            virtualSlot.awaitingVirtualCalibration = false;
            if (calibrated) {
                g_guiMouseVirtualCalibrations.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        ReleaseSRWLockExclusive(&g_guiMouseLock);
    }
    const bool motion = relative != nullptr &&
                        (relative->x != 0 || relative->y != 0);
    RecordGuiMouseCall(gui, false, motion);
}

void GuiMouseVirtualSetterDetour(void* const gui,
                                 const Float2* const position,
                                 const Float2* const relative) {
    Float2 corrected{};
    const Float2* delivered = relative;
    bool fixedCorrection = false;
    if (gui != nullptr && position != nullptr && relative != nullptr) {
        corrected = *relative;
        AcquireSRWLockShared(&g_guiMouseLock);
        const GuiMouseSlot* const slot = FindGuiMouseSlotLocked(
            reinterpret_cast<std::uintptr_t>(gui), false);
        if (slot != nullptr && slot->positionValid &&
            slot->usesVirtualPosition) {
            corrected.x = position->x - slot->virtualPresented.x;
            corrected.y = position->y - slot->virtualPresented.y;
            fixedCorrection = corrected.x != relative->x ||
                              corrected.y != relative->y;
        }
        ReleaseSRWLockShared(&g_guiMouseLock);
        delivered = &corrected;
    }
    g_originalGuiMouseVirtualSetter(gui, position, delivered);
    if (fixedCorrection) {
        g_guiMouseFixedCorrections.fetch_add(1,
                                              std::memory_order_relaxed);
    }
    if (gui != nullptr && position != nullptr && relative != nullptr &&
        std::isfinite(position->x) && std::isfinite(position->y) &&
        std::isfinite(relative->x) && std::isfinite(relative->y)) {
        Int2 cursor{};
        const bool hasCursor = ReadClientCursor(cursor);
        AcquireSRWLockExclusive(&g_guiMouseLock);
        GuiMouseSlot* const slot = FindGuiMouseSlotLocked(
            reinterpret_cast<std::uintptr_t>(gui), true);
        if (slot != nullptr) {
            slot->usesVirtualPosition = true;
            slot->positionValid = true;
            slot->virtualFixed = *position;
            slot->virtualPresented = *position;
            slot->virtualStockRelative = *relative;
            if (hasCursor) slot->fixedCursor = cursor;
            slot->lastStockSerial = g_fixedSerial;
            slot->awaitingVirtualCalibration = true;
        }
        ReleaseSRWLockExclusive(&g_guiMouseLock);
    }
    const bool motion = relative != nullptr &&
                        std::isfinite(relative->x) &&
                        std::isfinite(relative->y) &&
                        (relative->x != 0.0F || relative->y != 0.0F);
    RecordGuiMouseCall(gui, true, motion);
}

bool SyncGuiMousePreviousToCurrent(void* const gui,
                                   Float2* const synchronized) noexcept {
    if (gui == nullptr) return false;

    Float2 current{};
    if (!ReadAt(gui, mapping::kGuiMouseCurrentPositionOffset, current) ||
        !std::isfinite(current.x) || !std::isfinite(current.y)) {
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(gui) +
                    mapping::kGuiMousePreviousPositionOffset,
                &current, sizeof(current));
    if (synchronized != nullptr) *synchronized = current;
    return true;
}

void ApplyGuiMousePresentation() noexcept {
    if (!g_guiMouseOverlayEnabled ||
        g_originalGuiMousePixelSetter == nullptr ||
        g_originalGuiMouseVirtualSetter == nullptr ||
        g_sdlGetRelativeMouseMode == nullptr) {
        return;
    }
    if (g_sdlGetRelativeMouseMode() != 0) {
        g_guiMouseRelativeModeSkips.fetch_add(1,
                                              std::memory_order_relaxed);
        return;
    }
    Int2 cursor{};
    if (!ReadClientCursor(cursor)) {
        g_guiMouseInvalidSkips.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    g_guiMousePresentationCalls.fetch_add(1,
                                           std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_guiMouseLock);
    for (auto& slot : g_guiMouseSlots) {
        if (!slot.positionValid || slot.gui == 0 ||
            slot.lastStockSerial > g_fixedSerial ||
            g_fixedSerial - slot.lastStockSerial > 2) {
            if (slot.positionValid && slot.gui != 0) {
                g_guiMouseStaleSkips.fetch_add(1,
                                               std::memory_order_relaxed);
            }
            continue;
        }
        if (!slot.usesVirtualPosition) {
            const Int2 movement{
                SaturatingDifference(cursor.x, slot.pixelPresented.x),
                SaturatingDifference(cursor.y, slot.pixelPresented.y)};
            if (movement.x == 0 && movement.y == 0) continue;
            g_originalGuiMousePixelSetter(
                reinterpret_cast<void*>(slot.gui), &cursor, &movement);
            Float2 synchronized{};
            if (SyncGuiMousePreviousToCurrent(
                    reinterpret_cast<void*>(slot.gui), &synchronized)) {
                g_guiMousePreviousSyncs.fetch_add(
                    1, std::memory_order_relaxed);
                (void)ApplyGuiCursorCommand(slot, synchronized);
            } else {
                slot.positionValid = false;
                g_guiMousePreviousSyncFailures.fetch_add(
                    1, std::memory_order_relaxed);
            }
            slot.pixelPresented = cursor;
            g_guiMousePresentationMoves.fetch_add(1,
                                                  std::memory_order_relaxed);
            continue;
        }
        if (!slot.virtualScaleXValid || !slot.virtualScaleYValid) continue;
        const Float2 predicted{
            slot.virtualFixed.x +
                static_cast<float>(cursor.x - slot.fixedCursor.x) *
                    slot.virtualScaleX,
            slot.virtualFixed.y +
                static_cast<float>(cursor.y - slot.fixedCursor.y) *
                    slot.virtualScaleY};
        const Float2 movement{
            predicted.x - slot.virtualPresented.x,
            predicted.y - slot.virtualPresented.y};
        if (!std::isfinite(predicted.x) || !std::isfinite(predicted.y) ||
            !std::isfinite(movement.x) || !std::isfinite(movement.y) ||
            (movement.x == 0.0F && movement.y == 0.0F)) {
            continue;
        }
        g_originalGuiMouseVirtualSetter(
            reinterpret_cast<void*>(slot.gui), &predicted, &movement);
        Float2 synchronized{};
        if (SyncGuiMousePreviousToCurrent(
                reinterpret_cast<void*>(slot.gui), &synchronized)) {
            g_guiMousePreviousSyncs.fetch_add(
                1, std::memory_order_relaxed);
            (void)ApplyGuiCursorCommand(slot, synchronized);
        } else {
            slot.positionValid = false;
            g_guiMousePreviousSyncFailures.fetch_add(
                1, std::memory_order_relaxed);
        }
        slot.virtualPresented = predicted;
        g_guiMousePresentationMoves.fetch_add(1,
                                              std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_guiMouseLock);
}

void ReportTelemetry(const bool force) {
    if (!g_telemetryEnabled ||
        g_reportLock.test_and_set(std::memory_order_acquire)) {
        return;
    }

    LARGE_INTEGER now{};
    (void)QueryPerformanceCounter(&now);
    const double interval =
        static_cast<double>(now.QuadPart - g_previousReport.QuadPart) /
        static_cast<double>(g_qpcFrequency.QuadPart);
    if (!force && interval < static_cast<double>(g_reportIntervalSeconds)) {
        g_reportLock.clear(std::memory_order_release);
        return;
    }
    if (interval <= 0.0) {
        g_reportLock.clear(std::memory_order_release);
        return;
    }

    const std::uint64_t fixedTicks = g_fixedTicks.load(std::memory_order_relaxed);
    const std::uint64_t draws = g_draws.load(std::memory_order_relaxed);
    const std::uint64_t postRenders =
        g_postRenders.load(std::memory_order_relaxed);
    const std::uint64_t postBufferSwaps =
        g_postBufferSwaps.load(std::memory_order_relaxed);
    const std::uint64_t presents = g_presents.load(std::memory_order_relaxed);
    const std::uint64_t newtonSteps =
        g_newtonSteps.load(std::memory_order_relaxed);
    const double newtonSeconds =
        g_newtonSeconds.load(std::memory_order_relaxed);
    const std::uint64_t phaseSamples =
        g_phaseSamples.load(std::memory_order_relaxed);
    const std::uint64_t phaseInvalid =
        g_phaseInvalid.load(std::memory_order_relaxed);
    const std::uint64_t phaseBelow =
        g_phaseBelowRange.load(std::memory_order_relaxed);
    const std::uint64_t phaseAbove =
        g_phaseAboveRange.load(std::memory_order_relaxed);
    const double phaseSum = g_phaseSum.load(std::memory_order_relaxed);
    const double phaseMinimum =
        g_phaseMinimum.load(std::memory_order_relaxed);
    const double phaseMaximum =
        g_phaseMaximum.load(std::memory_order_relaxed);
    const std::uint64_t clockPublishes =
        g_clockPublishes.load(std::memory_order_relaxed);
    const std::uint64_t clockPublishResets =
        g_clockPublishResets.load(std::memory_order_relaxed);
    const std::uint64_t clockInterpolated =
        g_clockInterpolated.load(std::memory_order_relaxed);
    const std::uint64_t clockUnavailable =
        g_clockUnavailable.load(std::memory_order_relaxed);
    const std::uint64_t clockEvaluationResets =
        g_clockEvaluationResets.load(std::memory_order_relaxed);
    const std::uint64_t viewportRenders =
        g_viewportRenders.load(std::memory_order_relaxed);
    const std::uint64_t cameraValid =
        g_cameraPoseValid.load(std::memory_order_relaxed);
    const std::uint64_t cameraInvalid =
        g_cameraPoseInvalid.load(std::memory_order_relaxed);
    const std::uint64_t cameraOverflow =
        g_cameraIdentityOverflow.load(std::memory_order_relaxed);
    const std::uint64_t cameraCaptures =
        g_cameraCaptures.load(std::memory_order_relaxed);
    const std::uint64_t cameraCaptureResets =
        g_cameraCaptureResets.load(std::memory_order_relaxed);
    const std::uint64_t cameraOverrides =
        g_cameraOverrides.load(std::memory_order_relaxed);
    const std::uint64_t cameraRestores =
        g_cameraRestores.load(std::memory_order_relaxed);
    const std::uint64_t cameraOverrideUnavailable =
        g_cameraOverrideUnavailable.load(std::memory_order_relaxed);
    const std::uint64_t cameraRestoreFailures =
        g_cameraRestoreFailures.load(std::memory_order_relaxed);
    const std::uint64_t entitySetMatrixCalls =
        g_entitySetMatrixCalls.load(std::memory_order_relaxed);
    const std::uint64_t handsSetMatrixCalls =
        g_handsSetMatrixCalls.load(std::memory_order_relaxed);
    const std::uint64_t handsIdentityOverflow =
        g_handsIdentityOverflow.load(std::memory_order_relaxed);
    const std::uint64_t meshUpdateGraphicsCalls =
        g_meshUpdateGraphicsCalls.load(std::memory_order_relaxed);
    const std::uint64_t handsPaletteUpdates =
        g_handsPaletteUpdates.load(std::memory_order_relaxed);
    const std::uint64_t handsPaletteMisses =
        g_handsPaletteMisses.load(std::memory_order_relaxed);
    const std::uint64_t handsRootCaptures =
        g_handsRootCaptures.load(std::memory_order_relaxed);
    const std::uint64_t handsRootApplies =
        g_handsRootApplies.load(std::memory_order_relaxed);
    const std::uint64_t handsRootUnavailable =
        g_handsRootUnavailable.load(std::memory_order_relaxed);
    const std::uint64_t handsPoseCaptures =
        g_handsPoseCaptures.load(std::memory_order_relaxed);
    const std::uint64_t handsPoseApplies =
        g_handsPoseApplies.load(std::memory_order_relaxed);
    const std::uint64_t handsPoseRestores =
        g_handsPoseRestores.load(std::memory_order_relaxed);
    const std::uint64_t handsPoseUnavailable =
        g_handsPoseUnavailable.load(std::memory_order_relaxed);
    const std::uint64_t handsSkinningApplies =
        g_handsSkinningApplies.load(std::memory_order_relaxed);
    const std::uint64_t handsSkinningRestores =
        g_handsSkinningRestores.load(std::memory_order_relaxed);
    const std::uint64_t handObjectUpdates =
        g_handObjectUpdates.load(std::memory_order_relaxed);
    const std::uint64_t handObjectCaptures =
        g_handObjectCaptures.load(std::memory_order_relaxed);
    const std::uint64_t handObjectApplies =
        g_handObjectApplies.load(std::memory_order_relaxed);
    std::array<CameraSlot, kMaximumCameraIdentities> cameraSlots{};
    AcquireSRWLockShared(&g_cameraLock);
    cameraSlots = g_cameraSlots;
    ReleaseSRWLockShared(&g_cameraLock);
    std::size_t cameraIdentities = 0;
    const CameraSlot* primaryCamera = nullptr;
    for (const auto& slot : cameraSlots) {
        if (slot.camera == 0) continue;
        ++cameraIdentities;
        if (primaryCamera == nullptr ||
            slot.samples > primaryCamera->samples) {
            primaryCamera = &slot;
        }
    }
    std::array<GuiMouseSlot, kMaximumGuiMouseIdentities> guiMouseSlots{};
    AcquireSRWLockShared(&g_guiMouseLock);
    guiMouseSlots = g_guiMouseSlots;
    ReleaseSRWLockShared(&g_guiMouseLock);
    std::size_t guiMouseIdentities = 0;
    for (const auto& slot : guiMouseSlots) {
        if (slot.gui != 0) ++guiMouseIdentities;
    }
    const auto rate = [interval](const std::uint64_t current,
                                 const std::uint64_t previous) {
        return static_cast<double>(current - previous) / interval;
    };
    const std::uint64_t intervalNewtonSteps =
        newtonSteps - g_previousNewtonSteps;
    const double intervalNewtonSeconds =
        newtonSeconds - g_previousNewtonSeconds;
    const double newtonStepMilliseconds = intervalNewtonSteps == 0
        ? 0.0
        : intervalNewtonSeconds * 1000.0 /
              static_cast<double>(intervalNewtonSteps);

    std::wostringstream message;
    message << std::fixed << std::setprecision(2)
            << L"Cadence " << interval << L"s: fixed="
            << rate(fixedTicks, g_previousFixedTicks) << L" Hz, draw="
            << rate(draws, g_previousDraws) << L" Hz, postRender="
            << rate(postRenders, g_previousPostRenders)
            << L" Hz, postBufferSwap="
            << rate(postBufferSwaps, g_previousPostBufferSwaps)
            << L" Hz, present=" << rate(presents, g_previousPresents)
            << L" Hz, Newton="
            << rate(newtonSteps, g_previousNewtonSteps)
            << L" Hz @ " << newtonStepMilliseconds << L" ms"
            << L"; totals fixed=" << fixedTicks << L", draw=" << draws
            << L", postRender=" << postRenders << L", postBufferSwap="
            << postBufferSwaps << L", present=" << presents
            << L", Newton=" << newtonSteps;
    if (phaseSamples != 0) {
        message << L"; phase n=" << phaseSamples << L", mean="
                << phaseSum / static_cast<double>(phaseSamples)
                << L", min=" << phaseMinimum << L", max=" << phaseMaximum
                << L", below=" << phaseBelow << L", above=" << phaseAbove
                << L", invalid=" << phaseInvalid;
    } else {
        message << L"; phase n=0, invalid=" << phaseInvalid;
    }
    message << L"; clock publish=" << clockPublishes
            << L", publishReset=" << clockPublishResets
            << L", interpolated=" << clockInterpolated
            << L", unavailable=" << clockUnavailable
            << L", evaluationReset=" << clockEvaluationResets;
    message << L"; camera viewport=" << viewportRenders
            << L", valid=" << cameraValid
            << L", invalid=" << cameraInvalid
            << L", identities=" << cameraIdentities
            << L", overflow=" << cameraOverflow
            << L", captures=" << cameraCaptures
            << L", captureResets=" << cameraCaptureResets
            << L", overrides=" << cameraOverrides
            << L", matrixModeOverrides="
            << g_cameraMatrixModeOverrides.load(std::memory_order_relaxed)
            << L", restores=" << cameraRestores
            << L", overrideUnavailable=" << cameraOverrideUnavailable
            << L", restoreFailures=" << cameraRestoreFailures;
    if (g_mouseLookPreviewEnabled) {
        const mouse::Counts acquired = g_lookPreview.Acquired();
        const mouse::Counts delivered = g_lookPreview.Delivered();
        const mouse::Counts pending = g_lookPreview.Pending();
        const auto describe = [&message](const wchar_t* const name,
                                         const mouse::AxisModel& lag0,
                                         const mouse::AxisModel& lag1) {
            const mouse::AxisModel& chosen =
                lag0.Direct() || !lag1.Direct() ? lag0 : lag1;
            message << L", " << name << L"Direct="
                    << (chosen.Direct() ? 1 : 0) << L", " << name
                    << L"Lag=" << (&chosen == &lag0 ? 0 : 1) << L", "
                    << name << L"MicroRadPerCount="
                    << (chosen.HasScale() ? chosen.Scale() * 1.0e6 : 0.0)
                    << L", " << name << L"Mismatches="
                    << chosen.Mismatches();
        };
        message << L"; mouseLook engineReads="
                << g_lookEngineReads.load(std::memory_order_relaxed)
                << L", engineDiscards="
                << g_lookEngineDiscards.load(std::memory_order_relaxed)
                << L", presentationReads="
                << g_lookPresentationReads.load(std::memory_order_relaxed)
                << L", pumps="
                << g_lookPumpCalls.load(std::memory_order_relaxed)
                << L", capturedMismatchTicks="
                << g_lookCapturedMismatchTicks.load(
                       std::memory_order_relaxed)
                << L", pumpLimitHits="
                << g_lookPumpLimitHits.load(std::memory_order_relaxed)
                << L", foreignThreadReads="
                << g_lookForeignThreadReads.load(std::memory_order_relaxed)
                << L", acquired=(" << acquired.x << L", " << acquired.y
                << L"), delivered=(" << delivered.x << L", " << delivered.y
                << L"), pending=(" << pending.x << L", " << pending.y
                << L"), yawApplies="
                << g_lookYawApplies.load(std::memory_order_relaxed)
                << L", pitchApplies="
                << g_lookPitchApplies.load(std::memory_order_relaxed)
                << L", unavailable="
                << g_lookUnavailable.load(std::memory_order_relaxed);
        describe(L"yaw", g_lookPreview.Yaw(0), g_lookPreview.Yaw(1));
        describe(L"pitch", g_lookPreview.Pitch(0), g_lookPreview.Pitch(1));
        message << L", reportIntervalMs="
                << g_lookPreview.ReportInterval() * 1000.0
                << L", readIntervalMs="
                << g_lookPreview.ReadInterval() * 1000.0
                << L", reportSmoothing="
                << (g_lookPreview.SmoothingActive() ? 1 : 0)
                << L", readToPresentNormalMs="
                << g_lookAgeNormal.seconds * 1000.0
                << L", readToPresentTickMs="
                << g_lookAgeTick.seconds * 1000.0;
    }
    message << L"; hands entitySetMatrix=" << entitySetMatrixCalls
            << L", matched=" << handsSetMatrixCalls
            << L", overflow=" << handsIdentityOverflow
            << L", meshUpdates=" << meshUpdateGraphicsCalls
            << L", paletteMatched=" << handsPaletteUpdates
            << L", paletteMisses=" << handsPaletteMisses
            << L", rootCaptures=" << handsRootCaptures
            << L", rootApplies=" << handsRootApplies
            << L", rootUnavailable=" << handsRootUnavailable
            << L", poseCaptures=" << handsPoseCaptures
            << L", poseApplies=" << handsPoseApplies
            << L", poseRestores=" << handsPoseRestores
            << L", poseUnavailable=" << handsPoseUnavailable
            << L", skinningApplies=" << handsSkinningApplies
            << L", skinningRestores=" << handsSkinningRestores
            << L", objectUpdates=" << handObjectUpdates
            << L", objectCaptures=" << handObjectCaptures
            << L", objectApplies=" << handObjectApplies;
    message << L"; agents updates="
            << g_agentUpdates.load(std::memory_order_relaxed)
            << L", meshMissing="
            << g_agentMeshMissing.load(std::memory_order_relaxed)
            << L", meshInsertions="
            << g_agentMeshInsertions.load(std::memory_order_relaxed)
            << L", registryFull="
            << g_agentRegistryFull.load(std::memory_order_relaxed)
            << L", paletteCalls="
            << g_agentPaletteCalls.load(std::memory_order_relaxed)
            << L", paletteCaptures="
            << g_agentPaletteCaptures.load(std::memory_order_relaxed)
            << L", paletteRepeats="
            << g_agentPaletteRepeats.load(std::memory_order_relaxed)
            << L", paletteChanges="
            << g_agentPaletteChanges.load(std::memory_order_relaxed)
            << L", paletteInvalid="
            << g_agentPaletteInvalid.load(std::memory_order_relaxed)
            << L", paletteIdentityChanges="
            << g_agentPaletteIdentityChanges.load(
                   std::memory_order_relaxed)
            << L", bones="
            << g_agentMinimumBones.load(std::memory_order_relaxed)
            << L".."
            << g_agentMaximumBones.load(std::memory_order_relaxed)
            << L", messages=[";
    for (std::size_t index = 0; index < g_agentMessageCounts.size();
         ++index) {
        if (index != 0) message << L", ";
        message << (index == 9 ? L"outside" : std::to_wstring(index))
                << L"="
                << g_agentMessageCounts[index].load(
                       std::memory_order_relaxed);
    }
    message << L"], modelCalls="
            << g_agentModelCalls.load(std::memory_order_relaxed)
            << L", modelRecent="
            << g_agentModelRecent.load(std::memory_order_relaxed)
            << L", modelStale="
            << g_agentModelStale.load(std::memory_order_relaxed)
            << L", modelCaptures="
            << g_agentModelCaptures.load(std::memory_order_relaxed)
            << L", modelApplies="
            << g_agentModelApplies.load(std::memory_order_relaxed)
            << L", modelUnavailable="
            << g_agentModelUnavailable.load(std::memory_order_relaxed)
            << L", modelInvalid="
            << g_agentModelInvalid.load(std::memory_order_relaxed)
            << L", modelSameTickMutation="
            << g_agentModelSameTickMutation.load(std::memory_order_relaxed)
            << L", modelDiscontinuity="
            << g_agentModelDiscontinuity.load(std::memory_order_relaxed)
            << L", modelSlotFull="
            << g_agentModelSlotFull.load(std::memory_order_relaxed)
            << L", poseCalls="
            << g_agentPoseCalls.load(std::memory_order_relaxed)
            << L", poseEligible="
            << g_agentPoseEligible.load(std::memory_order_relaxed)
            << L", poseCaptures="
            << g_agentPoseCaptures.load(std::memory_order_relaxed)
            << L", poseApplies="
            << g_agentPoseApplies.load(std::memory_order_relaxed)
            << L", poseRestores="
            << g_agentPoseRestores.load(std::memory_order_relaxed)
            << L", poseUnavailable="
            << g_agentPoseUnavailable.load(std::memory_order_relaxed)
            << L", poseRejected="
            << g_agentPoseRejected.load(std::memory_order_relaxed)
            << L", poseSlotFull="
            << g_agentPoseSlotFull.load(std::memory_order_relaxed)
            << L", poseRestoreFailures="
            << g_agentPoseRestoreFailures.load(std::memory_order_relaxed);
    message << L"; ragdolls propUpdates="
            << g_ragdollPropUpdates.load(std::memory_order_relaxed)
            << L", taggedBodies="
            << g_ragdollTaggedBodies.load(std::memory_order_relaxed)
            << L", bodyFlagInvalid="
            << g_ragdollBodyFlagInvalid.load(std::memory_order_relaxed)
            << L", paletteCalls="
            << g_ragdollPaletteCalls.load(std::memory_order_relaxed)
            << L", paletteCaptures="
            << g_ragdollPaletteCaptures.load(std::memory_order_relaxed)
            << L", paletteRepeats="
            << g_ragdollPaletteRepeats.load(std::memory_order_relaxed)
            << L", paletteChanges="
            << g_ragdollPaletteChanges.load(std::memory_order_relaxed)
            << L", paletteInvalid="
            << g_ragdollPaletteInvalid.load(std::memory_order_relaxed)
            << L", paletteIdentityChanges="
            << g_ragdollPaletteIdentityChanges.load(
                   std::memory_order_relaxed)
            << L", bones="
            << g_ragdollMinimumBones.load(std::memory_order_relaxed)
            << L".."
            << g_ragdollMaximumBones.load(std::memory_order_relaxed)
            << L", rootApplies="
            << g_ragdollRootApplies.load(std::memory_order_relaxed)
            << L", poseApplies="
            << g_ragdollPoseApplies.load(std::memory_order_relaxed);
    message << L"; guiMouse pixel="
            << g_guiMousePixelCalls.load(std::memory_order_relaxed)
            << L", virtual="
            << g_guiMouseVirtualCalls.load(std::memory_order_relaxed)
            << L", motion="
            << g_guiMouseMotionCalls.load(std::memory_order_relaxed)
            << L", relativeMode="
            << g_guiMouseRelativeModeCalls.load(std::memory_order_relaxed)
            << L", absoluteMode="
            << g_guiMouseAbsoluteModeCalls.load(std::memory_order_relaxed)
            << L", identities=" << guiMouseIdentities
            << L", overflow="
            << g_guiMouseIdentityOverflow.load(std::memory_order_relaxed)
            << L", presentationCalls="
            << g_guiMousePresentationCalls.load(std::memory_order_relaxed)
            << L", presentationMoves="
            << g_guiMousePresentationMoves.load(std::memory_order_relaxed)
            << L", fixedCorrections="
            << g_guiMouseFixedCorrections.load(std::memory_order_relaxed)
            << L", virtualCalibrations="
            << g_guiMouseVirtualCalibrations.load(
                   std::memory_order_relaxed)
            << L", staleSkips="
            << g_guiMouseStaleSkips.load(std::memory_order_relaxed)
            << L", relativeModeSkips="
            << g_guiMouseRelativeModeSkips.load(std::memory_order_relaxed)
            << L", invalidSkips="
            << g_guiMouseInvalidSkips.load(std::memory_order_relaxed)
            << L", previousSyncs="
            << g_guiMousePreviousSyncs.load(std::memory_order_relaxed)
            << L", previousSyncFailures="
            << g_guiMousePreviousSyncFailures.load(
                   std::memory_order_relaxed)
            << L", cursorQueueCalls="
            << g_guiCursorQueueCalls.load(std::memory_order_relaxed)
            << L", cursorCommandCaptures="
            << g_guiCursorCommandCaptures.load(std::memory_order_relaxed)
            << L", cursorCommandApplies="
            << g_guiCursorCommandApplies.load(std::memory_order_relaxed)
            << L", cursorCommandFailures="
            << g_guiCursorCommandFailures.load(std::memory_order_relaxed)
            << L", guiDoMouseCalls="
            << g_guiDoMouseCalls.load(std::memory_order_relaxed)
            << L", guiDoMouseCaptures="
            << g_guiDoMouseCaptures.load(std::memory_order_relaxed)
            << L", guiDoMouseFailures="
            << g_guiDoMouseFailures.load(std::memory_order_relaxed);
    message << L"; world propUpdates="
            << g_worldPropUpdates.load(std::memory_order_relaxed)
            << L", messageMismatch="
            << g_worldPropMessageMismatch.load(std::memory_order_relaxed)
            << L", layoutInvalid="
            << g_worldPropLayoutInvalid.load(std::memory_order_relaxed)
            << L", meshMissing="
            << g_worldPropMeshMissing.load(std::memory_order_relaxed)
            << L", bodyInvalid="
            << g_worldPropBodyInvalid.load(std::memory_order_relaxed)
            << L", zeroBody="
            << g_worldPropZeroBody.load(std::memory_order_relaxed)
            << L", oneBody="
            << g_worldPropOneBody.load(std::memory_order_relaxed)
            << L", multiBody="
            << g_worldPropMultiBody.load(std::memory_order_relaxed)
            << L", meshInsertions="
            << g_worldMeshRegistryInsertions.load(std::memory_order_relaxed)
            << L", registryFull="
            << g_worldMeshRegistryFull.load(std::memory_order_relaxed)
            << L", registryEvictions="
            << g_worldMeshRegistryEvictions.load(std::memory_order_relaxed)
            << L", modelCalls="
            << g_worldModelCalls.load(std::memory_order_relaxed)
            << L", modelRecent="
            << g_worldModelRecent.load(std::memory_order_relaxed)
            << L", modelStale="
            << g_worldModelStale.load(std::memory_order_relaxed)
            << L", captures="
            << g_worldModelCaptures.load(std::memory_order_relaxed)
            << L", applies="
            << g_worldModelApplies.load(std::memory_order_relaxed)
            << L", unavailable="
            << g_worldModelUnavailable.load(std::memory_order_relaxed)
            << L", invalid="
            << g_worldModelInvalid.load(std::memory_order_relaxed)
            << L", sameTickMutation="
            << g_worldModelSameTickMutation.load(std::memory_order_relaxed)
            << L", discontinuity="
            << g_worldModelDiscontinuity.load(std::memory_order_relaxed)
            << L", modelSlotFull="
            << g_worldModelSlotFull.load(std::memory_order_relaxed)
            << L", articulatedCalls="
            << g_worldArticulatedCalls.load(std::memory_order_relaxed)
            << L", articulatedCaptures="
            << g_worldArticulatedCaptures.load(std::memory_order_relaxed)
            << L", articulatedApplies="
            << g_worldArticulatedApplies.load(std::memory_order_relaxed)
            << L", articulatedUnavailable="
            << g_worldArticulatedUnavailable.load(
                   std::memory_order_relaxed)
            << L", articulatedRejected="
            << g_worldArticulatedRejected.load(std::memory_order_relaxed)
            << L", scriptDrivenSets="
            << g_worldScriptDrivenSetMatrixCalls.load(std::memory_order_relaxed)
            << L", scriptDrivenRefreshes="
            << g_worldScriptDrivenRefreshes.load(std::memory_order_relaxed)
            << L", scriptDrivenRegistryMisses="
            << g_worldScriptDrivenRegistryMisses.load(std::memory_order_relaxed)
            << L", scriptDrivenCalls="
            << g_worldScriptDrivenCalls.load(std::memory_order_relaxed)
            << L", scriptDrivenCaptures="
            << g_worldScriptDrivenCaptures.load(std::memory_order_relaxed)
            << L", scriptDrivenApplies="
            << g_worldScriptDrivenApplies.load(std::memory_order_relaxed)
            << L", scriptDrivenUnavailable="
            << g_worldScriptDrivenUnavailable.load(std::memory_order_relaxed)
            << L", scriptDrivenRejected="
            << g_worldScriptDrivenRejected.load(std::memory_order_relaxed)
             << L", poseCalls="
             << g_worldPoseCalls.load(std::memory_order_relaxed)
             << L", poseEligible="
             << g_worldPoseEligible.load(std::memory_order_relaxed)
             << L", poseCaptures="
             << g_worldPoseCaptures.load(std::memory_order_relaxed)
             << L", poseApplies="
             << g_worldPoseApplies.load(std::memory_order_relaxed)
             << L", poseRestores="
             << g_worldPoseRestores.load(std::memory_order_relaxed)
             << L", poseUnavailable="
             << g_worldPoseUnavailable.load(std::memory_order_relaxed)
             << L", poseRejected="
             << g_worldPoseRejected.load(std::memory_order_relaxed)
             << L", poseSlotFull="
             << g_worldPoseSlotFull.load(std::memory_order_relaxed)
             << L", poseRestoreFailures="
             << g_worldPoseRestoreFailures.load(std::memory_order_relaxed)
            << L", messages=[";
    for (std::size_t index = 0; index < g_worldPropMessageCounts.size();
         ++index) {
        if (index != 0) message << L", ";
        message << (index == 9 ? L"outside" : std::to_wstring(index))
                << L"="
                << g_worldPropMessageCounts[index].load(
                       std::memory_order_relaxed);
    }
    message << L"]";
    if (primaryCamera != nullptr) {
        message << L", primary=0x" << std::hex << std::uppercase
                << primaryCamera->camera << std::dec
                << L", samples=" << primaryCamera->samples
                << L", repeats=" << primaryCamera->repeats
                << L", fixedChanges="
                << primaryCamera->fixedBoundaryChanges
                << L", sameFixedChanges="
                << primaryCamera->sameFixedSerialChanges;
    }
    log::Write(log::Level::info, message.str());
    if (g_guiMouseOverlayEnabled) {
        for (const auto& slot : guiMouseSlots) {
            if (slot.gui == 0) continue;
            std::wostringstream guiMessage;
            guiMessage << L"GUI mouse identity=0x" << std::hex
                       << std::uppercase << slot.gui << std::dec
                       << L", pixel=" << slot.pixelCalls
                       << L", virtual=" << slot.virtualCalls
                       << L", motion=" << slot.motionCalls
                       << L", kind="
                       << (slot.usesVirtualPosition ? L"virtual" : L"pixel")
                       << L", valid=" << (slot.positionValid ? 1 : 0)
                       << L", cursorQueueCalls="
                       << slot.cursorQueueCalls
                       << L", cursorCommandValid="
                       << (slot.cursorCommandValid ? 1 : 0)
                       << L", cursorCommandIndex="
                       << slot.cursorCommandIndex
                       << L", scaleValid="
                       << (slot.virtualScaleXValid ? 1 : 0)
                       << L"/" << (slot.virtualScaleYValid ? 1 : 0)
                       << L", scale=(" << slot.virtualScaleX << L", "
                       << slot.virtualScaleY << L")"
                       << L", messages=[";
            for (std::size_t index = 0;
                 index < slot.messageCounts.size(); ++index) {
                if (index != 0) guiMessage << L", ";
                guiMessage << (index == 8 ? L"outside" :
                               std::to_wstring(index))
                           << L"=" << slot.messageCounts[index];
            }
            guiMessage << L"]";
            log::Write(log::Level::info, guiMessage.str());
        }
    }

    g_previousFixedTicks = fixedTicks;
    g_previousDraws = draws;
    g_previousPostRenders = postRenders;
    g_previousPostBufferSwaps = postBufferSwaps;
    g_previousPresents = presents;
    g_previousNewtonSteps = newtonSteps;
    g_previousNewtonSeconds = newtonSeconds;
    g_previousReport = now;
    g_reportLock.clear(std::memory_order_release);
}

// One short line per interval: cadence plus the counters that must stay
// zero. Reads only atomics, so it is safe from the reporter thread.
void WriteCompactReport(const double interval,
                        std::array<std::uint64_t, 4>& previous) {
    const std::array<std::uint64_t, 4> totals{
        g_fixedTicks.load(std::memory_order_relaxed),
        g_draws.load(std::memory_order_relaxed),
        g_presents.load(std::memory_order_relaxed),
        g_newtonSteps.load(std::memory_order_relaxed)};
    const auto rate = [interval](const std::uint64_t current,
                                 const std::uint64_t before) {
        return static_cast<double>(current - before) / interval;
    };
    const std::uint64_t restoreFailures =
        g_cameraRestoreFailures.load(std::memory_order_relaxed) +
        g_worldPoseRestoreFailures.load(std::memory_order_relaxed) +
        g_agentPoseRestoreFailures.load(std::memory_order_relaxed);
    const std::uint64_t invalid =
        g_worldPropLayoutInvalid.load(std::memory_order_relaxed) +
        g_worldPropBodyInvalid.load(std::memory_order_relaxed) +
        g_ragdollBodyFlagInvalid.load(std::memory_order_relaxed) +
        g_agentPaletteInvalid.load(std::memory_order_relaxed) +
        g_ragdollPaletteInvalid.load(std::memory_order_relaxed);
    const std::uint64_t capacity =
        g_worldMeshRegistryFull.load(std::memory_order_relaxed) +
        g_worldModelSlotFull.load(std::memory_order_relaxed) +
        g_worldPoseSlotFull.load(std::memory_order_relaxed) +
        g_agentRegistryFull.load(std::memory_order_relaxed) +
        g_agentModelSlotFull.load(std::memory_order_relaxed) +
        g_agentPoseSlotFull.load(std::memory_order_relaxed) +
        g_cameraIdentityOverflow.load(std::memory_order_relaxed) +
        g_handsIdentityOverflow.load(std::memory_order_relaxed);
    std::wostringstream message;
    message << std::fixed << std::setprecision(1) << L"Health " << interval
            << L"s: fixed=" << rate(totals[0], previous[0]) << L" Hz, draw="
            << rate(totals[1], previous[1]) << L" Hz, present="
            << rate(totals[2], previous[2]) << L" Hz, Newton="
            << rate(totals[3], previous[3]) << L" Hz; restoreFailures="
            << restoreFailures << L", invalid=" << invalid << L", capacity="
            << capacity;
    if (g_mouseLookPreviewEnabled) {
        message << L"; mouseLook yaw="
                << (g_lookYawDirect.load(std::memory_order_relaxed) ? 1 : 0)
                << L", pitch="
                << (g_lookPitchDirect.load(std::memory_order_relaxed) ? 1 : 0)
                << L", pumpLimitHits="
                << g_lookPumpLimitHits.load(std::memory_order_relaxed);
    }
    log::Write(log::Level::info, message.str());
    previous = totals;
}

// Formatting and file output stay off the render thread: this thread wakes
// once per report interval and is the only periodic writer.
DWORD WINAPI ReportThreadMain(void*) {
    std::array<std::uint64_t, 4> previous{};
    LARGE_INTEGER last{};
    (void)QueryPerformanceCounter(&last);
    const DWORD wait = g_reportIntervalSeconds * 1000U;
    while (WaitForSingleObject(g_reportStopEvent, wait) == WAIT_TIMEOUT) {
        if (!g_telemetryActive.load(std::memory_order_acquire)) continue;
        if (g_detailedReportsEnabled) {
            // Diagnostic mode. A few main-thread-only values are read here
            // without synchronization; they are plain counters used only
            // for this text.
            ReportTelemetry(true);
            continue;
        }
        LARGE_INTEGER now{};
        (void)QueryPerformanceCounter(&now);
        const double interval =
            static_cast<double>(now.QuadPart - last.QuadPart) /
            static_cast<double>(g_qpcFrequency.QuadPart);
        last = now;
        if (interval > 0.0) WriteCompactReport(interval, previous);
    }
    return 0;
}

void StartReportThread() noexcept {
    if (g_reportThread != nullptr ||
        (!g_healthReportsEnabled && !g_detailedReportsEnabled)) {
        return;
    }
    g_reportStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_reportStopEvent == nullptr) return;
    g_reportThread =
        CreateThread(nullptr, 0, &ReportThreadMain, nullptr, 0, nullptr);
    if (g_reportThread == nullptr) {
        CloseHandle(g_reportStopEvent);
        g_reportStopEvent = nullptr;
        return;
    }
    (void)SetThreadPriority(g_reportThread, THREAD_PRIORITY_BELOW_NORMAL);
}

void StopReportThread() noexcept {
    if (g_reportThread == nullptr) return;
    (void)SetEvent(g_reportStopEvent);
    (void)WaitForSingleObject(g_reportThread, 5000);
    CloseHandle(g_reportThread);
    CloseHandle(g_reportStopEvent);
    g_reportThread = nullptr;
    g_reportStopEvent = nullptr;
}

void UpdaterRunMessageDetour(void* const self, const int message,
                             const float timeStep) {
    const bool active =
        g_telemetryActive.load(std::memory_order_relaxed);
    if (active && message == 2) {
        double alpha = 0.0;
        double wallTimeSeconds = 0.0;
        g_latestClockSample = {};
        g_hasLatestClockSample = false;
        if (!RecordSchedulerPhase(alpha, wallTimeSeconds) ||
            !g_presentationClock.has_value()) {
            g_clockUnavailable.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_latestClockSample = g_presentationClock->Evaluate(
                wallTimeSeconds, alpha, true);
            if (g_latestClockSample.kind ==
                clock::SampleKind::interpolated) {
                g_hasLatestClockSample = true;
                g_clockInterpolated.fetch_add(1,
                                              std::memory_order_relaxed);
            } else if (g_latestClockSample.kind ==
                       clock::SampleKind::reset) {
                g_clockEvaluationResets.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                g_clockUnavailable.fetch_add(1,
                                             std::memory_order_relaxed);
            }
        }
    }

    if (active && message == 2) {
        // cImGui queues its software cursor while OnDraw listeners run.
        // Update only presentation coordinates before that queue is built;
        // buttons and clicks remain on the stock fixed GUI update.
        ApplyGuiMousePresentation();
        AcquireLookPreview();
    }

    const int previousMessage =
        g_updaterMessageContext.exchange(message, std::memory_order_relaxed);
    g_originalUpdaterRunMessage(self, message, timeStep);
    g_updaterMessageContext.store(previousMessage,
                                  std::memory_order_relaxed);
    if (!active) return;
    switch (message) {
    case 0:
        g_postBufferSwaps.fetch_add(1, std::memory_order_relaxed);
        break;
    case 2:
        g_draws.fetch_add(1, std::memory_order_relaxed);
        break;
    case 3:
        g_postRenders.fetch_add(1, std::memory_order_relaxed);
        break;
    case 5:
        g_fixedTicks.fetch_add(1, std::memory_order_relaxed);
        g_lookTickSinceSample = true;
        if (g_presentationClock.has_value() &&
            g_engineMicroseconds != nullptr) {
            ++g_fixedSerial;
            const double wallTimeSeconds =
                static_cast<double>(g_engineMicroseconds()) / 1'000'000.0;
            const clock::PublishResult result =
                g_presentationClock->Publish(g_fixedSerial, wallTimeSeconds);
            if (result.accepted) {
                g_clockPublishes.fetch_add(1, std::memory_order_relaxed);
            }
            if (result.resetReason != clock::ResetReason::none) {
                g_clockPublishResets.fetch_add(1,
                                               std::memory_order_relaxed);
            }
            CaptureCameraSnapshot();
            ObserveLookPreviewTick();
            CommitHandsInterpolation();
        }
        break;
    default:
        break;
    }
}

void NewtonUpdateDetour(void* const world, const float timeStep) {
    g_originalNewtonUpdate(world, timeStep);
    if (!g_telemetryActive.load(std::memory_order_relaxed)) return;
    g_newtonSteps.fetch_add(1, std::memory_order_relaxed);
    if (std::isfinite(timeStep) && timeStep > 0.0F) {
        g_newtonSeconds.fetch_add(static_cast<double>(timeStep),
                                  std::memory_order_relaxed);
    }
}

void HandsSetMatrixDetour(void* const entity, const void* const matrix) {
    const bool active =
        g_telemetryActive.load(std::memory_order_relaxed);
    std::array<float, 16> captured{};
    // Classify from one name read; the view is not used after the stock call.
    const char* text = nullptr;
    std::size_t size = 0;
    const std::string_view name =
        active && ReadLuxEntityName(entity, text, size)
            ? std::string_view{text, size}
            : std::string_view{};
    const bool isHands = IsPlayerHandsName(name) &&
        IsPlausiblePointer(matrix, alignof(float));
    const bool isHudObject = mapping::IsHandsHudObjectName(name);
    if (isHands) {
        std::memcpy(captured.data(), matrix, sizeof(captured));
    }
    g_originalHandsSetMatrix(entity, matrix);
    if (!active) return;
    g_entitySetMatrixCalls.fetch_add(1, std::memory_order_relaxed);
    RefreshScriptDrivenProp(entity);
    if (isHands) {
        void* const mesh = GetLuxEntityMesh(entity);
        RecordHandsRoot(entity, mesh, captured);
        StageHandsInterpolation(entity, mesh, captured);
    }
    if (isHudObject) TrackHandsHudObject(entity);
}

void UpdateAttachmentDetour(void* const entity) {
    g_originalUpdateAttachment(entity);
    if (!g_telemetryActive.load(std::memory_order_relaxed) ||
        g_updaterMessageContext.load(std::memory_order_relaxed) != 5 ||
        !IsHandsAttachmentObject(entity)) {
        return;
    }
    TrackHandsHudObject(entity);
}

void MeshUpdateGraphicsDetour(void* const mesh) {
    RestoreWorldPoseBeforeUpdate(mesh);
    RestoreAgentPoseBeforeUpdate(mesh);
    const bool restoreBeforeUpdate =
        g_handsRenderActive && g_handsPoseOverridden &&
        mesh == g_interpolatedHandsMesh &&
        g_handsPoseOverrideBuffer != nullptr &&
        g_handsSkinningOverrideBuffer != nullptr;
    if (restoreBeforeUpdate && !RestoreHandsPoseBuffers()) {
        g_handsPoseOverridden = false;
        g_handsPoseInterpolationEnabled = false;
        log::Write(log::Level::error,
                   L"Hands pose interpolation disabled: source/GPU palettes "
                   L"could not be restored before a graphics update.");
    }
    g_originalMeshUpdateGraphics(mesh);
    if (!g_telemetryActive.load(std::memory_order_relaxed)) return;
    g_meshUpdateGraphicsCalls.fetch_add(1, std::memory_order_relaxed);
    RecordAgentPalette(mesh);
    RecordRagdollPalette(mesh);
    if (reinterpret_cast<std::uintptr_t>(mesh) ==
        g_latestHandsMesh.load(std::memory_order_relaxed)) {
        RecordHandsPalette(mesh);
    }
    if (restoreBeforeUpdate && g_handsPoseOverridden) {
        MatrixVectorView view{};
        if (ReadHandsPalette(mesh, view) &&
            view.count == g_handsPoseOverride.boneCount &&
            const_cast<std::array<float, 16>*>(view.begin) ==
                reinterpret_cast<std::array<float, 16>*>(
                    g_handsPoseOverrideBuffer) &&
            ApplyHandsPoseBuffers(g_handsPoseOverride, true)) {
        } else {
            g_handsPoseOverridden = false;
            g_handsPoseOverrideBuffer = nullptr;
            g_handsSkinningOverrideBuffer = nullptr;
            g_handsSkinningVectorCount = 0;
            g_handsPoseInterpolationEnabled = false;
            log::Write(log::Level::error,
                       L"Hands pose interpolation disabled: source/GPU palette "
                       L"identity changed during rendering.");
        }
    }
    CaptureAndApplyAgentPose(mesh);
    CaptureAndApplyWorldPose(mesh);
}

void LuxPropOnUpdateDetour(void* const prop, const float timeStep) {
    g_originalLuxPropOnUpdate(prop, timeStep);
    if (g_telemetryActive.load(std::memory_order_relaxed)) {
        RecordWorldProp(prop);
    }
}

void LuxAgentOnUpdateDetour(void* const agent, const float timeStep) {
    g_originalLuxAgentOnUpdate(agent, timeStep);
    if (g_telemetryActive.load(std::memory_order_relaxed)) {
        RegisterAgentMesh(agent);
    }
}

hands::Matrix* SubMeshGetModelMatrixDetour(void* const subMesh,
                                           void* const frustum) {
    hands::Matrix* const original =
        g_originalSubMeshGetModelMatrix(subMesh, frustum);
    // This hook runs for every rendered submesh: one owner read, then O(1)
    // registry probes. Unrelated renderables leave here without further work.
    void* owner = nullptr;
    if (!g_telemetryActive.load(std::memory_order_relaxed) ||
        !IsPlausiblePointer(original, alignof(float)) ||
        !ReadAt(subMesh, mapping::kSubMeshOwnerOffset, owner) ||
        owner == nullptr) {
        return original;
    }
    hands::Matrix* worldResult = original;
    hands::Matrix* agentResult = original;
    const WorldMeshRegistrySlot* const registry = RecordWorldModelCall(owner);
    if (registry != nullptr) {
        worldResult = InterpolateWorldModel(subMesh, *registry, original);
    }
    const AgentMeshRegistrySlot* const agentRegistry =
        RecordAgentModelCall(owner);
    if (agentRegistry != nullptr) {
        agentResult = InterpolateAgentModel(subMesh, *agentRegistry, original);
    }
    hands::Matrix* const sceneResult =
        agentResult != original ? agentResult : worldResult;
    if (!g_handsRootRenderActive) return sceneResult;
    const bool handsOwner = owner == g_interpolatedHandsMesh;
    const bool handObjectOwner = !handsOwner && IsRecentHandObjectMesh(owner);
    if (!handsOwner && !handObjectOwner) return sceneResult;
    HandsModelSlot* slot = nullptr;
    for (std::size_t index = 0; index < g_handsModelSlotCount; ++index) {
        if (g_handsModelSlots[index].subMesh == subMesh) {
            slot = &g_handsModelSlots[index];
            break;
        }
    }
    if (slot == nullptr) {
        if (g_handsModelSlotCount == g_handsModelSlots.size()) return original;
        slot = &g_handsModelSlots[g_handsModelSlotCount++];
        slot->subMesh = subMesh;
    }
    if (handObjectOwner && g_presentationClock.has_value()) {
        HandObjectHistorySlot* history = nullptr;
        for (auto& candidate : g_handObjectHistories) {
            if (candidate.occupied && candidate.subMesh == subMesh) {
                history = &candidate;
                break;
            }
        }
        if (history == nullptr) {
            // Take a free slot, else the one idle longest once its object
            // has stopped rendering.
            HandObjectHistorySlot* reusable = nullptr;
            for (auto& candidate : g_handObjectHistories) {
                if (!candidate.occupied) {
                    reusable = &candidate;
                    break;
                }
                if (g_fixedSerial - candidate.lastCapturedSerial >
                        world::kMaximumRecentSerialAge &&
                    (reusable == nullptr ||
                     candidate.lastCapturedSerial <
                         reusable->lastCapturedSerial)) {
                    reusable = &candidate;
                }
            }
            if (reusable != nullptr) {
                *reusable = {};
                reusable->occupied = true;
                reusable->subMesh = subMesh;
                history = reusable;
            }
        }
        if (history != nullptr) {
            if (history->lastCapturedSerial != g_fixedSerial) {
                const hands::PublishKind published = history->history.Publish(
                    g_fixedSerial, g_presentationClock->Generation(), *original);
                history->lastCapturedSerial = g_fixedSerial;
                if (published == hands::PublishKind::appended ||
                    published == hands::PublishKind::unchanged) {
                    g_handObjectCaptures.fetch_add(1,
                                                   std::memory_order_relaxed);
                }
            }
            const hands::SampleResult sampled =
                history->history.Evaluate(g_latestClockSample);
            if (sampled.kind == hands::SampleKind::applied) {
                slot->matrix = sampled.matrix;
                ApplyLookCorrection(slot->matrix);
                g_handObjectApplies.fetch_add(1,
                                              std::memory_order_relaxed);
                return &slot->matrix;
            }
        }
    }
    if (!hands::Multiply(g_handsRootCorrection, *original, slot->matrix)) {
        return original;
    }
    ApplyLookCorrection(slot->matrix);
    g_handsRootApplies.fetch_add(1, std::memory_order_relaxed);
    return &slot->matrix;
}

void ViewportRenderDetour(void* const scene, void* const viewport,
                          const float timeStep,
                          const std::uint64_t flags) {
    const bool active =
        g_telemetryActive.load(std::memory_order_relaxed);
    void* const camera = active ? ResolveViewportCamera(viewport) : nullptr;
    if (active) {
        RecordCameraTelemetry(camera);
        BeginHandsRender();
        BeginWorldPoseRender();
        BeginAgentPoseRender();
    }

    CameraSavedState saved{};
    bool applied = false;
    if (active && g_cameraInterpolationEnabled &&
        g_hasLatestClockSample && camera != nullptr &&
        reinterpret_cast<std::uintptr_t>(camera) ==
            g_cameraHistoryIdentity) {
        const camera::SampleResult sample =
            g_cameraHistory.Evaluate(g_latestClockSample);
        if (sample.kind == camera::SampleKind::interpolated &&
            SaveCameraState(camera, saved)) {
            camera::Pose rendered = sample.pose;
            ApplyLookPreview(saved.pose, sample.pose, rendered);
            if (ApplyCameraPose(camera, rendered, saved)) {
                applied = true;
                g_cameraOverrides.fetch_add(1,
                                            std::memory_order_relaxed);
                if (sample.pose.rotateMode == camera::kRotateModeMatrix) {
                    g_cameraMatrixModeOverrides.fetch_add(
                        1, std::memory_order_relaxed);
                }
            } else if (RestoreCameraState(camera, saved)) {
                g_cameraRestores.fetch_add(1,
                                           std::memory_order_relaxed);
                g_cameraOverrideUnavailable.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                g_cameraRestoreFailures.fetch_add(
                    1, std::memory_order_relaxed);
                g_cameraInterpolationEnabled = false;
                log::Write(log::Level::error,
                           L"Camera interpolation disabled: recovery from "
                           L"an apply failure could not restore camera state.");
            }
        } else {
            g_cameraOverrideUnavailable.fetch_add(
                1, std::memory_order_relaxed);
        }
    } else if (active && g_cameraInterpolationEnabled) {
        g_cameraOverrideUnavailable.fetch_add(1,
                                              std::memory_order_relaxed);
    }

    g_originalViewportRender(scene, viewport, timeStep, flags);
    g_lookCorrectionActive = false;
    if (active) {
        EndWorldPoseRender();
        EndAgentPoseRender();
        EndHandsRender();
    }
    if (applied) {
        if (RestoreCameraState(camera, saved)) {
            g_cameraRestores.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_cameraRestoreFailures.fetch_add(1,
                                              std::memory_order_relaxed);
            g_cameraInterpolationEnabled = false;
            log::Write(log::Level::error,
                       L"Camera interpolation disabled: exact post-render "
                       L"camera-state restoration failed.");
        }
    }
}

void SdlGlSwapWindowDetour(void* const window) {
    g_originalSdlGlSwapWindow(window);
    if (!g_telemetryActive.load(std::memory_order_relaxed)) return;
    NoteLookPresent();
    g_presents.fetch_add(1, std::memory_order_relaxed);
}

void ReportCameraTelemetry() {
    std::array<CameraSlot, kMaximumCameraIdentities> slots{};
    AcquireSRWLockShared(&g_cameraLock);
    slots = g_cameraSlots;
    ReleaseSRWLockShared(&g_cameraLock);

    std::size_t index = 0;
    for (const auto& slot : slots) {
        if (slot.camera == 0 || !slot.hasPose) continue;
        const CameraPose& pose = slot.lastPose;
        std::wostringstream message;
        message << std::fixed << std::setprecision(4)
                << L"Camera identity[" << index << L"] pointer=0x"
                << std::hex << std::uppercase << slot.camera << std::dec
                << L", samples=" << slot.samples
                << L", repeats=" << slot.repeats
                << L", fixedChanges=" << slot.fixedBoundaryChanges
                << L", sameFixedChanges=" << slot.sameFixedSerialChanges
                << L", position=(" << pose.position[0] << L", "
                << pose.position[1] << L", " << pose.position[2] << L")"
                << L", basePYR=(" << pose.pitch << L", " << pose.yaw
                << L", " << pose.roll << L")"
                << L", extendedPYR=(" << pose.extendedPitch << L", "
                << pose.extendedYaw << L", " << pose.extendedRoll << L")"
                << L", projection=" << pose.projectionType
                << L", rotateMode=" << pose.rotateMode;
        log::Write(log::Level::info, message.str());
        ++index;
    }
}

void ReportHandsTelemetry() {
    std::array<HandsRootSlot, kMaximumHandsIdentities> slots{};
    AcquireSRWLockShared(&g_handsLock);
    slots = g_handsSlots;
    ReleaseSRWLockShared(&g_handsLock);

    std::size_t index = 0;
    for (const auto& slot : slots) {
        if (slot.entity == 0) continue;
        std::wostringstream message;
        message << std::fixed << std::setprecision(4)
                << L"Hands root[" << index << L"] entity=0x"
                << std::hex << std::uppercase << slot.entity << std::dec
                << L", mesh=0x" << std::hex << std::uppercase
                << slot.mesh << std::dec
                << L", calls=" << slot.calls
                << L", repeats=" << slot.repeats
                << L", fixedChanges=" << slot.fixedBoundaryChanges
                << L", sameFixedChanges="
                << slot.sameFixedSerialChanges
                << L", invalid=" << slot.invalidMatrices;
        if (slot.hasMatrix) {
            message << L", position=(" << slot.lastMatrix[3] << L", "
                    << slot.lastMatrix[7] << L", "
                    << slot.lastMatrix[11] << L")";
        }
        message << L", paletteBuffer=0x" << std::hex << std::uppercase
                << slot.paletteBuffer << std::dec
                << L", bones=" << slot.boneCount
                << L", paletteCalls=" << slot.paletteCalls
                << L", paletteRepeats=" << slot.paletteRepeats
                << L", paletteFixedChanges="
                << slot.paletteFixedBoundaryChanges
                << L", paletteSameFixedChanges="
                << slot.paletteSameFixedSerialChanges
                << L", paletteInvalid=" << slot.paletteInvalid
                << L", paletteIdentityChanges="
                << slot.paletteIdentityChanges;
        message << L", messages=[";
        for (std::size_t messageIndex = 0;
             messageIndex < slot.messageCounts.size(); ++messageIndex) {
            if (messageIndex != 0) message << L", ";
            if (messageIndex == 8) {
                message << L"outside=";
            } else {
                message << messageIndex << L"=";
            }
            message << slot.messageCounts[messageIndex];
        }
        message << L"]";
        log::Write(log::Level::info, message.str());
        ++index;
    }
}

void FinishTelemetry() {
    if (!g_telemetryEnabled) return;
    g_telemetryActive.store(false, std::memory_order_release);
    StopReportThread();
    EndHandsRender();
    ReportTelemetry(true);
    ReportCameraTelemetry();
    ReportHandsTelemetry();
    ReportLookTrace();
    g_logicTimer.store(0, std::memory_order_release);
    g_presentationClock.reset();
    g_cameraHistory.Reset();
    g_cameraHistoryIdentity = 0;
    g_latestClockSample = {};
    g_hasLatestClockSample = false;
    ResetHandsInterpolation();
}

void EngineRunDetour(void* const self) {
    if (g_telemetryEnabled) {
        ResetTelemetry();
        g_lookPreviewThread = GetCurrentThreadId();
        g_logicTimer.store(0, std::memory_order_relaxed);
        const bool timerCaptured = CaptureLogicTimer(self);
        g_telemetryActive.store(true, std::memory_order_release);
        StartReportThread();
        log::Write(log::Level::info,
                   L"Cadence telemetry started at cEngine::Run entry.");
        log::Write(timerCaptured ? log::Level::info : log::Level::warning,
                   timerCaptured
                       ? L"Logic-timer instance captured for native phase telemetry."
                       : L"Logic-timer instance was unavailable; phase samples will "
                         L"be rejected.");
    }

    if (!g_renderUnlockEnabled) {
        g_originalEngineRun(self);
        FinishTelemetry();
        return;
    }
    if (self == nullptr) {
        g_renderUnlockState.store(RenderUnlockState::rejected,
                                  std::memory_order_release);
        log::Write(log::Level::error,
                   L"Render unlock rejected: cEngine::Run received a null instance.");
        g_originalEngineRun(self);
        FinishTelemetry();
        return;
    }

    auto* const limitFps =
        reinterpret_cast<std::uint8_t*>(self) + g_limitFpsOffset;
    if (!QueryCommittedRange(limitFps, sizeof(*limitFps), true)) {
        g_renderUnlockState.store(RenderUnlockState::rejected,
                                  std::memory_order_release);
        log::Write(log::Level::error,
                   L"Render unlock rejected: cEngine::mbLimitFPS is not in writable "
                   L"committed memory.");
        g_originalEngineRun(self);
        FinishTelemetry();
        return;
    }

    const std::uint8_t originalValue = *limitFps;
    if (originalValue > 1U) {
        g_renderUnlockState.store(RenderUnlockState::rejected,
                                  std::memory_order_release);
        log::Write(log::Level::error,
                   L"Render unlock rejected: cEngine::mbLimitFPS failed its runtime "
                   L"boolean check.");
        g_originalEngineRun(self);
        FinishTelemetry();
        return;
    }

    *limitFps = 0;
    g_renderUnlockState.store(RenderUnlockState::active,
                              std::memory_order_release);
    log::Write(log::Level::info,
               L"Render unlock active: cEngine::mbLimitFPS=false; fixed-update "
               L"scheduler state was not modified.");
    g_originalEngineRun(self);
    FinishTelemetry();

    if (QueryCommittedRange(limitFps, sizeof(*limitFps), true)) {
        *limitFps = originalValue;
        g_renderUnlockState.store(RenderUnlockState::restored,
                                  std::memory_order_release);
        log::Write(log::Level::info,
                   L"Render unlock restored the original LimitFPS value on "
                   L"cEngine::Run exit.");
    } else {
        g_renderUnlockState.store(RenderUnlockState::rejected,
                                  std::memory_order_release);
        log::Write(log::Level::error,
                   L"Render unlock could not restore LimitFPS because the engine "
                   L"instance was no longer writable.");
    }
}

void RollBackHooks() noexcept {
    (void)MH_DisableHook(MH_ALL_HOOKS);
    if (g_sdlGetRelativeMouseStateTarget != nullptr) {
        (void)MH_RemoveHook(g_sdlGetRelativeMouseStateTarget);
    }
    if (g_guiDoMouseTarget != nullptr) {
        (void)MH_RemoveHook(g_guiDoMouseTarget);
    }
    if (g_guiCursorQueueTarget != nullptr) {
        (void)MH_RemoveHook(g_guiCursorQueueTarget);
    }
    if (g_guiMouseVirtualSetterTarget != nullptr) {
        (void)MH_RemoveHook(g_guiMouseVirtualSetterTarget);
    }
    if (g_guiMousePixelSetterTarget != nullptr) {
        (void)MH_RemoveHook(g_guiMousePixelSetterTarget);
    }
    if (g_luxAgentOnUpdateTarget != nullptr) {
        (void)MH_RemoveHook(g_luxAgentOnUpdateTarget);
    }
    if (g_luxPropOnUpdateTarget != nullptr) {
        (void)MH_RemoveHook(g_luxPropOnUpdateTarget);
    }
    if (g_subMeshGetModelMatrixTarget != nullptr) {
        (void)MH_RemoveHook(g_subMeshGetModelMatrixTarget);
    }
    if (g_meshUpdateGraphicsTarget != nullptr) {
        (void)MH_RemoveHook(g_meshUpdateGraphicsTarget);
    }
    if (g_updateAttachmentTarget != nullptr) {
        (void)MH_RemoveHook(g_updateAttachmentTarget);
    }
    if (g_handsSetMatrixTarget != nullptr) {
        (void)MH_RemoveHook(g_handsSetMatrixTarget);
    }
    if (g_viewportRenderTarget != nullptr) {
        (void)MH_RemoveHook(g_viewportRenderTarget);
    }
    if (g_newtonUpdateTarget != nullptr) {
        (void)MH_RemoveHook(g_newtonUpdateTarget);
    }
    if (g_sdlGlSwapWindowTarget != nullptr) {
        (void)MH_RemoveHook(g_sdlGlSwapWindowTarget);
    }
    if (g_updaterRunMessageTarget != nullptr) {
        (void)MH_RemoveHook(g_updaterRunMessageTarget);
    }
    if (g_engineRunTarget != nullptr) {
        (void)MH_RemoveHook(g_engineRunTarget);
    }
    (void)MH_Uninitialize();
    g_originalSdlGlSwapWindow = nullptr;
    g_originalNewtonUpdate = nullptr;
    g_originalViewportRender = nullptr;
    g_originalHandsSetMatrix = nullptr;
    g_originalUpdateAttachment = nullptr;
    g_originalMeshUpdateGraphics = nullptr;
    g_originalSubMeshGetModelMatrix = nullptr;
    g_originalLuxPropOnUpdate = nullptr;
    g_originalLuxAgentOnUpdate = nullptr;
    g_originalGuiMousePixelSetter = nullptr;
    g_originalGuiMouseVirtualSetter = nullptr;
    g_originalGuiCursorQueue = nullptr;
    g_originalGuiDoMouse = nullptr;
    g_originalSdlGetRelativeMouseState = nullptr;
    g_sdlPumpEvents = nullptr;
    g_originalUpdaterRunMessage = nullptr;
    g_originalEngineRun = nullptr;
    g_engineMicroseconds = nullptr;
    g_skinningConvert = nullptr;
    g_sdlGetRelativeMouseMode = nullptr;
}

bool CreateHook(void* const target, void* const detour, void** const original,
                const wchar_t* const name, std::wstring& error) {
    const MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) return true;
    error = HookError(name, status);
    return false;
}

} // namespace

bool Initialize(const target::RuntimeMappings& mappings,
                const Options& options, std::wstring& error) noexcept {
    if ((options.cameraInterpolation || options.handsRootInterpolation ||
         options.handsPoseInterpolation || options.worldRootInterpolation ||
         options.worldPoseInterpolation || options.agentRootInterpolation ||
         options.agentPoseInterpolation ||
         options.guiMouseOverlay || options.mouseLookPreview) &&
        !options.telemetry) {
        error = L"Smoothing features require the runtime timing hooks.";
        return false;
    }
    if (options.ragdollPoseInterpolation &&
        (!options.worldRootInterpolation ||
         !options.worldPoseInterpolation)) {
        error = L"Ragdoll interpolation requires world root and pose "
                L"interpolation.";
        return false;
    }
    if (options.mouseLookPreview && !options.cameraInterpolation) {
        error = L"Mouse look preview requires camera interpolation.";
        return false;
    }
    if (mappings.engineRun == 0 ||
        (options.renderUnlock && mappings.limitFpsOffset == 0) ||
        (options.telemetry &&
         (mappings.updaterRunMessage == 0 ||
          mappings.engineMicroseconds == 0 ||
          mappings.engineTimerOffset == 0 ||
          mappings.viewportRender == 0 ||
          mappings.viewportCameraOffset == 0 ||
          mappings.handsSetMatrix == 0 ||
          mappings.meshUpdateGraphics == 0 ||
          mappings.subMeshGetModelMatrix == 0 ||
          mappings.luxPropOnUpdate == 0 ||
          mappings.luxAgentOnUpdate == 0 ||
          (options.guiMouseOverlay &&
           (mappings.guiMousePixelSetter == 0 ||
            mappings.guiMouseVirtualSetter == 0 ||
            mappings.guiCursorQueue == 0 ||
            mappings.guiDoMouse == 0)) ||
          (options.handsRootInterpolation &&
           mappings.attachmentUpdate == 0) ||
           ((options.handsPoseInterpolation || options.worldPoseInterpolation ||
             options.agentPoseInterpolation) &&
           mappings.skinningConvert == 0)))) {
        error = L"Requested runtime mapping is incomplete.";
        return false;
    }
    if (options.telemetry && !QueryPerformanceFrequency(&g_qpcFrequency)) {
        error = L"QueryPerformanceFrequency failed with Win32 error " +
                std::to_wstring(GetLastError());
        return false;
    }

    if (options.telemetry) {
        const HMODULE sdl = GetModuleHandleW(L"SDL2.dll");
        if (sdl == nullptr) {
            error = L"SDL2.dll is not loaded.";
            return false;
        }
        g_sdlGlSwapWindowTarget = reinterpret_cast<void*>(
            GetProcAddress(sdl, "SDL_GL_SwapWindow"));
        if (g_sdlGlSwapWindowTarget == nullptr) {
            error = L"SDL_GL_SwapWindow export is unavailable.";
            return false;
        }
        if (options.mouseLookPreview) {
            g_sdlGetRelativeMouseStateTarget = reinterpret_cast<void*>(
                GetProcAddress(sdl, "SDL_GetRelativeMouseState"));
            g_sdlPumpEvents = reinterpret_cast<SdlPumpEvents>(
                GetProcAddress(sdl, "SDL_PumpEvents"));
            if (g_sdlGetRelativeMouseStateTarget == nullptr ||
                g_sdlPumpEvents == nullptr) {
                error = L"SDL relative mouse exports are unavailable.";
                return false;
            }
        }
        if (options.guiMouseOverlay || options.mouseLookPreview) {
            g_sdlGetRelativeMouseMode =
                reinterpret_cast<SdlGetRelativeMouseMode>(
                    GetProcAddress(sdl, "SDL_GetRelativeMouseMode"));
            if (g_sdlGetRelativeMouseMode == nullptr) {
                error = L"SDL_GetRelativeMouseMode export is unavailable.";
                return false;
            }
        }
        const HMODULE newton = GetModuleHandleW(L"Newton.dll");
        if (newton == nullptr) {
            error = L"Newton.dll is not loaded.";
            return false;
        }
        g_newtonUpdateTarget =
            reinterpret_cast<void*>(GetProcAddress(newton, "NewtonUpdate"));
        if (g_newtonUpdateTarget == nullptr) {
            error = L"NewtonUpdate export is unavailable.";
            return false;
        }
    }

    // The host executable is already mapped and inspected by the bootstrap;
    // its bounds let entity vtable dispatch be checked without an OS query.
    const auto* const hostImage =
        reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* const dosHeader =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(hostImage);
    if (hostImage == nullptr || dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        error = L"The host executable image is unavailable.";
        return false;
    }
    const auto* const ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        hostImage + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        error = L"The host executable image is unavailable.";
        return false;
    }
    g_hostImageBegin = reinterpret_cast<std::uintptr_t>(hostImage);
    g_hostImageEnd = g_hostImageBegin + ntHeaders->OptionalHeader.SizeOfImage;

    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK) {
        error = HookError(L"MH_Initialize", initializeStatus);
        return false;
    }

    g_renderUnlockEnabled = options.renderUnlock;
    g_telemetryEnabled = options.telemetry;
    g_cameraInterpolationEnabled = options.cameraInterpolation;
    g_handsRootInterpolationEnabled = options.handsRootInterpolation;
    g_handsPoseInterpolationEnabled = options.handsPoseInterpolation;
    g_worldRootInterpolationEnabled = options.worldRootInterpolation;
    g_worldPoseInterpolationEnabled = options.worldPoseInterpolation;
    g_agentRootInterpolationEnabled = options.agentRootInterpolation;
    g_agentPoseInterpolationEnabled = options.agentPoseInterpolation;
    g_guiMouseOverlayEnabled = options.guiMouseOverlay;
    g_mouseLookPreviewEnabled = options.mouseLookPreview;
    g_ragdollPoseInterpolationEnabled = options.ragdollPoseInterpolation;
    g_reportIntervalSeconds = options.reportIntervalSeconds;
    g_detailedReportsEnabled = options.detailedReports;
    g_healthReportsEnabled = options.healthReports;
    g_limitFpsOffset = mappings.limitFpsOffset;
    g_engineTimerOffset = mappings.engineTimerOffset;
    g_viewportCameraOffset = mappings.viewportCameraOffset;
    g_engineMicroseconds = options.telemetry
        ? reinterpret_cast<EngineMicroseconds>(mappings.engineMicroseconds)
        : nullptr;
    g_engineRunTarget = reinterpret_cast<void*>(mappings.engineRun);
    g_updaterRunMessageTarget =
        options.telemetry ? reinterpret_cast<void*>(mappings.updaterRunMessage)
                          : nullptr;
    g_viewportRenderTarget =
        options.telemetry ? reinterpret_cast<void*>(mappings.viewportRender)
                          : nullptr;
    g_handsSetMatrixTarget =
        options.telemetry ? reinterpret_cast<void*>(mappings.handsSetMatrix)
                          : nullptr;
    g_updateAttachmentTarget = options.handsRootInterpolation
        ? reinterpret_cast<void*>(mappings.attachmentUpdate)
        : nullptr;
    g_meshUpdateGraphicsTarget = options.telemetry
        ? reinterpret_cast<void*>(mappings.meshUpdateGraphics)
        : nullptr;
    g_subMeshGetModelMatrixTarget = options.telemetry
        ? reinterpret_cast<void*>(mappings.subMeshGetModelMatrix)
        : nullptr;
    g_luxPropOnUpdateTarget = options.telemetry
        ? reinterpret_cast<void*>(mappings.luxPropOnUpdate)
        : nullptr;
    g_luxAgentOnUpdateTarget = options.telemetry
        ? reinterpret_cast<void*>(mappings.luxAgentOnUpdate)
        : nullptr;
    g_guiMousePixelSetterTarget = options.guiMouseOverlay
        ? reinterpret_cast<void*>(mappings.guiMousePixelSetter)
        : nullptr;
    g_guiMouseVirtualSetterTarget = options.guiMouseOverlay
        ? reinterpret_cast<void*>(mappings.guiMouseVirtualSetter)
        : nullptr;
    g_guiCursorQueueTarget = options.guiMouseOverlay
        ? reinterpret_cast<void*>(mappings.guiCursorQueue)
        : nullptr;
    g_guiDoMouseTarget = options.guiMouseOverlay
        ? reinterpret_cast<void*>(mappings.guiDoMouse)
        : nullptr;
    g_skinningConvert =
        (options.handsPoseInterpolation || options.worldPoseInterpolation ||
         options.agentPoseInterpolation)
        ? reinterpret_cast<SkinningConvert>(mappings.skinningConvert)
        : nullptr;
    if (options.renderUnlock) {
        g_renderUnlockState.store(RenderUnlockState::pending,
                                  std::memory_order_release);
    }

    if (!CreateHook(g_engineRunTarget,
                    reinterpret_cast<void*>(&EngineRunDetour),
                    reinterpret_cast<void**>(&g_originalEngineRun),
                    L"MH_CreateHook(cEngine::Run)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_updaterRunMessageTarget,
                    reinterpret_cast<void*>(&UpdaterRunMessageDetour),
                    reinterpret_cast<void**>(&g_originalUpdaterRunMessage),
                    L"MH_CreateHook(cUpdater::RunMessage)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_sdlGlSwapWindowTarget,
                    reinterpret_cast<void*>(&SdlGlSwapWindowDetour),
                    reinterpret_cast<void**>(&g_originalSdlGlSwapWindow),
                    L"MH_CreateHook(SDL_GL_SwapWindow)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_newtonUpdateTarget,
                    reinterpret_cast<void*>(&NewtonUpdateDetour),
                    reinterpret_cast<void**>(&g_originalNewtonUpdate),
                    L"MH_CreateHook(NewtonUpdate)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_viewportRenderTarget,
                    reinterpret_cast<void*>(&ViewportRenderDetour),
                    reinterpret_cast<void**>(&g_originalViewportRender),
                    L"MH_CreateHook(per-viewport render)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_handsSetMatrixTarget,
                    reinterpret_cast<void*>(&HandsSetMatrixDetour),
                    reinterpret_cast<void**>(&g_originalHandsSetMatrix),
                    L"MH_CreateHook(iLuxEntity::SetMatrix)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.handsRootInterpolation &&
        !CreateHook(g_updateAttachmentTarget,
                    reinterpret_cast<void*>(&UpdateAttachmentDetour),
                    reinterpret_cast<void**>(&g_originalUpdateAttachment),
                    L"MH_CreateHook(iLuxEntity::UpdateEntityAttachment)",
                    error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_meshUpdateGraphicsTarget,
                    reinterpret_cast<void*>(&MeshUpdateGraphicsDetour),
                    reinterpret_cast<void**>(&g_originalMeshUpdateGraphics),
                    L"MH_CreateHook(cMeshEntity::UpdateGraphicsForFrame)",
                    error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_subMeshGetModelMatrixTarget,
                    reinterpret_cast<void*>(&SubMeshGetModelMatrixDetour),
                    reinterpret_cast<void**>(
                        &g_originalSubMeshGetModelMatrix),
                    L"MH_CreateHook(cSubMeshEntity::GetModelMatrix)",
                    error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_luxPropOnUpdateTarget,
                    reinterpret_cast<void*>(&LuxPropOnUpdateDetour),
                    reinterpret_cast<void**>(&g_originalLuxPropOnUpdate),
                    L"MH_CreateHook(cLuxProp::OnUpdate)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.telemetry &&
        !CreateHook(g_luxAgentOnUpdateTarget,
                    reinterpret_cast<void*>(&LuxAgentOnUpdateDetour),
                    reinterpret_cast<void**>(&g_originalLuxAgentOnUpdate),
                    L"MH_CreateHook(cLuxAgent::OnUpdate)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.guiMouseOverlay &&
        !CreateHook(g_guiMousePixelSetterTarget,
                    reinterpret_cast<void*>(&GuiMousePixelSetterDetour),
                    reinterpret_cast<void**>(
                        &g_originalGuiMousePixelSetter),
                    L"MH_CreateHook(cImGui::SendMousePosition)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.guiMouseOverlay &&
        !CreateHook(g_guiMouseVirtualSetterTarget,
                    reinterpret_cast<void*>(&GuiMouseVirtualSetterDetour),
                    reinterpret_cast<void**>(
                        &g_originalGuiMouseVirtualSetter),
                    L"MH_CreateHook(cImGui::SendMouseVirtualPosition)",
                    error)) {
        RollBackHooks();
        return false;
    }
    if (options.guiMouseOverlay &&
        !CreateHook(g_guiCursorQueueTarget,
                    reinterpret_cast<void*>(&GuiCursorQueueDetour),
                    reinterpret_cast<void**>(&g_originalGuiCursorQueue),
                    L"MH_CreateHook(cImGui::Draw cursor queue)", error)) {
        RollBackHooks();
        return false;
    }
    if (options.guiMouseOverlay &&
        !CreateHook(g_guiDoMouseTarget,
                    reinterpret_cast<void*>(&GuiDoMouseDetour),
                    reinterpret_cast<void**>(&g_originalGuiDoMouse),
                    L"MH_CreateHook(cImGui::DoMouse)", error)) {
        RollBackHooks();
        return false;
    }

    if (options.mouseLookPreview &&
        !CreateHook(g_sdlGetRelativeMouseStateTarget,
                    reinterpret_cast<void*>(&SdlGetRelativeMouseStateDetour),
                    reinterpret_cast<void**>(
                        &g_originalSdlGetRelativeMouseState),
                    L"MH_CreateHook(SDL_GetRelativeMouseState)", error)) {
        RollBackHooks();
        return false;
    }

    const MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
    if (enableStatus != MH_OK) {
        error = HookError(L"MH_EnableHook", enableStatus);
        RollBackHooks();
        return false;
    }

    log::Write(log::Level::info,
               L"Runtime hooks enabled; requested features are pending live "
               L"instance/cadence validation.");
    return true;
}

bool InitializeRenderUnlock(const target::RuntimeMappings& mappings,
                            std::wstring& error) noexcept {
    return Initialize(mappings,
                      Options{true, false, false, false, false, false, false,
                              false, false, false, false, false, 5},
                      error);
}

} // namespace soma::runtime
