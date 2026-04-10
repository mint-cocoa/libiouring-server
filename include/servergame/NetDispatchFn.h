#pragma once

#include <servercore/Types.h>
#include <servercore/net/IoCommand.h>

#include <functional>

namespace servergame {

using servercore::ContextId;

/// Callback to dispatch an IoCommand to a specific IoContext.
using NetDispatcher = std::function<void(ContextId, servercore::net::IoCommand)>;

} // namespace servergame
