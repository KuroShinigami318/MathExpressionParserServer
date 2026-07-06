#include "ServerStdafx.h"
#include "ServerFactory.h"
#include "ServerConfig.h"
#include "CommandLineParser.h"

#include "asio/io_context.hpp"

int main(int argc, char* argv[])
{
   try
   {
      CommandLineParser commandLineParser(argc, argv);
      std::optional<CommandLineParser::ParsedInput> parsedInput = commandLineParser.ParseInput();
      if (!parsedInput.has_value())
      {
         commandLineParser.PrintUsage();
         return 1;
      }

      asio::io_context io_context;

      ServerFactory serverFactory;
      ServerConfig serverConfig(io_context, parsedInput->port);
      utils::unique_ref<IServer> server = serverFactory.CreateServer(parsedInput->serverType, serverConfig);

      io_context.run();
   }
   catch (std::exception& e)
   {
      std::cerr << "Exception: " << e.what() << "\n";
   }

   return 0;
}