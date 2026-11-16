#include "ScriptHostService.h"

namespace ps::scripting
{
namespace
{
HostService *g_host = nullptr;
} // namespace

void setHostService(HostService *service)
{
    g_host = service;
}

HostService *hostService()
{
    return g_host;
}

} // namespace ps::scripting
