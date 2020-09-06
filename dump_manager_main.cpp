#include "config.h"

#include "dump_internal.hpp"
#include "dump_manager.hpp"
#include "dump_manager_bmc.hpp"
#include "dump_manager_system.hpp"
#include "elog_watch.hpp"
#include "watch.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <sdbusplus/bus.hpp>

int main()
{
    using namespace phosphor::logging;
    using InternalFailure =
        sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

    auto bus = sdbusplus::bus::new_default();
    sd_event* event = nullptr;
    auto rc = sd_event_default(&event);
    if (rc < 0)
    {
        log<level::ERR>("Error occurred during the sd_event_default",
                        entry("RC=%d", rc));
        report<InternalFailure>();
        return rc;
    }
    phosphor::dump::EventPtr eventP{event};
    event = nullptr;

    // Blocking SIGCHLD is needed for calling sd_event_add_child
    sigset_t mask;
    if (sigemptyset(&mask) < 0)
    {
        log<level::ERR>("Unable to initialize signal set",
                        entry("ERRNO=%d", errno));
        return EXIT_FAILURE;
    }

    if (sigaddset(&mask, SIGCHLD) < 0)
    {
        log<level::ERR>("Unable to add signal to signal set",
                        entry("ERRNO=%d", errno));
        return EXIT_FAILURE;
    }

    // Block SIGCHLD first, so that the event loop can handle it
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0)
    {
        log<level::ERR>("Unable to block signal", entry("ERRNO=%d", errno));
        return EXIT_FAILURE;
    }

    // Add sdbusplus ObjectManager for the 'root' path of the DUMP manager.
    sdbusplus::server::manager::manager objManager(bus, DUMP_OBJPATH);
    bus.request_name(DUMP_BUSNAME);

    try
    {
        phosphor::dump::bmc::Manager bmcDumpMgr(
            bus, eventP, BMC_DUMP_OBJPATH, BMC_DUMP_OBJ_ENTRY, BMC_DUMP_PATH);
        // Restore dump d-bus objects.
        bmcDumpMgr.restore();
        phosphor::dump::bmc::internal::Manager mgr(bus, bmcDumpMgr,
                                                   OBJ_INTERNAL);
        phosphor::dump::system::Manager systemDumpMgr(bus, SYSTEM_DUMP_OBJPATH,
                                                      SYSTEM_DUMP_OBJ_ENTRY);
        phosphor::dump::elog::Watch eWatch(bus, mgr);
        bus.attach_event(eventP.get(), SD_EVENT_PRIORITY_NORMAL);

        auto rc = sd_event_loop(eventP.get());
        if (rc < 0)
        {
            log<level::ERR>("Error occurred during the sd_event_loop",
                            entry("RC=%d", rc));
            elog<InternalFailure>();
        }
    }
    catch (InternalFailure& e)
    {
        commit<InternalFailure>();
        return -1;
    }

    return 0;
}
