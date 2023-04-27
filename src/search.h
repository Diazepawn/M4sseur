
//#define DISABLE_TT

#ifdef DUMP
  #define TRACK_PV
#endif

#ifdef TEST
  int test_maxDepth;
#endif

#define QSEARCH_MAX_DEPTH 12
#define MAX_SEARCH_DEPTH 112


int plyCounter{};                       //@-
int currentIndex, TIME_UP;
flt thinkTime;


#ifdef TRACK_PV
 #ifndef TRACK_ONLY_ESSENTIAL
  string pvString(Move mv, int cnt)
  {
    #ifdef DISABLE_TT
      return ""s;
    #endif

    #ifndef NDEBUG
      set<u64> played; // catch repetitions (to force draw, crazy rook, etc)
      cnt += 32;
    #endif
      string res;
      Position pos = rootPosition;
      for (;;)
      {
          pos.makeMove(mv);
          u64 hsh = makeHash(pos);

          TTDataEntry tt;
          loadTTEntry(hsh, tt);

          mv = tt.move;
          if (cnt-- < 0 || tt.depth < 0 || mv == 0 || mv == 56)
              return res;

          res += " " + convertMoveToText(alignMove(mv, pos.flipped));

    #ifndef NDEBUG
        if (played.contains(hsh))
            return res + " ...repeats..."s;
        played.insert(hsh);
    #endif
      }
  }
 #endif
#endif


struct SearchThread : jthread
{
    int index, bestScore, confirmedBestDeep{}, bestMove, confirmedBestMove;

#ifdef DUMP
    s16 confirmedBestScore, padding;
#endif

#ifdef TRACK_PV
    u64 numNodes{};
#endif

    struct Entry
    {
        u64 hsh;
        struct /*PriotizedMoves*/
        {
            Position pos;
            int sortPrio, move;
        } moves[128];
    } stack[128];

    SearchThread() :
        jthread([=, this] { iterativeM4ssaging(); }),
        index(currentIndex++)
    {}

    template<bool QSEARCH = 0, bool ROOT = 0>
    s16 negaMax(const Position& pos, s16 alpha, s16 beta, s16 plyDist_ROOT, char depth)
    {
        Entry& s(stack[plyDist_ROOT]);
        TTDataEntry ttData, t;
        s16 alphaOrig = alpha, val = -SCORE_INFINITY, phase;
        Move m{};
        char cnt{};

        if (TIME_UP)
            return 0;

#ifdef TRACK_PV
        numNodes++;
#endif
      
#ifndef DISABLE_TT
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////// TT READ
        s.hsh = makeHash(pos);

        for (auto i = plyDist_ROOT - 2; i >= 0; i--)
            if (s.hsh == stack[i].hsh)
                return 0; // repetition

        IFC (!ROOT /*&& !QSEARCH*/)
            loadTTEntry(s.hsh, ttData);

        if (ttData.depth >= 0 && ttData.movePlayedCnt)
            return 0; // repetition
    
        // has valid TT data?
        if (ttData.depth >= depth)
        {
            alpha = max(alpha, ttData.leap & LEAP_LOWER? ttData.score : alpha);
            beta =  min(beta,  ttData.leap & LEAP_UPPER? ttData.score : beta);
            if (alpha >= beta || ttData.leap & LEAP_EQUAL)
                return ttData.score;
        }
        else if (depth > 4 && (index + depth) & 1)
            depth--;
        ///////////////////////////////////////////////////////////////////////////////////////////////// TT READ
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif

        IFC (QSEARCH)
        {
            phase = pos.evaluate();
            if (phase >= beta)
                return beta;
            alpha = max(alpha, phase);

            if (!depth)
                return alpha;
        }

        if (!depth)
            return negaMax<1>(pos, alpha, beta, plyDist_ROOT, QSEARCH_MAX_DEPTH);

        ////////////////////////////////////////////////////
        MoveList moves;
        bool inCheck = pos.generateLegalMoves(moves);
        ////////////////////////////////////////////////////

        if (!moves.cnt)
            // *** Either stalemate or mate ***
            // in case this is a root note, a position where no legal moves are available was given, which is must not occur
            // "If the root becomes a terminal node, the game is finished."
            return QSEARCH? alpha : inCheck? -(SCORE_MATE - plyDist_ROOT) : 0;
        
        // make lazy SMP speedy
        phase = pos.getPhase() - 128 + (index + plyDist_ROOT) % NUM_THREADS * 256 / NUM_THREADS;
        for (auto i = 0; i < moves.cnt; i++)
        {
            s.moves[cnt].pos = pos;
            s.moves[cnt].pos.makeMove(moves.entries[i]);
            if (!QSEARCH || s.moves[cnt].pos.flags || inCheck)
                s.moves[cnt].sortPrio = moves.entries[i] == ttData.move? -(1 << 30) : s.moves[cnt].pos.evaluateFast(phase),
                s.moves[cnt++].move = moves.entries[i];
        }

        // move sorting
        sort(s.moves, s.moves + cnt, [](auto& c, auto& i) { return c.sortPrio < i.sortPrio; });

        // eval moves
        for (auto i = 0; i < cnt; i++)
        {
            ////////////////////////////////////////////////////////////////////////////////////////
            s16 t = -negaMax<QSEARCH>(s.moves[i].pos, -beta, -alpha, plyDist_ROOT + 1, depth - 1);
            ////////////////////////////////////////////////////////////////////////////////////////
            IFC (QSEARCH)
            {
                if (t >= beta)
                    return beta;
                alpha = max(alpha, t);
            }
            else if (t > val)
            {
                val = t;
                m = s.moves[i].move;

                IFC (ROOT)
                    bestMove = m;

                alpha = max(alpha, val);
                if (alpha >= beta)
                    break;
            }
        }

        // important: never write to TT when time is up, since the current values may be messed up
        if (QSEARCH || TIME_UP)
            return alpha;

#ifndef DISABLE_TT
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////// TT WRITE
        if (val > 30000)
            alphaOrig = alpha;

        // check whether in the meantime the TT slot was updated by a deeper search
        loadTTEntry(s.hsh, t);
        if (ROOT || t.depth > ttData.depth)
            return val;

        // don't overwrite entries with a higher depth or repetition entries
        if (ttData.depth <= depth && !ttData.movePlayedCnt && (val <= -SCORE_MATE + plyDist_ROOT || val > -30000))
            storeTTEntry(s.hsh, { depth, 0, char(val <= alphaOrig ? LEAP_UPPER :
                                                 val >= beta      ? LEAP_LOWER :
                                                                    LEAP_EQUAL), m, val } );

        ///////////////////////////////////////////////////////////////////////////////////////////////// TT WRITE
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif
        return val;
    }

    void iterativeM4ssaging()
    {
#ifdef TRACK_PV
#ifndef TRACK_ONLY_ESSENTIAL
            auto t = steady_clock::now();
#endif
#endif

#ifdef TEST
        for (auto depth = 1; depth <= test_maxDepth; depth++)
#else
        for (auto depth = 1; depth < MAX_SEARCH_DEPTH; depth++)
#endif
        {
            ///////////////////////////////////////////////////////////////////////////////////
            bestScore = negaMax<0,1>(rootPosition, -SCORE_INFINITY, SCORE_INFINITY, 0, depth);
            ///////////////////////////////////////////////////////////////////////////////////
            if (TIME_UP) return;
            assert(bestMove != 0);
#ifdef TEST
            // in testing: either we can stop searching when any thread reaches max depth, or all
            if (depth == test_maxDepth)
                TIME_UP = 1;
#endif
            confirmedBestDeep = depth;
            confirmedBestMove = alignMove(bestMove, rootPosition.flipped);
            assert(confirmedBestMove != 0 && confirmedBestMove != 56);

#ifdef DUMP
            confirmedBestScore = bestScore;
            assert(bestScore < SCORE_INFINITY);
            assert(bestScore > -SCORE_INFINITY);
#endif

#ifdef TRACK_PV
            if (index == 0
#ifdef TEST
                           || depth == test_maxDepth
#endif
                                                     )
            {
#ifndef TRACK_ONLY_ESSENTIAL
                auto timeElapsed = duration<flt>(steady_clock::now() - t).count();
                long long nn = numNodes * NUM_THREADS;

                printf("info depth %d nodes %lld nps %d time %d score cp %d pv %s%s\n",
                    depth, nn, int(nn / timeElapsed), int(timeElapsed * 1000.), bestScore,
                    convertMoveToText(confirmedBestMove).data(), pvString(bestMove, depth).data());
#else
                printf("info depth %d nodes %d score cp %d pv %s\n",
                    depth, int(numNodes), bestScore, convertMoveToText(confirmedBestMove).data());
#endif
            }
#endif
        }
    }
};


Move findBestMove()
{
#ifdef DUMP
#ifndef TEST
    printf("--- findBestMove started; Max time to find a move = %.2f s\n", thinkTime);
#else
    printf("--- findBestMove started; Max depth to find a move = %d\n", test_maxDepth);
#endif
#endif
    currentIndex = TIME_UP = 0;
    vector<SearchThread> threads(NUM_THREADS);

    auto t = steady_clock::now();

#ifndef TEST
    for (;steady_clock::now() - t < duration<flt>(thinkTime); this_thread::sleep_for(1s))
        ;
    TIME_UP = 1;
#endif

    for (auto& t : threads)
        t.join();

    // Find best search thread
#ifdef DUMP
    s16 bestScr = 0;
    int bestThreadIndex = 0;
#endif
    auto deepestSearch = 0;
    auto bestMv = 0;
    for (auto& t : threads)
        if (t.confirmedBestDeep > deepestSearch)
#ifdef DUMP
        {
#endif
            bestMv = t.confirmedBestMove, deepestSearch = t.confirmedBestDeep;
#ifdef DUMP
            bestScr = t.confirmedBestScore;
            bestThreadIndex = t.index;
        }
#endif

    assert(bestMv != 0);

#ifdef TEST
    for (auto& t : threads)
        if (t.confirmedBestDeep == deepestSearch && t.confirmedBestMove != bestMv)
            printf("!!!!!!!!!!!!!\nDifferent best moves found %s vs %s\n!!!!!!!!!!!!!\n",
                convertMoveToText(t.confirmedBestMove).data(), convertMoveToText(bestMv).data());
#endif

#ifdef DUMP
    u64 totalNodes = 0;
    for (auto& t : threads)
        totalNodes += t.numNodes;
    flt timeElapsed = duration<flt, milli>(qnow - t).count();
    printf("*** Mnps: %.2f *** (%.2f M Nodes in %.2f s) Thread#%d (deep %d) found Best Move %s Score %d\n",
        totalNodes / (timeElapsed * 1000.), totalNodes / 1000000., timeElapsed / 1000., bestThreadIndex, deepestSearch,
        convertMoveToText(bestMv).data(), bestScr);
#endif

    return bestMv;
}
