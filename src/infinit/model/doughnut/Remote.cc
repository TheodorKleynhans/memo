#include <infinit/model/doughnut/Remote.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/utils.hh>
#include <elle/bench.hh>

#include <reactor/Scope.hh>
#include <reactor/thread.hh>
#include <reactor/scheduler.hh>

#include <infinit/RPC.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote")

#define BENCH(name)                                      \
  static elle::Bench bench("bench.remote." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-----.
      | Auth |
      `-----*/

      Remote::Auth::Auth(Address id,
                         Challenge challenge,
                         Passport passport)
        : id(std::move(id))
        , challenge(std::move(challenge))
        , passport(std::move(passport))
      {}

      /*-------------.
      | Construction |
      `-------------*/

      Remote::Remote(Doughnut& dht,
                     std::shared_ptr<Dock::Connection> connection)
        : Super(dht, connection->location().id())
        , _connection()
        , _connected()
        , _connecting_since(std::chrono::system_clock::now())
      {
        ELLE_TRACE_SCOPE("%s: construct", this);
        ELLE_ASSERT_NEQ(connection->location().id(), Address::null);
        this->connection(std::move(connection));
        this->_connected.changed().connect(
          [this] (bool opened)
          {
            if (opened)
            {
              this->_disconnected_exception = {};
              if (!this->_connected.exception())
                this->Peer::connected()();
            }
            else
              this->Peer::disconnected()();
          });
      }

      void
      Remote::connection(std::shared_ptr<Dock::Connection> connection)
      {
        this->_connection = std::move(connection);
        this->_connections.clear();
        this->_connections.emplace_back(
          this->_connection->on_connection().connect(
            // Make sure we note this peer is connected before anything else
            // otherwise other slots trying to perform RPCs will deadlock
            // (e.g. Kouncil).
            boost::signals2::at_front,
            [this]
            {
              ELLE_TRACE("%s: connected", this);
              this->_connected.open();
            }));
        this->_connections.emplace_back(
          this->_connection->on_disconnection().connect(
            [this]
            {
              if (!this->_connection->connected())
              {
                ELLE_TRACE("%s: disconnected with exception",
                           this, elle::exception_string(
                             this->_connection->disconnected_exception()));
                this->_connected.raise(
                  this->_connection->disconnected_exception());
              }
              else
              {
                ELLE_TRACE_SCOPE("%s: disconnected", this);
                auto hold = this->shared_from_this();
                this->_connected.close();
              }
            }));
        auto connected =
          this->_connection->connected() && !this->_connection->disconnected();
        if (!this->_connected && connected)
        {
          ELLE_TRACE("%s: connected", this);
          this->_connected.open();
        }
        else if (this->_connected && !connected)
        {
          ELLE_TRACE("%s: disconnected YYY", this);
          this->_connected.close();
        }
      }

      Remote::~Remote()
      {
        ELLE_TRACE_SCOPE("%s: destruct", this);
        this->_cleanup();
        this->_doughnut.dock()._peer_cache.erase(this->_cache_iterator);
      }

      void
      Remote::_cleanup()
      {
        this->_connection->disconnect();
      }

      Endpoints const&
      Remote::endpoints() const
      {
        return this->_connection->location().endpoints();
      }

      elle::Buffer const&
      Remote::credentials() const
      {
        return this->_connection->credentials();
      }

      void
      Remote::reconnect()
      {
        this->_connection->disconnect();
        if (this->_connection->connected())
          this->_disconnected_exception =
            this->_connection->disconnected_exception();
        this->_connecting_since = std::chrono::system_clock::now();
        this->_doughnut.dock().connect(this->_connection->location());
      }

      /*-----------.
      | Networking |
      `-----------*/

      void
      Remote::connect(elle::DurationOpt timeout)
      {
        if (!this->_connected)
        {
          if (this->_connection->disconnected())
          {
            ELLE_TRACE_SCOPE("%s: restart closed connection to %s",
                             this, this->_connection->location());
            this->connection(this->doughnut().dock().connect(
                               this->_connection->location()));
          }
          ELLE_DEBUG_SCOPE(
            "%s: wait for connection with timeout %s", this, timeout);
          if (!reactor::wait(this->_connected, timeout))
            throw reactor::network::TimeOut();
        }
      }

      void
      Remote::disconnect()
      {
        this->_connection->disconnect();
      }

      /*-------.
      | Blocks |
      `-------*/

      void
      Remote::store(blocks::Block const& block, StoreMode mode)
      {
        BENCH("store");
        ELLE_ASSERT(&block);
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        auto store = make_rpc<void (blocks::Block const&, StoreMode)>("store");
        store.set_context<Doughnut*>(&this->_doughnut);
        store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Remote::_fetch(Address address,
                    boost::optional<int> local_version) const
      {
        BENCH("fetch");
        auto fetch = elle::unconst(this)->make_rpc<
          std::unique_ptr<blocks::Block>(Address,
                                         boost::optional<int>)>("fetch");
        fetch.set_context<Doughnut*>(&this->_doughnut);
        return fetch(std::move(address), std::move(local_version));
      }

      void
      Remote::remove(Address address, blocks::RemoveSignature rs)
      {
        BENCH("remove");
        ELLE_TRACE_SCOPE("%s: remove %x", *this, address);
        if (this->_doughnut.version() >= elle::Version(0, 4, 0))
        {
          auto remove = make_rpc<void (Address, blocks::RemoveSignature)>
            ("remove");
          remove.set_context<Doughnut*>(&this->_doughnut);
          remove(address, rs);
        }
        else
        {
          auto remove = make_rpc<void (Address)>
            ("remove");
          remove(address);
        }
      }

      /*-----.
      | Keys |
      `-----*/

      std::vector<cryptography::rsa::PublicKey>
      Remote::_resolve_keys(std::vector<int> ids)
      {
        static elle::Bench bench("bench.remote_key_cache_hit", 1000_sec);
        {
          std::vector<int> missing;
          for (auto id: ids)
            if (this->key_hash_cache().get<1>().find(id) ==
                this->key_hash_cache().get<1>().end())
              missing.emplace_back(id);
          if (missing.size())
          {
            bench.add(0);
            ELLE_TRACE("%s: fetch %s keys by ids", this, missing.size());
            auto rpc = this->make_rpc<std::vector<cryptography::rsa::PublicKey>(
              std::vector<int> const&)>("resolve_keys");
            auto missing_keys = rpc(missing);
            if (missing_keys.size() != missing.size())
              elle::err("resolve_keys for %s keys on %s gave %s replies",
                        missing.size(), this, missing_keys.size());
            auto id_it = missing.begin();
            auto key_it = missing_keys.begin();
            for (; id_it != missing.end(); ++id_it, ++key_it)
              this->key_hash_cache().emplace(*id_it, std::move(*key_it));
          }
          else
            bench.add(1);
        }
        std::vector<cryptography::rsa::PublicKey> res;
        for (auto id: ids)
        {
          auto it = this->key_hash_cache().get<1>().find(id);
          ELLE_ASSERT(it != this->key_hash_cache().get<1>().end());
          res.emplace_back(*it->key);
        }
        return res;
      }

      std::unordered_map<int, cryptography::rsa::PublicKey>
      Remote::_resolve_all_keys()
      {
        using Keys = std::unordered_map<int, cryptography::rsa::PublicKey>;
        auto res = this->make_rpc<Keys()>("resolve_all_keys")();
        for (auto const& key: res)
          if (this->key_hash_cache().get<1>().find(key.first) ==
              this->key_hash_cache().get<1>().end())
            this->key_hash_cache().emplace(key.first, key.second);
        return res;
      }

      KeyCache const&
      Remote::key_hash_cache() const
      {
        return ELLE_ENFORCE(this->_connection)->key_hash_cache();
      }

      KeyCache&
      Remote::key_hash_cache()
      {
        return ELLE_ENFORCE(this->_connection)->key_hash_cache();
      }
    }
  }
}
