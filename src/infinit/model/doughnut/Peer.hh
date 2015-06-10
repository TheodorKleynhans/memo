#ifndef INFINIT_MODEL_DOUGHNUT_PEER_HH
# define INFINIT_MODEL_DOUGHNUT_PEER_HH

# include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Peer
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Peer();
        virtual
        ~Peer();

      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block& block) = 0;
        virtual
        std::unique_ptr<blocks::Block>
        fetch(Address address) const = 0;
        virtual
        void
        remove(Address address) = 0;
      };
    }
  }
}

#endif
