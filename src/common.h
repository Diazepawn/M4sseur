
static_assert(__cplusplus >= 202002L,  "C++20 required (Clang >= 10, GCC >= 11, MSVC >= 19.29 with /Zc:__cplusplus)");
static_assert(__STDCPP_THREADS__ == 1, "Thread support required");

//#include <bits/stdc++.h>
//#include <cstdint>
//#include <cstdio>
#include <cassert>      //@-
#include <chrono>       //@-
#include <ctime>        //@-
#include <cstring>
#include <algorithm>
#include <thread>
#include <vector>
#include <set>         //@-
#include <random>      //@-

#ifdef _WIN32
#include <immintrin.h>
#include <time.h>
#endif

#define IFC if constexpr

using namespace std;
using namespace chrono;

using u8 = uint8_t;
using s16 = int16_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t; // prefer to use u64 over auto (nanonizer will optimize this better for lzma compression)
using flt = double;

using Mask = u64;
using Move = u16;


enum /* @Color */ : int
{
    WHITE = 0,
    BLACK,
    WHITE_BLACK,
    NO_COLOR
};
using Color = int;


enum /* @Square */ : u32
{
    A1 = 0, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8
};
using Square = u32;


enum /* @File */ : u32
{
    FileA = 0, FileB, FileC, FileD, FileE, FileF, FileG, FileH
};
using File = u32;


enum /* @Rank */ : u32
{
    Rank1 = 0, Rank2, Rank3, Rank4, Rank5, Rank6, Rank7, Rank8
};
using Rank = u32;


enum /* @Piece */ : u32
{
    // Pieces
    PAWN = 0,   // 0
    KNIGHT,     // 1  
    BISHOP,     // 2
    ROOK,       // 3
    QUEEN,      // 4
    KING,       // 5
    NONE
};
using Piece = u32;


enum /* @PieceMask */ : int
{
    // Piece Masks
    PMASK_NONE = 0,
    PMASK_PAWN = 1,
    PMASK_KNIGHT = 2,
    PMASK_BISHOP = 4,
    PMASK_ROOK = 8,
    PMASK_QUEEN = 16,
    PMASK_KING = 32,
    PMASK_NB = 6,     // light pieces
    PMASK_RQ = 24,    // heavy pieces
    PMASK_BRQ = 28,   // sliders
    PMASK_NBRQ = 30,  // pieces
    PMASK_ALLBUTKING = 31,
    PMASK_ALL = 63
};
using PieceMask = int;


char strBuf[30000];
char* strPtr;

void readUCILine()
{
    memset(strBuf, 0, 30000);
//    for (auto& t : strBuf) t = 0;

    strPtr = strBuf;
    while ((*strPtr++ = getchar()) != 10 /*'\n'*/)
        ;
    *--strPtr = 0 /*'\0'*/;
    strPtr = strBuf;
}


auto strCompare(auto strLiteral)
{
    assert(strLiteral != nullptr);

    if (!strncmp(strLiteral, strPtr, strlen(strLiteral)))
    {
        strPtr += strlen(strLiteral);
        while (*strPtr == ' ') strPtr++;

        return 1;
    }
    return 0;
}

// --- Time helpers
using Clock = steady_clock;                                 //@-
#define qnow Clock::now()                                   //@-


auto alignMove(Move m, bool c)
{
    return m & 0x3f ^ 56 * c | (m >> 6 & 0x3f ^ 56 * c) << 6 | m & 0xf000;
}

auto mksq(File c, Rank m)                                   //@---
{
    return c | m << 3;
}                                                           //---@

auto mkmove(Square s, Square i, Piece p = 0)
{
    return s | i << 6 | p << 12;
}

auto mkmsk(Square s)
{
    return 1ull << s;
}

auto convertMoveToText(Move m)
{
    string strBuf;
    strBuf += 'a' + (m >> 0 & 7);
    strBuf += '1' + (m >> 3 & 7);
    strBuf += 'a' + (m >> 6 & 7);
    strBuf += '1' + (m >> 9 & 7);

    if (m >> 12)
        strBuf += " nbrq"[m >> 12];

    return strBuf;
}

auto convertTextToMove(auto s)
{
#if 1
    return mkmove(s[0] - 'a' | (s[1] - '1') << 3, s[2] - 'a' | (s[3] - '1') << 3,
        s[4] == 'n' ? KNIGHT : s[4] == 'b' ? BISHOP :
        s[4] == 'r' ? ROOK : s[4] == 'q' ? QUEEN : 0);
#else // orig
    return mkmove(mksq(s[0] - 'a', s[1] - '1'), mksq(s[2] - 'a', s[3] - '1'),
        s[4] == 'n' ? KNIGHT : s[4] == 'b' ? BISHOP :
        s[4] == 'r' ? ROOK : s[4] == 'q' ? QUEEN : 0);
#endif
}


auto qrotl(auto val, int i)
{
    return val << i | val >> (8 * sizeof(val) - i);
}


#ifdef _WIN32
auto qcount(Mask m) { return (int)_mm_popcnt_u64(m); }
#else
auto qcount(Mask m) { return __builtin_popcountll(m); }
#endif


auto getNextBit(Mask& m)
{
#ifdef _MSC_VER
    unsigned long ret = 0u;
    _BitScanForward64(&ret, m);
    m &= m - 1;
    return (int)ret;
#else
    auto s = __builtin_ctzll(m);
    m &= m - 1;
    return s;
#endif
}

#ifdef _WIN32
u64 qflip(Mask m, bool c = 1) { return c ? _byteswap_uint64(m) : m; }
#else
u64 qflip(Mask m, bool c = 1) { return c ? __builtin_bswap64(m) : m; }
#endif
