#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include "../bench/campaign_support.hpp"
#include "lockstep/common/endian.hpp"
int main(int argc, char** argv) {
    try {
        uint64_t runs = 2000, seed = 12345;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (i + 1 >= argc)
                throw std::invalid_argument("Missing fuzz option");
            auto n = std::stoull(argv[++i]);
            if (arg == "--runs")
                runs = n;
            else if (arg == "--seed")
                seed = n;
            else
                throw std::invalid_argument("Unknown fuzz option");
        }
        if (!runs || runs > 1000000)
            throw std::invalid_argument("Invalid fuzz bound");
        auto dir = std::filesystem::temp_directory_path() /
                   ("lockstep-snapshot-fuzz-" + std::to_string(getpid()));
        std::filesystem::create_directory(dir);
        auto base = dir / "base";
        lockstep::MatchingEngine initial(campaign::config());
        lockstep::RiskEngine initialRisk;
        for (const auto& s : campaign::commands(seed, 20)) {
            std::string error;
            if (!lockstep::RecoveryManager::applyRecord(initial, initialRisk, s.record, error))
                throw std::runtime_error(error);
        }
        lockstep::SnapshotWriter writer(base.string());
        if (!writer.write(initial, initialRisk))
            throw std::runtime_error(writer.error());
        std::ifstream in(base, std::ios::binary);
        std::vector<uint8_t> valid{std::istreambuf_iterator<char>(in), {}};
        std::mt19937_64 rng(seed);
        uint64_t accepted = 0, rejected = 0;
        for (uint64_t i = 0; i < runs; ++i) {
            auto bytes = valid;
            if (i % 4 == 0)
                bytes.resize(static_cast<size_t>(rng() % (bytes.size() + 1)));
            else if (i % 4 < 3) {
                bytes[static_cast<size_t>(rng() % (bytes.size() - 4))] ^=
                    static_cast<uint8_t>(1u << (rng() % 8));
                if (i % 4 == 2)
                    lockstep::writeBeU32(bytes.data() + bytes.size() - 4,
                                         lockstep::Crc32C::compute(bytes.data(), bytes.size() - 4));
            }
            auto path = dir / "mutant";
            {
                std::ofstream out(path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
            }
            lockstep::MatchingEngine engine(campaign::config());
            lockstep::RiskEngine risk;
            lockstep::SnapshotReader reader(path.string());
            bool ok = reader.read(engine, risk);
            if (i % 4 == 3 && (!ok || engine.computeStateDigest() != initial.computeStateDigest() ||
                               risk.computeDigest() != initialRisk.computeDigest()))
                throw std::runtime_error("Valid snapshot state changed");
            if (!ok && (engine.totalOrderCount() != 0 || !risk.clients().empty()))
                throw std::runtime_error("Rejected snapshot partially installed");
            ok ? ++accepted : ++rejected;
        }
        std::filesystem::remove_all(dir);
        std::cout << "Total tests: " << runs << "; accepted=" << accepted
                  << "; rejected=" << rejected << "; seed=" << seed << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
