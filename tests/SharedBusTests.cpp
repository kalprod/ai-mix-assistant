#include "TestFramework.h"
#include "aimix/SharedBus.h"

#include <cstring>
#include <string>

#if ! defined (_WIN32)
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/wait.h>
 #include <unistd.h>
#endif

using namespace aimix;

static std::string uniqueName (const char* base)
{
    return std::string (base) + "_" + std::to_string (currentProcessId());
}

TEST_CASE ("bus: slots can be claimed until full, released and re-claimed")
{
    auto bus = SharedBus::open (uniqueName ("aimix_t_claim"), SharedBus::Backend::ProcessLocal);
    REQUIRE (bus != nullptr);

    for (int i = 0; i < kMaxBusSlots; ++i)
        CHECK (bus->claimSlot (1) == i);
    CHECK (bus->claimSlot (1) == -1);

    const auto gen = bus->slot (5).generation.load();
    bus->releaseSlot (5);
    CHECK (bus->claimSlot (2) == 5);
    CHECK (bus->slot (5).generation.load() == gen + 1);   // engine sees a new owner
}

TEST_CASE ("bus: publisher RAII claims a slot and frees it on destruction")
{
    auto bus = SharedBus::open (uniqueName ("aimix_t_raii"), SharedBus::Backend::ProcessLocal);
    {
        BusPublisher a (bus), b (bus);
        CHECK (a.slot() == 0);
        CHECK (b.slot() == 1);
        CHECK (bus->slot (1).state.load() == (uint32_t) SlotState::Active);
    }
    CHECK (bus->slot (0).state.load() == (uint32_t) SlotState::Free);
    CHECK (bus->slot (1).state.load() == (uint32_t) SlotState::Free);
}

TEST_CASE ("bus: only one master engine at a time, takeover after timeout")
{
    auto bus = SharedBus::open (uniqueName ("aimix_t_master"), SharedBus::Backend::ProcessLocal);
    CHECK (bus->tryBecomeMaster (111, 1000));
    CHECK (! bus->tryBecomeMaster (222, 1500));
    CHECK (bus->isMasterAlive (1500));
    CHECK (bus->tryBecomeMaster (111, 1600));                          // re-entrant for the owner
    CHECK (bus->tryBecomeMaster (222, 1600 + kMasterTimeoutMs + 1));   // stale master replaced
    bus->releaseMaster (222);
    CHECK (! bus->isMasterAlive (9000));
}

#if ! defined (_WIN32)
TEST_CASE ("bus/shm: two mappings of the same segment see each other's frames")
{
    const auto name = uniqueName ("aimix_t_shm");
    SharedBus::unlinkSharedMemory (name);

    auto producerSide = SharedBus::open (name);
    auto consumerSide = SharedBus::open (name);
    REQUIRE (producerSide && consumerSide);
    CHECK (producerSide->backend() == SharedBus::Backend::SharedMemory);
    CHECK (consumerSide->backend() == SharedBus::Backend::SharedMemory);
    CHECK (&producerSide->layout() != &consumerSide->layout());   // genuinely different mappings

    BusPublisher pub (producerSide);
    REQUIRE (pub.isConnected());
    auto* p = pub.acquire();
    REQUIRE (p != nullptr);
    p->trackId = 42;
    p->fftMagnitudeDb[511] = -3.5f;
    pub.commit();

    AnalysisPayload out {};
    CHECK (consumerSide->slot (pub.slot()).ring.tryPop (out));
    CHECK (out.trackId == 42);
    CHECK (out.fftMagnitudeDb[511] == -3.5f);

    SharedBus::unlinkSharedMemory (name);
}

TEST_CASE ("bus/shm: frames cross a real process boundary; dead owner's slot is reclaimed")
{
    const auto name = uniqueName ("aimix_t_fork");
    SharedBus::unlinkSharedMemory (name);
    auto bus = SharedBus::open (name);
    REQUIRE (bus && bus->backend() == SharedBus::Backend::SharedMemory);

    const pid_t child = fork();
    if (child == 0)
    {
        // Child process = a Listener in another (sandboxed) plugin host process.
        auto childBus = SharedBus::open (name);
        const int slot = childBus->claimSlot (monotonicMillis());
        AnalysisPayload p {};
        p.trackId = (uint32_t) slot;
        p.sequence = 7;
        p.phaseCorrelation = -0.25f;
        std::strcpy (p.trackName, "from child");
        const bool ok = slot == 0 && childBus->slot (slot).ring.tryPush (p);
        _exit (ok ? 0 : 1);   // exit WITHOUT releasing the slot, like a crash
    }

    int status = 0;
    waitpid (child, &status, 0);
    REQUIRE (WIFEXITED (status) && WEXITSTATUS (status) == 0);

    AnalysisPayload out {};
    CHECK (bus->slot (0).state.load() == (uint32_t) SlotState::Active);
    CHECK (bus->slot (0).ring.tryPop (out));
    CHECK (out.sequence == 7);
    CHECK (out.phaseCorrelation == -0.25f);
    CHECK (std::string (out.trackName) == "from child");

    // Fill the rest of the bus; the next claim must reclaim the dead child's slot.
    for (int i = 1; i < kMaxBusSlots; ++i)
        CHECK (bus->claimSlot (1) == i);
    CHECK (bus->claimSlot (1) == 0);

    SharedBus::unlinkSharedMemory (name);
}

TEST_CASE ("bus/shm: incompatible segment from another build falls back to process-local")
{
    const auto name = uniqueName ("aimix_t_compat");
    SharedBus::unlinkSharedMemory (name);
    const int fd = shm_open (("/" + name).c_str(), O_RDWR | O_CREAT, 0600);
    REQUIRE (fd >= 0);
    CHECK (ftruncate (fd, 4096) == 0);
    close (fd);

    auto bus = SharedBus::open (name);
    REQUIRE (bus != nullptr);
    CHECK (bus->backend() == SharedBus::Backend::ProcessLocal);
    SharedBus::unlinkSharedMemory (name);
}
#endif
