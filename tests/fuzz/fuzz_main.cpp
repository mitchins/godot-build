// Portable fuzz driver (D0010): bounded mutation runs over the committed
// seed corpus and regression inputs, same flags interface as libFuzzer
// (-runs=N -max_len=B), deterministic by default so failures reproduce.
// Rationale: Apple clang 21 ships no libclang_rt.fuzzer runtime; this keeps
// the fuzz gate executable on every platform with only ASan/UBSan. Linux CI
// may additionally use libFuzzer against the same target function.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

struct Options {
    std::uint64_t runs = 20000;
    std::size_t max_len = 65536;
    std::uint64_t seed = 0xC0FFEE;
    std::vector<std::string> corpus_dirs;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("-runs=", 0) == 0) {
            options.runs = std::strtoull(arg.c_str() + 6, nullptr, 10);
        } else if (arg.rfind("-max_len=", 0) == 0) {
            options.max_len = std::strtoul(arg.c_str() + 9, nullptr, 10);
        } else if (arg.rfind("-seed=", 0) == 0) {
            options.seed = std::strtoull(arg.c_str() + 6, nullptr, 10);
        } else {
            options.corpus_dirs.push_back(arg);
        }
    }
    return options;
}

std::vector<std::vector<std::uint8_t>> load_corpus(const Options& options) {
    std::vector<std::vector<std::uint8_t>> seeds;
    for (const auto& dir : options.corpus_dirs) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }
            std::FILE* file = std::fopen(it->path().string().c_str(), "rb");
            if (!file) {
                continue;
            }
            std::vector<std::uint8_t> bytes;
            std::uint8_t buffer[4096];
            std::size_t read;
            while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
                bytes.insert(bytes.end(), buffer, buffer + read);
            }
            std::fclose(file);
            if (!bytes.empty()) {
                seeds.push_back(std::move(bytes));
            }
        }
    }
    if (seeds.empty()) {
        seeds.emplace_back(); // empty input is itself a seed
    }
    return seeds;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse(argc, argv);
    const auto seeds = load_corpus(options);

    std::fprintf(stderr, "fuzz-driver: %llu runs over %zu seeds (max_len=%zu, seed=%llu)\n",
                 static_cast<unsigned long long>(options.runs), seeds.size(), options.max_len,
                 static_cast<unsigned long long>(options.seed));

    // Run every seed verbatim first: committed corpus + regressions always execute.
    for (const auto& seed : seeds) {
        LLVMFuzzerTestOneInput(seed.data(), seed.size());
    }

    std::mt19937_64 rng(options.seed);
    std::vector<std::uint8_t> input;
    for (std::uint64_t run = 0; run < options.runs; ++run) {
        input = seeds[rng() % seeds.size()];
        const int mutations = 1 + static_cast<int>(rng() % 8);
        for (int m = 0; m < mutations; ++m) {
            switch (rng() % 5) {
            case 0: // flip a bit
                if (!input.empty()) {
                    input[rng() % input.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
                }
                break;
            case 1: // overwrite a byte
                if (!input.empty()) {
                    input[rng() % input.size()] = static_cast<std::uint8_t>(rng());
                }
                break;
            case 2: // truncate
                if (!input.empty()) {
                    input.resize(rng() % input.size());
                }
                break;
            case 3: // append up to 64 random bytes
                for (int i = 0, n = static_cast<int>(rng() % 64); i < n; ++i) {
                    input.push_back(static_cast<std::uint8_t>(rng()));
                }
                break;
            case 4: // splice two seeds
                const auto& other = seeds[rng() % seeds.size()];
                if (!other.empty()) {
                    input.insert(input.end(), other.begin(),
                                 other.begin() + static_cast<std::ptrdiff_t>(rng() % other.size()));
                }
                break;
            }
            if (input.size() > options.max_len) {
                input.resize(options.max_len);
            }
        }
        LLVMFuzzerTestOneInput(input.data(), input.size());
    }

    std::fprintf(stderr, "fuzz-driver: completed without crashes\n");
    return 0;
}
