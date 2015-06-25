#ifndef INFINIT_MODEL_BLOCKS_MUTABLE_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_MUTABLE_BLOCK_HH

# include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class MutableBlock
        : public Block
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef MutableBlock Self;
        typedef Block Super;

      /*-------------.
      | Construction |
      `-------------*/
      protected:
        MutableBlock(Address address);
        MutableBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;
        bool _data_changed;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        MutableBlock(elle::serialization::Serializer& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
      private:
        void
        _serialize(elle::serialization::Serializer& s);

      /*--------.
      | Content |
      `--------*/
      public:
        using Super::data;
        void
        data(elle::Buffer data);
        void
        data(std::function<void (elle::Buffer&)> transformation);
      };
    }
  }
}

#endif
