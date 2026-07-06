#include "ServerStdafx.h"
#include "CommandLineParser.h"
#include "ConstantTypes/ServerTypes.h"

namespace
{
constexpr const char* k_helpOption = "--help";
constexpr const char* k_helpShortOption = "-h";
constexpr const char* k_protocolOption = "--protocol";
constexpr const char* k_protocolShortOption = "-pro";
constexpr const char* k_portOption = "--port";
constexpr const char* k_portShortOption = "-P";
constexpr const char* k_tcpProtocol = "tcp";
constexpr const char* k_defaultPortValue = "8081";

struct ValueVisitor
{
   ValueVisitor(CommandLineParser::ParsedInput& i_parsedInput)
      : m_parsedInput(i_parsedInput)
   {
   }

   template <typename T>
   void operator()(const T&) const
   {
      CRASH("Unsupported value type!");
   }

   void operator()(const Constants::ServerTypes& i_serverType) const
   {
      m_parsedInput.serverType = i_serverType;
   }

   CommandLineParser::ParsedInput& m_parsedInput;
};
}

bool operator==(const InputOptions& lhs, const InputOptions& rhs)
{
   return lhs.options == rhs.options;
}

size_t std::hash<InputOptions>::operator()(const InputOptions& i_inputOptions) const
{
   size_t hashValue = 0;
   for (const std::string& option : i_inputOptions.options)
   {
      hash_combine(hashValue, option);
   }
   return hashValue;
}

CommandLineParser::CommandLineParser(int argc, char** argv)
    : m_inputParser(argc, argv)
{
    BuildInputOptionMaps();
    BuildValueMaps();
}

void CommandLineParser::PrintUsage() const
{
   std::cout << "Usage: MathExpressionParserServer [Options]" << std::endl;
   std::cout << "Options:" << std::endl;
   for (const auto& [inputOptions, value] : m_inputOptionMaps)
   {
      std::cout << utils::Format("  {} <value> (default: {})", inputOptions.options[0], value) << std::endl;
   }
}

std::optional<CommandLineParser::ParsedInput> CommandLineParser::ParseInput()
{
   InputOptions helpOptions({ k_helpOption, k_helpShortOption });
   if (m_inputParser.ContainsInputOptions(helpOptions))
   {
      return std::nullopt;
   }

   ParsedInput parsedInput{ Constants::ServerTypes::_COUNT, -1 };
   InputOptions protocolOptions({ k_protocolOption, k_protocolShortOption });
   if (InputParser::position_t parsedPosition = m_inputParser.HaveInputOptions(protocolOptions))
   {
      std::string protocolValue = m_inputParser.ExtractValue(parsedPosition);
      auto range = m_inputOptionMaps.equal_range(protocolOptions);
      for (; range.first != range.second; ++range.first)
      {
         if (range.first->second == protocolValue)
         {
            auto expectedProtocolIt = m_valueMaps.find(protocolValue);
            if (expectedProtocolIt != m_valueMaps.end())
            {
               std::visit(ValueVisitor(parsedInput), expectedProtocolIt->second);
               break;
            }
            else
            {
               CRASH("Unexpected protocol value! Did you forget to add it to the value map?");
            }
         }
      }
   }

   if (parsedInput.serverType == Constants::ServerTypes::_COUNT)
   {
      std::cout << "Warning: No protocol specified. Defaulting to TCP." << std::endl;
      parsedInput.serverType = Constants::ServerTypes::TCP;
   }

   if (InputParser::position_t parsedPosition = m_inputParser.HaveInputOptions(InputOptions({ k_portOption, k_portShortOption })))
   {
      std::string portValue = m_inputParser.ExtractValue(parsedPosition);
      try
      {
         parsedInput.port = std::stoi(portValue);
      }
      catch (const std::exception& e)
      {
         std::cerr << "Error: Invalid port value. Please provide a valid integer." << std::endl;
         return std::nullopt;
      }
   }
   else
   {
      std::cout << utils::Format("Warning: No port specified. Defaulting to {}.", k_defaultPortValue) << std::endl;
      parsedInput.port = std::stoi(k_defaultPortValue);
   }

   return parsedInput;
}

void CommandLineParser::BuildInputOptionMaps()
{
   m_inputOptionMaps.insert({ InputOptions({ k_protocolOption, k_protocolShortOption }), k_tcpProtocol });
   m_inputOptionMaps.insert({ InputOptions({ k_portOption, k_portShortOption }), k_defaultPortValue });
}

void CommandLineParser::BuildValueMaps()
{
   m_valueMaps.insert({ k_tcpProtocol, Constants::ServerTypes::TCP });
}