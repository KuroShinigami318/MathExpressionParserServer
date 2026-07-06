#pragma once
#include "Interface.h"
#include "ConstantTypes/ActionResultTypesFwd.h"

class ISession;

class IAction : public Interface
{
protected:
   struct SignalKey;

public:
   virtual ActionResultTypeFwd RegisterSession(ISession& i_session) = 0;
   virtual ActionResultTypeFwd UnregisterSession(ISession& i_session) = 0;
   virtual ActionResultTypeFwd ProcessRawData(ISession& i_session, const char* i_data, size_t i_size) = 0;

public:
   utils::Signal_public<void(ISession&, const std::string&), SignalKey> sig_onActionCompleted;
};