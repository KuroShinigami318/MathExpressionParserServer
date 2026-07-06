#pragma once

class Interface
{
   Interface(const Interface&) = delete;
   Interface& operator=(const Interface&) = delete;
public:
   Interface() = default;
   virtual ~Interface() = 0;
};

inline Interface::~Interface() = default;