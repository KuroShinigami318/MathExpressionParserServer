#pragma once

namespace Constants
{
enum class ServerTypes : uint8_t;
}

class IServer;
struct ServerConfig;

class ServerFactory
{
public:
   ServerFactory();
   utils::unique_ref<IServer> CreateServer(Constants::ServerTypes i_serverType, const ServerConfig& i_serverConfig);

private:
   void ValidateServerCreators() const;

private:
   std::unordered_map<Constants::ServerTypes, utils::CallableBound<utils::unique_ref<IServer>(const ServerConfig&)>> m_serverCreators;
};