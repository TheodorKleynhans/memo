#ifndef INFINIT_MODEL_DOUGHNUT_NB_HH
# define INFINIT_MODEL_DOUGHNUT_NB_HH

# include <elle/attribute.hh>

# include <cryptography/rsa/PublicKey.hh>

# include <infinit/model/blocks/ImmutableBlock.hh>
# include <infinit/model/doughnut/fwd.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class NB
        : public blocks::ImmutableBlock
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef NB Self;
        typedef blocks::ImmutableBlock Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        NB(Doughnut* doughnut,
           infinit::cryptography::rsa::PublicKey owner,
           std::string name,
           elle::Buffer data);
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut);
        ELLE_ATTRIBUTE_R(infinit::cryptography::rsa::PublicKey, owner);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE_R(elle::Buffer, signature);
        static
        Address
        address(infinit::cryptography::rsa::PublicKey const& owner,
                std::string const& name);
        using Super::address;

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        virtual
        void
        _seal() override;
        virtual
        blocks::ValidationResult
        _validate() const override;
      private:
        elle::Buffer
        _data_sign() const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        NB(elle::serialization::SerializerIn& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}

#endif
