#include "aimix/SharedBus.h"

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <thread>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <signal.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace aimix
{
const char* toString (TrackRole role) noexcept
{
    switch (role)
    {
        case TrackRole::Unknown:   return "Unknown";
        case TrackRole::Vocal:     return "Vocal";
        case TrackRole::Kick:      return "Kick";
        case TrackRole::Snare:     return "Snare";
        case TrackRole::Drums:     return "Drums";
        case TrackRole::Bass:      return "Bass";
        case TrackRole::Guitar:    return "Guitar";
        case TrackRole::Keys:      return "Keys";
        case TrackRole::Synth:     return "Synth";
        case TrackRole::Fx:        return "FX";
        case TrackRole::MasterBus: return "Master";
        case TrackRole::NumRoles:  break;
    }
    return "Unknown";
}

uint64_t monotonicMillis() noexcept
{
    using namespace std::chrono;
    return (uint64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

uint32_t currentProcessId() noexcept
{
   #if defined (_WIN32)
    return (uint32_t) GetCurrentProcessId();
   #else
    return (uint32_t) getpid();
   #endif
}

static bool isProcessAlive (uint32_t pid) noexcept
{
    if (pid == 0)
        return false;
   #if defined (_WIN32)
    HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (h == nullptr)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess (h, &code) && code == STILL_ACTIVE;
    CloseHandle (h);
    return alive;
   #else
    return kill ((pid_t) pid, 0) == 0 || errno == EPERM;
   #endif
}

//==============================================================================
struct SharedBus::Mapping
{
    void* address = nullptr;
    size_t size = 0;
   #if defined (_WIN32)
    HANDLE handle = nullptr;
   #endif

    ~Mapping()
    {
       #if defined (_WIN32)
        if (address != nullptr) UnmapViewOfFile (address);
        if (handle != nullptr)  CloseHandle (handle);
       #else
        if (address != nullptr) munmap (address, size);
       #endif
    }
};

static std::unique_ptr<SharedBus::Mapping> mapSharedMemory (const std::string& name, size_t size);

#if defined (_WIN32)
static std::unique_ptr<SharedBus::Mapping> mapSharedMemory (const std::string& name, size_t size)
{
    const std::string fullName = "Local\\" + name;
    HANDLE h = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   (DWORD) ((uint64_t) size >> 32), (DWORD) (size & 0xffffffffu), fullName.c_str());
    if (h == nullptr)
        return {};
    void* addr = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (addr == nullptr) { CloseHandle (h); return {}; }
    auto m = std::make_unique<SharedBus::Mapping>();
    m->address = addr; m->size = size; m->handle = h;
    return m;   // pages of a new mapping are zero-filled by the OS
}

void SharedBus::unlinkSharedMemory (const std::string&) {}
#else
static std::unique_ptr<SharedBus::Mapping> mapSharedMemory (const std::string& name, size_t size)
{
    const std::string shmName = "/" + name;
    const int fd = shm_open (shmName.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return {};

    struct stat st {};
    if (fstat (fd, &st) != 0) { close (fd); return {}; }

    if (st.st_size == 0)
    {
        if (ftruncate (fd, (off_t) size) != 0) { close (fd); return {}; }   // zero-filled
    }
    else if ((size_t) st.st_size != size)
    {
        close (fd);   // segment from an incompatible build
        return {};
    }

    void* addr = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close (fd);
    if (addr == MAP_FAILED)
        return {};

    auto m = std::make_unique<SharedBus::Mapping>();
    m->address = addr; m->size = size;
    return m;
}

void SharedBus::unlinkSharedMemory (const std::string& name)
{
    shm_unlink (("/" + name).c_str());
}
#endif

// All-zero memory is the valid "empty" state of every field, so the first
// opener only has to construct the slots and stamp the header.
static bool initialiseOrAttach (SharedBusLayout* layout)
{
    auto& h = layout->header;
    uint32_t expected = 0;

    if (h.initState.compare_exchange_strong (expected, 1, std::memory_order_acq_rel))
    {
        for (auto& s : layout->slots)
            new (&s) BusSlot();

        h.magic          = kBusMagic;
        h.layoutVersion  = kBusLayoutVersion;
        h.payloadVersion = kPayloadVersion;
        h.layoutSize     = sizeof (SharedBusLayout);
        h.initState.store (2, std::memory_order_release);
        return true;
    }

    for (int i = 0; i < 2000 && h.initState.load (std::memory_order_acquire) != 2; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));

    return h.initState.load (std::memory_order_acquire) == 2
        && h.magic == kBusMagic
        && h.layoutVersion == kBusLayoutVersion
        && h.payloadVersion == kPayloadVersion
        && h.layoutSize == sizeof (SharedBusLayout);
}

static SharedBusLayout* processLocalLayout (const std::string& name)
{
    static std::mutex lock;
    static std::map<std::string, SharedBusLayout*> buses;   // intentionally leaked: lives as long as the module

    std::lock_guard<std::mutex> guard (lock);
    auto& entry = buses[name];
    if (entry == nullptr)
    {
        void* mem = ::operator new (sizeof (SharedBusLayout), std::align_val_t { alignof (SharedBusLayout) });
        std::memset (mem, 0, sizeof (SharedBusLayout));
        entry = static_cast<SharedBusLayout*> (mem);
    }
    return entry;
}

std::shared_ptr<SharedBus> SharedBus::open (const std::string& name, Backend preferred)
{
    std::shared_ptr<SharedBus> result (new SharedBus());

    if (preferred == Backend::SharedMemory)
    {
        if (auto m = mapSharedMemory (name, sizeof (SharedBusLayout)))
        {
            auto* layout = static_cast<SharedBusLayout*> (m->address);
            if (initialiseOrAttach (layout))
            {
                result->bus = layout;
                result->mapping = std::move (m);
                result->backendInUse = Backend::SharedMemory;
                return result;
            }
        }
    }

    auto* layout = processLocalLayout (name);
    if (! initialiseOrAttach (layout))
        return {};

    result->bus = layout;
    result->backendInUse = Backend::ProcessLocal;
    return result;
}

SharedBus::~SharedBus() = default;

int SharedBus::claimSlot (uint64_t nowMs)
{
    const uint32_t pid = currentProcessId();

    auto takeOver = [&] (BusSlot& s)
    {
        s.ring.reset();
        s.droppedFrames.store (0, std::memory_order_relaxed);
        s.ownerPid.store (pid, std::memory_order_relaxed);
        s.heartbeatMs.store (nowMs, std::memory_order_relaxed);
        s.generation.fetch_add (1, std::memory_order_relaxed);
        s.state.store ((uint32_t) SlotState::Active, std::memory_order_release);
    };

    for (int i = 0; i < kMaxBusSlots; ++i)
    {
        auto& s = bus->slots[i];
        uint32_t expected = (uint32_t) SlotState::Free;
        if (s.state.compare_exchange_strong (expected, (uint32_t) SlotState::Claiming, std::memory_order_acq_rel))
        {
            takeOver (s);
            return i;
        }
    }

    // Bus full: reclaim slots whose owning process crashed without releasing.
    if (backendInUse == Backend::SharedMemory)
    {
        for (int i = 0; i < kMaxBusSlots; ++i)
        {
            auto& s = bus->slots[i];
            const auto owner = s.ownerPid.load (std::memory_order_relaxed);
            if (owner == pid || isProcessAlive (owner))
                continue;

            uint32_t expected = (uint32_t) SlotState::Active;
            if (s.state.compare_exchange_strong (expected, (uint32_t) SlotState::Claiming, std::memory_order_acq_rel))
            {
                takeOver (s);
                return i;
            }
        }
    }

    return -1;
}

void SharedBus::releaseSlot (int index)
{
    if (index >= 0 && index < kMaxBusSlots)
        bus->slots[index].state.store ((uint32_t) SlotState::Free, std::memory_order_release);
}

bool SharedBus::tryBecomeMaster (uint32_t token, uint64_t nowMs)
{
    auto& h = bus->header;
    uint32_t current = 0;

    if (h.masterToken.compare_exchange_strong (current, token, std::memory_order_acq_rel) || current == token)
    {
        h.masterHeartbeatMs.store (nowMs, std::memory_order_release);
        return true;
    }

    const auto last = h.masterHeartbeatMs.load (std::memory_order_acquire);
    if (nowMs > last && nowMs - last > kMasterTimeoutMs
        && h.masterToken.compare_exchange_strong (current, token, std::memory_order_acq_rel))
    {
        h.masterHeartbeatMs.store (nowMs, std::memory_order_release);
        return true;
    }
    return false;
}

void SharedBus::heartbeatMaster (uint32_t token, uint64_t nowMs)
{
    if (bus->header.masterToken.load (std::memory_order_acquire) == token)
        bus->header.masterHeartbeatMs.store (nowMs, std::memory_order_release);
}

void SharedBus::releaseMaster (uint32_t token)
{
    uint32_t expected = token;
    bus->header.masterToken.compare_exchange_strong (expected, 0, std::memory_order_acq_rel);
}

bool SharedBus::isMasterAlive (uint64_t nowMs) const noexcept
{
    const auto& h = bus->header;
    if (h.masterToken.load (std::memory_order_acquire) == 0)
        return false;
    const auto last = h.masterHeartbeatMs.load (std::memory_order_acquire);
    return nowMs < last || nowMs - last <= kMasterTimeoutMs;
}

//==============================================================================
BusPublisher::BusPublisher (std::shared_ptr<SharedBus> b) : bus (std::move (b))
{
    if (bus != nullptr)
        slotIndex = bus->claimSlot (monotonicMillis());
}

BusPublisher::~BusPublisher()
{
    if (bus != nullptr && slotIndex >= 0)
        bus->releaseSlot (slotIndex);
}

AnalysisPayload* BusPublisher::acquire() noexcept
{
    if (slotIndex < 0)
        return nullptr;
    auto& s = bus->slot (slotIndex);
    s.heartbeatMs.store (monotonicMillis(), std::memory_order_relaxed);
    return s.ring.tryAcquireWrite();
}

bool BusPublisher::hasSpace() const noexcept
{
    return slotIndex >= 0 && bus->slot (slotIndex).ring.sizeApprox() < kSlotRingCapacity;
}

void BusPublisher::commit() noexcept
{
    bus->slot (slotIndex).ring.commitWrite();
}

void BusPublisher::markDropped() noexcept
{
    if (slotIndex >= 0)
        bus->slot (slotIndex).droppedFrames.fetch_add (1, std::memory_order_relaxed);
}

} // namespace aimix
