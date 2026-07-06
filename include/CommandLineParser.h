#pragma once
#include "ConstantTypes/ServerTypesFwd.h"
#include "InputParser.h"
#include <variant>

template <>
struct std::hash<InputOptions>
{
   size_t operator()(const InputOptions& i_inputOptions) const;
};

class CommandLineParser
{
public:
   using ValueTypesT = std::variant<Constants::ServerTypes>;
   struct ParsedInput
   {
      Constants::ServerTypes serverType;
      int port;
   };

public:
   CommandLineParser(int argc, char** argv);
   void PrintUsage() const;
   std::optional<ParsedInput> ParseInput();

private:
   void BuildInputOptionMaps();
   void BuildValueMaps();

private:
   InputParser m_inputParser;
   std::unordered_multimap<InputOptions, std::string> m_inputOptionMaps;
   std::unordered_map<std::string, ValueTypesT> m_valueMaps;
};