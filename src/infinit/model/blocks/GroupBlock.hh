#ifndef INFINIT_MODEL_BLOCKS_GROUP_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_GROUP_BLOCK_HH

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/User.hh>
# include <infinit/model/blocks/ACLBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class GroupBlock
        : public ACLBlock
      {
      public:
        typedef GroupBlock Self;
        typedef ACLBlock Super;

        GroupBlock(GroupBlock const& other);
      protected:
        GroupBlock(Address);
        GroupBlock(Address, elle::Buffer data);
        friend class infinit::model::Model;

      public:
        virtual
        void
        add_member(model::User const& user);
        virtual
        void
        remove_member(model::User const& user);
        virtual
        cryptography::rsa::PublicKey
        current_key();
        virtual
        std::vector<cryptography::rsa::KeyPair>
        all_keys();

      public:
        GroupBlock(elle::serialization::Serializer& input);
      };
    }
  }
}



#endif