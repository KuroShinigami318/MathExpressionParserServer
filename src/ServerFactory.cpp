#include "ServerStdafx.h"
#include "ServerFactory.h"
#include "ConstantTypes/ServerTypes.h"
#include "TCPServer.h"

#include "MacrosUtils.h"

namespace
{
utils::unique_ref<IServer> CreateTCPServer(const ServerConfig& i_serverConfig)
{
   return utils::make_unique<TCPServer>(i_serverConfig);
}
}

ServerFactory::ServerFactory()
{
   m_serverCreators.try_emplace(Constants::ServerTypes::TCP, CreateTCPServer);

   ValidateServerCreators();
}

utils::unique_ref<IServer> ServerFactory::CreateServer(Constants::ServerTypes i_serverType, const ServerConfig& i_serverConfig)
{
   return m_serverCreators.at(i_serverType)(i_serverConfig);
}

void ServerFactory::ValidateServerCreators() const
{
   NOT_RELEASE(for (Constants::ServerTypes serverType = Constants::ServerTypes::_FIRST; serverType < Constants::ServerTypes::_COUNT; ++serverType)
   {
      ASSERT_PLAIN_MSG(m_serverCreators.find(serverType) != m_serverCreators.end(), "Server creator not found for server type: {}", serverType);
   })
}