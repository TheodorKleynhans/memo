#pragma once

#include <memo/overlay/Overlay.hh>

namespace memo
{
  namespace overlay
  {
    namespace kouncil
    {
      /// Serializable configuration for Kouncil.
      struct Configuration
        : public overlay::Configuration
      {
      /*------.
      | Types |
      `------*/
      public:
        /// Ourself.
        using Self = Configuration;
        /// Parent class.
        using Super = overlay::Configuration;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /// Construct a configuration.
        Configuration(std::chrono::seconds eviction_delay = std::chrono::seconds{12000});
      private:
        /// Check construction postconditions.
        void
        _validate() const;

      /*---------.
      | Clonable |
      `---------*/
      public:
        ELLE_CLONABLE();

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        /// Deserialize a Configuration.
        ///
        /// @arg input Source serializer.
        Configuration(elle::serialization::SerializerIn& input);
        /// Serialize Configuration.
        ///
        /// @arg s Source or target serializer.
        void
        serialize(elle::serialization::Serializer& s) override;

      /*--------.
      | Factory |
      `--------*/
      public:
        /// Construct Kouncil from this configuration.
        ///
        /// @arg local       Local server, null if pure client.
        /// @arg doughnut   Owning Doughnut.
        /// @return The built Kouncil.
        std::unique_ptr<memo::overlay::Overlay>
        make(std::shared_ptr<model::doughnut::Local> local,
             model::doughnut::Doughnut* doughnut) override;

        ELLE_ATTRIBUTE_RW(boost::optional<std::chrono::seconds>, eviction_delay)
      };
    }
  }
}
