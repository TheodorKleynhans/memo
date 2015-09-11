#ifndef INFINIT_MODEL_DOUGHNUT_REMOTE_HH
# define INFINIT_MODEL_DOUGHNUT_REMOTE_HH

# include <reactor/network/tcp-socket.hh>
# include <reactor/network/utp-socket.hh>

# include <protocol/Serializer.hh>
# include <protocol/ChanneledStream.hh>

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/Peer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Remote
        : public Peer
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Remote(Doughnut& doughnut, boost::asio::ip::tcp::endpoint endpoint);
        Remote(Doughnut& doughnut, std::string const& host, int port);
        Remote(Doughnut& doughnut, boost::asio::ip::udp::endpoint endpoint,
               reactor::network::UTPServer& server);
        ELLE_ATTRIBUTE(Doughnut&, doughnut);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::TCPSocket>, socket);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::UTPSocket>, utp_socket);
        ELLE_ATTRIBUTE(protocol::Serializer, serializer);
        ELLE_ATTRIBUTE(protocol::ChanneledStream, channels);

      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block const& block, StoreMode mode) override;
        virtual
        std::unique_ptr<blocks::Block>
        fetch(Address address) const override;
        virtual
        void
        remove(Address address) override;
      private:
      };
    }
  }
}

#endif
