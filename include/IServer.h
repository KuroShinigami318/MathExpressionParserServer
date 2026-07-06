#pragma once
#include "Interface.h"

class IServer : public Interface
{
public:
   virtual void Start() = 0;
};