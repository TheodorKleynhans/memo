#include <elle/log.hh>

#include <infinit/overlay/Overlay.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Overlay");

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Overlay::Overlay(elle::UUID node_id)
      : _node_id(std::move(node_id))
      , _doughnut(nullptr)
    {}

    /*-------.
    | Lookup |
    `-------*/

    void
    Overlay::register_local(std::shared_ptr<model::doughnut::Local> local)
    {}

    Overlay::Members
    Overlay::lookup(model::Address address, int n, Operation op, bool strict) const
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %s", *this, n, address);
      auto res = this->_lookup(address, n, op);
      if (strict && signed(res.size()) < n)
        throw elle::Error(elle::sprintf("%s: got only %s of the %s requested nodes",
                                        *this, res.size(), n));
      if (strict && signed(res.size()) > n)
      { // this should not happen at least with kelips
        ELLE_WARN("%s: got %s results with %s requested", *this, res.size(), n);
        res.resize(n);
      }
      return res;
    }

    Overlay::Member
    Overlay::lookup(model::Address address, Operation op) const
    {
      return this->lookup(address, 1, op)[0];
    }

    void
    Configuration::join()
    {
      this->_node_id = elle::UUID::random();
    }

    void
    Configuration::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("node_id", this->_node_id);
    }
  }
}
