#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace Soundux
{
    namespace Objects
    {
#if defined(__linux__)
        std::string cliSocketPath();
        bool cliSendAll(int, const std::string &);

        /* Talks to an already running Soundux instance over a unix socket.
         * Usage: soundux play <sound name|id> | soundux stop | soundux list */
        namespace Cli
        {
            bool isCommand(const std::vector<std::string> &args);
            int runClient(const std::vector<std::string> &args);
        } // namespace Cli

        /* Runs inside the main instance and executes commands received from the client */
        class CliServer
        {
            int fd = -1;
            std::string path;
            std::thread worker;
            std::atomic<bool> running{false};

            std::string handle(const std::string &);

          public:
            ~CliServer();
            void start();
            void stop();
        };
#endif
    } // namespace Objects
} // namespace Soundux
