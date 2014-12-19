#ifndef INFINIT_STORAGE_STRIP_HH
#define INFINIT_STORAGE_STRIP_HH

#include <infinit/storage/Storage.hh>
namespace infinit
{
  namespace storage
  {
    /** Balance blocks on the list of specified backend storage.
     * @warning: The same list must be passed each time, in the same order.
    */
    class Strip: public Storage
    {
    public:
      Strip(std::vector<Storage*> backend);
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
      ELLE_ATTRIBUTE(std::vector<Storage*>, backend);
      int _disk_of(Key k) const ;
    };
  }
}

#endif