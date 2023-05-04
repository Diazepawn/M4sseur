
//////////////////// Enable to get PV/NPS/Depth/NodeCount info during the game ///////
#define TRACK_PV
#define TRACK_ONLY_ESSENTIAL
//////////////////////////////////////////////////////////////////////////////////////

//#define DUMP
//#define TEST
//#define TRAIN
//#undef NDEBUG

#define NUM_THREADS 8         // Will be reset for TCEC build by Nanonizer
#define TTSize 0x20000000ull  /*  0x4000000ull =  1 GB,  0x8000000ull =  2 GB,  0x10000000ull =  4 GB,  0x20000000ull =  8 GB,
                                 0x40000000ull = 16 GB, 0x80000000ull = 32 GB, 0x100000000ull = 64 GB (=used for TCEC build)  */

#include "common.h"
#include "random.h"
#include "tables.h"
#include "movelist.h"
#include "movegen.h"
#include "position.h"
#include "dump.h"
#include "fen.h"
#include "ttable.h"
#include "eval.h"
#include "search.h"
#include "testing.h"
#include "training.h"


int main(
         int argc, char* argv[]                             //@-
                                )
{
#ifndef NDEBUG
    auto sizeInMB = (int)((ttable.size() * 16ull) / (1024ull * 1024ull));
    printf("TT size in MB = %d\n", sizeInMB);
    for (int i = 0; i < argc; i++)
        printf("Arg#%d = %s\n", i, argv[i]);
#endif

    memset(&ttable[0], 0, TTSize * 16);

    rootPosition.setStartpos();                             //@-

#ifdef TEST
    doTesting();
    return 0;
#endif

#ifdef TRAIN
    doTraining();
    return 0;
#endif

    setbuf(stdout, 0);

    // Wait for "uci"
    readUCILine();

#ifndef NDEBUG
    if (strcmp("uci", strBuf) == 0)
        printf("Warning! First input is not 'uci'\n");
#endif

    // --- Send UCI info and "uciok"
//    printf("id name M4sseur\nid author Maik Guntermann\nuciok\n");
    printf("id name M4sseur\nuciok\n");
    // -----------------------------------------------------------
    for (;;)
    {
        // ****** MAIN FETCH *******
        readUCILine();

        if (strCompare("q"/*uit*/ ))
            break;
        if (strCompare("i"/*sready*/ ))
            printf("readyok\n");
        if (strCompare("g"/*o*/ ))
        {
            // go and massage some pawns...
            auto thinkTime = 30.;                           //@-
            while (*strPtr)
                if (119 - 21 * rootPosition.flipped == *strPtr++)
#ifdef TCEC
                {
                    printf("bestmove %s\n", convertMoveToText(findBestMove(atoi(strPtr + 5) / 34000. + 2.9)).data());
                    break;
                }
#else
                {
                    thinkTime = atoi(strPtr + 5) / 34000. + 2.9;
                    break;
                }
            printf("bestmove %s\n", convertMoveToText(findBestMove(thinkTime)).data());
#endif
        }
        if (strCompare("ucinewgame"))                       //@---
        {
            // --- ucinewgame (TODO: make sure this isn't needed in TCEC; I strongly assume they will restart the engine for a new game)
            plyCounter = 0;
            rootPosition.setStartpos();
            memset(&ttable[0], 0, TTSize * 16);
        }                                                   //---@
        if (strCompare("position"))
        {
            if (strCompare("startpos"))
                rootPosition.setStartpos();
            if (strCompare("fen"))                          //@---
            {
                // --- set FEN position
                u16 fullmoveCount = 1;
                strPtr += rootPosition.initFromFEN(strPtr, nullptr, &fullmoveCount);
                plyCounter = (((fullmoveCount - 1) / 2) * 2) + rootPosition.flipped;
            }                                               //---@
            if (strCompare("moves"))
                while (*strPtr)
                {
                    // mark move as played in order to detect repetitions
                    storeTTEntry(makeHash(rootPosition), { 0, 1 });

                    rootPosition.makeMove(alignMove(convertTextToMove(strPtr),
                                            rootPosition.flipped));

                    plyCounter++;                           //@-
                    strPtr += 5;
                    while (*strPtr == ' ') strPtr++;
                }
#ifdef DUMP
            rootPosition.dump();
#endif
        }
    }
    return 0;                                               //@-
}
