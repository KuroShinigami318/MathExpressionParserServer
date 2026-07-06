#pragma once

namespace asio
{
class io_context;
}

struct ServerConfig
{
   ServerConfig(asio::io_context& i_ioContext, const int& i_port)
      : ioContext(i_ioContext)
      , port(i_port)
   {
   }

   asio::io_context& ioContext;
   int port;
};