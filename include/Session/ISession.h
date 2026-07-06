#pragma once
#include "Interface.h"

namespace asio
{
class any_io_executor;
}

class ISession : public Interface
{
protected:
   struct SignalKey;

public:
   virtual const asio::any_io_executor& GetExecuter() = 0;
   virtual void Start() = 0;
   virtual void Stop() = 0;
   virtual void SendResult(const char* i_data, size_t i_size) = 0;

public:
   utils::Signal_public<void(), SignalKey> sig_onSessionClosed;
   utils::Signal_public<void(const char*, size_t), SignalKey> sig_onBytesReceived;
};