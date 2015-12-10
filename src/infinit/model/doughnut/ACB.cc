#include <infinit/model/doughnut/ACB.hh>

#include <boost/iterator/zip_iterator.hpp>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh>
#include <elle/utility/Move.hh>

#include <das/model.hh>
#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>
#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/SecretKey.hh>
#include <cryptography/hash.hh>

#include <reactor/exception.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/GroupBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/serialization.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.ACB");

DAS_MODEL_FIELDS(infinit::model::doughnut::ACLEntry,
                 (key, read, write, token));

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      DAS_MODEL_DEFINE(ACLEntry, (key, read, write, token),
                       DasACLEntry);
      DAS_MODEL_DEFINE(ACLEntry, (key, read, write),
                       DasACLEntryPermissions);
    }
  }
}

DAS_MODEL_DEFAULT(infinit::model::doughnut::ACLEntry,
                  infinit::model::doughnut::DasACLEntry);
// FAILS in binary mode
// DAS_MODEL_SERIALIZE(infinit::model::doughnut::ACB::ACLEntry);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*---------.
      | ACLEntry |
      `---------*/

      ACLEntry::ACLEntry(infinit::cryptography::rsa::PublicKey key_,
                              bool read_,
                              bool write_,
                              elle::Buffer token_)
        : key(std::move(key_))
        , read(read_)
        , write(write_)
        , token(std::move(token_))
      {}

      ACLEntry::ACLEntry(ACLEntry const& other)
        : key{other.key}
        , read{other.read}
        , write{other.write}
        , token{other.token}
      {}

      ACLEntry::ACLEntry(elle::serialization::SerializerIn& s)
        : ACLEntry(deserialize(s))
      {}

      ACLEntry
      ACLEntry::deserialize(elle::serialization::SerializerIn& s)
      {

        auto key = s.deserialize<cryptography::rsa::PublicKey>("key");
        auto read = s.deserialize<bool>("read");
        auto write = s.deserialize<bool>("write");
        auto token = s.deserialize<elle::Buffer>("token");
        return ACLEntry(std::move(key), read, write, std::move(token));

        /*
        DasACLEntry::Update content(s);
        return ACLEntry(std::move(content.key.get()),
                        content.read.get(),
                        content.write.get(),
                        std::move(content.token.get()));*/

      }

      void
      ACLEntry::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("key", key);
        s.serialize("read", read);
        s.serialize("write", write);
        s.serialize("token", token);
      }

      bool
      ACLEntry::operator == (ACLEntry const& b) const
      {
        return key == b.key && read == b.read && write && b.write
          && token == b.token;
      }

      /*-------------.
      | Construction |
      `-------------*/

      template <typename Block>
      BaseACB<Block>::BaseACB(Doughnut* owner,
               elle::Buffer data,
               boost::optional<elle::Buffer> salt,
               boost::optional<cryptography::rsa::KeyPair> kp)
        : Super(owner, std::move(data), std::move(salt), std::move(kp))
        , _editor(-1)
        , _owner_token()
        , _acl_changed(true)
        , _data_version(-1)
        , _data_signature()
        , _world_readable(false)
        , _world_writable(false)
      {}

      template <typename Block>
      BaseACB<Block>::BaseACB(BaseACB<Block> const& other, bool sealed_copy)
        : Super(other, sealed_copy)
        , _editor(other._editor)
        , _owner_token(other._owner_token)
        , _acl_changed(other._acl_changed)
        , _acl_entries(other._acl_entries)
        , _data_version(other._data_version)
        , _world_readable(other._world_readable)
        , _world_writable(other._world_writable)
      {
        if (sealed_copy)
        {
          _data_signature = other._data_signature.value();
        }
        else
        {
          this->_data_signature = elle::Buffer();
        }
      }

      /*-------.
      | Clone  |
      `-------*/
      template <typename Block>
      std::unique_ptr<blocks::Block>
      BaseACB<Block>::clone(bool sealed_copy) const
      {
        return std::unique_ptr<blocks::Block>(new Self(*this, sealed_copy));
      }

      /*--------.
      | Content |
      `--------*/

      template <typename Block>
      int
      BaseACB<Block>::version() const
      {
        return this->_data_version;
      }

      template <typename Block>
      std::pair<std::vector<ACLEntry>::const_iterator,
                  std::shared_ptr<infinit::cryptography::rsa::KeyPair const>>
      BaseACB<Block>::_find_token() const
      {
        auto& mine = this->doughnut()->keys().K();
        auto it = std::find_if
            (this->_acl_entries.begin(), this->_acl_entries.end(),
             [&] (ACLEntry const& e) { return e.key == mine; });
        if (it != this->_acl_entries.end())
          return std::make_pair(it,
            this->doughnut()->keys_shared());
        // search in other keys
        auto const& other_keys = this->doughnut()->other_keys();
        for (auto it = this->_acl_entries.begin();
             it != this->_acl_entries.end(); ++it)
        {
          ELLE_DEBUG("scanning %s", it->key);
          auto hit = other_keys.find(std::hash<infinit::cryptography::rsa::PublicKey>()(it->key));
          if (hit != other_keys.end())
          {
            ELLE_DEBUG("hit");
            return std::make_pair(it, hit->second);
          }
        }
        return std::make_pair(this->_acl_entries.end(), nullptr);
      }

      template <typename Block>
      elle::Buffer
      BaseACB<Block>::_decrypt_data(elle::Buffer const& data) const
      {
        if (this->world_readable())
          return this->_data;
        auto& mine = this->doughnut()->keys().K();
        elle::Buffer const* encrypted_secret = nullptr;
        infinit::cryptography::rsa::PrivateKey const* priv = nullptr;
        std::vector<ACLEntry> entries;
        if (mine == *this->owner_key())
        {
          ELLE_DEBUG("%s: we are owner", *this);
          encrypted_secret = &this->_owner_token;
          priv = &this->doughnut()->keys().k();
        }
        else if (!this->_acl_entries.empty())
        {
          // FIXME: factor searching the token
          auto entry = this->_find_token();
          if (entry.second && entry.first->read)
          {
            ELLE_DEBUG("%s: we are an editor", *this);
            encrypted_secret = &entry.first->token;
            priv = &entry.second->k();
          }
        }
        if (!encrypted_secret)
        {
          // FIXME: better exceptions
          throw ValidationFailed("no read permissions");
        }
        auto secret_buffer =
          priv->open(*encrypted_secret);
        auto secret = elle::serialization::json::deserialize
          <cryptography::SecretKey>(secret_buffer);
        ELLE_DUMP("%s: secret: %s", *this, secret);
        return secret.decipher(this->_data);
      }

      /*------------.
      | Permissions |
      `------------*/

      template <typename Block>
      void
      BaseACB<Block>::_set_world_permissions(bool read, bool write)
      {
        if (this->_world_readable == read && this->_world_writable == write)
          return;
        this->_world_readable = read;
        this->_world_writable = write;
        this->_acl_changed = true;
        this->_data_changed = true;
      }

      template <typename Block>
      std::pair<bool, bool>
      BaseACB<Block>::_get_world_permissions()
      {
        return std::make_pair(this->_world_readable, this->_world_writable);
      }

      template <typename Block>
      void
      BaseACB<Block>::set_permissions(cryptography::rsa::PublicKey const& key,
                           bool read,
                           bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permisions for %s: %s, %s",
                         *this, key, read, write);
        if (key == *this->owner_key())
          throw elle::Error("Cannot set permissions for owner");
        auto& acl_entries = this->_acl_entries;
        ELLE_DUMP("%s: ACL entries: %s", *this, acl_entries);
        auto it = std::find_if
          (acl_entries.begin(), acl_entries.end(),
           [&] (ACLEntry const& e) { return e.key == key; });
        if (it == acl_entries.end())
        {
          if (!read && !write)
          {
            ELLE_DUMP("%s: new user with no read or write permissions, "
                      "do nothing", *this);
            return;
          }
          ELLE_DEBUG_SCOPE("%s: new user, insert ACL entry", *this);
          // If the owner token is empty, this block was never pushed and
          // sealing will generate a new secret and update the token.
          // FIXME: the block will always be sealed anyway, why encrypt a token
          // now ?
          elle::Buffer token;
          if (this->_owner_token.size())
          {
            auto& k = this->keys() ? this->keys()->k() : this->doughnut()->keys().k();
            auto secret = k.open(this->_owner_token);
            token = key.seal(secret);
          }
          acl_entries.emplace_back(ACLEntry(key, read, write, token));
          this->_acl_changed = true;
        }
        else
        {
          if (!read && !write)
          {
            ELLE_DEBUG_SCOPE("%s: user (%s) no longer has read or write "
                             "permissions, remove ACL entry", *this, key);
            acl_entries.erase(it);
            this->_acl_changed = true;
            return;
          }
          ELLE_DEBUG_SCOPE("%s: edit ACL entry", *this);
          if (it->read != read)
          {
            it->read = read;
            this->_acl_changed = true;
          }
          if (it->write != write)
          {
            it->write = write;
            this->_acl_changed = true;
          }
        }
      }

      template <typename Block>
      void
      BaseACB<Block>::_set_permissions(model::User const& user_, bool read, bool write)
      {
        try
        {
          auto& user = dynamic_cast<User const&>(user_);
          this->set_permissions(user.key(), read, write);
        }
        catch (std::bad_cast const&)
        {
          ELLE_ABORT("doughnut was passed a non-doughnut user.");
        }
      }

      template <typename Block>
      void
      BaseACB<Block>::_copy_permissions(blocks::ACLBlock& to)
      {
        Self* other = dynamic_cast<Self*>(&to);
        if (!other)
          throw elle::Error("Other block is not an ACB");
        // FIXME: better implementation
        for (auto const& e: this->_acl_entries)
        {
          if (e.key != *other->owner_key())
            other->set_permissions(e.key, e.read, e.write);
        }
        if (*other->owner_key() != *this->owner_key())
          other->set_permissions(*this->owner_key(), true, true);
        other->_world_readable = this->_world_readable;
        other->_world_writable = this->_world_writable;
      }

      template <typename Block>
      std::vector<blocks::ACLBlock::Entry>
      BaseACB<Block>::_list_permissions(boost::optional<Model const&> model)
      {
        auto make_user =
          [&] (cryptography::rsa::PublicKey const& k)
          -> std::unique_ptr<infinit::model::User>
          {
            try
            {
              if (model)
                return
                  model->make_user(elle::serialization::json::serialize(k));
              else
                return elle::make_unique<doughnut::User>(k, "");
            }
            catch(elle::Error const& e)
            {
              ELLE_WARN("exception making user: %s", e);
              return nullptr;
            }
          };
        std::vector<ACB::Entry> res;
        auto owner = make_user(*this->owner_key());
        if (owner)
          res.emplace_back(std::move(owner), true, true);
        for (auto const& ent: this->_acl_entries)
        {
          auto user = make_user(ent.key);
          if (user)
            res.emplace_back(std::move(user), ent.read, ent.write);
        }
        return res;
      }

      /*-----------.
      | Validation |
      `-----------*/

      template <typename Block>
      blocks::ValidationResult
      BaseACB<Block>::_validate() const
      {
        if (this->_is_local)
          return blocks::ValidationResult::success();
        ELLE_DEBUG("%s: validate owner part", *this)
          if (auto res = Super::_validate()); else
            return res;
        if (this->_world_writable)
          return blocks::ValidationResult::success();
        ELLE_DEBUG_SCOPE("%s: validate author part", *this);
        ACLEntry* entry = nullptr;
        if (this->_editor != -1)
        {
          ELLE_DEBUG_SCOPE("%s: check author has write permissions", *this);
          if (this->_editor < 0)
          {
            ELLE_DEBUG("%s: no ACL or no editor", *this);
            return blocks::ValidationResult::failure("no ACL or no editor");
          }
          if (this->_editor >= signed(this->_acl_entries.size()))
          {
            ELLE_DEBUG("%s: editor index out of bounds", *this);
            return blocks::ValidationResult::failure
              ("editor index out of bounds");
          }
          entry = elle::unconst(&this->_acl_entries[this->_editor]);
          if (!entry->write)
          {
            ELLE_DEBUG("%s: no write permissions", *this);
            return blocks::ValidationResult::failure("no write permissions");
          }
        }
        ELLE_DEBUG("%s: check author signature, entry=%s", *this, !!entry)
        {
          auto sign = this->_data_sign();
          auto& key = entry ? entry->key : *this->owner_key();
          if (!this->_check_signature(key, this->data_signature(), sign, "data"))
          {
            ELLE_DEBUG("%s: author signature invalid", *this);
            return blocks::ValidationResult::failure
              ("author signature invalid");
          }
        }
        return blocks::ValidationResult::success();
      }

      template <typename T>
      static
      void
      null_deleter(T*)
      {}

      template <typename Block>
      void
      BaseACB<Block>::seal(cryptography::SecretKey const& key)
      {
        this->_seal(key);
      }

      template <typename Block>
      void
      BaseACB<Block>::_seal()
      {
        this->_seal({});
      }

      template <typename Block>
      void
      BaseACB<Block>::_seal(boost::optional<cryptography::SecretKey const&> key)
      {
        static elle::Bench bench("bench.acb.seal", 10000_sec);
        elle::Bench::BenchScope scope(bench);
        bool acl_changed = this->_acl_changed;
        bool data_changed = this->_data_changed;
        std::shared_ptr<infinit::cryptography::rsa::KeyPair const> sign_keys;
        if (acl_changed)
        {
          static elle::Bench bench("bench.acb.seal.aclchange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ELLE_DEBUG_SCOPE("%s: ACL changed, seal", *this);
          this->_acl_changed = false;
          bool owner = this->doughnut()->keys().K() == *this->owner_key();
          if (this->keys())
            owner |= this->keys()->K() == *this->owner_key();
          if (owner)
          {
            sign_keys = this->doughnut()->keys_shared();
            this->_editor = -1;
          }
          Super::_seal_okb();
          if (!data_changed)
            // FIXME: idempotence in case the write fails ?
            ++this->_data_version;
        }
        else if (!this->_signature.running() && this->_signature.value().empty())
        {
          ELLE_DEBUG("%s: signature missing, recalculating...", *this);
          this->_seal_okb(false);
        }
        else
          ELLE_DEBUG("%s: ACL didn't change", *this);
        if (data_changed)
        {
          static elle::Bench bench("bench.acb.seal.datachange", 10000_sec);
          elle::Bench::BenchScope scope(bench);
          ++this->_data_version; // FIXME: idempotence in case the write fails ?
          ELLE_TRACE_SCOPE("%s: data changed, seal version %s",
                           *this, this->_data_version);
          bool owner = this->doughnut()->keys().K() == *this->owner_key();
          if (this->keys())
            owner |= this->keys()->K() == *this->owner_key();
          if (owner)
          {
            sign_keys = this->doughnut()->keys_shared();
            this->_editor = -1;
          }
          boost::optional<cryptography::SecretKey> secret;
          elle::Buffer secret_buffer;
          if (!key)
          {
            secret = cryptography::secretkey::generate(256);
            key = secret;
          }
          ELLE_DUMP("%s: new block secret: %s", *this, key.get());
          secret_buffer = elle::serialization::json::serialize(key.get());
          this->_owner_token = this->owner_key()->seal(secret_buffer);
          bool found = false;
          int idx = 0;
          for (auto& e: this->_acl_entries)
          {
            if (e.read)
            {
              e.token = e.key.seal(secret_buffer);
            }
            if (!owner && e.key == this->doughnut()->keys().K())
            {
              found = true;
              this->_editor = idx;
              sign_keys = this->doughnut()->keys_shared();
            }
            ++idx;
          }
          if (!owner && !found && !this->_world_writable)
          {
            // group search
            auto hit = _find_token();
            if (!hit.second)
              throw ValidationFailed("not owner and no write permissions");
            this->_editor = hit.first - this->_acl_entries.begin();
            sign_keys = hit.second;
          }
          if (!sign_keys && this->_world_writable)
            sign_keys = this->doughnut()->keys_shared();
          if (!this->_world_readable)
            this->blocks::MutableBlock::data(key->encipher(this->data_plain()));
          else
            this->blocks::MutableBlock::data(this->data_plain());
          this->_data_changed = false;
        }
        else
          ELLE_DEBUG("%s: data didn't change", *this);
        // Even if only the ACL was changed, we need to re-sign because the ACL
        // address is part of the signature.
        if (acl_changed || data_changed ||
          (!this->_data_signature.running() && this->_data_signature.value().empty()))
        {
          // note: in world_writable mode, the signing key might not be
          // present in the block, so signing might not be that important. 
          if (this->keys())
            sign_keys = std::shared_ptr<cryptography::rsa::KeyPair>(&*this->keys(), null_deleter<cryptography::rsa::KeyPair>);
          auto to_sign = elle::utility::move_on_copy(this->_data_sign());
          this->_data_signature =
            [sign_keys, to_sign]
            {
              static elle::Bench bench("bench.acb.seal.signing", 10000_sec);
              elle::Bench::BenchScope scope(bench);
              return sign_keys->k().sign(*to_sign);
            };
        }
        Super::_seal();
      }

      template <typename Block>
      BaseACB<Block>::~BaseACB()
      {}

      template <typename Block>
      elle::Buffer const&
      BaseACB<Block>::data_signature() const
      {
        return this->_data_signature.value();
      }

      template <typename Block>
      elle::Buffer
      BaseACB<Block>::_data_sign() const
      {
        elle::Buffer res;
        {
          elle::IOStream output(res.ostreambuf());
          elle::serialization::binary::SerializerOut s(output, false);
          this->_data_sign(s);
        }
        return res;
      }

      template <typename Block>
      void
      BaseACB<Block>::_data_sign(elle::serialization::SerializerOut& s) const
      {
        s.serialize("salt", this->salt());
        s.serialize("key", this->owner_key());
        s.serialize("version", this->_data_version);
        s.serialize("data", this->Block::data());
        s.serialize("owner_token", this->_owner_token);
        s.serialize("acl", this->_acl_entries);
      }

      template <typename Block>
      void
      BaseACB<Block>::_sign(elle::serialization::SerializerOut& s) const
      {
        s.serialize(
          "acls", elle::unconst(this)->_acl_entries,
          elle::serialization::as<das::Serializer<DasACLEntryPermissions>>());
        // BREAKS BACKWARDS
        s.serialize("world_readable", this->_world_readable);
        s.serialize("world_writable", this->_world_writable);
      }

      template <typename... T>
      auto zip(const T&... containers)
        -> boost::iterator_range<boost::zip_iterator<
             decltype(boost::make_tuple(std::begin(containers)...))>>
      {
        auto zip_begin = boost::make_zip_iterator(
          boost::make_tuple(std::begin(containers)...));
        auto zip_end = boost::make_zip_iterator(
          boost::make_tuple(std::end(containers)...));
        return boost::make_iterator_range(zip_begin, zip_end);
      }

      /*--------.
      | Content |
      `--------*/

      /*--------.
      | Content |
      `--------*/
      template <typename Block>
      void
      BaseACB<Block>::_stored()
      {
      }

      template <typename Block>
      bool
      BaseACB<Block>::operator ==(blocks::Block const& rhs) const
      {
        auto other_acb = dynamic_cast<Self const*>(&rhs);
        if (!other_acb)
          return false;
        if (this->_editor != other_acb->_editor)
          return false;
        if (this->_owner_token != other_acb->_owner_token)
          return false;
        if (this->_acl_entries != other_acb->_acl_entries)
          return false;
        if (this->_data_version != other_acb->_data_version)
          return false;
        if (this->_data_signature.value() != other_acb->_data_signature.value())
          return false;
        if (this->_world_readable != other_acb->_world_readable)
          return false;
        if (this->_world_writable != other_acb->_world_writable)
          return false;
        return this->Super::operator ==(rhs);
      }

      /*--------------.
      | Serialization |
      `--------------*/

      template <typename Block>
      BaseACB<Block>::BaseACB(elle::serialization::SerializerIn& input)
        : Super(input)
        , _editor(-2)
        , _owner_token()
        , _acl_changed(false)
        , _data_version(-1)
        , _data_signature()
      {
        this->_serialize(input);
      }

      template <typename Block>
      void
      BaseACB<Block>::serialize(elle::serialization::Serializer& s)
      {
        Super::serialize(s);
        this->_serialize(s);
      }

      template <typename Block>
      void
      BaseACB<Block>::_serialize(elle::serialization::Serializer& s)
      {
        s.serialize("editor", this->_editor);
        s.serialize("owner_token", this->_owner_token);
        s.serialize("acl", this->_acl_entries);
        s.serialize("data_version", this->_data_version);
        bool need_signature = !s.context().has<ACBDontWaitForSignature>();
        if (need_signature)
          s.serialize("data_signature", this->_data_signature.value());
        else
        {
          elle::Buffer signature;
          s.serialize("data_signature", signature);
          if (s.in())
          {
            auto keys = this->doughnut()->keys_shared();
            auto sign = elle::utility::move_on_copy(this->_data_sign());
            this->_data_signature =
              [keys, sign] { return keys->k().sign(*sign); };
          }
        }
        // BREAKS BACKWARD
        s.serialize("world_readable", this->_world_readable);
        s.serialize("world_writable", this->_world_writable);
      }

      template
      class BaseACB<blocks::ACLBlock>;

      template
      class BaseACB<blocks::GroupBlock>;

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<ACB> _register_okb_serialization("ACB");
    }
  }
}
