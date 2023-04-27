
//#define DETECT_HARD_COLLISIONS

#ifdef DETECT_HARD_COLLISIONS
#include <unordered_map>
mutex dhcMutex;
unordered_map<u64, Position> positionsMade;
#endif

enum /* @Score */ : int
{
    SCORE_DRAW = 0,
    SCORE_MATE = 31000, // minus "mate in plies"
    SCORE_INVALID = 31999,
    SCORE_INFINITY = 32000
};
using Score = int;


enum /* @TTLeap */ : char
{
    LEAP_EQUAL = 1,
    LEAP_LOWER = 2,
    LEAP_UPPER = 4
};
using TTLeap = char;

static_assert((TTSize& (TTSize - 1u)) == 0, "TTSize must be a power of 2 value.");


struct TTDataEntry
{
    char depth = -1, movePlayedCnt{}, leap;
    u16 move{};
    s16 score;

    operator u64() { return *(u64*)this; }
};
static_assert(sizeof(TTDataEntry) == 8);


struct TTEntry
{
    atomic<u64> hsh/*{}*/, dat/*{}*/;

    static_assert(atomic_uint64_t::is_always_lock_free);
};
static_assert(sizeof(TTEntry) == 16);


#ifdef _MSC_VER
struct TTable // due to compiler bug: fatal error C1001: Interner Compilerfehler.
{
    auto& operator[](u64 index)
    {
        assert(index < table.size());
        return table[index];
    }

    auto size()  { return table.size(); }
    auto begin() { return table.begin(); }
    auto end()   { return table.begin(); }

    TTable() : table(TTSize) {}

    vector<TTEntry> table;
};
    TTable ttable;
#else
    vector<TTEntry> ttable(TTSize);
#endif


u64 makeHash(const Position& pos)
{
    static_assert(sizeof(Position) == 64);
    static_assert(offsetof(Position, flags) == 0);
    static_assert(sizeof(Flags) == 2);

    // (This is way smaller (in C++ code size) and almost as fast as ZOBRIST, and causes
    //  the same amount of hash collisions (see testing.h "hashingQualityScore"))
    // **Update**: Not using ZOBRIST sucks!

#if 0 // orig fast
    const u64* p = (u64*)&pos;
    u64 m = (*p++ & ~0xffffull);
#define Z m = qrotl(qrotl(*p++ * 0x9e3779b185ebca87ull, 31) ^ m, 27);
    Z Z Z Z Z Z Z

#else // improved, slower, less collisions
    const u64* p = (u64*)&pos;
    auto i = 24;
    u64 m = *p++;
#define Z m = qrotl(*p, i & 63) ^ qrotl(*p, (29 + i) & 63) ^ qrotl(qrotl(*p * 0x9e3779b185ebca87ull, 31) ^ m,  27); i += 18; p++;
    Z Z Z Z Z Z Z
#endif

#ifdef DETECT_HARD_COLLISIONS
    u64 hsh =
#else
    return
#endif
        /////////////////////////////////////////////////////////////
        m ^ qrotl(m, 51) ^ qrotl(m, 3) ^ qrotl(m, 16) ^ qrotl(m, 32);
        /////////////////////////////////////////////////////////////

#ifdef DETECT_HARD_COLLISIONS
    {
        lock_guard<mutex> lock(dhcMutex);

        auto it = positionsMade.find(hsh);
        if (it != positionsMade.end())
        {
            Position pos1st = (*it).second;
            Position pos2nd = pos;
            pos1st.flags = pos2nd.flags = 0;
            if (pos1st != pos2nd)
            {
                printf("#### HARD COLLISION DETECTED (%llx) ####\n", (long long unsigned int) hsh);
#ifdef DUMP
                printf("#### position generated first:\n");
                pos1st.dump();

                printf("#### vs current position:\n");
                pos2nd.dump();
#endif
                assert(false);
            }
        }
        else
            positionsMade[hsh] = pos;
    }
    return hsh;
#endif
}

void loadTTEntry(u64 m, TTDataEntry& data)
{
#if 1
    u64 ttHash = ttable[m % TTSize].hsh.load(memory_order_relaxed); // .load(mor) very important i.o. to prevent gaps, see assembly
    u64 ttData = ttable[m % TTSize].dat.load(memory_order_relaxed);
#else
    u64 ttHash = ttable[m % TTSize].hsh; // .load(memory_order_..) very important i.o. to prevent gaps in between loads, see assembly
    u64 ttData = ttable[m % TTSize].dat;
#endif

    // Note: TTDataEntry::depth will be -1 if load wasn't ok (messed up hash due to race condition or TT Entry not present)
    //       *ANYHOW*, the data part is important as it might contain the important "movePlayedCnt" value that prevents repetitions;
    //                 never overwrite these entries
    *((u64*)&data) = ttData;
    if ((ttHash ^ ttData) != m)
        data.depth = -1, data.move = 0;
}

// Note: Even if the load fails, TTDataEntry::movePlayedCnt can be checked before
//       overwriting a possible played move (important for repetition recognition)
void storeTTEntry(u64 m, TTDataEntry data)
{
#if 1
    ttable[m % TTSize].hsh.store(m ^ data, memory_order_relaxed);
    ttable[m % TTSize].dat.store(data, memory_order_relaxed);
#else
    ttable[m % TTSize].hsh = m ^ data;
    ttable[m % TTSize].dat = data;
#endif
}
