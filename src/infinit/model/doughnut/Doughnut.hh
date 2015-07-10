#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH

# include <memory>

# include <cryptography/KeyPair.hh>

# include <infinit/model/Model.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Doughnut // Doughnut. DougHnuT. Get it ?
        : public Model
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Doughnut(infinit::cryptography::KeyPair keys,
                 std::unique_ptr<overlay::Overlay> overlay,
                 bool plain = false, int write_n = 1, int read_n = 1);
        ELLE_ATTRIBUTE(std::unique_ptr<overlay::Overlay>, overlay)
        ELLE_ATTRIBUTE_R(infinit::cryptography::KeyPair, keys);

      protected:
        virtual
        std::unique_ptr<blocks::MutableBlock>
        _make_mutable_block() const override;
        virtual
        std::unique_ptr<blocks::ImmutableBlock>
        _make_immutable_block(elle::Buffer content) const override;
        virtual
        std::unique_ptr<blocks::ACLBlock>
        _make_acl_block() const override;
        virtual
        std::unique_ptr<model::User>
        _make_user(elle::Buffer const& data) const;
        virtual
        void
        _store(blocks::Block& block, StoreMode mode) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address) const override;
        virtual
        void
        _remove(Address address) override;
        ELLE_ATTRIBUTE_R(std::vector<std::unique_ptr<Peer>>, peers);
        friend class Local;

      private:
        std::unique_ptr<Peer>
        _owner(Address const& address, overlay::Operation op) const;
        ELLE_ATTRIBUTE(bool, plain);
        ELLE_ATTRIBUTE(int, write_n);
        ELLE_ATTRIBUTE(int, read_n);
      };
    }
  }
}

#endif
