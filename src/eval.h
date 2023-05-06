
#ifdef TRAIN                                                    //@---
#define EDUMP(fmt, ...) if (detailDump) printf(fmt, ##__VA_ARGS__);
#else
#define EDUMP(fmt, ...) ((void)0);
#endif                                                          //---@

#define FSTEVAL_SCL 44
#define NRMEVAL_SCL 36


Score Position::evaluate(
#ifdef TRAIN
        const TuningTapWeights& tapWeights,
        const TuningTableWeights& tableWeights,
        bool detailDump
#endif
                            ) const
{
    // If this->flipped is true, "we" HAVE the black pieces (in theory) but PLAY with the white pieces (actually)
    // Compute the eval from the "white" perspective! (so that the score is computed for the player to move)
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // determine game phase
    auto phase = getPhase();

    /********************************/
    Weight score[2][2]{900, 900, 0, 0}; // tempi bonus for side to move
    /********************************/

#ifdef TRAIN
    int pwnSupAny[2]{};
    int pwnDoublePen[2]{};
    int pwnPassed[2]{};
    int pwnFork[2]{};
    int kingPawnProt[2]{};
    int mobAll[2]{};
    int threatAtt[2]{};
    int threatAttWhich[4][2]{};
#endif

    EDUMP("|- TempiBonus   %5d | %5d (=%5d)  phase %d\n", (score[0][0] + (score[0][1] - score[0][0]) * phase / 256) / NRMEVAL_SCL, (score[1][0] + (score[1][1] - score[1][0]) * phase / 256) / NRMEVAL_SCL, ((score[0][0] + (score[0][1] - score[0][0]) * phase / 256) - (score[1][0] + (score[1][1] - score[1][0]) * phase / 256)) / NRMEVAL_SCL, phase) //@-
    tapWeight2(TAP_BISHPAIR_BONUS, score[0], qcount(_(BISHOP, 0)) == 2); // TODO: check if in for() loop is better to compress
    tapWeight2(TAP_BISHPAIR_BONUS, score[1], qcount(_(BISHOP, 1)) == 2);
    EDUMP("|- BishPair     %5d | %5d (=%5d)\n", (qcount(_(BISHOP, 0)) == 2)* tapWeight(TAP_BISHPAIR_BONUS, phase) / NRMEVAL_SCL, (qcount(_(BISHOP, 1)) == 2)* tapWeight(TAP_BISHPAIR_BONUS, phase) / NRMEVAL_SCL, (qcount(_(BISHOP, 0)) == 2)* tapWeight(TAP_BISHPAIR_BONUS, phase) / NRMEVAL_SCL - (qcount(_(BISHOP, 1)) == 2) * tapWeight(TAP_BISHPAIR_BONUS, phase) / NRMEVAL_SCL) //@-

    iterate([&]<Piece p, Color c>(Square s)
    {
        // sw = seen from white perspective (if color is black)
        auto sw = s ^ c * 56;
        // m = mask with moves/captures of current piece; pawns [NOT: opponent pieces] are blockers
        Mask m, mw = mkmsk(sw), t = mw << 8;
        t |= t << 8;

        IFC (p == KING)
        {
            t = qflip(t, c); // 2 squares above the king
            tapWeight2(TAP_KING_SAFETY_PAWNPROT, score[c], !!(t & _(PAWN, c)) + !!((moveLeft(t) | moveRight(t)) & _(PAWN, c)));
#ifdef TRAIN
            if (detailDump)
                kingPawnProt[c] += (!!(t & _(PAWN, c)) + !!((moveLeft(t) | moveRight(t)) & _(PAWN, c))) * tapWeight(TAP_KING_SAFETY_PAWNPROT, phase);
#endif
            return;
        }
        IFC (p == PAWN)
        {
            m = qflip(moveUpLeft(mw) | moveUpRight(mw), c); 

            // fork two pieces (>=knight)?
            tapWeight2(TAP_PAWN_FORK, score[c], qcount(m & _c(!c) & ~_(PAWN, !c)) == 2);
#ifdef TRAIN
            if (detailDump)
                pwnFork[c] += (score[c], qcount(m & (_c(!c) & ~pieceMask[PAWN])) == 2) * tapWeight(TAP_PAWN_FORK, phase);
#endif

            // add minimal bonus if pawn protects anything own
            tapWeight2(TAP_PAWN_SUPPORTS_ANY, score[c], qcount(m & _c(c)) );
#ifdef TRAIN
            if (detailDump)
                pwnSupAny[c] += qcount(m & _c(c)) * tapWeight(TAP_PAWN_SUPPORTS_ANY, phase);
#endif

            // double pawn penalty if a pawn is above this pawn on the same file
            t |= t << 16; t |= t << 32;
            t = qflip(t, c); // t is now the "north" mask
            tapWeight2(TAP_PAWN_DOUBLE_PEN, score[c], !!(t & _(PAWN, c)));
#ifdef TRAIN
            if (detailDump)
                pwnDoublePen[c] += !!(t & _(PAWN, c)) * tapWeight(TAP_PAWN_DOUBLE_PEN, phase);
#endif

            // skip isolated pawn check; they already get a penality since they are not protected
            // ...

            // is passed pawn? double bonus if no other pawn is in the way on the same file
            tapWeight2(TAP_PAWN_PASSED, score[c], (sw / 8 + 4) / 4 * !((t | moveRight(t) | moveLeft(t)  ) & _(PAWN, !c) ));
#ifdef TRAIN
            if (detailDump)
                pwnPassed[c] += ((sw / 8 + 4) / 4 * !((t | moveRight(t) | moveLeft(t)) & _(PAWN, !c))) * tapWeight(TAP_PAWN_PASSED, phase);
#endif
        }
        else
        {
            ///////////////////////////////////////////////////////////
            m = iteratePieceMoves<p>(_(p, c), _(PAWN, WHITE_BLACK));
            ///////////////////////////////////////////////////////////
        }

        tapWeight2(TAP_MOB_MOVESALL, score[c], qcount(m & ~_(PAWN, c)));
#ifdef TRAIN
        if (detailDump)
            mobAll[c] += qcount(m) * tapWeight(TAP_MOB_MOVESALL, phase);
#endif

        // If a low-valued piece attacks a high values piece, give a huge bonus
        // If a high-valued piece attacks a low valued piece, give a small bonus
        const int scorePieces[] = { 0, 1, 1, 2, 3, 3, 3 };

        m &= _c(!c);
        while (m)
        {
            s = getNextBit(m); // rename to t

#ifndef TRAIN
            tapWeight2(TAP_THREATATTK_SMALL + scorePieces[getPiece(mkmsk(s))] - scorePieces[p], score[c]);
#else
            tapWeight2(TAP_THREATATTK_SMALL + scorePieces[getPiece(mkmsk(s))] - scorePieces[p], score[c], 1);
            if (detailDump)
            {
                threatAtt[c] += tapWeight(TAP_THREATATTK_SMALL + scorePieces[getPiece(mkmsk(s))] - scorePieces[p], phase);
                if (scorePieces[getPiece(mkmsk(s))] - scorePieces[p] >= 0)
                    threatAttWhich[scorePieces[getPiece(mkmsk(s))] - scorePieces[p]][c] += tapWeight(TAP_THREATATTK_SMALL + scorePieces[getPiece(mkmsk(s))] - scorePieces[p], phase);
            }
#endif
        }

    });

    EDUMP("|- PwnSuprtAny  %5d | %5d (=%5d)\n", pwnSupAny[0]/NRMEVAL_SCL, pwnSupAny[1]/NRMEVAL_SCL, pwnSupAny[0] / NRMEVAL_SCL - pwnSupAny[1] / NRMEVAL_SCL) //@-
    EDUMP("|- PwnDoublePen %5d | %5d (=%5d)\n", pwnDoublePen[0]/ NRMEVAL_SCL, pwnDoublePen[1]/ NRMEVAL_SCL, pwnDoublePen[0] / NRMEVAL_SCL - pwnDoublePen[1] / NRMEVAL_SCL) //@-
    EDUMP("|- PwnPassed    %5d | %5d (=%5d)\n", pwnPassed[0]/ NRMEVAL_SCL, pwnPassed[1]/ NRMEVAL_SCL, pwnPassed[0] / NRMEVAL_SCL - pwnPassed[1] / NRMEVAL_SCL) //@-
    EDUMP("|- PwnFork      %5d | %5d (=%5d)\n", pwnFork[0]/ NRMEVAL_SCL, pwnFork[1]/ NRMEVAL_SCL, pwnFork[0] / NRMEVAL_SCL - pwnFork[1] / NRMEVAL_SCL) //@-

    EDUMP("|- ThreatAtkSml %5d | %5d (=%5d)\n", threatAttWhich[0][0] / NRMEVAL_SCL, threatAttWhich[0][1] / NRMEVAL_SCL, threatAttWhich[0][0] / NRMEVAL_SCL - threatAttWhich[0][1] / NRMEVAL_SCL) //@-
    EDUMP("|- ThreatAtkMed %5d | %5d (=%5d)\n", threatAttWhich[1][0] / NRMEVAL_SCL, threatAttWhich[1][1] / NRMEVAL_SCL, threatAttWhich[1][0] / NRMEVAL_SCL - threatAttWhich[1][1] / NRMEVAL_SCL) //@-
    EDUMP("|- ThreatAtkBig %5d | %5d (=%5d)\n", threatAttWhich[2][0] / NRMEVAL_SCL, threatAttWhich[2][1] / NRMEVAL_SCL, threatAttWhich[2][0] / NRMEVAL_SCL - threatAttWhich[2][1] / NRMEVAL_SCL) //@-
    EDUMP("|- ThreatAtkHuge%5d | %5d (=%5d)\n", threatAttWhich[3][0] / NRMEVAL_SCL, threatAttWhich[3][1] / NRMEVAL_SCL, threatAttWhich[3][0] / NRMEVAL_SCL - threatAttWhich[3][1] / NRMEVAL_SCL) //@-
    EDUMP("|- ThreatAtt-All%5d | %5d (=%5d)\n", threatAtt[0] / NRMEVAL_SCL, threatAtt[1] / NRMEVAL_SCL, threatAtt[0] / NRMEVAL_SCL - threatAtt[1] / NRMEVAL_SCL) //@-

    EDUMP("|- MobilAll     %5d | %5d (=%5d)\n", mobAll[0] / NRMEVAL_SCL, mobAll[1] / NRMEVAL_SCL, mobAll[0] / NRMEVAL_SCL - mobAll[1] / NRMEVAL_SCL) //@-

    EDUMP("|- KngSavPwnProt%5d | %5d (=%5d)\n", kingPawnProt[0] / NRMEVAL_SCL, kingPawnProt[1] / NRMEVAL_SCL, kingPawnProt[0] / NRMEVAL_SCL - kingPawnProt[1] / NRMEVAL_SCL) //@-

    EDUMP("|-------------------------------------\n") //@-
    EDUMP("|- Base Score cp%5d | %5d (=%5d)\n", (score[0][0] + (score[0][1] - score[0][0]) * phase / 256) / NRMEVAL_SCL, (score[1][0] + (score[1][1] - score[1][0]) * phase / 256) / NRMEVAL_SCL, ((score[0][0] + (score[0][1] - score[0][0]) * phase / 256) - (score[1][0] + (score[1][1] - score[1][0]) * phase / 256)) / NRMEVAL_SCL) //@-

#ifdef TRAIN
    auto finalScore1 = (score[0][0] + (score[0][1] - score[0][0]) * phase / 256 -
                        score[1][0] - (score[1][1] - score[1][0]) * phase / 256) / NRMEVAL_SCL;
    auto finalScore2 = evaluateFast(tapWeights, tableWeights, detailDump, phase) / FSTEVAL_SCL;

    return finalScore1 + finalScore2;
#else
    return  (score[0][0] + (score[0][1] - score[0][0]) * phase / 256 -
             score[1][0] - (score[1][1] - score[1][0]) * phase / 256) / NRMEVAL_SCL +
               evaluateFast(phase) / FSTEVAL_SCL; // convert from .12 fixed to centipawn base-100
#endif

#if 0
    EDUMP("|- + kingDist                 (=%5d)\n", finalScore* (12 - (abs(kingsSquare[0] % 8 - kingsSquare[1] % 8) + abs(kingsSquare[0] / 8 - kingsSquare[1] / 8))) / 256) //@-

    // ********* using this makes the lzma compression size smaller ?!?!
    // in endgames for side that is better: smaller distance to enemy king counts!
    // Example: cp=300, dist=14; _cp=300*14/512=8(+300) or cp=-50, dist=12; _cp=-50*12/512=-1(-50)
    finalScore += finalScore * (12 - abs(kingsSquare[0] % 8 - kingsSquare[1] % 8) -
                                     abs(kingsSquare[0] / 8 - kingsSquare[1] / 8)) / 256;

    EDUMP("|-------------------------------------\n") //@-
    EDUMP("|- FinlScore cp                 %5d\n", finalScore) //@-
    EDUMP("|=====================================\n") //@-
#endif
}
