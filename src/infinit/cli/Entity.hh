#pragma once

#include <elle/attribute.hh>

#include <das/bound-method.hh>
#include <das/cli.hh>

#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>

namespace infinit
{
  namespace cli
  {
    template <typename Self>
    class Entity;

    template <typename Self>
    using EntityCallable =
      decltype(
        das::named::function(
          das::bind_method(std::declval<Entity<Self>&>(), cli::call),
          help = false));

    template <typename Self>
    class Entity
      : public EntityCallable<Self>
    {
    public:
      Entity(Infinit& infinit);
      void
      help(std::ostream& s);
      void
      call(bool help);
      template <typename Symbol, typename ... Args>
      static
      auto
      binding(Symbol const&, Args&& ... args)
        -> decltype(das::named::function(
                      das::bind_method<Self, Symbol>(std::declval<Self&>()),
                      std::forward<Args>(args)...));
      template <typename Symbol, typename ... Args>
      auto
      bind(Symbol const& s, Args&& ... args)
        -> decltype(binding(s, std::forward<Args>(args)...))
      {
        return das::named::function(
          das::bind_method<Self, Symbol>(static_cast<Self&>(*this)),
          std::forward<Args>(args)...);
      }
      ELLE_ATTRIBUTE_R(Infinit&, cli);
      ELLE_ATTRIBUTE_R(das::cli::Options, options, protected);
    };
  }
}
