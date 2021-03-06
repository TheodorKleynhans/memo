#include <memo/environ.hh>

#include <map>

#include <elle/algorithm.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/printf.hh>

ELLE_LOG_COMPONENT("memo.environ");

namespace
{
  using Vars = std::map<std::string, std::string>;

  // Silly Singleton imposed by static initialization failure on our
  // Mingw build.
  Vars const& vars()
  {
    static auto const res = Vars
    {
      {"ASYNC_NOPOP", ""},
      {"ASYNC_POP_DELAY", ""},
      {"ASYNC_SQUASH", ""},
      {"BACKGROUND_DECODE", ""},
      {"BACKTRACE", ""},
      {"BALANCED_TRANSFERS", ""},
      {"BEYOND", ""},
      {"CACHE_REFRESH_BATCH_SIZE", ""},
      {"CONNECT_TIMEOUT", ""},
      {"CRASH", "Generate a crash"},
      {"CRASH_REPORT", "Activate crash-reporting"},
      {"CRASH_REPORT_HOST", ""},
      {"DATA_HOME", ""},
      {"FIRST_BLOCK_DATA_SIZE", ""},
      {"HOME", ""},
      {"HOME_OVERRIDE", ""},
      {"IPV4", "Enable IPv4 (default: true)"},
      {"IPV6", "Enable IPv6 (default: true)"},
      {"KELIPS_ASYNC", ""},
      {"KELIPS_ASYNC_SEND", ""},
      {"KELIPS_SNUB", ""},
      {"KEY_HASH", ""},
      {"LOG_REACHABILITY", ""},
      {"LOOKAHEAD_BLOCKS", ""},
      {"LOOKAHEAD_THREADS", ""},
      {"MAX_EMBED_SIZE", ""},
      {"MAX_SQUASH_SIZE", ""},
      {"PAXOS_LENIENT_FETCH", ""},
      {"PREEMPT_DECODE", ""},
      {"PREFETCH_DEPTH", ""},
      {"PREFETCH_GROUP", ""},
      {"PREFETCH_TASKS", ""},
      {"PREFETCH_THREADS", ""},
      {"PRESERVE_ACLS", ""},
      {"PROMETHEUS_ENDPOINT", ""},
      {"RDV", ""},
      {"RPC_DISABLE_CRYPTO", ""},
      {"RPC_SERVE_THREADS", ""},
      {"SIGNAL_HANDLER", ""},
      {"SOFTFAIL_RUNNING", ""},
      {"SOFTFAIL_TIMEOUT", ""},
      {"USER", ""},
      {"UTP", ""},
    };
    return res;
  }

  /// Whether this is a known variable suffix (e.g., "HOME", not "MEMO_HOME").
  bool
  known_name(std::string const& v)
  {
    if (elle::contains(vars(), v))
      return true;
    else
    {
      ELLE_WARN("suspicious environment variable: MEMO_%s", v);
      return false;
    }
  }

  bool
  check_environment_()
  {
    // Number of unknown var names.
    auto unknown = 0;
    auto const env = elle::os::environ();
    ELLE_DUMP("checking: %s", env);
    for (auto const& p: env)
      if (auto v = elle::tail(p.first, "MEMO_"))
      {
        // Whether we checked the variable name.
        auto checked = false;

        // Map MEMO_NO_FOO=1 to MEMO_FOO=0, and MEMO_DISABLE_FOO=1 to MEMO_FOO=0.
        for (auto prefix: {"DISABLE_", "NO_"})
          if (auto suffix = elle::tail(*v, prefix))
          {
            const auto& old_var = p.first;
            const auto old_val = elle::os::getenv(old_var, false);
            const auto new_var = "MEMO_" + *suffix;
            const auto new_val = elle::os::getenv(new_var, true);
            if (elle::os::inenv(new_var) && old_val != !new_val)
            {
              ELLE_WARN("%s=%s and %s=%s conflict: proceeding with the latter",
                        old_var, old_val, new_var, new_val);
            }
            else
            {
              ELLE_WARN("prefer %s=%s to %s=%s",
                        new_var, !old_val, old_var, old_val);
              elle::os::setenv(new_var, elle::print("%s", !old_val));
            }
            unknown += !known_name(*suffix);
            checked = true;
          }

        // Check variables that are not MEMO_NO_* nor MEMO_DISABLE_*.
        if (!checked)
          unknown += !known_name(*v);
      }
    if (unknown)
      ELLE_WARN("known MEMO_* environment variables: %s", elle::keys(vars()));
    return true;
  }
}

namespace memo
{
  void
  check_environment()
  {
    ELLE_COMPILER_ATTRIBUTE_MAYBE_UNUSED
    static auto _ = check_environment_();
  }
}
