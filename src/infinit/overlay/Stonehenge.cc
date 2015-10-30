#include <infinit/overlay/Stonehenge.hh>

#include <iterator>

#include <elle/Error.hh>
#include <elle/assert.hh>
#include <elle/log.hh>

#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh> // FIXME

ELLE_LOG_COMPONENT("infinit.overlay.Stonehenge");

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Stonehenge::Stonehenge(elle::UUID node_id,
                           Hosts hosts,
                           model::doughnut::Doughnut* doughnut)
      : Overlay(std::move(node_id))
      , _hosts(std::move(hosts))
    {
      if (this->_hosts.empty())
        throw elle::Error("empty peer list");
      this->doughnut(doughnut);
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<Overlay::Member>
    Stonehenge::_lookup(model::Address address,
                        int n,
                        Operation) const
    {
      // Use modulo on the address to determine the owner and yield the n
      // following nodes.
      return reactor::generator<Overlay::Member>( [this, address, n]
        (reactor::Generator<Overlay::Member>::yielder const& yield)
      {
        int size = this->_hosts.size();
        ELLE_ASSERT_LTE(n, size);
        auto owner = address.value()[0] % size;
        ELLE_ASSERT(this->doughnut());
        int i = owner;
        do
        {
          ELLE_DEBUG("%s: yield %s", *this, this->_hosts[i]);
          // FIXME: don't always yield Paxos
          yield(
            Overlay::Member(
              new infinit::model::doughnut::consensus::Paxos::RemotePeer(
                const_cast<model::doughnut::Doughnut&>(*this->doughnut()),
                /* FIXME BEARCLAW */ model::Address(),
                this->_hosts[i])));
          i = (i + 1) % size;
        }
        while (i != (owner + n) % size);
        });
    }

    Overlay::Member
    Stonehenge::_lookup_node(model::Address address)
    {
      return Overlay::Member();
    }

    StonehengeConfiguration::StonehengeConfiguration()
      : overlay::Configuration()
    {}

    StonehengeConfiguration::StonehengeConfiguration
      (elle::serialization::SerializerIn& input)
      : Configuration()
    {
      this->serialize(input);
    }

    void
    StonehengeConfiguration::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("hosts", this->hosts);
    }

    std::unique_ptr<infinit::overlay::Overlay>
    StonehengeConfiguration::make(NodeEndpoints const&, bool,
     model::doughnut::Doughnut* doughnut)
    {
      Stonehenge::Hosts hosts;
      for (auto const& host: this->hosts)
      {
        size_t p = host.find_first_of(':');
        if (p == host.npos)
          throw elle::Error(
            elle::sprintf("failed to parse host:port: %s", host));
        hosts.emplace_back(
          boost::asio::ip::address::from_string(host.substr(0, p)),
          std::stoi(host.substr(p + 1)));
      }
      return elle::make_unique<infinit::overlay::Stonehenge>(
        this->node_id(), hosts, doughnut);
    }

    static const elle::serialization::Hierarchy<Configuration>::
    Register<StonehengeConfiguration> _registerStonehengeConfiguration("stonehenge");
  }
}
