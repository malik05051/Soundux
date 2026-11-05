#if defined(__linux__)
#include "cli.hpp"
#include <algorithm>
#include <cctype>
#include <core/global/globals.hpp>
#include <cstdlib>
#include <fancy.hpp>
#include <filesystem>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace Soundux::Objects
{
    namespace
    {
        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        std::string stem(const std::string &s)
        {
            auto pos = s.rfind('.');
            return pos == std::string::npos || pos == 0 ? s : s.substr(0, pos);
        }
    } // namespace

    CliServer::~CliServer()
    {
        stop();
    }

    void CliServer::start()
    {
        if (running)
        {
            return;
        }

        path = cliSocketPath();
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path))
        {
            Fancy::fancy.logTime().failure() << "CLI socket path too long" << std::endl;
            return;
        }
        std::copy(path.begin(), path.end(), addr.sun_path);

        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            Fancy::fancy.logTime().failure() << "Failed to create CLI socket" << std::endl;
            return;
        }

        /* The single instance guard guarantees nobody else is serving, so a leftover socket is stale */
        ::unlink(path.c_str());
        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(fd, 8) != 0)
        {
            Fancy::fancy.logTime().failure() << "Failed to bind CLI socket " << path << std::endl;
            ::close(fd);
            fd = -1;
            return;
        }
        ::chmod(path.c_str(), 0600);

        running = true;
        worker = std::thread([this] {
            while (running)
            {
                pollfd pfd{fd, POLLIN, 0};
                if (::poll(&pfd, 1, 200) <= 0)
                {
                    continue;
                }
                int client = ::accept(fd, nullptr, nullptr);
                if (client < 0)
                {
                    continue;
                }

                std::string request;
                char buffer[1024];
                pollfd cfd{client, POLLIN, 0};
                while (request.find('\n') == std::string::npos && request.size() < 4096 &&
                       ::poll(&cfd, 1, 2000) > 0)
                {
                    auto n = ::read(client, buffer, sizeof(buffer));
                    if (n <= 0)
                    {
                        break;
                    }
                    request.append(buffer, static_cast<std::size_t>(n));
                }
                request = request.substr(0, request.find('\n'));

                cliSendAll(client, handle(request));
                ::close(client);
            }
        });
        Fancy::fancy.logTime().success() << "CLI listening on " << path << std::endl;
    }

    void CliServer::stop()
    {
        if (!running.exchange(false))
        {
            return;
        }
        if (worker.joinable())
        {
            worker.join();
        }
        ::close(fd);
        fd = -1;
        ::unlink(path.c_str());
    }

    std::string CliServer::handle(const std::string &request)
    {
        auto space = request.find(' ');
        auto command = request.substr(0, space);
        auto argument = space == std::string::npos ? std::string() : request.substr(space + 1);

        if (command == "stop")
        {
            Globals::gGui->stopSounds();
            return "OK\n";
        }

        if (command == "list")
        {
            std::string out = "OK\n";
            for (const auto &tab : Globals::gData.getTabs())
            {
                for (const auto &sound : tab.sounds)
                {
                    out += std::to_string(sound.id) + "\t" + tab.name + "\t" + sound.name + "\n";
                }
            }
            return out;
        }

        if (command != "play" || argument.empty())
        {
            return "ERR unknown command\n";
        }

        struct Match
        {
            std::uint32_t id;
            std::string tab, name;
        };
        std::vector<Match> exact, partial;
        auto needle = lower(argument);
        bool isId = std::all_of(argument.begin(), argument.end(), [](unsigned char c) { return std::isdigit(c); });

        for (const auto &tab : Globals::gData.getTabs())
        {
            for (const auto &sound : tab.sounds)
            {
                auto name = lower(sound.name);
                Match match{sound.id, tab.name, sound.name};
                if ((isId && std::to_string(sound.id) == argument) || name == needle || lower(stem(sound.name)) == needle)
                {
                    exact.push_back(match);
                }
                else if (name.find(needle) != std::string::npos)
                {
                    partial.push_back(match);
                }
            }
        }

        auto &candidates = exact.empty() ? partial : exact;
        if (candidates.empty())
        {
            return "ERR no sound matching '" + argument + "'\n";
        }
        if (candidates.size() > 1)
        {
            std::string out = "ERR '" + argument + "' is ambiguous, matches:\n";
            for (const auto &m : candidates)
            {
                out += "  " + std::to_string(m.id) + "\t" + m.tab + "\t" + m.name + "\n";
            }
            return out;
        }

        auto playing = Globals::gGui->playSound(candidates.front().id);
        if (!playing)
        {
            return "ERR failed to play '" + candidates.front().name + "'\n";
        }
        Globals::gGui->onSoundPlayed(*playing);
        return "OK\nPlaying " + candidates.front().name + "\n";
    }
} // namespace Soundux::Objects
#endif
