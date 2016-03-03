#include <infinit/filesystem/filesystem.hh>
#include <infinit/filesystem/AnyBlock.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/umbrella.hh>

#include <infinit/model/MissingBlock.hh>

#include <boost/filesystem/fstream.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/unordered_map.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>
#include <reactor/exception.hh>

#include <cryptography/hash.hh>

#include <infinit/utility.hh>
#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/serialization.hh>


#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif

ELLE_LOG_COMPONENT("infinit.filesystem");

namespace rfs = reactor::filesystem;
namespace dht = infinit::model::doughnut;

namespace infinit
{
  namespace filesystem
  {
    FileSystem::FileSystem(std::string const& volume_name,
                           std::shared_ptr<model::Model> model,
                           boost::optional<boost::filesystem::path> state_dir)
      : _block_store(std::move(model))
      , _single_mount(false)
      , _volume_name(volume_name)
      , _state_dir(state_dir)
    {
      auto& dht = dynamic_cast<model::doughnut::Doughnut&>(
        *this->_block_store.get());
      auto passport = dht.passport();
      this->_read_only = !passport.allow_write();
      this->_network_name = passport.network();
#ifndef INFINIT_WINDOWS
      reactor::scheduler().signal_handle
        (SIGUSR1, [this] { this->print_cache_stats();});
#endif
    }

    void
    FileSystem::unchecked_remove(model::Address address)
    {
      try
      {
        _block_store->remove(address);
      }
      catch (model::MissingBlock const&)
      {
        ELLE_DEBUG("%s: block was not published", *this);
      }
      catch (elle::Exception const& e)
      {
        ELLE_ERR("%s: unexpected exception: %s", *this, e.what());
        throw;
      }
      catch (...)
      {
        ELLE_ERR("%s: unknown exception", *this);
        throw;
      }
    }

    void
    FileSystem::store_or_die(model::blocks::Block& block,
                             model::StoreMode mode,
                             std::unique_ptr<model::ConflictResolver> resolver)
    {
      block.seal();
      auto copy = block.clone();
      store_or_die(std::move(copy), mode, std::move(resolver));
    }

    void
    FileSystem::store_or_die(std::unique_ptr<model::blocks::Block> block,
                             model::StoreMode mode,
                             std::unique_ptr<model::ConflictResolver> resolver)
    {
      ELLE_TRACE_SCOPE("%s: store or die: %s", *this, *block);
      auto address = block->address();
      try
      {
        this->_block_store->store(std::move(block), mode, std::move(resolver));
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch (infinit::storage::InsufficientSpace const& e)
      {

        ELLE_TRACE("store_or_die: %s", e.what());
        THROW_ENOSPC;
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("unexpected exception storing %x: %s",
                  address, e);
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::Block>
    FileSystem::fetch_or_die(model::Address address,
                             boost::optional<int> local_version,
                             Node* node)
    {
      try
      {
        return this->_block_store->fetch(address, std::move(local_version));
      }
      catch(reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        throw rfs::Error(EACCES, elle::sprintf("%s", e));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("data not found fetching \"/%s\": %s",
                  node ? node->full_path().string() : "", mb);
        if (node)
          node->_remove_from_cache();
        throw rfs::Error(EIO, elle::sprintf("%s", mb));
      }
      catch (elle::serialization::Error const& se)
      {
        ELLE_WARN("serialization error fetching %f: %s", address, se);
        throw rfs::Error(EIO, elle::sprintf("%s", se));
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected exception fetching %f: %s", address, e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected exception on fetching %f: %s", address, e.what());
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::MutableBlock>
    FileSystem::unchecked_fetch(model::Address address)
    {
      try
      {
        return elle::cast<model::blocks::MutableBlock>::runtime
          (_block_store->fetch(address));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("Unexpected storage result: %s", mb);
      }
      return {};
    }

    void
    FileSystem::print_cache_stats()
    {
      auto root = std::dynamic_pointer_cast<Directory>(filesystem()->path("/"));
      CacheStats stats;
      memset(&stats, 0, sizeof(CacheStats));
      root->cache_stats(stats);
      std::cerr << "Statistics:\n"
      << stats.directories << " dirs\n"
      << stats.files << " files\n"
      << stats.blocks <<" blocks\n"
      << stats.size << " bytes"
      << std::endl;
    }

    std::shared_ptr<rfs::Path>
    FileSystem::path(std::string const& path)
    {
      ELLE_TRACE_SCOPE("%s: fetch root", *this);
      // In the infinit filesystem, we never query a path other than the root.
      ELLE_ASSERT_EQ(path, "/");
      auto root = this->_root_block();
      ELLE_ASSERT(!!root);
      auto acl_root =  elle::cast<ACLBlock>::runtime(std::move(root));
      ELLE_ASSERT(!!acl_root);
      auto res =
        std::make_shared<Directory>(nullptr, *this, "", acl_root->address());
      res->_fetch(std::move(acl_root));
      return res;
    }

    std::unique_ptr<MutableBlock>
    FileSystem::_root_block()
    {
      boost::optional<boost::filesystem::path> root_cache;
      if (this->_state_dir)
      {
        auto root_block_cache_dir =
          *this->_state_dir / this->_network_name / this->_volume_name;
        if (!boost::filesystem::exists(root_block_cache_dir))
          boost::filesystem::create_directories(root_block_cache_dir);
        root_cache = root_block_cache_dir / "root_block";
        ELLE_DEBUG("root block cache: %s", root_cache);
      }
      bool migrate = true;
      auto dn = std::dynamic_pointer_cast<dht::Doughnut>(this->_block_store);
      auto const bootstrap_name = this->_volume_name + ".root";
      Address addr = dht::NB::address(
        *dn->owner(), bootstrap_name, dn->version());
      while (true)
      {
        try
        {
          ELLE_DEBUG_SCOPE("fetch root bootstrap block at %f", addr);
          auto block = this->_block_store->fetch(addr);
          addr = Address(
            Address::from_string(block->data().string().substr(2)).value(),
            model::flags::mutable_block,
            false);
          ELLE_DEBUG_SCOPE("fetch root block at %f", addr);
          break;
        }
        catch (model::MissingBlock const& e)
        {
          if (migrate)
            try
            {
              migrate = false;
              Address old_addr = dht::NB::address(
                *dn->owner(), bootstrap_name, elle::Version(0, 4, 0));
              auto old = std::dynamic_pointer_cast<dht::NB>(
                this->_block_store->fetch(old_addr));
              ELLE_LOG_SCOPE(
                "migrate old bootstrap block from %s to %s", old_addr, addr);
              auto nb = elle::make_unique<dht::NB>(
                dn.get(), dn->owner(), bootstrap_name,
                old->data(), old->signature());
              this->store_or_die(std::move(nb), model::STORE_INSERT);
              continue;
            }
            catch (model::MissingBlock const&)
            {}
          if (*dn->owner() == dn->keys().K())
          {
            if (root_cache && boost::filesystem::exists(*root_cache))
              ELLE_TRACE("root block marker is set, refusing to recreate");
            else
            {
              std::unique_ptr<MutableBlock> mb;
              ELLE_TRACE("create missing root block")
              {
                mb = dn->make_block<ACLBlock>();
                this->store_or_die(mb->clone(), model::STORE_INSERT);
              }
              ELLE_TRACE("create missing root bootstrap block")
              {
                auto saddr = elle::sprintf("%x", mb->address());
                elle::Buffer baddr = elle::Buffer(saddr.data(), saddr.size());
                auto nb = elle::make_unique<dht::NB>(
                  dn.get(), dn->owner(), bootstrap_name, baddr);
                this->store_or_die(std::move(nb), model::STORE_INSERT);
                if (root_cache)
                  boost::filesystem::ofstream(*root_cache) << saddr;
              }
              on_root_block_create();
              return mb;
            }
          }
          reactor::sleep(1_sec);
        }
      }
      if (root_cache && !boost::filesystem::exists(*root_cache))
      {
        boost::filesystem::ofstream ofs(*root_cache);
        elle::fprintf(ofs, "%x", addr);
      }
      return elle::cast<MutableBlock>::runtime(fetch_or_die(addr));
    }

    std::pair<bool, bool>
    FileSystem::get_permissions(model::blocks::Block const& block)
    {
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(block_store());
      auto acb = dynamic_cast<const model::doughnut::ACB*>(&block);
      bool r = false, w = false;
      ELLE_ASSERT(acb);
      if (dn->keys().K() == *acb->owner_key())
        return std::make_pair(true, true);
      for (auto const& e: acb->acl_entries())
      {
        if (e.read <= r && e.write <= w)
          continue; // this entry doesnt add any perm
        if (e.key == dn->keys().K())
        {
          r = r || e.read;
          w = w || e.write;
          if (r && w)
            return std::make_pair(r, w);
        }
      }
      int idx = 0;
      for (auto const& e: acb->acl_group_entries())
      {
        if (e.read <= r && e.write <= w)
        {
          ++idx;
          continue; // this entry doesnt add any perm
        }
        try
        {
          model::doughnut::Group g(*dn, e.key);
          auto keys = g.group_keys();
          if (acb->group_version()[idx] < signed(keys.size()))
          {
            r = r || e.read;
            w = w || e.write;
            if (r && w)
              return std::make_pair(r, w);
          }
        }
        catch (elle::Error const& e)
        {
          ELLE_DEBUG("error accessing group: %s", e);
        }
        ++idx;
      }
      auto wp = elle::unconst(acb)->get_world_permissions();
      r = r || wp.first;
      w = w || wp.second;
      return std::make_pair(r, w);
    }

    void
    FileSystem::ensure_permissions(model::blocks::Block const& block,
                                   bool r, bool w)
    {
      auto perms = get_permissions(block);
      if (perms.first < r || perms.second < w)
        throw rfs::Error(EACCES, "Access denied.");
    }
  }
}
