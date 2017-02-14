#pragma once

#include <das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

/// Whether to enable Docker support.
#if !defined INFINIT_PRODUCTION_BUILD || defined INFINIT_LINUX
# define WITH_DOCKER
#endif

namespace infinit
{
  namespace cli
  {
    class Daemon
      : public Object<Daemon>
    {
    public:
      Daemon(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::disable_storage,
                                    cli::enable_storage,
                                    cli::fetch,
                                    cli::run,
                                    cli::start,
                                    cli::status,
                                    cli::stop,
                                    cli::manage_volumes));
      using Strings = std::vector<std::string>;

      /*------------------------.
      | Mode: disable_storage.  |
      `------------------------*/
      using ModeDisableStorage =
        Mode<decltype(binding(modes::mode_disable_storage,
                              cli::name))>;
      ModeDisableStorage disable_storage;
      void
      mode_disable_storage(std::string const& name);


      /*-----------------------.
      | Mode: enable_storage.  |
      `-----------------------*/
      using ModeEnableStorage =
        Mode<decltype(binding(modes::mode_enable_storage,
                              cli::name,
                              cli::hold))>;
      ModeEnableStorage enable_storage;
      void
      mode_enable_storage(std::string const& name,
                          bool hold);


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      using ModeFetch =
        Mode<decltype(binding(modes::mode_fetch,
                              cli::name))>;
      ModeFetch fetch;
      void
      mode_fetch(std::string const& name);

      /*-----------------------.
      | Mode: manage_volumes.  |
      `-----------------------*/
      using ModeManageVolumes =
        Mode<decltype(binding(modes::mode_manage_volumes,
                              cli::list = false,
                              cli::status = false,
                              cli::start = false,
                              cli::stop = false,
                              cli::restart = false,
                              cli::name = boost::none))>;
      ModeManageVolumes manage_volumes;
      void
      mode_manage_volumes(bool list = false,
                          bool status = false,
                          bool start = false,
                          bool stop = false,
                          bool restart = false,
                          boost::optional<std::string> const& name = {});
      /*------------.
      | Mode: run.  |
      `------------*/
      using ModeRun =
        Mode<decltype(binding(modes::mode_run,
                              cli::login_user = Strings{},
                              cli::mount = Strings{},
                              cli::mount_root = boost::none,
                              cli::default_network = boost::none,
                              cli::advertise_host = Strings{},
                              cli::fetch = false,
                              cli::push = false,
#ifdef WITH_DOCKER
                              cli::docker = true,
                              cli::docker_user = boost::none,
                              cli::docker_home = boost::none,
                              cli::docker_socket_tcp = false,
                              cli::docker_socket_port = 0,
                              cli::docker_socket_path = "/run/docker/plugins",
                              cli::docker_descriptor_path = "/usr/lib/docker/plugins",
                              cli::docker_mount_substitute = "",
#endif
                              cli::log_level = boost::none,
                              cli::log_path = boost::none))>;
      ModeRun run;
      void
      mode_run(Strings const& login_user,
               Strings const& mount,
               boost::optional<std::string> const& mount_root,
               boost::optional<std::string> const& default_network,
               Strings const& advertise_host,
               bool fetch,
               bool push,
#ifdef WITH_DOCKER
               bool docker,
               boost::optional<std::string> const& docker_user,
               boost::optional<std::string> const& docker_home,
               bool docker_socket_tcp,
               int const& docker_socket_port,
               std::string const& docker_socket_path,
               std::string const& docker_descriptor_path,
               std::string const& docker_mount_substitute,
#endif
               boost::optional<std::string> const& log_level,
               boost::optional<std::string> const& log_path);

      /*--------------.
      | Mode: start.  |
      `--------------*/
      using ModeStart =
        Mode<decltype(binding(modes::mode_start,
                              cli::login_user = Strings{},
                              cli::mount = Strings{},
                              cli::mount_root = boost::none,
                              cli::default_network = boost::none,
                              cli::advertise_host = Strings{},
                              cli::fetch = false,
                              cli::push = false,
#ifdef WITH_DOCKER
                              cli::docker = true,
                              cli::docker_user = boost::none,
                              cli::docker_home = boost::none,
                              cli::docker_socket_tcp = false,
                              cli::docker_socket_port = 0,
                              cli::docker_socket_path = "/run/docker/plugins",
                              cli::docker_descriptor_path = "/usr/lib/docker/plugins",
                              cli::docker_mount_substitute = "",
#endif
                              cli::log_level = boost::none,
                              cli::log_path = boost::none))>;
      ModeStart start;
      void
      mode_start(Strings const& login_user,
                 Strings const& mount,
                 boost::optional<std::string> const& mount_root,
                 boost::optional<std::string> const& default_network,
                 Strings const& advertise_host,
                 bool fetch,
                 bool push,
#ifdef WITH_DOCKER
                 bool docker,
                 boost::optional<std::string> const& docker_user,
                 boost::optional<std::string> const& docker_home,
                 bool docker_socket_tcp,
                 int const& docker_socket_port,
                 std::string const& docker_socket_path,
                 std::string const& docker_descriptor_path,
                 std::string const& docker_mount_substitute,
#endif
                 boost::optional<std::string> const& log_level,
                 boost::optional<std::string> const& log_path);


      /*---------------.
      | Mode: status.  |
      `---------------*/
      using ModeStatus =
        Mode<decltype(binding(modes::mode_status))>;
      ModeStatus status;
      void
      mode_status();

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      using ModeStop =
        Mode<decltype(binding(modes::mode_stop))>;
      ModeStop stop;
      void
      mode_stop();
    };
  }
}