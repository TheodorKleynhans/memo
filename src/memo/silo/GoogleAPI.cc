#include <memo/silo/GoogleAPI.hh>

#include <elle/algorithm.hh>
#include <elle/bench.hh>

#include <elle/reactor/scheduler.hh>
#include <elle/log.hh>

#include <memo/environ.hh>

using namespace std::literals;

ELLE_LOG_COMPONENT("memo.silo.GoogleAPI");

#define BENCH(name)                                                     \
  static auto bench = elle::Bench("bench.googleapi." name, 10000s);     \
  auto bs = elle::Bench::BenchScope(bench)

namespace
{
  auto static const beyond
    = memo::getenv("BEYOND", "https://beyond.infinit.sh"s);

  elle::reactor::Duration
  delay(int attempt)
  {
    auto factor = pow(2, std::min(8, attempt));
    return boost::posix_time::milliseconds(factor * 100);
  }
}

namespace memo
{
  using namespace std::literals;

  namespace silo
  {
    GoogleAPI::GoogleAPI(std::string name,
                         std::string refresh_token)
      : _name{std::move(name)}
      , _refresh_token{std::move(refresh_token)}
    {}

    auto
    GoogleAPI::_request(std::string url,
                        Method method,
                        Request::QueryDict query,
                        Request::Configuration conf,
                        std::vector<StatusCode> expected_codes,
                        elle::Buffer const& payload) const
      -> Request
    {
      ELLE_DUMP("_request %s", method);

      expected_codes.push_back(StatusCode::OK);
      unsigned attempt = 0;

      conf.timeout(elle::reactor::DurationOpt());
      conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));

      if (this->_token.empty())
        elle::unconst(this)->_refresh();
      while (true)
      {
        conf.header_remove("Authorization");
        conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));
        auto r = Request{url, method, conf};
        {
          BENCH("query");
          r.query_string(query);
          if (!payload.empty())
            r.write((const char*)payload.contents(), payload.size());
          r.finalize();
          r.status();
        }

        if (elle::contains(expected_codes, r.status()))
          return r;
        else if (r.status() == StatusCode::Forbidden
                 || r.status() == StatusCode::Unauthorized)
          const_cast<GoogleAPI*>(this)->_refresh();

        ELLE_WARN("Unexpected google HTTP response on %s %s payload %s: %s, attempt %s",
                  method,
                  url,
                  payload.size(),
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        ++attempt;
        elle::reactor::sleep(delay(attempt));
      }
    }

    void
    GoogleAPI::_refresh()
    {
      ELLE_DUMP("_refresh");

      // Autoconf rulez!
      auto conf = Request::Configuration{};
      conf.timeout(elle::reactor::DurationOpt());
      unsigned attempt = 0;

      auto query = Request::QueryDict{{"refresh_token", this->_refresh_token}};

      while (true)
      {
        auto url = elle::sprintf("%s/users/%s/credentials/google/refresh",
                                beyond, this->_name);
        auto r = Request{url, Method::GET, conf};
        r.query_string(query);
        r.finalize();

        if (r.status() == StatusCode::OK)
        {
          this->_token = r.response().string();
          // FIXME: Update the conf file. Credentials or storage or both ?
          break;
        }

        ELLE_WARN("Unexpected google HTTP status (refresh): %s, attempt %s",
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        elle::reactor::sleep(delay(attempt++));
      }
    }
  }
}
