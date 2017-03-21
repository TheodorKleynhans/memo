#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/grpc++.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/security/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#include <elle/reactor/scheduler.hh>

#include <infinit/grpc/grpc.hh>

ELLE_LOG_COMPONENT("infinit.grpc");

namespace infinit
{
  namespace grpc
  {
    void serve_grpc(infinit::model::Model& dht,
                    boost::optional<elle::reactor::filesystem::FileSystem&> fs,
                    model::Endpoint ep,
                    int* effective_port)
    {
      ELLE_TRACE("serving grpc on %s", ep);
      std::unique_ptr< ::grpc::Service> fs_service;
      if (fs)
        fs_service = filesystem_service(*fs);
      auto ds = doughnut_service(dht);
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials(),
        effective_port);
      builder.RegisterService(ds.get());
      if (fs_service)
        builder.RegisterService(fs_service.get());
      auto server = builder.BuildAndStart();
      elle::reactor::sleep();
    }
  }
}