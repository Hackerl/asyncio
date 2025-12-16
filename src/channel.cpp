#include <asyncio/channel.h>

Z_DEFINE_ERROR_CATEGORY_INSTANCES(
    asyncio::TrySendError,
    asyncio::SendSyncError,
    asyncio::SendError,
    asyncio::TryReceiveError,
    asyncio::ReceiveSyncError,
    asyncio::ReceiveError,
    asyncio::ChannelError
)
