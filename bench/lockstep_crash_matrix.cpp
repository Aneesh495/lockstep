#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include "campaign_support.hpp"
namespace {
using namespace lockstep;
const char* modes[] = {
    "wal_only",      "snapshot_plus_wal",    "periodic_sync",          "durable_prefix",
    "torn_tail",     "interior_corruption",  "snapshot_before_rename", "recovery_then_trading",
    "random_timing", "snapshot_after_rename"};
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(in), "Trial artifact open failed");
    const auto size = in.tellg();
    require(size >= 0 && size < 1000000, "Trial artifact length invalid");
    std::string bytes(static_cast<size_t>(size), '\0');
    in.seekg(0);
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(in.gcount() == static_cast<std::streamsize>(bytes.size()), "Trial artifact truncated");
    return bytes;
}
uint64_t digest(const std::string& bytes) {
    uint64_t h = 1469598103934665603ULL;
    for (char b : bytes)
        h = (h ^ static_cast<unsigned char>(b)) * 1099511628211ULL;
    return h;
}
void acknowledge(int fd, uint64_t prefix, bool block) {
    require(::write(fd, &prefix, sizeof(prefix)) == sizeof(prefix),
            "Checkpoint acknowledgement failed");
    if (block)
        while (true)
            ::pause();
}
int writerChild(const std::filesystem::path& dir, uint64_t seed, int mode, int stage, int ackFd) {
    auto cmds = campaign::commands(seed);
    MatchingEngine engine(campaign::config());
    RiskEngine risk;
    WalWriter writer((dir / "wal.log").string());
    require(writer.isOpen(), "WAL open failed");
    const uint64_t target = (mode == 3 || mode == 4) ? 12 : ((mode == 6 || mode == 9) ? 16 : 20);
    for (uint64_t i = 0; i < target; ++i) {
        const auto& r = cmds[i].record;
        require(writer.append(r.commandSeq, r.timestamp, r.payload.data(), r.payload.size(),
                              r.recordKind),
                "WAL append failed");
        std::string error;
        require(RecoveryManager::applyRecord(engine, risk, r, error), error.c_str());
        if (mode != 2 || (i + 1) % 4 == 0)
            require(writer.sync(), "WAL sync failed");
        if (i + 1 == 8 && (mode == 1 || mode == 6 || mode == 9)) {
            SnapshotWriter snapshot((dir / "snapshot.bin").string());
            require(snapshot.write(engine, risk), "Initial snapshot publication failed");
        }
        if (mode == 8 && i + 1 == 12) {
            require(writer.sync(), "Random checkpoint sync failed");
            acknowledge(ackFd, 12, false);
        }
        if (mode == 8 && i + 1 > 12)
            ::usleep(300);
    }
    require(writer.sync(), "Final sync failed");
    if (mode == 4) {
        writer.setInterruptionHook([&]() { acknowledge(ackFd, target, true); });
        const auto& r = cmds[target].record;
        writer.append(r.commandSeq, r.timestamp, r.payload.data(), r.payload.size(), r.recordKind);
        throw std::runtime_error("Torn checkpoint returned");
    }
    if (mode == 5) {
        int fd = ::open((dir / "wal.log").c_str(), O_RDWR);
        require(fd >= 0, "Corruption open failed");
        uint8_t byte = 0;
        require(::pread(fd, &byte, 1, 30) == 1, "Corruption read failed");
        byte ^= 0x80;
        require(::pwrite(fd, &byte, 1, 30) == 1 && ::fsync(fd) == 0, "Corruption write failed");
        ::close(fd);
    }
    if (mode == 6 || mode == 9) {
        SnapshotWriter snapshot((dir / "snapshot.bin").string());
        snapshot.setPublicationHook([&](int point) {
            if (point == stage)
                acknowledge(ackFd, target, true);
        });
        require(snapshot.write(engine, risk), "Interrupted snapshot returned");
        throw std::runtime_error("Snapshot checkpoint missed");
    }
    if (mode == 8)
        while (true)
            ::pause();
    acknowledge(ackFd, target, true);
    return 1;
}
int recoveryChild(const std::filesystem::path& dir, uint64_t seed, int mode) {
    auto cfg = campaign::config();
    MatchingEngine engine(cfg);
    RiskEngine risk;
    const auto snapshot =
        std::filesystem::exists(dir / "snapshot.bin") ? (dir / "snapshot.bin").string() : "";
    RecoveryManager recovery(snapshot, (dir / "wal.log").string());
    if (!recovery.recover(engine, risk)) {
        std::ofstream out(dir / "recovery-error.txt");
        out << recovery.error();
        return 2;
    }
    const auto once = campaign::observed(engine, risk, recovery.replayedMatches());
    require(recovery.recover(engine, risk), "Second recovery failed");
    require(once == campaign::observed(engine, risk, recovery.replayedMatches()),
            "Second recovery changed state/events");
    auto events = recovery.replayedMatches();
    if (mode == 7) {
        auto cmds = campaign::commands(seed);
        WalWriter writer((dir / "wal.log").string());
        for (std::size_t i = 20; i < 24; ++i) {
            const auto& r = cmds[i].record;
            require(writer.append(r.commandSeq, r.timestamp, r.payload.data(), r.payload.size(),
                                  r.recordKind),
                    "Post-recovery append failed");
            MatchingEngine::Result observed;
            std::string error;
            require(RecoveryManager::applyRecord(engine, risk, r, error, &observed), error.c_str());
            events.insert(events.end(), observed.matches.begin(), observed.matches.end());
        }
        require(writer.sync(), "Post-recovery sync failed");
    }
    std::ofstream out(dir / "actual.txt");
    out << campaign::observed(engine, risk, events);
    out.close();
    require(static_cast<bool>(out), "Recovered state output failed");
    return 0;
}
pid_t launch(const std::string& self, const std::filesystem::path& dir, uint64_t seed, int mode,
             int stage, int fd, bool recovery) {
    pid_t child = ::fork();
    require(child >= 0, "fork failed");
    if (!child) {
        int log = ::open((dir / (recovery ? "restart.log" : "writer.log")).c_str(),
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log < 0)
            _exit(126);
        ::dup2(log, STDOUT_FILENO);
        ::dup2(log, STDERR_FILENO);
        ::close(log);
        auto seedText = std::to_string(seed), modeText = std::to_string(mode),
             stageText = std::to_string(stage), fdText = std::to_string(fd), path = dir.string();
        ::execl(self.c_str(), self.c_str(), recovery ? "--recover-child" : "--writer-child",
                path.c_str(), seedText.c_str(), modeText.c_str(), stageText.c_str(), fdText.c_str(),
                static_cast<char*>(nullptr));
        _exit(127);
    }
    return child;
}
int waitBounded(pid_t child) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = ::waitpid(child, &status, WNOHANG);
        if (result == child)
            return status;
        require(result >= 0, "waitpid failed");
        ::usleep(1000);
    }
    ::kill(child, SIGKILL);
    ::waitpid(child, &status, 0);
    throw std::runtime_error("Child restart timeout");
}
void writeCounts(std::ostream& out, const std::map<std::string, uint64_t>& counts) {
    out << "{";
    bool first = true;
    for (const auto& [k, v] : counts) {
        if (!first)
            out << ",";
        first = false;
        out << "\"" << k << "\":" << v;
    }
    out << "}";
}
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 7 && std::string(argv[1]).find("-child") != std::string::npos) {
            auto dir = std::filesystem::path(argv[2]);
            auto seed = std::stoull(argv[3]);
            auto mode = std::stoi(argv[4]);
            auto stage = std::stoi(argv[5]);
            auto fd = std::stoi(argv[6]);
            return std::string(argv[1]) == "--writer-child"
                       ? writerChild(dir, seed, mode, stage, fd)
                       : recoveryChild(dir, seed, mode);
        }
        uint64_t trials = 10000, seed = 12345;
        bool incomplete = false;
        std::filesystem::path output = "artifacts/stress";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--allow-incomplete") {
                incomplete = true;
                continue;
            }
            require(i + 1 < argc, "Missing argument");
            std::string value = argv[++i];
            if (arg == "--trials")
                trials = std::stoull(value);
            else if (arg == "--seed")
                seed = std::stoull(value);
            else if (arg == "--output")
                output = value;
            else
                throw std::invalid_argument("Unknown crash argument");
        }
        require(trials > 0, "Empty campaign");
        std::filesystem::create_directories(output / "trials");
        std::ofstream raw(output / "recovery-trials.jsonl");
        std::map<std::string, uint64_t> totals, coverage;
        auto started = std::chrono::steady_clock::now();
        const auto self = std::filesystem::absolute(argv[0]).string();
        for (uint64_t trial = 0; trial < trials; ++trial) {
            int mode = static_cast<int>(trial % 10);
            int stage = mode == 6 ? static_cast<int>((trial / 10) % 2)
                                  : 2 + static_cast<int>((trial / 10) % 2);
            uint64_t trialSeed = seed + trial * 10007;
            auto dir = output / "trials" / std::to_string(trial);
            std::filesystem::create_directory(dir);
            auto cmds = campaign::commands(trialSeed);
            int pipeFds[2];
            require(::pipe(pipeFds) == 0, "Checkpoint pipe failed");
            pid_t writer = launch(self, dir, trialSeed, mode, stage, pipeFds[1], false);
            ::close(pipeFds[1]);
            pollfd pollFd{pipeFds[0], POLLIN, 0};
            uint64_t acknowledged = 0;
            int ready = ::poll(&pollFd, 1, 20000);
            if (ready <= 0 ||
                ::read(pipeFds[0], &acknowledged, sizeof(acknowledged)) != sizeof(acknowledged)) {
                ::kill(writer, SIGKILL);
                int status;
                ::waitpid(writer, &status, 0);
                throw std::runtime_error("Writer failed checkpoint: " +
                                         readFile(dir / "writer.log"));
            }
            ::close(pipeFds[0]);
            if (mode == 8)
                ::usleep(static_cast<useconds_t>(trialSeed % 2000));
            require(::kill(writer, SIGKILL) == 0, "Controlled interruption failed");
            int interrupted = waitBounded(writer);
            require(WIFSIGNALED(interrupted) && WTERMSIG(interrupted) == SIGKILL,
                    "Writer was not interrupted");
            ++totals["interrupted_processes"];
            uint64_t prefix = 0;
            WalStatus tail = WalStatus::CleanEof;
            if (mode != 5) {
                WalReader reader((dir / "wal.log").string());
                WalRecord r;
                while ((tail = reader.readRecord(r)) == WalStatus::Ok) {
                    require(r.commandSeq == prefix + 1, "Parent observed sequence gap");
                    ++prefix;
                }
                require(
                    tail == WalStatus::CleanEof || (mode == 4 && tail == WalStatus::IncompleteTail),
                    "Unexpected WAL tail");
                require(prefix >= acknowledged && prefix <= 20, "Durable prefix not retained");
            } else
                prefix = 20;
            uint64_t covered = mode == 1 || mode == 6 ? 8 : (mode == 9 ? 16 : 0);
            uint64_t count = mode == 7 ? 24 : prefix;
            const auto expected = campaign::expected(cmds, static_cast<std::size_t>(count),
                                                     static_cast<std::size_t>(covered));
            std::ofstream expectedOut(dir / "expected.txt");
            expectedOut << expected;
            expectedOut.close();
            pid_t restart = launch(self, dir, trialSeed, mode, stage, -1, true);
            int status = waitBounded(restart);
            ++totals["restarted_processes"];
            ++totals["trials_run"];
            ++coverage[modes[mode]];
            if (mode == 6 || mode == 9)
                ++coverage["snapshot_stage_" + std::to_string(stage)];
            bool good = false;
            std::string actual;
            if (mode == 5) {
                actual = readFile(dir / "recovery-error.txt");
                good = WIFEXITED(status) && WEXITSTATUS(status) == 2 &&
                       actual.find("CRC") != std::string::npos;
                if (good)
                    ++totals["expected_corruption_rejections"];
            } else {
                actual = readFile(dir / "actual.txt");
                good = WIFEXITED(status) && WEXITSTATUS(status) == 0 && actual == expected;
                ++totals["state_comparisons"];
                if (good)
                    ++totals["successful_recoveries"];
            }
            if (!good) {
                ++totals["unexpected_failures"];
                std::cerr << "Trial " << trial << " mode " << mode << " failed: " << dir << "\n";
            }
            raw << "{\"trial\":" << trial << ",\"seed\":" << trialSeed << ",\"mode\":\""
                << modes[mode] << "\",\"stage\":" << stage
                << ",\"acknowledged_prefix\":" << acknowledged << ",\"recovered_prefix\":" << prefix
                << ",\"covered_snapshot_prefix\":" << covered
                << ",\"writer_signal\":" << WTERMSIG(interrupted)
                << ",\"restart_exit\":" << (WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                << ",\"expected_digest\":" << digest(expected)
                << ",\"actual_digest\":" << digest(actual)
                << ",\"comparison_ran\":" << (mode == 5 ? "false" : "true")
                << ",\"passed\":" << (good ? "true" : "false") << "}\n";
            if ((trial + 1) % 1000 == 0) {
                raw.flush();
                std::cout << trial + 1 << " real interrupted/restarted trials\n" << std::flush;
            }
        }
        bool complete = trials >= 10000 && totals["unexpected_failures"] == 0 &&
                        totals["interrupted_processes"] == trials &&
                        totals["restarted_processes"] == trials;
        for (auto name : modes)
            if (!coverage[name])
                complete = false;
        std::ofstream out(output / "recovery.json");
        out << "{\"schema\":2,\"complete\":" << (complete ? "true" : "false")
            << ",\"requested_trials\":" << trials << ",\"seed\":" << seed << ",\"elapsed_seconds\":"
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
            << ",\"totals\":";
        writeCounts(out, totals);
        out << ",\"coverage\":";
        writeCounts(out, coverage);
        out << "}\n";
        out.close();
        raw.close();
        require(static_cast<bool>(out) && static_cast<bool>(raw), "Recovery artifacts failed");
        std::cout << (complete ? "PASS" : "INCOMPLETE") << ": " << trials << " trials, "
                  << totals["successful_recoveries"] << " recoveries, "
                  << totals["expected_corruption_rejections"] << " expected rejections, "
                  << totals["unexpected_failures"] << " unexpected failures\n";
        return complete || (incomplete && totals["unexpected_failures"] == 0) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
