#ifdef TRAIN

#include <fstream>
#include <filesystem>
#include <span>
#include <cmath>
#include <map>
#include <array>

//#define RUN_OPTIMIZE_SESSION

const auto NUM_TRAIN_THREADS = 16;
const auto MAX_TRAIN_FILES = 0;
const auto NUM_SESSIONS_CACHED = 16;
const auto NUM_BIN_FILES = 64;
const auto DETAIL_DUMP = true;
const auto READ_BINARY_DATA = true;
const auto CREATE_BINARY_DATA = false;
const auto PRINT_GAMEPHASE_DISTRIBUTION = false;
const auto FLATTEN_GAME_PHASES = false;
const auto IGNORE_CAPTURE_POS = true;
const auto IGNORE_CHECK_POS = true;
const auto RATE_BASE_POSITION = true;
const bool RATE_NTH_POSITION[3] = { true, true, false };

static double SCALE_M4_EVAL = 1.;
static double SCALE_SF_EVAL = .99;

const string INSPECT_SINGLE_FEN = "";// "r1b1k2r/pp1nqppp/2pb4/3p4/3P1P2/2NB1N2/PP1Q1PPP/R3K2R w KQkq - 0 11"s;

namespace fs = filesystem;

#ifdef USE_CONSTEXPR_TABLES
const TuningTapWeights origTapWeights(iTapWeights.entries);
#else
const TuningTapWeights origTapWeights(iTapWeights);
#endif
const TuningTableWeights origTableWeights(rawTableWeights);



static flt elapsedS(auto span)
{
    return chrono::duration<flt>(span).count();
}

struct EvalPos
{
    using Eval = pair<Move, int16_t>;
    
    Position pos;
    u8 isCheck;
    u8 isCapture;
    u16 fullmoveNumber;
    Eval bestMoves[3];

#ifdef DUMP
    void dump()
    {
        printf("--------- M4EvalPos Dump: check=%d, capture=%d, move=%d\n", isCheck, isCapture, fullmoveNumber);
        for (int i = 0; i < 3; i++)
            printf("Best Move#%d: %s cp=%d\n", i + 1, convertMoveToText(bestMoves[i].first).data(), bestMoves[i].second);
        pos.dump();
    }
#endif
};
static_assert(sizeof(EvalPos) == 80);
using EvalList = vector<EvalPos>;
using EvalSpan = span<EvalPos>;

static EvalList m4evals;
static Weight phaseDistriWeights[257];

void dump(const TuningTapWeights& taps, const char* info = "Tap weights:")
{
    printf("----------------------------------------\n");
    printf("%s\n", info);
    printf("----------------------------------------\n");
    for (auto i = 0; i < TAP_WEIGHTS_LAST /*TAP_WEIGHTS_SIZE*/; i++)
    {
        if ((i - 1) % 4 == 3)
            printf("\n");
        printf("%.3f,%.3f,   ", taps.values[i * 2 + 0] / 4096., taps.values[i * 2 + 1] / 4096.);
    }
    printf("\n");

    printf("---------------------\n");

    printf("constexpr Weight iTapWeights[] = {\n    ");

    for (auto i = 0; i < TAP_WEIGHTS_LAST; i++)
    {
        if ((i - 1) % 4 == 3)
            printf("\n    ");
        printf("%5d,%5d,   ", taps.values[i * 2 + 0], taps.values[i * 2 + 1]);
    }
    printf("\n};\n");

    printf("----------------------------------------\n");
}

void dump(const TuningTableWeights& table, const char* info = "TABLE weights:")
{
    printf("%s\n", info);
    printf("----------------------------------------\n");

    printf("constexpr u8 rawTableWeights[] = \"");
    for (auto i = 0; i < 768; i++)
    {
       auto val = table.values[i];
       int chr = val / RAWTABLE_SCALE + RAWTABLE_MID;

        // illegal values: (34-79)*110= **-4950**, (37-79)*110= **-4620**, (92-79)*110= **1430**
        // max value 126= (126-79)*110= **5170**
        // min value 32= (32-79)*110= **-5170**

        // is illegal chr value?
        if (chr > 126)
            printf("|bad max|");
        else if (chr < 32)
            printf("|bad min|");
        else if (chr == 37)
            printf("|bad %%|");
        else if (chr == 92)
            printf("|bad \\|");
        else
            printf("%c", chr);
    }
    printf("\";\n");
    printf("---------------------\n");
    for (auto i = 0; i < 6; i++)
    {
        const char* whichType[6] = { "Pawns", "Knights", "Bishops", "Rooks", "Queens", "King"  };
        printf("%s:\n", whichType[i]);
        for (auto j = 0; j < 64; j++)
        {
            if ((j - 1) % 8 == 7)
                printf("\n");

            auto val1 = table.values[i * 128 + j + 0] / 41;
            auto val2 = table.values[i * 128 + j + 64] / 41;
            printf("%4d,%4d,   ", val1, val2);
        }
        printf("\n");
    }
    printf("----------------------------------------\n");
}


namespace TrainingSession
{
    enum ResultType
    {
        RESULTTYPE_TOTAL = 0,
        RESULTTYPE_QUITE,
        RESULTTYPE_CAPTURE,
        RESULTTYPE_CHECK,

        RESULTTYPE_COUNT
    };

    struct Result
    {
        array<long long, RESULTTYPE_COUNT> devi;
        array<int, RESULTTYPE_COUNT> deviCnt;
        array<long long, RESULTTYPE_COUNT> hugeDeviCnt;
//        long long correctBaseAdj{};

        // custom constructor due to MSVC bug https://developercommunity.visualstudio.com/t/error-message-predefined-c-types-compiler-internal/1176546
        Result() { memset(this, 0, sizeof(Result)); };

        bool has(ResultType type = RESULTTYPE_TOTAL) const { return deviCnt[type] > 0; }
        double get(ResultType type = RESULTTYPE_TOTAL) const { return (deviCnt[type] == 0) ? .0 : devi[type] / double(deviCnt[type]); }
        double getHugeFactor(ResultType type = RESULTTYPE_TOTAL) const { return (deviCnt[type] == 0) ? .0 : hugeDeviCnt[type] / (deviCnt[type] * 4096.); }
        double getSamples(ResultType type = RESULTTYPE_TOTAL) const { return deviCnt[type] * 1.; }

        void add(Score scoreSF, Score scoreM4, /*Score scoreFast, Score scoreBase,*/ bool isCapture, bool isCheck, int phase)
        {
            scoreM4 = fixMul(scoreM4, SCALE_M4_EVAL);
            scoreSF = fixMul(scoreSF, SCALE_SF_EVAL);

            Score deviation = abs(scoreSF - scoreM4);

            auto compensation = max(SCALE_M4_EVAL, SCALE_SF_EVAL) / min<flt>(SCALE_M4_EVAL, SCALE_SF_EVAL);
            deviation = fixMul(deviation, compensation);

            // scale down high deviations
            if (deviation > 100)
                deviation = min(220, Score(100 + pow(deviation - 100, 0.8)));

            Score thres = 99;
            if (abs(scoreSF) < 25 && abs(scoreM4) > 80)
                deviation = (deviation * 63) / 100;
            if (abs(scoreSF) < 10)
                thres = 110;

            // IMPORTANT: If scoreBase improves scoreFast, add a bonus in form of a reduced deviation

            // ------------------------- | -----x------------ + ---------------- |
            //                          min    hit           mid                max
            //          -123...          0     0.3            1                  0             ...-123

/*            int m4ScoreAdded = scoreFast + scoreBase;
            int correctionSpan = scoreSF - scoreFast;
            int minCorrectionPoint = scoreFast;
            int midCorrectionPoint = scoreFast + correctionSpan;
            int maxCorrectionPoint = scoreFast + correctionSpan * 2;
            int hit                = m4ScoreAdded;
            float q = 0.2f;
            if (correctionSpan != 0)
            {
                bool maxHigh = maxCorrectionPoint > minCorrectionPoint;
                if (maxHigh &&  hit >= minCorrectionPoint && hit <= midCorrectionPoint ||
                    !maxHigh && hit <= minCorrectionPoint && hit >= midCorrectionPoint)
                    q = float(hit - minCorrectionPoint) / correctionSpan;
                else if (maxHigh &&  hit >= midCorrectionPoint && hit <= maxCorrectionPoint ||
                         !maxHigh && hit <= midCorrectionPoint && hit >= maxCorrectionPoint)
                    q = float(maxCorrectionPoint - hit) / correctionSpan;
                else if (maxHigh &&  hit < minCorrectionPoint ||
                         !maxHigh && hit > minCorrectionPoint)
                    q = float(hit - minCorrectionPoint) / correctionSpan;
                else
                    q = float(maxCorrectionPoint - hit) / correctionSpan;
            }
            if (q < -1.0f)
                q = -1.0f;

            float qFinal = (q >= 0.0f) ? pow(q, 0.38f) : -pow(-q, 0.65f);
            deviation = deviation / 2 + int((deviation / 2) * (1.0f - qFinal));
            */
/*            if (qFinal >= 0.)
                correctBaseAdj++;
            else
                correctBaseAdj--;
                */
//            printf("m4=%4d sf=%4d fast=%4d base=%4d | QFinal=%.2f q=%.2f    min=%4d mid=%4d max=%4d  hit=%4d  cnt=%d\n",
//                m4ScoreAdded, scoreSF, scoreFast, scoreBase, qFinal, q, minCorrectionPoint, midCorrectionPoint, maxCorrectionPoint, hit, cnt);

            int isHuge = (deviation > thres) * phaseDistriWeights[max(0, phase)];

            // weight according phase distribution in order to get better results
            deviation = fixMul(deviation, phaseDistriWeights[max(0, phase)]);

            devi[RESULTTYPE_TOTAL] += deviation;
            deviCnt[RESULTTYPE_TOTAL]++;
            hugeDeviCnt[RESULTTYPE_TOTAL] += isHuge;

            if (!isCapture && !isCheck)
            {
                devi[RESULTTYPE_QUITE] += deviation;
                deviCnt[RESULTTYPE_QUITE]++;
                hugeDeviCnt[RESULTTYPE_QUITE] += isHuge;
            }
            else
            {
                if (isCapture)
                {
                    devi[RESULTTYPE_CAPTURE] += deviation;
                    deviCnt[RESULTTYPE_CAPTURE]++;
                    hugeDeviCnt[RESULTTYPE_CAPTURE] += isHuge;
                }
                if (isCheck)
                {
                    devi[RESULTTYPE_CHECK] += deviation;
                    deviCnt[RESULTTYPE_CHECK]++;
                    hugeDeviCnt[RESULTTYPE_CHECK] += isHuge;
                }
            }
        }

        Result& operator+= (const Result& o)
        {
            for (int i = 0; i < RESULTTYPE_COUNT; i++)
            {
                devi[i] += o.devi[i];
                deviCnt[i] += o.deviCnt[i];
                hugeDeviCnt[i] += o.hugeDeviCnt[i];
            }
            return *this;
        }
    };

    static void rate(size_t id, const TuningTapWeights& taps, const TuningTableWeights& tables, EvalSpan&& evals, Result& res)
    {
        auto t = qnow;

        int count = 0;
        const bool DDUMP = DETAIL_DUMP && id == 0;

        for (auto& e : evals)
        {
            count++;
#ifdef _DEBUG
            auto fenString = e.pos.generateFEN(false, e.fullmoveNumber);
            const char* fenStringC = fenString.data();
#endif
            if (!INSPECT_SINGLE_FEN.empty()) 
            {
                if (INSPECT_SINGLE_FEN != e.pos.generateFEN(false, e.fullmoveNumber))
                    continue;
            }

            volatile Score scores[3] = { e.bestMoves[0].second, e.bestMoves[1].second, e.bestMoves[2].second };
            Move moves[3] = { e.bestMoves[0].first, e.bestMoves[1].first, e.bestMoves[2].first };
            Move movesDspl[3] = { (Move)alignMove(moves[0], e.pos.flipped),
                                  (Move)alignMove(moves[1], e.pos.flipped),
                                  (Move)alignMove(moves[2], e.pos.flipped) };

            volatile Score stcScoreBasePos{ SCORE_INVALID };
            volatile Score stcScore[3]{ SCORE_INVALID, SCORE_INVALID, SCORE_INVALID };

            if (DDUMP)
                printf("/----- TrainPos#%d/move#%d: BestMoves/Scores: %s=%d  %s=%d  %s=%d ----\n|=========================================================================\n| FEN: %s\n",
                    count, e.fullmoveNumber,
                    convertMoveToText(movesDspl[0]).data(), scores[0],
                    convertMoveToText(movesDspl[1]).data(), scores[1],
                    (scores[2] != SCORE_INVALID? convertMoveToText(movesDspl[2]).data() : "NONE"), scores[2],
                    e.pos.generateFEN(false, e.fullmoveNumber).data());


            bool isCapture = e.isCapture;
            bool isCheck = e.isCheck;

            if (RATE_BASE_POSITION && (!isCapture || !IGNORE_CAPTURE_POS) && (!isCheck || !IGNORE_CHECK_POS) && (abs(scores[0]) < SCORE_INVALID))
            {
                Position pos = e.pos;
                int phase = pos.getPhase();

                if (DDUMP)
                    printf("|\n+----- Evaluating base position, phase=%d ->\n", phase);

                if (DDUMP)
                    pos.dump(e.fullmoveNumber);

//                currentTapWeights.scoreFast = 0; currentTapWeights.scoreBase = 0;
                stcScoreBasePos = pos.evaluate(taps, tables, DDUMP);
                if (pos.flipped)
                {
                    stcScoreBasePos = -stcScoreBasePos;
//                    currentTapWeights.scoreFast = -currentTapWeights.scoreFast;
//                    currentTapWeights.scoreBase = -currentTapWeights.scoreBase;
                }

                res.add(scores[0], stcScoreBasePos/*, currentTapWeights.scoreFast, currentTapWeights.scoreBase*/, isCapture, isCheck, phase);

                if (DDUMP)
                    printf("----------------------------------------\n|-----|%d SF vs %d M4|--(%d)---\n+---------------------------------------\n",
                        scores[0], stcScoreBasePos, stcScoreBasePos - scores[0]);
            }
            else
                if (DDUMP)
                    printf("------ Skipping base position move due to %s...\n",
                        (isCapture && isCheck)? "capture and check" :
                        isCapture?              "capture" : "check");

            for (int i = 0; i < 3; i++)
            {
                if (abs(scores[i]) > SCORE_INVALID)
                {
                    // mate score
                }
                else if (scores[i] != SCORE_INVALID && RATE_NTH_POSITION[i])
                {
                    Position pos = e.pos;
                    pos.makeMove(moves[i]);
                    isCapture = pos.flags & FLAGS_CAPTURE;
                    if (isCapture && IGNORE_CAPTURE_POS)
                    {
                        if (DDUMP)
                            printf("------ Skipping move#%d because move is a capture...\n", i + 1);
                        continue;
                    }

                    isCheck = pos._(KING, WHITE) & pos.getAttackMap();

                    if (isCheck && IGNORE_CHECK_POS)
                    {
                        if (DDUMP)
                            printf("------ Skipping move#%d because king is in check after this move...\n", i + 1);
                        continue;
                    }

                    int phase = pos.getPhase();

                    if (DDUMP)
                        printf("|\n+----- Evaluating %s best move %s, phase=%d ->\n",
                            (i == 0? "1st" : i == 1? "2nd" : "3rd"), convertMoveToText(movesDspl[i]).data(), phase);

                    if (DDUMP)
                        pos.dump(e.fullmoveNumber);

//                    currentTapWeights.scoreFast = 0; currentTapWeights.scoreBase = 0;
                    stcScore[i] = pos.evaluate(taps, tables, DDUMP);
                    if (pos.flipped)
                    {
                        stcScore[i] = -stcScore[i];
//                        currentTapWeights.scoreFast = -currentTapWeights.scoreFast;
//                        currentTapWeights.scoreBase = -currentTapWeights.scoreBase;
                    }

                    res.add(scores[i], stcScore[i]/*, currentTapWeights.scoreFast, currentTapWeights.scoreBase*/, isCapture, isCheck, phase);

                    if (DDUMP)
                        printf("----------------------------------------\n|-----|%d SF vs %d M4|--(%d)---\n+---------------------------------------\n",
                            scores[i], stcScore[i], stcScore[i] - scores[i]);
                }
            }
            if (DDUMP)
                printf("|\n\\----------------------------------------\n\n");
        }
        auto e = elapsedS(qnow - t);
//        printf("Thread took %.2f s. %d Positions evaluated.\n", e, count);
    }

    Result run(const TuningTapWeights& taps, const TuningTableWeights& tables)
    {
        auto t = qnow;
        Result result{};

        size_t posPerThread = m4evals.size() / NUM_TRAIN_THREADS;
        {
            array<unique_ptr<jthread>, NUM_TRAIN_THREADS> threads;
            array<Result, NUM_TRAIN_THREADS> results{};

            auto evlIt = m4evals.begin();
            for (int i = 0; i < NUM_TRAIN_THREADS; i++, evlIt += posPerThread)
                threads[i] = make_unique<jthread>(&rate, i, taps, tables,
                    move(EvalSpan(evlIt, posPerThread)), reference_wrapper(results[i]));

            for (auto& t : threads)
                t->join();

            for (auto& r : results)
                result += r;
        }
        auto e = elapsedS(qnow - t);

        printf("Rating %.1f M positions (in %.1f M evals) took %.2f s\n\n",
            result.deviCnt[RESULTTYPE_TOTAL] / 1000000., m4evals.size() / 1000000., e);

        return result;
    }

    void optimizeFor(auto dur, bool optimizeTaps = true, bool optimizeTables = false)
    {
        assert(optimizeTaps || optimizeTables);
        assert(!optimizeTables); // table opt out of order

        RandomGenerator r;
        r.seeds[0] += qnow.time_since_epoch().count() % 10000; // force non-determinism

        TuningTapWeights bestTaps = origTapWeights;
        TuningTableWeights bestTables = origTableWeights;

        TuningTapWeights currentTapWeights;
        TuningTableWeights currentTableWeights;

        // get the result of the current (=currently best)
        Result bestResult = run(bestTaps, bestTables);
        Result startResult = bestResult;
        
        auto numRuns = 0;
        auto t = qnow;
        while (qnow - t < dur)
        {
/*            bool tapsOrTables = (r() % 100) > 70; // true= taps, change tables more often
            if (tapsOrTables && !optimizeTaps)
                continue;
            if (!tapsOrTables && !optimizeTables)
                continue;
*/
            printf("Current run: %d, best result: %.4f\n", numRuns, bestResult.get());
            numRuns++;

            if (numRuns % 100 == 10)
            {
                if (optimizeTaps)
                {
                    dump(origTapWeights, "*Intermediate*  Original Tap Weights:");
                    dump(bestTaps, "*Intermediate*  Best Tap Weights:");
                }
                if (optimizeTables)
                {
                    dump(origTableWeights, "*Intermediate*  Original TABLE Weights:");
                    dump(bestTables, "*Intermediate*  Best TABLE Weights:");
                }
            }

            currentTapWeights = bestTaps;
            currentTableWeights = bestTables;

//            if (tapsOrTables)
            {
                // optimize taps
                int id = 12 + (r() % (TAP_WEIGHTS_SIZE - 12));
                auto val = currentTapWeights.values[id];
                auto newVal = 0;
                if (abs(val) < 200)
                {
                    if (r() & 1)
                        newVal = val + 10;
                    else
                        newVal = val - 10;
                }
                else
                {
                    // add/decrease in between 5 and 20%
                    int coeff = (int)(3 + (abs((int)r()) % 19));
                    if (r() & 1)
                        coeff *= -1;
                    newVal = val + (val * coeff) / 100;

                    if (val == newVal)
                    {
                        printf("No value change (should not happen); skipping\n");
                        continue;
                    }
                }

//                if (newVal < 0)
//                    newVal = 0;

                // --
                currentTapWeights.values[id] = newVal;

                Result res = run(currentTapWeights, currentTableWeights);
//                printf("Tried to change tap value id %d from %d to %d (%.4f -> %.4f)  (huge %.4f -> %.4f)\n", id, val, newVal, bestResult.get(), res.get(), bestResult.getHugeFactor(), res.getHugeFactor());

                if (res.get() < bestResult.get() && res.getHugeFactor() <= bestResult.getHugeFactor())
                {
                    printf("New best result (%.4f -> %.4f) after changing tap value id %d from %d to %d  (huge %.4f -> %.4f)\n", bestResult.get(), res.get(), id, val, newVal, bestResult.getHugeFactor(), res.getHugeFactor());
                    bestResult = res;
                    bestTaps = currentTapWeights;
                }
                // --
            }
/* don't word           else
            {
                // optimize tables
                int id = r() % 768;
                auto val = currentTableWeights.values[id];
                auto newVal = 0;
                if (val == 5170)
                    newVal = val - 110; // max val
                else if (val == -5170)
                    newVal = val + 110; // min val
                else
                {
                    if (r() & 1)
                    {
                        // illegal values: (34-79)*110= **-4950**, (37-79)*110= **-4620**, (92-79)*110= **1430**
                        newVal = val + 110;
                        if (newVal == -4950 || newVal == -4620 || newVal == 1430)
                            newVal += 110;
                    }
                    else
                    {
                        newVal = val - 110;
                        if (newVal == -4950 || newVal == -4620 || newVal == 1430)
                            newVal -= 110;
                    }
                }
                assert(val != newVal);

                // --
                currentTableWeights.values[id] = newVal;

                Result res = run();
                if (res.get() < bestResult.get() && res.getHugeFactor() <= bestResult.getHugeFactor())
                {
                    printf("New best result (%.4f -> %.4f) after changing TABLE value id %d from %d to %d  (huge %.4f -> %.4f)\n", bestResult.get(), res.get(), id, val, newVal, bestResult.getHugeFactor(), res.getHugeFactor());
                    bestResult = res;
                    bestTables = currentTableWeights;
                }
                // --
            }
*/
        }

        if (optimizeTaps)
        {
            dump(origTapWeights, "Original Tap Weights:");
            dump(bestTaps, "Best Tap Weights:");
        }
        if (optimizeTables)
        {
            dump(origTableWeights, "Original TABLE Weights:");
            dump(bestTables, "Best TABLE Weights:");
        }

        printf(" ---- Start/final result: %.4f / %.4f   (huge %.4f -> %.4f)\n", startResult.get(), bestResult.get(), startResult.getHugeFactor(), bestResult.getHugeFactor());
    }

};


void readM4EvalFiles(const vector<string>& pathList, EvalList& evalsPart)
{
    RandomGenerator r;
    for (auto& p: pathList)
    {
        ifstream strm(p);
        
        while (!strm.eof())
        {
            // old-style char* parse is like 20 times faster on MSVC Debug, std::string tokenizing sucks
            char line[1024];
            if (!strm.getline(line, 1024).good())
                continue;

            evalsPart.resize(evalsPart.size() + 1);
            EvalPos& e = *(evalsPart.end() - 1);

            const char* p = line;
            p += e.pos.initFromFEN(p, nullptr, &e.fullmoveNumber);

            e.isCapture = atoi(p);
            e.isCheck = atoi(p + 2);
            p += 4;

            for (int i = 0; i < 3; i++)
            {
                if (*p != '\0' && *p != '\n' && *p != '\r')
                {
                    e.bestMoves[i].first = alignMove(convertTextToMove(p), e.pos.flipped);

                    p += 5;
                    while (*p == ' ') p++;
                    e.bestMoves[i].second = atoi(p);
                    while (*p != '\0' && *p != ' ' && *p != '\n' && *p != '\r')
                        p++;
                    while (*p == ' ') p++;
                }
                else
                    e.bestMoves[i] = { 0, SCORE_INVALID };
            }

            if (FLATTEN_GAME_PHASES) // Feature not in use - instead, evals are weighted based on the game phase distribution
            {
                // flatten distribution based on printGamePhaseDistribution() info
                int phase = e.pos.getPhase();

                int chance = 100;
                if (phase == 0)
                    chance = 50;
                if (phase == 26 || phase == 28)
                    chance = 60;
                if (phase == 56)
                    chance = 66;

                // is bestmove a capture?
                Position pos = e.pos;
                pos.makeMove(e.bestMoves[0].first);
                if (pos.flags & FLAGS_CAPTURE)
                    chance /= 2;
                    
                if (chance <= (r() % 100))
                    evalsPart.resize(evalsPart.size() - 1);
            }
        }
    }
}

static void setCurrentPath()
{
    // make sure we are in the right directory
    for (int count = 0; !fs::exists(fs::current_path() / "data"); count++)
    {
        assert(count < 5);
        fs::current_path(fs::current_path().parent_path());
    }
}

static void readTrainingData()
{
    setCurrentPath();

    printf("Reading training database...\n");
    size_t numFiles = 0;
    size_t numPositions = 0;
    auto t = qnow;

    if (READ_BINARY_DATA)
    {
        error_code ec;
        auto fileSize = filesystem::file_size(fs::current_path() / "data" / "m4evalsBin" / "bin1.m4evalBin", ec);
        if (ec || fileSize == 0)
        {
            printf("No binary data present!\n");
            assert(false);
        }

        auto evalsPerFile = fileSize / sizeof(EvalPos);
        m4evals.resize(evalsPerFile * NUM_BIN_FILES);

        // load all binary .m4evalBin files in ./data/m4evalsBin/
        for (auto i = 0; i < NUM_BIN_FILES; i++)
        {
            auto fn = "bin"s + to_string(i + 1) + ".m4evalBin"s;
            auto p = fs::current_path() / "data" / "m4evalsBin" / fn;
//            printf("Loading %s\n", p.string().data());

            auto strm = ifstream(p, ios::binary | ios::in);
            assert(strm.good());

            strm.read((char*)(&m4evals[i * evalsPerFile]), fileSize);
        }

        numPositions = evalsPerFile * NUM_BIN_FILES;
        numFiles = NUM_BIN_FILES;
    }
    else
    {
        using Job = pair<vector<string>, EvalList>;
        Job jobs[NUM_TRAIN_THREADS];

        // iterate over all .m4eval files in ./data/m4evals/
        for (auto& p : fs::recursive_directory_iterator(fs::current_path() / "data" / "m4evals"))
        {
            if (p.is_regular_file() && p.path().extension() == ".m4eval")
            {
                if (DETAIL_DUMP)
                    printf("Adding <%s>\n", p.path().filename().string().data());
                int threadId = numFiles % NUM_TRAIN_THREADS;
                jobs[threadId].first.emplace_back(p.path().string());
                jobs[threadId].second.reserve((8 * 1024 * 1024) / NUM_TRAIN_THREADS);
                numFiles++;

                if (MAX_TRAIN_FILES != 0 && numFiles >= MAX_TRAIN_FILES)
                    break;
            }
        }

        // fire worker threads that load the m4 eval files
        {
            vector<unique_ptr<jthread>> threads;
            for (auto& j : jobs)
                threads.emplace_back(make_unique<jthread>(readM4EvalFiles,
                    j.first, reference_wrapper(j.second)));
        }

        for (auto& j : jobs)
            numPositions += j.second.size();

        m4evals.reserve(numPositions);
        for (auto& j : jobs)
            m4evals.insert(m4evals.end(), j.second.begin(), j.second.end());
    }
    auto e = elapsedS(qnow - t);

    printf("Reading %.1f M evals in %d files took %.2f s\n\n", numPositions / 1000000., int(numFiles), e);
}

static void sortShuffleSaveBin()
{
    printf("--- Sort out duplicates, shuffle and save as binary\n");
    assert(!READ_BINARY_DATA);

    int duplicates = 0;
    int hardCollisions = 0;
    {
        map<u64, EvalPos> evals;
        for (auto& e : m4evals)
        {
            u64 hsh = makeHash(e.pos);

            auto it = evals.find(hsh);
            if (it == evals.end())
            {
                // not in map
                evals[hsh] = e;
            }
            else
            {
                // Is hard collision?
                if (e.pos != (*it).second.pos)
                {
                    printf("HARD COLLISION DETECTED!\n");
                    hardCollisions++;
                    printf("FIRST (%llu):\n", (long long)hsh);
                    e.pos.dump();
                    printf("SECOND (%llu):\n", (long long)(*it).first);
                    (*it).second.pos.dump();
                }
                else
                    duplicates++;
            }
        }

        m4evals.clear();
        for (auto& e : evals)
            m4evals.push_back(e.second);
    }

    mt19937 rng;
    shuffle(m4evals.begin(), m4evals.end(), rng);

    auto blackPos = count_if(m4evals.begin(), m4evals.end(), [](auto& e) { return e.pos.flipped; });

    printf("# of duplicated: %d (out of %d, %.2f%%), # of hard collisions: %d  White/Black ratio=%.2f\n",
        duplicates, (int)m4evals.size(), duplicates / double(m4evals.size()) * 100., hardCollisions, blackPos / double(m4evals.size()));

    // Save
    auto ePerFile = m4evals.size() / NUM_BIN_FILES;
    for (auto i = 0; i < NUM_BIN_FILES; i++)
    {
        auto fn = "bin"s + to_string(i + 1) + ".m4evalBin"s;
        auto p = fs::current_path() / "data" / "m4evalsBin" / fn;
        printf("Saving %s\n", p.string().data());

        auto strm = ofstream(p, ios::binary | ios::out);
        assert(strm.good());

        strm.write((char*)(&m4evals[i * ePerFile]), sizeof(EvalPos) * ePerFile);
    }
}

static array<TrainingSession::Result, NUM_SESSIONS_CACHED> cachedRes{};

static void printResult(TrainingSession::Result res)
{
    cachedRes[0] = res;
    int numCachedSessions = (int)count_if(cachedRes.begin(), cachedRes.end(), [](auto& s) { return s.has(); });
    //    printf("# of cached sessions = %d\n", numCachedSessions);

    printf(".---------------------------------------------------------------------------------------------------------.\n");
    printf("| Session | Eval deviation in cp          | %% of huge deviations          | # of samples                  |\n");
    printf("|---------+-------------------------------+-------------------------------+-------------------------------|\n");
    printf("|         | Total | Quite | Captu | Check | Total | Quite | Captu | Check | Total | Quite | Captu | Check |\n");
    printf("|---------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------|\n");

    for (int i = 0; i < numCachedSessions; i++)
    {
        TrainingSession::Result& r(cachedRes[i]);
        if (i < 2)
            printf("| %s |", ((i == 0) ? "Current" : " Last  "));
        else
            printf("|  %3d    |", -i);

        double to = r.get(TrainingSession::RESULTTYPE_TOTAL);
        double qu = r.get(TrainingSession::RESULTTYPE_QUITE);
        double ca = r.get(TrainingSession::RESULTTYPE_CAPTURE);
        double ch = r.get(TrainingSession::RESULTTYPE_CHECK);

        printf("%6.3f |%6.3f |%6.3f |%6.3f |", to, qu, ca, ch);

        to = r.getHugeFactor(TrainingSession::RESULTTYPE_TOTAL) * 100.;
        qu = r.getHugeFactor(TrainingSession::RESULTTYPE_QUITE) * 100.;
        ca = r.getHugeFactor(TrainingSession::RESULTTYPE_CAPTURE) * 100.;
        ch = r.getHugeFactor(TrainingSession::RESULTTYPE_CHECK) * 100.;

        printf("%03.3f%%%s|%03.3f%%%s|%03.3f%%%s|%03.3f%%%s|",
            to, ((to < 10.) ? " " : ""), qu, ((qu < 10.) ? " " : ""), ca, ((ca < 10.) ? " " : ""), ch, ((ch < 10.) ? " " : ""));

        to = r.getSamples(TrainingSession::RESULTTYPE_TOTAL) / 1000000.;
        qu = r.getSamples(TrainingSession::RESULTTYPE_QUITE) / 1000000.;
        ca = r.getSamples(TrainingSession::RESULTTYPE_CAPTURE) / 1000000.;
        ch = r.getSamples(TrainingSession::RESULTTYPE_CHECK) / 1000000.;

        printf(" %4.1fM | %4.1fM | %4.1fM | %4.1fM |\n", to, qu, ca, ch);
    }
    printf("'---------------------------------------------------------------------------------------------------------'\n");
}

static void loadResults()
{
    // load cached results
    {
        ifstream is("sessions.m4train", ios::binary | ios::in);
        if (is.good())
            is.read((char*)cachedRes.data(), sizeof(cachedRes));
    }

    // shift cache one right and (later) add own result to the beginning
    copy(cachedRes.begin(), cachedRes.end() - 1, cachedRes.begin() + 1);
}

static void saveResults()
{
    // save cached results
    ofstream os("sessions.m4train", ios::binary | ios::out);
    if (os.good())
        os.write((char*)cachedRes.data(), sizeof(cachedRes));
}

static void printGamePhaseDistribution()
{
    int phasesCount[256 / 4]{};
    int count = 0;

    for (auto& e : m4evals)
    {
        int phase = max(0, (e.pos.getPhase() * 255) / 256);
        assert(phase >= 0 && phase < 256);

        phasesCount[phase / 4]++;
        count++;
    }

    printf("Game phase distruction in test data:\n");
    printf("----------------------------------------------------------------\n");

    printf("Number of phases = 256, samples = 64, perfect normalized value = %.2f%%:\n", 100. / 64.);
    printf("----------------------------------------------------------------\n");
    for (auto h = 0; h < 64; h++)
    {
        auto v = phasesCount[h] / double(count) * 100.;
        printf("[%3d ... %3d] = %.2f%% ", h * 4, (h + 1) * 4 - 1, v);
        if (v < 10.0)
            printf(" ");

        for (auto x = 0; x < 64; x++)
            printf((x == 6)? ((v > x * 0.25) ? "+" : "|") : (v > x * 0.25)? "*" : " ");
        printf("\n");
    }
    printf("----------------------------------------------------------------\n");
    {
        int phasesCount[257]{};
        int count = 0;

        for (auto& e : m4evals)
        {
            int phase = max(0, e.pos.getPhase());
            assert(phase >= 0 && phase <= 256);

            phasesCount[phase]++;
            count++;
        }

        for (auto y = 0; y < 32; y++)
        {
            for (auto x = 0; x < ((y == 31)? 9 : 8); x++)
            {
                auto v = phasesCount[x + y * 8] / double(count) * 100.;
                printf("[%3d]=%.2f%%%s ", x + y * 8, v, (v < 10.0) ? " " : "");
            }
            printf("\n");
        }
    }
    printf("----------------------------------------------------------------\n");
}

static void generatePhaseDistributionTable()
{
    int phasesCount[257]{};
    int count = 0;

    for (auto& e : m4evals)
    {
        int phase = max(0, e.pos.getPhase());
        assert(phase >= 0 && phase <= 256);

        phasesCount[phase]++;
        count++;
    }

    for (auto i = 0; i <= 256; i++)
    {
        auto v = phasesCount[i] / (count / 100.);
        if (v < .05)
            phaseDistriWeights[i] = 4096 * 4;
        else // normalized weight = .39
            phaseDistriWeights[i] = min(4096 * 5, max(4096 / 3, Weight((.39 * 4096.) / v) ));
    }
    if (PRINT_GAMEPHASE_DISTRIBUTION)
    {
        printf("##################################################\n");
        printf("# Game phase distribution weights used for rating:\n");
        printf("##################################################\n");

        for (auto y = 0; y < 32; y++)
        {
            for (auto x = 0; x < ((y == 31) ? 9 : 8); x++)
            {
                auto v = phaseDistriWeights[x + y * 8] / 4096.;
                printf("[%3d]=%.2f%s ", x + y * 8, v, (v < 10.0) ? " " : "");
            }
            printf("\n");
        }
        printf("#################################################\n");
    }
    else
        printf("Generated game phase distribution weights.\n");
}

static void debugEval(const char* fen)
{
    Position pos;
    u16 fullmoveNumber = 1;
    pos.initFromFEN(fen, nullptr, &fullmoveNumber);

    printf("\n/---------------------------------- Debug Eval; Game Phase: %3d -----\n", pos.getPhase());
    pos.dump(fullmoveNumber);

/*    currentTapWeights.scoreFast = 0; currentTapWeights.scoreBase = 0;*/
    int score = pos.evaluate(origTapWeights, origTableWeights, true);
    if (pos.flipped)
        score = -score;
    printf("|-        Eval from white's view:%4d\n\\------------------------------------------------------------------\n", score);
}

static void doTraining()
{
#if 0
    // eval custom position with extended eval information output
    printf("* WINNING/DRAWING:\n");
    debugEval("r3k1nr/1pqnb1pp/p3p3/2ppPb2/NP6/P4N1P/2P1BPP1/R1BQ1RK1 w kq - 2 13");

    printf("* AFTER BAD MOVE:\n");
    debugEval("r3k1nr/1pqnb1pp/p3p3/2ppPb2/NP3B2/P4N1P/2P1BPP1/R2Q1RK1 b kq - 3 13");

    printf("* SHOULD HAVE PLAYED:\n");
    debugEval("r3k1nr/1pqnb1pp/p3p3/2ppPb2/NP6/P2B1N1P/2P2PP1/R1BQ1RK1 b kq - 3 13");
    return;
#endif

    printf("Training session started. Running on %d Threads (%d available).\n", NUM_TRAIN_THREADS, thread::hardware_concurrency());

    readTrainingData();

    // to create binaries; note: deploy only binary data
    if (CREATE_BINARY_DATA)
        sortShuffleSaveBin();

    generatePhaseDistributionTable();

    if (PRINT_GAMEPHASE_DISTRIBUTION)
        printGamePhaseDistribution();

    using namespace TrainingSession;

#ifdef RUN_OPTIMIZE_SESSION
    printf("---- Running optimization session\n");

    dump(origTapWeights, "Original Tap Weights:");
    dump(origTableWeights, "Original TABLE Weights:");

    optimizeFor(3h);
#else
    Result res = run(origTapWeights, origTableWeights);

    if (!INSPECT_SINGLE_FEN.empty())
        return;

    loadResults();

    printResult(res);
//    printf("Correct base adj: %lld\n", res.correctBaseAdj / 1024ll);

    saveResults();
#endif
}

#endif
