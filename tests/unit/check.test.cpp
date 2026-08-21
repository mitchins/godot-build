#include <cstdio>

#include <doctest/doctest.h>

#include "fauxbuild/check.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#endif

TEST_CASE("FB_CHECK passes on a true condition") {
    FB_CHECK(1 + 1 == 2);
    CHECK(true);
}

TEST_CASE("NDEBUG is not defined in any build configuration") {
    // Plan §3.3 / D0006: release must retain content-safety assertions, so no
    // configuration may define NDEBUG. This test fails to compile-check at
    // build time of any configuration that reintroduces it.
#ifdef NDEBUG
    FAIL_CHECK("NDEBUG must not be defined by any FauxBuild build configuration");
#else
    CHECK(true);
#endif
}

#if !defined(_WIN32)
TEST_CASE("FB_CHECK aborts on a false condition") {
    // Flush doctest's buffered banner so the child's termination cannot re-emit it.
    std::fflush(stdout);
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        // fork() copies doctest's signal handlers; restore the default so abort()
        // terminates the child without doctest reporting a spurious crash.
        std::signal(SIGABRT, SIG_DFL);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        FB_CHECK(1 == 2);
        _exit(0); // reachable only if FB_CHECK failed to fire
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}
#endif
