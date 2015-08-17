#ifndef INFINIT_STORAGE_ASYNC_HH
#define INFINIT_STORAGE_ASYNC_HH

#include <deque>
#include <reactor/scheduler.hh>
#include <reactor/Barrier.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/model/Address.hh>

namespace infinit
{
  namespace storage
  {
    class Async: public Storage
    {
    public:
      Async(std::unique_ptr<Storage> backend, int max_blocks = 100, int64_t max_size = -1);
      ~Async();
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
    private:
      void _worker();
      void _inc(int64_t size);
      void _dec(int64_t size);
      void _wait(); /// Wait for cache to go bellow limit
      std::unique_ptr<Storage> _backend;
      int _max_blocks;
      int64_t _max_size;
      reactor::Barrier _dequeueing;
      reactor::Barrier _queueing;
      reactor::Thread _thread;
      enum class Operation
      {
        erase,
        set
      };
      typedef std::tuple<Key, elle::Buffer, Operation> Entry;
      std::deque<Entry> _op_cache;
      int _blocks;
      int _bytes;
    };
  }
}


#endif
