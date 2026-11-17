#include "HostDataService.h"

namespace ps::host
{
namespace
{
HostDataService *g_host = nullptr;
} // namespace

void setHostService(HostDataService *service)
{
    g_host = service;
}

HostDataService *hostService()
{
    return g_host;
}

} // namespace ps::host
