#if defined(__linux__)
#include "cli.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Soundux::Objects
{
    std::string cliSocketPath()
    {
        if (const auto *runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) // NOLINT
        {
            return std::string(runtime) + "/soundux.sock";
        }
        return "/tmp/soundux-" + std::to_string(getuid()) + ".sock";
    }

    bool cliSendAll(int fd, const std::string &data)
    {
        std::size_t sent = 0;
        while (sent < data.size())
        {
            auto n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n <= 0)
            {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    namespace
    {
        void usage()
        {
            std::cerr << "Usage:\n"
                         "  soundux play <name|id>   play a sound (exact name, id or unique partial match)\n"
                         "  soundux stop             stop all playing sounds\n"
                         "  soundux list             list all sounds as '<id>\\t<tab>\\t<name>'\n";
        }
    } // namespace

    namespace Cli
    {
        bool isCommand(const std::vector<std::string> &args)
        {
            return args.size() > 1 && (args[1] == "play" || args[1] == "stop" || args[1] == "list");
        }

        int runClient(const std::vector<std::string> &args)
        {
            std::string request = args[1];
            if (args[1] == "play")
            {
                if (args.size() < 3)
                {
                    usage();
                    return 2;
                }
                for (std::size_t i = 2; i < args.size(); i++)
                {
                    request += " " + args[i];
                }
            }
            request += "\n";

            auto path = cliSocketPath();
            int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            if (sock < 0 || path.size() >= sizeof(addr.sun_path))
            {
                std::cerr << "soundux: failed to create socket\n";
                return 1;
            }
            std::copy(path.begin(), path.end(), addr.sun_path);
            if (::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
            {
                std::cerr << "soundux: no running instance found, start Soundux first (e.g. `soundux --hidden`)\n";
                ::close(sock);
                return 1;
            }

            if (!cliSendAll(sock, request))
            {
                std::cerr << "soundux: failed to send request\n";
                ::close(sock);
                return 1;
            }
            ::shutdown(sock, SHUT_WR);

            std::string response;
            char buffer[4096];
            ssize_t n;
            while ((n = ::read(sock, buffer, sizeof(buffer))) > 0)
            {
                response.append(buffer, static_cast<std::size_t>(n));
            }
            ::close(sock);

            std::istringstream stream(response);
            std::string status;
            std::getline(stream, status);
            std::string rest((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

            if (status == "OK")
            {
                std::cout << rest;
                return 0;
            }
            std::cerr << "soundux: " << (status.rfind("ERR ", 0) == 0 ? status.substr(4) : "no response") << "\n"
                      << rest;
            return 1;
        }
    } // namespace Cli

} // namespace Soundux::Objects
#endif
