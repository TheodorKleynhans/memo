#include <elle/test.hh>

#include <elle/err.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kouncil/Kouncil.hh>
#include <infinit/storage/MissingKey.hh>

#include <infinit/filesystem/filesystem.hh>

#include <infinit/grpc/kv.grpc.pb.h>
#include <infinit/grpc/kv.grpc.pb.cc>
#include <infinit/grpc/kv.pb.cc>
#include <infinit/grpc/grpc.hh>

#include <grpc++/grpc++.h>

#include "DHT.hh"

ELLE_LOG_COMPONENT("test");

class DHTs
{
public:
  template <typename ... Args>
  DHTs(int count)
   : DHTs(count, {})
  {
  }
  template <typename ... Args>
  DHTs(int count,
       boost::optional<infinit::cryptography::rsa::KeyPair> kp,
       Args ... args)
    : owner_keys(kp? *kp : infinit::cryptography::rsa::keypair::generate(512))
    , dhts()
  {
    pax = true;
    if (count < 0)
    {
      pax = false;
      count *= -1;
    }
    for (int i = 0; i < count; ++i)
    {
      this->dhts.emplace_back(paxos = pax,
                              owner = this->owner_keys,
                              std::forward<Args>(args) ...);
      for (int j = 0; j < i; ++j)
        this->dhts[j].overlay->connect(*this->dhts[i].overlay);
    }
  }

  struct Client
  {
    template<typename... Args>
    Client(std::string const& name, DHT dht, Args...args)
      : dht(std::move(dht))
      , fs(std::make_unique<reactor::filesystem::FileSystem>(
             std::make_unique<infinit::filesystem::FileSystem>(
               name, this->dht.dht, infinit::filesystem::allow_root_creation = true,
               std::forward<Args>(args)...),
             true))
    {}

    DHT dht;
    std::unique_ptr<reactor::filesystem::FileSystem> fs;
  };

  template<typename... Args>
  DHT
  dht(bool new_key,
         boost::optional<infinit::cryptography::rsa::KeyPair> kp,
         Args... args)
  {
    auto k = kp ? *kp
    : new_key ? infinit::cryptography::rsa::keypair::generate(512)
          : this->owner_keys;
    ELLE_LOG("new client with owner=%f key=%f", this->owner_keys.K(), k.K());
    DHT client(owner = this->owner_keys,
               keys = k,
               storage = nullptr,
               make_consensus = no_cheat_consensus,
               paxos = pax,
               std::forward<Args>(args) ...
               );
    for (auto& dht: this->dhts)
      dht.overlay->connect(*client.overlay);
    return client;
  }
  template<typename... Args>
  Client
  client(bool new_key,
         boost::optional<infinit::cryptography::rsa::KeyPair> kp,
         Args... args)
  {
    DHT client = dht(new_key, kp, std::forward<Args>(args)...);
    return Client("volume", std::move(client));
  }

  Client
  client(bool new_key = false)
  {
    return client(new_key, {});
  }

  infinit::cryptography::rsa::KeyPair owner_keys;
  std::vector<DHT> dhts;
  bool pax;
};


ELLE_TEST_SCHEDULED(basic)
{
  DHTs dhts(3);
  auto client = dhts.client();
  infinit::model::Endpoint ep("127.0.0.1", (rand()%10000)+50000);
  reactor::Barrier b;
  auto t = std::make_unique<reactor::Thread>("grpc",
    [&] {
      ELLE_LOG("open");
      b.open();
      ELLE_LOG("serve");
      infinit::grpc::serve_grpc(*client.dht.dht, ep);
      ELLE_LOG("done");
    });
  ELLE_LOG("wait");
  reactor::wait(b);
  ELLE_LOG("start");
  auto chan = grpc::CreateChannel(
      elle::sprintf("127.0.0.1:%s", ep.port()),
      grpc::InsecureChannelCredentials());
  auto stub = KV::NewStub(chan);
  { // get missing block
    grpc::ClientContext context;
    ::Address req;
    ::BlockStatus repl;
    req.set_address(elle::sprintf("%s", infinit::model::Address::null));
    ELLE_LOG("call...");
    auto res = stub->Get(&context, req, &repl);
    ELLE_LOG("...called");
    //BOOST_CHECK_EQUAL((int)res, (int)::grpc::Status::OK);
    BOOST_CHECK_EQUAL(repl.status().error(), ERROR_MISSING_BLOCK);
  }
  { // put/get chb
    grpc::ClientContext context;
    ::ModeBlock req;
    req.set_mode(STORE_INSERT);
    req.mutable_block()->set_payload("foo");
    req.mutable_block()->mutable_constant_block();
    ::Status repl;
    stub->Set(&context, req, &repl);
    ::Address addr;
    addr.set_address(repl.address());
    ::BlockStatus bs;
    {
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
    }
    BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
    BOOST_CHECK_EQUAL(bs.block().payload(), "foo");
    { // STORE_UPDATE chb
      req.set_mode(STORE_UPDATE);
      req.mutable_block()->set_payload("bar");
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_NO_PEERS);
    }
  }
  { // mb
    ::ModeBlock req;
    req.set_mode(STORE_INSERT);
    req.mutable_block()->set_payload("foo");
    req.mutable_block()->mutable_mutable_block();
    ::Status repl;
    { // insert
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    ::Address addr;
    addr.set_address(repl.address());
    ::BlockStatus bs;
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "foo");
    }
    { // update
      req.set_mode(STORE_UPDATE);
      req.mutable_block()->set_payload("bar");
      req.mutable_block()->set_address(addr.address());
      {
        grpc::ClientContext context;
        stub->Set(&context, req, &repl);
        BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
      }
    }
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "bar");
      BOOST_CHECK_EQUAL(bs.block().address(), addr.address());
    }
  }
  { // acb
    ::ModeBlock req;
    req.set_mode(STORE_INSERT);
    req.mutable_block()->set_payload("foo");
    req.mutable_block()->mutable_acl_block();
    ::Status repl;
    { // insert
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    ::Address addr;
    addr.set_address(repl.address());
    ::BlockStatus bs;
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "foo");
    }
    { // update
      req.set_mode(STORE_UPDATE);
      req.mutable_block()->set_payload("bar");
      req.mutable_block()->set_address(addr.address());
      {
        grpc::ClientContext context;
        stub->Set(&context, req, &repl);
        BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
      }
    }
    { // fetch
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "bar");
      BOOST_CHECK_EQUAL(bs.block().address(), addr.address());
    }
    // world perms
    BOOST_CHECK_EQUAL(bs.block().acl_block().world_read(), false);
    BOOST_CHECK_EQUAL(bs.block().acl_block().world_write(), false);
    req.mutable_block()->mutable_acl_block()->set_world_read(true);
    req.mutable_block()->mutable_acl_block()->set_world_write(true);
    {
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    {
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "bar");
      BOOST_CHECK_EQUAL(bs.block().acl_block().world_read(), true);
      BOOST_CHECK_EQUAL(bs.block().acl_block().world_write(), true);
    }
    // version conflict
    req.mutable_block()->mutable_acl_block()->set_version(1);
    req.mutable_block()->set_payload("baz");
    {
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_CONFLICT);
    }
    req.mutable_block()->mutable_acl_block()->set_version(repl.version()+1);
    {
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    // acls
    req.mutable_block()->mutable_acl_block()->set_version(0);
    auto alice = infinit::cryptography::rsa::keypair::generate(512);
    auto ubf = infinit::model::doughnut::UB(
      client.dht.dht.get(), "alice", alice.K(), false);
    auto ubr = infinit::model::doughnut::UB(
      client.dht.dht.get(), "alice", alice.K(), true);
    client.dht.dht->store(ubf, infinit::model::STORE_INSERT);
    client.dht.dht->store(ubr, infinit::model::STORE_INSERT);
    { // add alice
      auto acl = req.mutable_block()->mutable_acl_block()->add_permissions();
      acl->set_user("alice");
      acl->set_read(true);
      acl->set_write(true);
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    { // check alice
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().payload(), "baz");
      BOOST_CHECK_EQUAL(bs.block().acl_block().permissions_size(), 2);
      auto const& p = bs.block().acl_block().permissions(1);
      BOOST_CHECK_EQUAL(p.user(), "alice");
      BOOST_CHECK_EQUAL(p.admin(), false);
      BOOST_CHECK_EQUAL(p.owner(), false);
      BOOST_CHECK_EQUAL(p.read(), true);
      BOOST_CHECK_EQUAL(p.write(), true);
    }
    { // remove alice
      auto acl = req.mutable_block()->mutable_acl_block()->mutable_permissions(0);
      acl->set_read(false);
      acl->set_write(false);
      grpc::ClientContext context;
      stub->Set(&context, req, &repl);
      BOOST_CHECK_EQUAL(repl.error(), ERROR_OK);
    }
    { // check
      grpc::ClientContext context;
      stub->Get(&context, addr, &bs);
      BOOST_CHECK_EQUAL(bs.status().error(), ERROR_OK);
      BOOST_CHECK_EQUAL(bs.block().acl_block().permissions_size(), 1);
    }
  }
  ELLE_LOG("done");
}

ELLE_TEST_SUITE()
{
  elle::os::setenv("GRPC_EPOLL_SYMBOL", "reactor_epoll_pwait", 1);
  auto& master = boost::unit_test::framework::master_test_suite();
  master.add(BOOST_TEST_CASE(basic), 0, valgrind(10));
}