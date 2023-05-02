
#ifdef TEST

#include <fstream>
#include <optional>
#include <map>
#include <functional>
#include <set>

const auto TEST_ONLY_SEARCH = false;

using perfDurationNano = chrono::nanoseconds;
using perfDurationMicro = chrono::microseconds;
using perfDurationMilli = chrono::milliseconds;
using perfClock = chrono::high_resolution_clock;
using perfTimePoint = perfClock::time_point;


/////////////////////////////////////////////////////////////////////////////////////////
/// Test base class
/////////////////////////////////////////////////////////////////////////////////////////
template<class T>
struct Test
{
    #define __TESTNAME const char* name{};
    #define CONSTRUCT_TEST(_name) __TESTNAME _name() : name(__func__) {}

    struct Result
    {
        Result(optional<bool> p = nullopt, string r = "") :
            passed(p), reason(r) {}

        optional<bool> passed;
        string reason;
    };

    string run()
    {
        T& derived = static_cast<T&>(*this);

        string essInfo;
        string addInfo;

        if (derived.enable)
        {
            printf("-------------------------------------------------------------\n");
            printf("---- Running test  <%s>\n", derived.name);
            printf("-------------------------------------------------------------\n");

            Result res = derived.doRun();

            essInfo = res.passed.has_value() ? ((*res.passed) ? "passed  " : "FAILED  ") : ("executed");
            if (res.passed.has_value() && !(*res.passed) && !res.reason.empty())
                addInfo = ", reason: " + res.reason;
        }
        else
        {
            printf("---- Skipping test  <%s>\n", derived.name);
            essInfo = "SKIPPED ";
        }

        printf("---- Test %s %s\n", essInfo.data(), addInfo.data());
        printf("-------------------------------------------------------------\n\n");

        //		return format("{} test <{}>{}\n", essInfo, derived.name, addInfo);
        return essInfo + " <" + derived.name + ">" + addInfo + "\n";
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// HashPerformance testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct HashPerformance : Test<HashPerformance>
{
    CONSTRUCT_TEST(HashPerformance)

#ifdef _DEBUG
    const bool enable = false;
#else
    const bool enable = true;
#endif
    u64 iterations = 32 * 1024 * 1024;
    using perfDuration = perfDurationMilli;

    Result doRun() noexcept
    {
        printf("\n************ TESTING SPEED OF HASH FUNCTION ************\n");

        Position pos[16];
        pos[0].initFromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -");

        MoveList moves;
        pos[0].generateLegalMoves(moves);
        assert(moves.cnt >= 15);

        for (int i = 1; i < 16; i++)
        {
            pos[i] = pos[0];
            pos[i].makeMove(moves.entries[i - 1]);
        }

        u64 magicValue = 1234u;
        perfTimePoint t = perfClock::now();
        ////////////////////////////////////////////////////////////////////////////////////
        {
            for (int i = 0; i < iterations; i++)
            {
                u64 hsh = makeHash(pos[i & 15]);

                magicValue ^= hsh; // prevent that optimizer will remove stuff
            }
        }
        ////////////////////////////////////////////////////////////////////////////////////
        perfDuration elapsed = chrono::duration_cast<perfDuration>(perfClock::now() - t);

        printf("---- Creation of %d M hashes (out of %d MB data) took %d ms (magic=%d)\n",
            (int) (iterations / (1024 * 1024)), (int)(iterations * 64 / (1024 * 1024)),
            (int)elapsed.count(), (int)magicValue);

        return {};
    }

};

bool SUCCESS;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// TTable testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct TTableTest : Test<TTableTest>
{
    CONSTRUCT_TEST(TTableTest)

    const bool enable = true;
    const bool checkForHardCollisions = false; // <-- needs much memory and slows everything down, use it rarely

#ifdef _DEBUG
    u64 iterations = 1024 * 1024;
#else
    u64 iterations = 4096 * 1024;
#endif
    using perfDuration = perfDurationMilli;

    RandomGenerator randGen;
    mt19937 rng;

    Position pos;
    Position posDo;
    MoveList moves;

    void doRun(u64 numIters, u64 TestTTSize, Result& res) noexcept
    {
#if 0
        for (int j = 0; j < 10; j++)
        {
            // say we have a table with size 1024, writing 1024 entries...
            u64 NUM_ENTRIES = 1000000;
            u64 NUM_ITERS =    500000;

            int simulatedCollisions = 0;
            int simulatedNumValidEntries = 0;
            for (u64 i = 0; i < NUM_ITERS; i++)
            {
                double chanceOfNoCollision = (double)(NUM_ENTRIES - simulatedNumValidEntries) / NUM_ENTRIES;
                {
                    u64 veryRandom = randGen() ^ qrotl(randGen(), 29) ^ qrotl(randGen(), 17) ^ qrotl(randGen(), 38) ^ qrotl(randGen(), 12) ^ qrotl(randGen(), 55);
                    if ((veryRandom & 0xffffffffffffull) > (double)0xffffffffffffull * chanceOfNoCollision)
                        simulatedCollisions++;
                    else
                        simulatedNumValidEntries++;
                }
            }

            double avgChanceSimulatedColl = (double)simulatedCollisions / NUM_ITERS;
            double avgChanceSimulatedNoColl = (double)simulatedNumValidEntries / NUM_ITERS;
            printf("NUM_ENTRIES %d / NUM_ITERS %d = %.3f%% %.3f%%\n", (int)NUM_ENTRIES, (int)NUM_ITERS, avgChanceSimulatedColl * 100.0, avgChanceSimulatedNoColl * 100.0);
        }
        return;
#else
        int repeatedPosCnt = 0;
        unordered_map<u64, Position> repeatedPos;
        int hardCollisions = 0;

        int totalIters = 0;
        int collisions = 0;
        int numValidEntries = 0;
        int simulatedCollisions = 0;
        int simulatedNumValidEntries = 0;
        double simulatedCollisionsVal = .0;

        // clear entire hash table first
        memset(&ttable[0], 0, TTSize * 16);

        perfTimePoint t = perfClock::now();

        const char* randomPositionsFEN[] = {
            "r3k1r1/p1ppqPb1/bn3np1/4N3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQq - 0 1", //TEST TODO REMOVE
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - ",
            "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
            "rnbqkbnr/pppp1ppp/8/8/4Pp2/5N2/PPPP2PP/RNBQKB1R b KQkq - 1 3",
            "r1bqkb1r/ppp2ppp/2np1n2/8/4PB2/2NP1N2/PPP3PP/R2QKB1R b KQkq - 0 6",
            "r1bqkbnr/pp1p1ppp/2n1p3/2p5/3PP3/2P2N2/PP3PPP/RNBQKB1R b KQkq - 0 4",
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
            "rn1qk2r/1p2bppp/p2pbn2/4p3/4P3/1NN1BP2/PPPQ2PP/R3KB1R b KQkq - 2 9",
            "r1bq1rk1/1p2n1bp/2pp1np1/4pp2/1PP5/2NPP1P1/4NPBP/1RBQ1RK1 w - - 0 13",
            "8/8/5k2/8/8/3P4/2K5/8 w - - 0 1",
            "rnbqk2r/ppp1ppbp/3p1np1/8/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 2 5",
            "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
            "2r1k3/pp3p2/1q2p3/n2pPP2/1b1P2rB/1PN5/P2Q2B1/2R2RK1 b - - 1 23",
            "8/p3k2p/2p3p1/8/2PN4/2P3P1/r6P/7K w - - 0 27",
            "8/8/5r2/8/1k4B1/p7/8/1K6 b - - 3 60",
            "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
            "8/1K2p3/8/8/2P2k2/8/8/8 w - - 0 1",
            "3r1rk1/6q1/pN4p1/P1pP1n1p/4p3/4P1P1/1Q3R1P/5RK1 w - - 1 30",
            "k7/p1K3pp/8/8/8/8/4PPP1/8 w - - 0 1",
            "2rq1rk1/1p2bppp/p3bn2/3p4/3Q4/1PN1P3/PB2BPPP/R2R2K1 w - - 2 14",
            "rn1qk2r/1bp1bppp/p3pn2/1p4B1/3P4/2NB1N2/PP3PPP/R2Q1RK1 w kq - 2 11",
            "R7/2r2pk1/4p1pp/1p2P3/2b1B2P/5PK1/3r2P1/R7 b - - 6 34",
            "1r3rk1/pp1qbnp1/2p1bp2/1P1pp2p/Q1P3nP/2NPPNP1/P2B1PB1/1R3RK1 w - - 0 18",
            "8/4kp2/7p/4PP2/6K1/3R4/3pr3/8 w - - 1 51",
            "1r1r1b1k/p4np1/3q1p2/3N3N/1ppP2QP/4P1P1/P2B1PB1/5RK1 w - - 1 29",
            "4k3/8/4pP2/6K1/8/3R3p/3pr3/8 w - - 0 56",
            "r1bqkbnr/pppp2pp/2n5/4pp2/2P5/2N3P1/PP1PPPBP/R1BQK1NR b KQkq - 1 4",
            "r2q1rk1/1b2bppp/p1n1p3/1pp5/4pP2/2PPBNN1/PP4PP/R2Q1RK1 w - - 0 13",
            "8/8/2p5/4kn2/6pp/R7/1PP4P/3K4 b - - 1 34",
            "2r3k1/pppn1pbp/3p2p1/3P1bq1/PP1P4/RN3R2/Q5PP/2B4K b - - 0 20",
            "r3kbnr/1q1p1ppp/p3p3/1p2P3/8/2NQB3/PPP2PPP/R3K2R b KQkq - 1 12",
            "8/n2p4/2k5/1p6/4P3/5K2/6P1/3B4 w - - 0 1",
            "8/p5k1/8/8/8/8/1K5P/8 b - - 0 1",
            "1rbqk2r/R3nppp/8/1p1pP3/2p1nP2/2P2Q2/1PB3PP/1NB2RK1 b k - 1 18",
            "r2qrbk1/1b1p1ppp/p1n3n1/1pp1pN2/4P3/2PPB3/PPBN1PPP/R2Q1RK1 w - - 9 13",
            "6k1/1r3pp1/R7/3p2p1/3P4/5P1P/3R1KP1/1r6 b - - 3 40",
            "r3r1k1/2pqnpp1/pb1p1n1p/8/3PP3/5NNP/1PQ2PP1/R1B1R1K1 w - - 3 20",
            "3r1rk1/4Rp1p/6p1/p4q2/Q2P2n1/5B2/PP4PP/3R2K1 b - - 0 25",
            "3q1rk1/1Q6/p2P1rp1/P1p1Nn2/4pR2/4P1p1/3R3P/6K1 w - - 4 41",
            "4r1k1/ppp2R2/3p3p/1q1P2p1/3P4/8/6PP/5RBK b - - 2 36",
        };
        constexpr auto numFENs = sizeof(randomPositionsFEN) / sizeof(*randomPositionsFEN);

        u64 check50 = 0;
        u64 check50Cnt = 0;
        double numComputed2{};

        rng = mt19937(0x78563412);

        for (u64 i = 0; ; i++)
        {
            pos.initFromFEN(randomPositionsFEN[i % numFENs]);
            if (i > numFENs)
            {
                moves.cnt = 0;
                pos.generateLegalMoves(moves);
                if (moves.cnt == 0)
                    continue;
                pos.makeMove(moves.entries[randGen() % moves.cnt]);
            }

            for (u64 depth = 0; depth < 512; depth++)
            {
                moves.cnt = 0;
                pos.generateLegalMoves(moves);
                if (moves.cnt == 0)
                    break;
                for (int j = 0; j < moves.cnt; j++)
                {
                    posDo = pos;
                    posDo.makeMove(moves.entries[j]);
                    //				posDo.flipSides();

                    u64 hsh = makeHash(posDo);
                    posDo.flags = 0u;

#if 0 // sanity check to make sure that makeHash masks out the "flags" var in Position
                    posDo.flags = 0u;
                    u64 hshSameOrMakeHashWrong = makeHash(posDo);
                    assert(hshSameOrMakeHashWrong == hsh);
#endif

                    u64 hashStored = ttable[hsh % TestTTSize].hsh.load(memory_order_relaxed);

#if 0 // sanity check
                    u64 hashStoredDirect = ttable[hsh % TestTTSize].hash;
                    assert(hashStored == hashStoredDirect);
#endif

                    if (hashStored == hsh) // repeated position (happens often, so filter out)
                    {
                        if (checkForHardCollisions)
                        {
                            auto it = repeatedPos.find(hsh);
                            if (it != repeatedPos.end())
                            {
                                Position tmp = (*it).second;
                                if (tmp != posDo)
                                {
                                    // This should NEVER happen; chances per check are about 1:64^2 (theoretically: for 32^2 checks about sqrt(64^2))
#ifdef DUMP
                                    printf("!!!!!!!!!!!!!!!! HARD COLLISION DETECTED (%llx) in TT: !!!!!!!!!!!!!!!!\n", (long long unsigned int) hsh);
                                    u64 tmpHsh = makeHash(tmp);
                                    printf("!!!!!!!!!!!!!!!! (%llx) !!!!!!!!!!!!!!!!\n", (long long unsigned int) tmpHsh);
                                    tmp.dump();

                                    printf("!!!!!!!!!!!!!!!! VS CURRENT: !!!!!!!!!!!!!!!!\n");
                                    posDo.dump();
#endif
                                    hardCollisions++;
                                }
                            }
                        }
                        repeatedPosCnt++;
                        continue;
                    }

                    // For all elements to collide, the elements should be equal to twice the total number of hash values.
                    // If we hash M values and total possible hash values is T, then the expected number of collisions will be C = M * (M - 1) / 2T

                    double chanceOfNoCollision = (double)(TestTTSize - simulatedNumValidEntries) / TestTTSize;
                    {
                        u64 veryRandom = randGen() ^ qrotl(randGen(), 29) ^ qrotl(randGen(), 17) ^ qrotl(randGen(), 38) ^ qrotl(randGen(), 12) ^ qrotl(randGen(), 55);

                        double checkVal = (veryRandom & 0xffffffffffffull) / (double)0xffffffffffffull;
                        if (checkVal <= 0.5)
                            check50++;
                        check50Cnt++;

                        simulatedCollisionsVal += 1. - chanceOfNoCollision;

                        if (checkVal > chanceOfNoCollision)
                            simulatedCollisions++;
                        else
                            simulatedNumValidEntries++;
                    }

                    if (hashStored == 0ull)
                        numValidEntries++;
                    else
                        collisions++;

                    if (checkForHardCollisions)
                        repeatedPos[hsh] = posDo;

                    assert(numValidEntries <= TestTTSize);

                    totalIters++;

                    u64 data = randGen();
                    ttable[hsh % TestTTSize].hsh.store(hsh, memory_order_relaxed);
                    ttable[hsh % TestTTSize].dat.store(data, memory_order_relaxed);

#if 0 // sanity check
                    u64 cmpStoredHash = ttable[hsh % TestTTSize].hash;
                    u64 cmpStoredData = ttable[hsh % TestTTSize].data;
                    assert(cmpStoredHash == hsh && cmpStoredData == data);
#endif
                }

                pos = posDo;
                moves.cnt = 0;
                pos.generateLegalMoves(moves);
                if (moves.cnt > 0)
                    pos.makeMove(moves.entries[randGen() % moves.cnt]);
                if (moves.cnt == 0)
                    break;

                if (totalIters >= numIters)
                    break;
            }
            if (totalIters >= numIters)
                break;
        }

        // For all elements to collide, the elements should be equal to twice the total number of hash values.
        // If we hash M valuesand total possible hash values is T, then the expected number of collisions will be C = M * (M - 1) / 2T
        //WRONG! const double numComputed = double(totalIters) * (double(totalIters) - 1.) / (2. * TestTTSize);
        //const double chanceComputed = numComputed / totalIters;

        perfDuration elapsed = chrono::duration_cast<perfDuration>(perfClock::now() - t);

        double collisionsPerc = (double)collisions / totalIters;
        printf("\n****** Hashing %d (%dK) positions in %d ms, collisions=%d (%.2f%%)\n       (repeat %d, hardCollision(!!) %d)\n",
            totalIters, totalIters / 1024, (int)elapsed.count(), collisions, collisionsPerc * 100.0, repeatedPosCnt, hardCollisions);

        printf("         Collisions Simulated # vs actual #:    %d %d\n", simulatedCollisions, collisions);
        printf("         Collisions Simulated val:              %.1f\n", simulatedCollisionsVal);

        // compute how good the hashing works in regard of probability
        double avgChanceSimulated = simulatedCollisionsVal /*(double)simulatedCollisions*/ / totalIters;
        printf("         Average chance simulated vs actual: %.3f%% %.3f%%\n",
            avgChanceSimulated * 100.0, collisionsPerc * 100.0);

        double hashingQualityScore = simulatedCollisionsVal /*(double)simulatedCollisions*/ / collisions;
        printf("         Hashing quality score (more is better) = ** % .3f **\n", hashingQualityScore);

        if (hashingQualityScore < 0.85) // quality should be 1.0 +/- 0.02
        {
            res.passed = false;
            res.reason += "hash qscore of " + to_string(hashingQualityScore) + " not sufficent ";
        }
#endif
    }

    Result doRun() noexcept
    {
        Result passed(true);

        printf("\n************ TESTING QUALITY OF HASHING FUNCTION ************\n");
        u64 TestTTSize = TTSize;
        u64 TestTTSizeInBytes = TTSize * 16;
        for (int i = 0; i < 2; i++, TestTTSize /= 4, TestTTSizeInBytes /= 4)
        {
            printf("\n--------------------------------------------------------------\n");
            printf("--- Hash table size: %llu (%llu M, sizeInBytes= %llu MB) ---\n",
                (long long unsigned int) TestTTSize,
                (long long unsigned int) TestTTSize / (1024 * 1024),
                (long long unsigned int) ((TestTTSize * sizeof(TTEntry)) / (1024 * 1024)));

            double chanceOfCollisionWith10KEntries = (10000.0 * 0.5) / TestTTSize;
            double chanceOfCollisionWith1MEntries = (1000000.0 * 0.5) / TestTTSize;
            double chanceOfCollisionWith10MEntries = (10000000.0 * 0.5) / TestTTSize;

            printf("  Chance of collision w/  10K entries = %.4f%%\n  Chance of collision w/   1M entries = %.4f%%\n  Chance of collision w/  10M entries = %.2f%%\n",
                chanceOfCollisionWith10KEntries * 100.0, chanceOfCollisionWith1MEntries * 100.0, chanceOfCollisionWith10MEntries * 100.0);

            doRun(iterations / 2, TestTTSize, passed);
            doRun(iterations, TestTTSize, passed);

            printf("-------------------------------------------------------------\n");
        }

        return passed;
    }

};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Random Generator testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct RandomGeneratorTest : Test<RandomGeneratorTest>
{
    CONSTRUCT_TEST(RandomGeneratorTest)

    const bool enable = true;
    volatile bool writeRandomDataFile = false;
    const int sizeInBytes = 1024 * 1024 * 4;
    const int size = sizeInBytes / sizeof(u64);

    using perfDuration = perfDurationMicro;

    Result doRun() noexcept
    {
        vector<u64> values(size);
        u64* v = &values[0];

        perfTimePoint t = perfClock::now();

        RandomGenerator randGen;
        for (int i = 0; i < size; i++)
        {
            *v++ = randGen();
        }

        perfDuration elapsed = chrono::duration_cast<perfDuration>(perfClock::now() - t);
        printf("Generating %d KB of random data took %d us.\n\n", sizeInBytes / 1024,  (int)elapsed.count());

        // test random distribution (basic test)
        printf("Testing random distribution... (samples=%d)\n", size);
        testRandDistribution(values.data(), 2);
        testRandDistribution(values.data(), 4);
        testRandDistribution(values.data(), 8);
        testRandDistribution(values.data(), 10);
        testRandDistribution(values.data(), 12);
        testRandDistribution(values.data(), 16);
        testRandDistribution(values.data(), 64);
        testRandDistribution(values.data(), 100);

        if (writeRandomDataFile)
        {
            // try to compress this file - if packed size is less than unpacked size, the random gen sucks
            ofstream strm("./testRandomData.dat", std::ios::binary);
            strm.write((char*)&values[0], sizeInBytes);
            printf("Random data file was written.\n");
        }

        return {};
    }

    void testRandDistribution(const u64* values, const int base)
    {
        int med = size / base;

        printf("... of mod %d (med=%d):\n  ", base, med);

        map<int, int> distri{};

        for (int i = 0; i < size; i++)
        {
            u64 v = *values++;
            int mod = (v % base);
            distri[mod]++;
        }

        double maxPerc = 0.0;
        double minPerc = 9999999.9;
        int maxDevi = 0;
        int avgDevi = 0;

        for (int i = 0; i < base; i++)
        {
            int nums = distri[i];
            double perc = (double)med / (double)nums - 1.0;
            maxPerc = max(maxPerc, max(perc, -perc));
            
            int devi = abs(med - nums);
            maxDevi = max(maxDevi, devi);
            avgDevi += devi;

            printf("%d=%d(%.3f) ", i, nums, perc);
            if ((i % 4) == 3)
                printf("\n  ");
        }
        avgDevi /= base;
        printf("\n**** Max/avg deviation = %d/%d ****\n", maxDevi, avgDevi);

#ifdef DUMP
        printf("     ----------------|----------------\n");
        for (int i = 0; i < base; i++)
        {
            int nums = distri[i];
            double perc = (((double)med / (double)nums) - 1.0) / maxPerc;

            int visualSpan = int(perc * 16.0);
            int left0s = min(16, 16 + visualSpan);
            int mid1s = (16 - left0s);
            int right1s = max(0, visualSpan);

            printf("%4d %s%s|%s\n", i, string(left0s, ' ').data(), string(mid1s, '*').data(), string(right1s, '*').data() );
        }

        printf("\n\n");
#endif
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Search testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SearchTest : Test<SearchTest>
{
    CONSTRUCT_TEST(SearchTest)

    const bool enable = true;

    using perfDuration = perfDurationMilli;

#define __TO_STR2(x)	#x
#define __TO_STR(x)		__TO_STR2(x)  
#define DO_SEARCH(FEN, MINDEPTH, MAXDEPTH, COND) {                                                              \
    bool hasPassed = true;                                                                                      \
    string trueCond = string(__TO_STR(COND));                                                                   \
    Position pos;                                                                                               \
    pos.initFromFEN(FEN);                                                                                       \
    printf("\n-> Testing Search FEN=%s\n   validResults (%s) (depth %d to %d)\n",                               \
        FEN, trueCond.data(), MINDEPTH, MAXDEPTH);                                                              \
    for (test_maxDepth = MINDEPTH; test_maxDepth <= MAXDEPTH; test_maxDepth++)                                  \
    {                                                                                                           \
        memset(&ttable[0], 0, TTSize * 16);                                                                     \
        numPositionsSearched++;                                                                                 \
        perfTimePoint t = perfClock::now();                                                                     \
        rootPosition = pos;                                                                                     \
        storeTTEntry(makeHash(rootPosition), { 0, 1 });                                                         \
        Move m = findBestMove();                                                                                \
        timeelapsed += chrono::duration_cast<perfDuration>(perfClock::now() - t).count() / 1000.0;              \
        if (!(COND))                                                                                            \
        {                                                                                                       \
            printf(">>>> WRONG bestmove %s (correct %s)\n", convertMoveToText(m).data(), trueCond.data());      \
            hasPassed = false;                                                                                  \
            res.reason += "Wrong bestmove " + convertMoveToText(m) +" (correct " +                              \
                            trueCond + ") in FEN " + string(FEN) + " | ";                                       \
            break;                                                                                              \
        }                                                                                                       \
        else printf("   > correct bestmove %s\n", convertMoveToText(m).data());                                 \
    }                                                                                                           \
    if (hasPassed && !disableTT)                                                                                \
    {                                                                                                           \
        test_maxDepth = MAXDEPTH;                                                                               \
        perfTimePoint t = perfClock::now();                                                                     \
        rootPosition = pos;                                                                                     \
        Move m = findBestMove(); /* research with last TT table should be fast */                               \
        timeelapsedResearch += chrono::duration_cast<perfDuration>(perfClock::now() - t).count() / 1000.0;      \
        if (!(COND))                                                                                            \
        {                                                                                                       \
            printf(">>>> RESEARCH FAILED ???\n");                                                               \
            hasPassed = false;                                                                                  \
            res.reason += "Research failed " + convertMoveToText(m) + " (correct " +                            \
                trueCond + ") in FEN " + string(FEN) + " | ";                                                   \
        }                                                                                                       \
    }                                                                                                           \
    res.passed = hasPassed; if (!hasPassed) goto exit; }                                                        \

    Result doRun() noexcept
    {
        Result res(true);
        int numPositionsSearched = 0;
        double timeelapsed = .0;
        double timeelapsedResearch = .0;
        bool disableTT = false;
#ifdef DISABLE_TT
        disableTT = true;
#endif
        // K1k1B3/8/8/8/8/8/7N/8 w - - 0 1  NBKvK endgame from worst possible position (mate in 33)

        // basic KvKR endgame; "a3e3" mates in 6, other moves takes at least 7
//        DO_SEARCH("8/2k5/7R/2K5/8/8/8/8 w - - 0 1", 16, 18, m == mkmove(H6, D6))
//        DO_SEARCH("8/8/8/8/5k2/r7/5K2/8 b - - 20 11", 16, 17, m==mkmove(A3,E3))

        // basic RKvK endgame; "h4h7" mates in 7, other moves takes at least 10
        // **** BUG 2023-04-23: FIXED!: find correct mate (score&move) at depth 14, then returns messed up move "h4h2" (which did nothing) on depth 17
//        DO_SEARCH("3k4/8/8/8/5K1R/8/8/8 w - - 12 7", 15, 18, m==mkmove(H4, H7))

        // bug: engined played Bb5c4 here *FIXED*
//        DO_SEARCH("r2qkb1r/pp1bpppp/2n2n2/1B1p4/3P4/4PN1P/PP3PP1/RNBQK2R w KQkq - 1 8", 8, 11, m==m)

        // only winning move "f1e3" (good for testing qsearch) *****
//        DO_SEARCH("8/pkp5/1p2P1Q1/7p/4p2P/1P6/PB3PP1/1q3nK1 b - - 0 30", 11, 11, m==mkmove(F1,E3) )

        // mate in 4 "a6f1"  bug: dont work anymore (find slower mate)  *FIXED*
//        DO_SEARCH("4r3/p4pkp/q7/3Bbb2/P2P1ppP/2N3n1/1PP2KPR/R1BQ4 b - - 0 1", 8, 8, m==mkmove(A6,F1))

        // black must play "c5xd4" or "c4xd3"ep or "c5d5", otherwise the endgame is lost
//        DO_SEARCH("8/8/8/2k5/2pP4/8/B7/4K3 b - d3 0 3", 12, 12, m == mkmove(C5, D4)||m == mkmove(C4, D3)||m == mkmove(C5, D5))

        // only winning move is "e1f1" (mate in 10), very hard to find; all other moves are losing
        // TODO: DON'T WORK WITH DEPTH <11 - CHECK LATER   *FIXED*
//        DO_SEARCH("8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1", 13, 13, m==mkmove(E1,F1))

        // basic RKvK endgame; "h1h7" mates in 12, other moves take longer  *FIXED* (must still go deep, but resolves in 14 secs on 8 threads)
        // TODO: giving wrong results when depth>=21, maybe due to hard hash collision? (TTSize=2048)
//        DO_SEARCH("5k2/8/8/8/8/8/8/4K2R w K - 0 1", 25, 25, m==mkmove(H1,H7))

        // basic KvKR endgame; "a5a2" mates in 8, other moves takes at least 9  *FIXED*
        // TODO: bug in search; mate in 9 with vanilla AB search has mate in 8 score, qsearch is not the problem
//        DO_SEARCH("8/8/8/r3k3/8/8/8/3K4 b - - 12 7", 15, 17, m==mkmove(A5,A2))

        // "c5xd4" and "c4xd3ep" mates in 12, other moves take longer  *FIXED* (tales some time to find actual mate, but correct move play at lower depths)
        // TODO: *** ADVANCED STUFF TEST LATER WHEN SEARCH IS FASTER (currently gives correct move at low depth by chance)
//        DO_SEARCH("8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 27, 27, m==mkmove(C5,D4)||m==mkmove(C4,D3))

        // endgame: black mates in 8 moves if "h5h4" is played (hard to find); "h5c5" is more obvious, but mates in 10
        // TODO: DON'T WORK WITH LOW DEPTH - CHECK LATER  *** IMPORTANT **   *FIXED*
//        DO_SEARCH("3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 16, 16, m==mkmove(H5,H4))

        // startpos after e2e4
        DO_SEARCH("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", 7, 10, m == m)

        // startpos after e2e4 d7d5 e4d5
        DO_SEARCH("rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", 7, 10, m == m)

        // early endgame
        DO_SEARCH("5k2/3n1pp1/4p2p/1R6/6PP/3N4/r3PPK1/8 w - - 5 32", 11, 12, m == m)

        // mid endgame
        DO_SEARCH("8/1Bb4p/1B6/pK2k3/Pn4p1/8/5PP1/8 b - - 1 34", 13, 14, m == m)

        // mid game w/ queen
        DO_SEARCH("3r1bk1/p4ppp/1p6/8/2PN4/q3P3/2Q2PPP/5RK1 b - - 1 24", 8, 9, m == m)

        // mid game wo/ queen
        DO_SEARCH("r5k1/p3b1p1/2p5/2p1N3/4P1pP/1P4P1/PBn2r2/1K2R2R w - - 0 24", 10, 11, m == m)

        // black mates in 1 "d8h4"
        DO_SEARCH("rnbqk1nr/ppp2ppp/8/2bpp3/P4PP1/8/1PPPP2P/RNBQKBNR b KQkq - 0 4", 3, 6, m==mkmove(D8,H4) )

        // white mates in 1 "d1h5"
        DO_SEARCH("2b1kbnr/rpp1p3/pNN4p/5pp1/8/8/PPPP1PPP/R1BQKB1R w KQk - 0 9", 3, 6, m==mkmove(D1,H5) )

        // "c3d5" is the winning move for white, "e2d3" gives white a small advantage, all other moves are draw
        // FIXED FOR UNKNOWN REASONS: TODO: DON'T WORK WITH DEPTH<9 - CHECK LATER  *FIXED*
        DO_SEARCH("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 7, m==mkmove(C3,D5))

        // mate in 1 "b2e2"
        DO_SEARCH("8/8/7K/8/5P2/3B4/1Q6/4k3 w - - 1 93", 3, 7, m==mkmove(B2,E2) )

        // mate in 1 "a5b7"
        DO_SEARCH("r1nk3r/2b2ppp/p3bq2/N7/Q2P4/B2B4/P4PPP/4R1K1 w - - 0 1", 3, 6, m==mkmove(A5,B7) )

        // mate in 3 "f5f2"
		DO_SEARCH("r2n1rk1/1ppb2pp/1p1p4/3Ppq1n/2B3P1/2P4P/PP1N1P1K/R2Q1RN1 b - - 0 1", 6, 7, m==mkmove(F5,F2))

        // mate in 3 "c3d5"
        DO_SEARCH("6rb/1p2k3/p2p1nQ1/q1p1p2r/B1P1P3/2N4P/PP4P1/1R3RK1 w - - 1 0", 5, 7, m==mkmove(C3,D5))

        // mate in 1 is threaten, don't play f4xe5
        DO_SEARCH("6n1/1kp3p1/p5q1/1p2r3/Pnb2Q2/6N1/1PP3PP/2KR3R w - - 4 25", 3, 5, m!=mkmove(F4,E5))

        // mate in 1 "h5g3"
        DO_SEARCH("r2n2k1/1ppb2pp/1p1p4/3Pp2n/2B3P1/2P4P/PP1N1r2/R2Q2NK b - - 1 3", 2, 5, m==mkmove(H5,G3))

        // mate in 3 "d5d7" !!!! (mate in 4 "d5d8)
        DO_SEARCH("4R3/1p4rk/6p1/2pQBpP1/p1P1pP2/Pq6/1P6/K7 w - - 1 0", 6, 7, m==mkmove(D5,D7)||m==mkmove(D5,D8))

          // mate in 4 "a6a8"
        DO_SEARCH("2k4r/1r1q2pp/QBp2p2/1p6/8/8/P4PPP/2R3K1 w - - 1 0", 5, 7, m==mkmove(A6,A8))

        // forced stalemate/draw "c4e5"
        DO_SEARCH("1q6/2b2ppb/4p1k1/7p/2Np1p1P/3P1Q2/6PK/8 w - - 0 1", 7, 8, m==mkmove(C4,E5))

        // crazy rook "e1e5" or "e1f1" or "e1a1"
        DO_SEARCH("7k/4B1pP/2p3P1/2P2K2/8/8/R7/4r3 b - - 0 2", 3, 8, m==mkmove(E1,E5)||m==mkmove(E1,F1)||m==mkmove(E1,A1))

        // perp "f1f6" or "f1f8"
        DO_SEARCH("3b3k/p6p/1p5P/3q4/8/n7/PP6/K4Q2 w - - 0 1", 3, 8, m==mkmove(F1,F6)||m==mkmove(F1,F8))

        // perp "d2c2"
        DO_SEARCH("8/PR5p/4pk2/5p2/2B2Pp1/1p2PnP1/3r4/2K5 b - - 7 41", 2, 8, m==mkmove(D2,C2))
            
        // white mates black in 6 if "e7e6", otherwise earlier
        DO_SEARCH("r6r/1b2k1bq/8/8/7B/8/8/R3K2R b KQ - 3 2", 10, 10, m == mkmove(E7,E6))

        DO_SEARCH("8/8/8/2k5/2pP4/8/B7/4K3 b - d3 0 3", 11, 13, m == mkmove(C5, D4) || m == mkmove(C4, D3) || m == mkmove(C5, D5))

        // black must play "f7xe6" or "d7xe6", otherwise the position is lost
        DO_SEARCH("r3k2r/p1pp1pb1/bn2Qnp1/2qPN3/1p2P3/2N5/PPPBBPPP/R3K2R b KQkq - 3 2", 2, 7, m==mkmove(F7,E6)||m==mkmove(D7,E6))

        // mate in 3 if "d7xc8=Q"  - "d7xc8=R" would be mate in 8
        DO_SEARCH("rnb2k1r/pp1Pbppp/2p5/q7/2B5/8/PPPQNnPP/RNB1K2R w KQ - 3 9", 5, 7, m==mkmove(D7,C8,QUEEN))

        //+ only winning move is "g5h6", medium hard to find; all other moves are unclear/drawing (if depth is not even, it doesn't find the move rn! -> qsearch needed)
        DO_SEARCH("1q1k4/2Rr4/8/2Q3K1/8/8/8/8 w - - 0 1", 8, 8, m==mkmove(G5,H6))

        //+ only drawing move is "f4xd5" or "d6d8", "f4e2" is unclear  (if depth is not odd, it doesn't find the move rn! -> qsearch needed)
        DO_SEARCH("8/8/1p1r1k2/p1pPN1p1/P3KnP1/1P6/8/3R4 b - - 0 1", 7, 9, m==mkmove(F4,D5)||m==mkmove(D6,D8)||m==mkmove(F4,E2))

        // startpos last, just to show which crazy move is currently played first
        DO_SEARCH("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 9, 9, m==m)
 
        // startpos after e2e4, just to show which crazy move is currently played by black
        DO_SEARCH("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", 9, 9, m==m)

exit:
        printf("\n  Searching %d positions took ##-->%.2f<--## s (Research took #%.2f# s). \n\n",
            numPositionsSearched, timeelapsed, timeelapsedResearch);

        return res;
    }
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Move Generator testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static const int numBoards = 500;
static Position tstPos[numBoards];
static Move tstMoves[numBoards];
static MoveList tstMoveList[numBoards];
static u8 zeroMovesDetect[numBoards];

struct MovegenTest : Test<MovegenTest>
{
    CONSTRUCT_TEST(MovegenTest)

    const bool enable = true;

    using perfDuration = perfDurationNano;

    static constexpr string_view TestFEN[] =
    {
        "5bnr/4pk2/r1p1Npp1/p6p/P1B1PP2/4B3/1PP3PP/R3K1NR b KQ - 1 16 ",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "5nk1/pp3pp1/2p4p/q7/2PPB2P/P5P1/1P5K/3Q4 w - - 1 28",
        "8/8/1k3K2/p7/P2p4/5N2/8/5r2 w - - 0 132 ",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - ", // "Kiwipete" by Peter McKenzie
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"
    };

    static constexpr string_view ZugzwangFEN[] =
    {
        "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1",
        "1q1k4/2Rr4/8/2Q3K1/8/8/8/8 w - - 0 1",
        "7k/5K2/5P1p/3p4/6P1/3p4/8/8 w - - 0 1",
        "8/6B1/p5p1/Pp4kp/1P5r/5P1Q/4q1PK/8 w - - 0 32",
        "8/8/1p1r1k2/p1pPN1p1/P3KnP1/1P6/8/3R4 b - - 0 1",
    };

    Result doRun() noexcept
    {
        Result res = criticalPositionsTest();
        performanceTest();
        return res;
    }

    Result criticalPositionsTest() noexcept
    {
        Result res(true);

        Position pos;
        MoveList moves;
#ifdef DUMP
        int eval;
#endif

#ifdef DUMP
    #define CHECKPOS(fen, numLegalMoves) pos.initFromFEN(fen); moves.cnt=0; pos.generateLegalMoves(moves); pos.dump(); moves.dump(); pos.flipSides(); eval = pos.evaluate(); \
                            printf("Eval = %d\n", eval); res.passed = (moves.cnt == numLegalMoves) ? res.passed : false; \
                            if (moves.cnt != numLegalMoves) printf(" ****** Move mismatch!!! %d vs %d\n", moves.cnt, numLegalMoves);
#else
    #define CHECKPOS(fen, numLegalMoves) pos.initFromFEN(fen); moves.cnt=0; pos.generateLegalMoves(moves); res.passed = (moves.cnt == numLegalMoves) ? res.passed : false;
#endif
        // Start pos with missing pieces
        CHECKPOS("r1bqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 20)
        CHECKPOS("r1bqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", 19)
    
        CHECKPOS("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN1 w KQkq - 0 1", 20)
        CHECKPOS("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN1 b KQkq - 0 1", 20)

        CHECKPOS("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN1 b KQkq - 0 1", 20)

        CHECKPOS("rnbqkbnr/pppppp1p/8/8/P5pP/8/1PPPPPP1/RNBQKBNR b KQkq h3 0 3", 22)


        // Pawn that can capture on EP field is pinned, but it can take the EP pawn anyways (very rare)
        CHECKPOS("1q6/8/8/3pP3/8/6K1/8/3k4 w - d6 0 10", 9)
        CHECKPOS("8/8/8/1q1pP1K1/8/8/8/3k4 w - d6 0 10", 9)
        CHECKPOS("3k4/8/3q4/8/K2R2r1/8/8/8 w - - 0 10", 8)
        CHECKPOS("3k4/8/3q4/8/K2R2b1/8/8/8 w - - 0 10", 13)
        CHECKPOS("3k3B/8/3q4/nn6/K5r1/nn6/8/8 w - - 0 10", 1)
        CHECKPOS("3K3b/3Qp3/NNp5/k5R1/NN6/8/8/8 b - - 0 10", 3)
        CHECKPOS("K7/1B/8/8/4q/8/4k3/8 w - - 0 10", 5)
        
        // "Kiwipete" by Peter McKenzie, https://www.chessprogramming.org/Perft_Results
        CHECKPOS("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - ", 48)
        CHECKPOS("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 14)
        CHECKPOS("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6)
        CHECKPOS("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 6)
        CHECKPOS("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 43)
        CHECKPOS("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 46)
            
#undef CHECKPOS

        return res;
    }

    void performanceTest() noexcept
    {
        const int numFEN = sizeof(TestFEN) / sizeof(*TestFEN);
        const int numMovesPerBoard = 80;
        int numZeroMovesPos = 0;
        int totalMovesGenerated = 0;

        perfDuration allMovegen{};
        perfDuration allMakeMove{};
        perfDuration allflipSides{};
        perfDuration allOverall{};

        RandomGenerator randGen;

        int fenCounter = 0;
        for (auto& fen : TestFEN)
        {
            perfDuration durationMovegen{};
            perfDuration durationMakeMove{};
            perfDuration durationFlipSides{};
            perfDuration durationOverall{};

            for (int i = 0; i < numBoards; i++)
                tstPos[i].initFromFEN(fen);

            memset(zeroMovesDetect, 0, numBoards);

            int moveCounter = 0;
            for (moveCounter = 0; moveCounter < numMovesPerBoard; moveCounter++)
            {
                perfTimePoint t1 = perfClock::now();

                // 1. Gen all legal moves on every board
                for (int i = 0; i < numBoards; i++)
                {
                    tstMoveList[i].cnt = 0;
                    tstPos[i].generateLegalMoves(tstMoveList[i]);
                }

                perfTimePoint t2 = perfClock::now();

                // select random moves for every board
                for (int i = 0; i < numBoards; i++)
                {
                    u16 moveCount = tstMoveList[i].cnt;
                    totalMovesGenerated += moveCount;

                    if (moveCount == 0)
                    {
                        if (!zeroMovesDetect[i])
                        {
                            numZeroMovesPos++;
                            zeroMovesDetect[i] = 1;
                        }
                        tstMoves[i] = 0;
                    }
                    else
                    {
                        Move moveToPlay = tstMoveList[i].entries[randGen() % moveCount];
                        tstMoves[i] = moveToPlay;
                    }
                }

                perfTimePoint t3 = perfClock::now();

                // 2. Make a random move on every board
                for (int i = 0; i < numBoards; i++)
                {
                    if (tstMoves[i] != 0)
                        tstPos[i].makeMove(tstMoves[i]);
                }

                perfTimePoint t4 = perfClock::now();

                // flip now in makeMove

                perfTimePoint t5 = perfClock::now();

                perfDuration dt1 = chrono::duration_cast<perfDuration>(t2 - t1);
                perfDuration dt2 = chrono::duration_cast<perfDuration>(t4 - t3);
                perfDuration dt3 = chrono::duration_cast<perfDuration>(t5 - t4);
                durationMovegen += dt1;
                durationMakeMove += dt2;
                durationOverall += dt1 + dt2 + dt3;
            }

#define PERFOUT(_x) ((double) _x.count() / 1000000.0)

            printf("** Performance for FEN#%d: Movegen: %.2f ms, MakeMove+Flip: %.2f ms, Combined: %.2f ms\n",
                fenCounter, PERFOUT(durationMovegen), PERFOUT(durationMakeMove), PERFOUT(durationOverall));

            fenCounter++;
            allMovegen += durationMovegen;
            allMakeMove += durationMakeMove;
            allflipSides += durationFlipSides;
            allOverall += durationMovegen + durationMakeMove;

            this_thread::sleep_for(20ms);
        }

        printf("**** Number of zero-move positions: %d", numZeroMovesPos);

        printf("\n******* OVERALL PERFORMANCE: Movegen: %.2f ms, MakeMove: %.2f ms, Flip: %.2f ms, Overall: %.2f ms\n",
            PERFOUT(allMovegen), PERFOUT(allMakeMove), PERFOUT(allflipSides), PERFOUT(allOverall));

        double totalMS = allOverall.count() / 1000000.0;
        double MNPS = totalMovesGenerated / (totalMS * 1000.0);

        // MSVC on Win10    |  GCC on Ubuntu22 VM   (CPU: AMD 7 2700)
        //  72                  57
        printf("\n***(total moves generated=%d in %d ms)\n  ##########\n  %.2f MNps\n  ##########\n",
            (int)totalMovesGenerated, (int)totalMS, MNPS);

#undef PERFOUT
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// General testing (performance/logic/validation) main function
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void doTesting()
{
    string testResult;

    if (!TEST_ONLY_SEARCH)
    {
          testResult += HashPerformance{}.run();
          testResult += TTableTest{}.run();
          testResult += RandomGeneratorTest{}.run();
          testResult += MovegenTest{}.run();
    }
    testResult += SearchTest{}.run();

    printf("\n=========================================================================================\n");
    printf("TEST RESULTS:\n%s", testResult.data());
    printf("=========================================================================================\n");
}

#endif
