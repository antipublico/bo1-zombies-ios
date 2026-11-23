#include <database/db_temp_stream_cursor.h>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
}

int main()
{
    using bo1::fastfile::detail::TempStreamCursor;

    TempStreamCursor temp(64);
    Check(temp.ResetRoot(), "reset starts an empty root");
    Check(temp.Advance(12) && temp.Position() == 12, "root TEMP header");
    Check(temp.Push() && temp.Depth() == 1, "enter nested header");
    Check(temp.Advance(40, 8) && temp.Position() == 56, "nested alignment and read");
    Check(!temp.Advance(9) && temp.Position() == 56, "reject oversized nested header");
    Check(temp.Pop() && temp.Position() == 12, "nested return restores parent cursor");
    Check(temp.Advance(8, 8) && temp.Position() == 24, "sibling reuses temporary space");
    Check(temp.HighWater() == 56, "track maximum nested TEMP use");
    Check(!temp.Pop() && temp.Position() == 24, "reject stack underflow");

    Check(temp.ResetRoot() && temp.Position() == 0 && temp.HighWater() == 0,
        "next root starts at TEMP base");
    Check(temp.Advance(12) && temp.Push(), "root to child");
    Check(temp.Advance(16) && temp.Push(), "child to grandchild");
    Check(temp.Advance(20) && temp.Position() == 48, "grandchild TEMP header");
    Check(temp.Pop() && temp.Position() == 28, "restore child cursor");
    Check(temp.Pop() && temp.Position() == 12, "restore root cursor");
    Check(temp.HighWater() == 48, "track nested peak across restores");

    Check(!temp.Advance(100) && temp.Position() == 12, "reject capacity overflow");
    Check(!temp.Advance(1, 0) && temp.Position() == 12, "reject zero alignment");
    Check(!temp.Advance(1, 3) && temp.Position() == 12, "reject non-power-of-two alignment");
    Check(!temp.Advance(std::numeric_limits<std::uint64_t>::max(), 8)
        && temp.Position() == 12, "reject integer overflow without wrapping");
    Check(!temp.Advance(0, 128) && temp.Position() == 12,
        "alignment alone may exceed TEMP capacity");
    Check(temp.Push() && !temp.ResetRoot() && temp.Position() == 12,
        "cannot reset root while nested");
    Check(temp.Pop() && temp.ResetRoot(), "reset after stack is balanced");

    for (int i = 0; i < 64; ++i)
        Check(temp.Push(), "push up to the engine's maximum stack depth");
    Check(!temp.Push() && temp.Depth() == 64, "reject stack overflow");
    Check(!temp.ResetRoot(), "reject reset with outstanding frames");
    for (int i = 0; i < 64; ++i)
        Check(temp.Pop(), "unwind the full stack");
    Check(temp.ResetRoot() && temp.Depth() == 0, "restore after full unwind");

    TempStreamCursor empty(0);
    Check(empty.Advance(0) && empty.Position() == 0, "empty block accepts empty read");
    Check(!empty.Advance(1) && empty.Position() == 0, "empty block rejects data");
    TempStreamCursor boundary(64);
    Check(boundary.Advance(64) && boundary.Position() == 64, "exact boundary accepted");
    Check(!boundary.Advance(1), "bytes past exact boundary rejected");

    std::printf("%u TEMP cursor checks, %u failures\n", checks, failures);
    return failures != 0;
}
