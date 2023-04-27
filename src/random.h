
// Fast 64 bit random generator. Its period is nearly 2^96, so sufficient for common usage.       ** NOTE: Only used in "test mode" **
// Refer to http://www.drdobbs.com/tools/fast-high-quality-parallel-random-number/229625477?pgno=2 for further details
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RandomGenerator                                              //@---
{
    u64 seeds[3] = { 0x22727full, 0xc8903ull, 0x30d287c0ull };

    /*INLINE CONSTEXPR constexpr*/ u64 operator()()
    {
        seeds[0] = qrotl(seeds[0], 52) - qrotl(seeds[0], 9);
        seeds[1] = qrotl(seeds[1], 24) - qrotl(seeds[1], 45);
        seeds[2] -= qrotl(seeds[2], 38);

        return seeds[0] ^ seeds[1] ^ seeds[2];
    }
};                                                                  //---@
