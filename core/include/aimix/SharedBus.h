#pragma once

// Inter-plugin communication bus.
//
//   Listener (track insert) ──audio thread──▶ [slot i: SPSC ring] ──▶ Master Engine thread
//
// The bus is a fixed array of slots. Each Listener claims one slot (CAS, on the
// message thread) and becomes that slot's only producer; the Master Engine is
// the only consumer of every slot. One producer + one consumer per ring keeps
// every hot-path operation wait-free.
//
// Backends
//   SharedMemory : named POSIX shm / Win32 file mapping. Works across processes,
//                  so it survives hosts that sandbox plugins (Bitwig, Reaper
//                  "run as separate process", AU out-of-process hosting).
//   ProcessLocal : static storage keyed by name. Fallback when shm is refused
//                  (e.g. macOS App Sandbox without an app-group prefix) and the
//                  backend used by unit tests.
//
// Everything in SharedBusLayout is address-free: integers, lock-free atomics
// and inline arrays only.

#include "AnalysisPayload.h"
#include "SpscRingBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace aimix
{
constexpr int      kMaxBusSlots          = 64;
constexpr uint32_t kSlotRingCapacity     = 8;          // ~350 ms of frames @ 48 kHz
constexpr uint32_t kBusMagic             = 0x58'4D'49'41; // "AIMX"
constexpr uint32_t kBusLayoutVersion     = 2;
constexpr uint64_t kMasterTimeoutMs      = 3000;       // master considered gone after this
constexpr const char* kDefaultBusName    = "aimix_bus_v2";

static_assert (std::atomic<uint64_t>::is_always_lock_free, "64-bit atomics must be lock-free for shared memory");

enum class SlotState : uint32_t { Free = 0, Claiming = 1, Active = 2 };

// BusSlot::detectedRole: the Master Engine's guess of what an "Auto" track is,
// so the Listener can show it. Zero means "still listening".
constexpr uint32_t kDetectedRoleValid = 0x100;

struct alignas (64) BusSlot
{
    std::atomic<uint32_t> state;          // SlotState
    std::atomic<uint32_t> generation;     // bumped on every claim, lets the engine detect a new owner
    std::atomic<uint32_t> ownerPid;       // for dead-owner reclamation with the shm backend
    std::atomic<uint32_t> droppedFrames;  // producer-side overflow counter
    std::atomic<uint64_t> heartbeatMs;    // last time the producer ran its process block
    std::atomic<uint32_t> detectedRole;   // written by the Master Engine: kDetectedRoleValid | TrackRole
    SpscRingBuffer<AnalysisPayload, kSlotRingCapacity> ring;
};

struct alignas (64) BusHeader
{
    std::atomic<uint32_t> initState;      // 0 = zeroed, 1 = initialising, 2 = ready
    uint32_t magic;
    uint32_t layoutVersion;
    uint32_t payloadVersion;
    uint64_t layoutSize;
    std::atomic<uint32_t> masterToken;    // 0 = no master engine
    std::atomic<uint64_t> masterHeartbeatMs;
    std::atomic<uint32_t> mixStyle;       // written by the Master Engine: MixStyle::pack(), 0 = not chosen
    std::atomic<uint32_t> requestedStyle; // a Listener's pick for the Master to adopt: (sequence << 16) | MixStyle::pack()
};
static_assert (sizeof (BusHeader) == 64, "BusHeader must stay one cache line");

struct SharedBusLayout
{
    BusHeader header;
    BusSlot   slots[kMaxBusSlots];
};

uint64_t monotonicMillis() noexcept;
uint32_t currentProcessId() noexcept;

class SharedBus
{
public:
    enum class Backend { SharedMemory, ProcessLocal };

    // Opens (creating if needed) the named bus. Not real-time safe: call from
    // the constructor / message thread. Falls back to ProcessLocal if the
    // shared-memory segment can't be created or has an incompatible layout.
    static std::shared_ptr<SharedBus> open (const std::string& name = kDefaultBusName,
                                            Backend preferred = Backend::SharedMemory);

    ~SharedBus();
    SharedBus (const SharedBus&) = delete;
    SharedBus& operator= (const SharedBus&) = delete;

    Backend backend() const noexcept            { return backendInUse; }
    SharedBusLayout& layout() noexcept          { return *bus; }
    BusSlot& slot (int index) noexcept          { return bus->slots[index]; }

    // ---- Listener side (message thread) ------------------------------------
    // Returns the claimed slot index or -1 if the bus is full. Reclaims slots
    // whose owning process has died (shm backend).
    int  claimSlot (uint64_t nowMs);
    void releaseSlot (int index);

    // ---- Master side ----------------------------------------------------------
    // Only one Master Engine drains the bus. A second master stays passive
    // until the first one stops heart-beating for kMasterTimeoutMs.
    bool tryBecomeMaster (uint32_t token, uint64_t nowMs);
    void heartbeatMaster (uint32_t token, uint64_t nowMs);
    void releaseMaster (uint32_t token);
    bool isMasterAlive (uint64_t nowMs) const noexcept;

    // For tests: removes a named shm segment.
    static void unlinkSharedMemory (const std::string& name);

    struct Mapping;   // platform shm handle (opaque)

private:
    SharedBus() = default;

    SharedBusLayout* bus = nullptr;
    Backend backendInUse = Backend::ProcessLocal;
    std::unique_ptr<Mapping> mapping;
};

// Producer helper owned by a Listener: claims a slot on construction,
// releases it on destruction, and exposes the zero-copy write API to the
// audio thread.
class PayloadSink
{
public:
    virtual ~PayloadSink() = default;
    virtual AnalysisPayload* acquire() noexcept = 0;   // nullptr when full / disconnected
    virtual bool hasSpace() const noexcept { return true; }   // cheap pre-check before doing FFT work
    virtual void commit() noexcept = 0;
    virtual void markDropped() noexcept {}
    virtual uint32_t trackId() const noexcept = 0;
};

class BusPublisher final : public PayloadSink
{
public:
    explicit BusPublisher (std::shared_ptr<SharedBus> bus);
    ~BusPublisher() override;

    bool isConnected() const noexcept { return slotIndex >= 0; }
    int  slot() const noexcept        { return slotIndex; }
    uint32_t trackId() const noexcept override { return slotIndex < 0 ? 0xffffffffu : (uint32_t) slotIndex; }

    AnalysisPayload* acquire() noexcept override;
    bool hasSpace() const noexcept override;
    void commit() noexcept override;
    void markDropped() noexcept override;

    SharedBus& getBus() noexcept { return *bus; }

private:
    std::shared_ptr<SharedBus> bus;
    int slotIndex = -1;
};

} // namespace aimix
