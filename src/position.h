
enum /* @Flags */ : u16
{
    FLAGS_NONE = 0,
    FLAGS_CAPTURE
};
using Flags = u16;


struct Position
{
    Flags flags;
    u8 flipped, enPassant, kingsSquare[2], castling[2]; // castling -> 1st bit = long, 2nd = short
    Mask colorsMask[2], pieceMask[5];

    auto operator<=>(Position const&) const = default;   //@-

    void setStartpos()
    {
#if 0
        pieceMask[PAWN] = mkmsk(A2) | mkmsk(B2) | mkmsk(C2) | mkmsk(D2) | mkmsk(E2) | mkmsk(F2) | mkmsk(G2) | mkmsk(H2) |
                          mkmsk(A7) | mkmsk(B7) | mkmsk(C7) | mkmsk(D7) | mkmsk(E7) | mkmsk(F7) | mkmsk(G7) | mkmsk(H7);
        pieceMask[KNIGHT] = mkmsk(B1) | mkmsk(G1) | mkmsk(B8) | mkmsk(G8);
        pieceMask[BISHOP] = mkmsk(C1) | mkmsk(F1) | mkmsk(C8) | mkmsk(F8);
        pieceMask[ROOK] = mkmsk(A1) | mkmsk(H1) | mkmsk(A8) | mkmsk(H8);
        pieceMask[QUEEN] = mkmsk(D1) | mkmsk(D8);

        colorsMask[0] = 0x000000000000ffffull;
        colorsMask[1] = 0xffff000000000000ull;

        kingsSquare[0] = E1;
        kingsSquare[1] = E8;

        castling[0] = 3;
        castling[1] = 3;

        enPassant = 0;
        flipped = 0;
        flags = FLAGS_NONE;

        dumpDataAsCode(this, sizeof(Position));
#else
        auto* p=(u64*)this;
        *p++= 0x3033c0400000000ull;
        *p++=            0xffffull;
        *p++=0xffff000000000000ull;
        *p++=  0xff00000000ff00ull;
        *p++=0x4200000000000042ull;
        *p++=0x2400000000000024ull;
        *p++=0x8100000000000081ull;
        *p++= 0x800000000000008ull;
#endif
    }

    Mask _c(Color c = WHITE) const
    {
        assert(c < NO_COLOR);

        return c == WHITE_BLACK? colorsMask[0] | colorsMask[1] : colorsMask[c];
    }

    Mask _(Piece p = NONE, Color c = WHITE) const
    {
        assert(p < NONE);

        return p == KING? mkmsk(kingsSquare[c]) : pieceMask[p] & _c(c);
    }

    auto getPhase() const
    {
        // determine game phase

        // (!!) Never change this; train.h as well as some Python scripts contain these values
        const int PhaseIncs[] = { 0, 13, 15, 24, 38 }; // 2*13+2*15+2*24+38 = 142*2 = 284

        auto phase = 0;
        iterate<PMASK_NBRQ>([&]<Piece p, Color c>(Square s)
        {
            phase += PhaseIncs[p];
        });
        return min(256, 284 - phase); // in rare situations, phase can be negative; in that case, just use the value and extrapolate
    }

    Piece getPiece(Mask m) const
    {
        for (auto i = 0; i < KING; i++)
            if (pieceMask[i] & m)
                return i;
        return NONE;
    }
    
    void makeMove(Move move)
    {
        Square ep = enPassant, posSrc = (move >> 0) & 0x3f, posDest = (move >> 6) & 0x3f;
        Mask maskSrc = mkmsk(posSrc), maskDest = mkmsk(posDest);
        flags = !!(maskDest & _c(BLACK));

#ifndef NDEBUG
        File fromFile = posSrc & 7u;
        Rank fromRank = posSrc >> 3;
        File toFile = posDest & 7u;
        Rank toRank = posDest >> 3;

        assert(move != 0);
        assert(posSrc != posDest);  // src and dest square must not be the same

        assert((colorsMask[0] & maskSrc) != 0); // There must be a piece on the src square

        assert(posDest != kingsSquare[0]); // Never ever capture the king
        assert(posDest != kingsSquare[1]);

        assert((colorsMask[0] & maskDest) == 0); // No OWN piece on dest square allowed

        assert((mkmsk(kingsSquare[0]) & colorsMask[0]) != 0);  // white king bit correctly set?
        assert((mkmsk(kingsSquare[1]) & colorsMask[1]) != 0);  // black king bit correctly set?
#endif
 
        Piece pieceDest = getPiece(maskDest);

        colorsMask[WHITE] ^= maskSrc | maskDest;
        enPassant = 0;

        if (posSrc != kingsSquare[WHITE]) /* LIKELY */
        {
            Piece pieceSrc = getPiece(maskSrc);
            pieceMask[pieceSrc] ^= maskSrc | maskDest;

            // check en passant capture
            if (ep == posDest && pieceSrc == PAWN)
                // remove pawn on 5th/4th rank
                pieceMask[PAWN] ^= maskDest >> 8,
                colorsMask[BLACK] ^= maskDest >> 8,
                flags = FLAGS_CAPTURE;
            else if (flags)
                pieceMask[pieceDest] ^= maskDest,
                colorsMask[BLACK] ^= maskDest;

            if (move >> 12) // promo
                pieceMask[PAWN] ^= maskDest, // bit for pawn on 8th rank is set, revert it
                pieceMask[move >> 12] ^= maskDest;

            if (ep = posDest & 7 | 16; posDest - posSrc == 16 && pieceSrc == PAWN)
                // double pawn move - en passant capture possible next move
                // but *ONLY* if there is another pawn that can "en passant"-capture this pawn
                if (_(PAWN, BLACK) & (mkmsk(ep + 7) | mkmsk(ep + 9)))
                    enPassant = ep ^ 56; // store the flipped ep square and just don't flip ep in flipSides()
        }
        else
        {
            // king move or castle
            castling[WHITE] = 0;
            kingsSquare[WHITE] = posDest;

            if (flags)
                pieceMask[pieceDest] ^= maskDest,
                colorsMask[BLACK] ^= maskDest;
            else if (move == 388u)  // castle short: mkmove(E1, G1) == 388u
                // short castle
                pieceMask[ROOK] ^= 0xa0,
                colorsMask[WHITE] ^= 0xa0;
            else if (move == 132u) // castle long: mkmove(E1, C1) == 132u
                // long castle
                pieceMask[ROOK] ^= 9,
                colorsMask[WHITE] ^= 9;
        }

        // last not least - if any rook moves from initial square, clear castling permission
        castling[0] &= ~(  ((maskSrc | maskDest) >>  0ull & 1) |
                           ((maskSrc | maskDest) >>  6ull & 2)  );
        castling[1] &= ~(  ((maskSrc | maskDest) >> 56ull & 1) |
                           ((maskSrc | maskDest) >> 62ull & 2)  );

#ifndef NDEBUG
        assert((mkmsk(kingsSquare[0]) & colorsMask[0]) != 0);  // white king bit correctly set?
        assert((mkmsk(kingsSquare[1]) & colorsMask[1]) != 0);  // black king bit correctly set?

        assert((maskSrc & (colorsMask[0] | colorsMask[1])) == 0);  // after moving, src square must be empty
        assert((maskDest & (colorsMask[0] | colorsMask[1])) != 0);  // after moving, dest square must be set

        assert(!((colorsMask[0] & maskSrc) != 0 && (colorsMask[1] & maskSrc) != 0));  // are there both a white and a black piece on this square?
        assert(!((colorsMask[0] & maskDest) != 0 && (colorsMask[1] & maskDest) != 0));  // are there both a white and a black piece on this square?

        int numFigSets[2] = { 0, 0 };
        for (int i = 0; i < 5; i++)
        {
            if ((pieceMask[i] & maskSrc) != 0)
                numFigSets[0]++;
            if ((pieceMask[i] & maskDest) != 0)
                numFigSets[1]++;
        }
        assert(numFigSets[0] <= 1); // more than one piece type on this square?
        assert(numFigSets[1] <= 1);

        Mask msks[2] = { maskSrc, maskDest };
        for (int i = 0; i < 2; i++)
        {
            if (msks[i] == mkmsk(kingsSquare[0]))
            {
                assert((colorsMask[0] & msks[i]) != 0); // if a white king is on this square, the particular bit in white's color mask must be set
                assert((colorsMask[1] & msks[i]) == 0); // ...and black's mask bit has to be 0
                assert(numFigSets[i] == 0);                // and no bit in the pieces masks shall be set
            }
            else if (msks[i] == mkmsk(kingsSquare[1]))
            {
                assert((colorsMask[1] & msks[i]) != 0); // (see above, other way round)
                assert((colorsMask[0] & msks[i]) == 0);
                assert(numFigSets[i] == 0);
            }
            else if (numFigSets[i] == 0)
            {
                // if there is no piece type defined for this square, both white and black color mask must be 0
                assert((colorsMask[0] & msks[i]) == 0 && (colorsMask[1] & msks[i]) == 0);
            }
            else /* (numFigSets == 1) */
            {
                // if there is a piece type defined for this square, either white or black color mask must be set, but not both at the same time
                assert((((colorsMask[0] & msks[i]) == 0) && ((colorsMask[1] & msks[i]) != 0)) ||
                       (((colorsMask[0] & msks[i]) != 0) && ((colorsMask[1] & msks[i]) == 0)));
            }
        }
#endif
        // flip sides
        for (auto i = 0; i < 2; i++)
        {
            colorsMask[i] = qflip(colorsMask[i]);
            kingsSquare[i] ^= 56;
        }

        for (auto i = 0; i < KING; i++)
            pieceMask[i] = qflip(pieceMask[i]);

        swap(colorsMask[0], colorsMask[1]);
        swap(kingsSquare[0], kingsSquare[1]);
        swap(castling[0], castling[1]);

        flipped = !flipped;
    }

    void flipSides()                                    //@---
    {
        for (auto i = 0; i < 2; i++)
        {
            colorsMask[i] = qflip(colorsMask[i]);
            kingsSquare[i] ^= 56;
        }

        for (auto i = 0; i < KING; i++)
            pieceMask[i] = qflip(pieceMask[i]);

        swap(colorsMask[0], colorsMask[1]);
        swap(kingsSquare[0], kingsSquare[1]);
        swap(castling[0], castling[1]);

        flipped = !flipped;
    }                                                   //---@

    template<PieceMask t = PMASK_ALL, Color c = WHITE_BLACK>
    void iterate(auto&& func) const
    {
        #define V(p, t) for(Mask m = _(p, t); m;) func.template operator()<p, t>(getNextBit(m));
        #define W(p)  IFC (c + 1 & 1 && t & 1 << p) V(p, 0) IFC (c + 1 & 2 && t & 1 << p) V(p, 1)

        W(PAWN) W(KNIGHT) W(BISHOP) W(ROOK) W(QUEEN) W(KING)
    }

    // ----- Methods used for move generation
    Mask getAttackMap(Color c = BLACK) const  // TODO: check which compress better Color/auto  and =BLACK
    {
        // bitboard of squares that are attacked
        Mask m = c ? moveDownRight(_(PAWN, c)) | moveDownLeft(_(PAWN, c)) :
                     moveUpRight(_(PAWN, c))   | moveUpLeft(_(PAWN, c));
        m |= iteratePieceMoves<KING>(_(KING, c), 0);
        m |= iteratePieceMoves<KNIGHT>(_(KNIGHT, c), ~0ull);
        m |= iteratePieceMoves<BISHOP>(_(BISHOP, c), _c(WHITE_BLACK));
        m |= iteratePieceMoves<ROOK>(_(ROOK, c), _c(WHITE_BLACK));
        m |= iteratePieceMoves<QUEEN>(_(QUEEN, c), _c(WHITE_BLACK));

        return m;
    }

    Mask getPinnedMask() const
    {
        // bitboard of squares that are pinned to the king and can't be moved
        const Mask straightDiagMask[2] = { _(ROOK, BLACK)   | _(QUEEN, BLACK),
                                           _(BISHOP, BLACK) | _(QUEEN, BLACK) };

        Mask o = enPassant ? _(PAWN) & (moveDownLeft(mkmsk(enPassant)) | moveDownRight(mkmsk(enPassant))) : 0;
        auto file = kingsSquare[WHITE] & 7, rank = kingsSquare[WHITE] >> 3;

        int itersL = file - 1, itersR = 6 - file, itersD = rank - 1, itersU = 6 - rank;

        const int numIters[] = { itersU, itersD, itersR, itersL, min(itersU, itersR), min(itersU, itersL), min(itersD, itersR), min(itersD, itersL) };
        // directionalIncrements  U    D    R    L   UR   UL   DR   DL
        const int t[] =         { 8,  -8,   1,  -1,   9,   7,  -7,  -9 };

        for (auto dir = 0; dir < 8; dir++)
        {
            auto s = kingsSquare[WHITE] + t[dir];
            for (auto i = 0; i < numIters[dir]; i++, s += t[dir])
            {
                assert(s >= 0 && s < 64);
                auto m = mkmsk(s);
                if (m & _c(BLACK))
                    break;

                if (m & _c())
                {
                    for (s += t[dir]; i < numIters[dir]; i++, s += t[dir])
                    {
                        assert(s >= 0 && s < 64);
                        if (mkmsk(s) & straightDiagMask[dir / 4])
                        {
                            o |= m;
                            break;
                        }
                        if (mkmsk(s) & _c(WHITE_BLACK))
                            break;
                    }
                    break;
                }
            }
        }

        return o;
    }

    auto generateLegalMoves(MoveList& moves) const
    {
        Mask attackMap = getAttackMap(), pinnedMap = getPinnedMask();
        bool inCheck = _(KING) & attackMap;

        iterate<PMASK_ALL, WHITE>([&]<Piece p, Color c>(Square s)
        {
            IFC (p /*p != PAWN*/)
            {
                Mask m = iteratePieceMoves<p>(mkmsk(s), _c(BLACK), _c() | (p == KING ? attackMap : 0));
                while (m)
                    moves._(mkmove(s, getNextBit(m)));
            }
            else
            {
                // straight pawn move
                if (moveUp(mkmsk(s)) & ~_c(WHITE_BLACK))
                {
                    moves._(mkmove(s, s + 8), s / 8 == 6);
                    if (s / 8 == 1 && moveUp(mkmsk(s + 8)) & ~_c(WHITE_BLACK))
                        moves._(mkmove(s, s + 16));
                }
                // captures
                if (moveUpLeft(mkmsk(s)) & (_c(BLACK) | mkmsk(enPassant)))
                    moves._(mkmove(s, s + 7), s / 8 == 6);
                if (moveUpRight(mkmsk(s)) & (_c(BLACK) | mkmsk(enPassant)))
                    moves._(mkmove(s, s + 9), s / 8 == 6);
            }
        });

        // castling
        if (castling[0] & 1 && !(inCheck || (attackMap | _c(WHITE_BLACK)) & 12 || _c(WHITE_BLACK) & 2))
            moves._(132); // mkmove(E1, C1) == 132u
        if (castling[0] & 2 && !(inCheck || (attackMap | _c(WHITE_BLACK)) & 96))
            moves._(388); // mkmove(E1, G1) == 388u

        // --- MAKE PSEUDO LEGAL MOVES LEGAL
        if (inCheck || pinnedMap)
        {
            auto c = 0;
            for (auto i = 0; i < moves.cnt; i++)
            {
                // check only moves where pieces moving from a pinned square
                if (mkmsk(moves.entries[i] & 63) & pinnedMap || inCheck)
                {
                    Position t = *this;
                    t.makeMove(moves.entries[i]);
                    if (t._(KING, BLACK) & t.getAttackMap(WHITE))
                        continue;
                }
#ifdef _DEBUG
                Position t = *this;
                t.makeMove(moves.entries[i]);
                assert(!(t._(KING, BLACK) & t.getAttackMap(WHITE)));
#endif

                moves.entries[c++] = moves.entries[i];
            }
            //////////////
            moves.cnt = c;
            //////////////
        }
#ifdef _DEBUG
        else
        {
            for (auto i = 0; i < moves.cnt; i++)
            {
                Position t = *this;
                t.makeMove(moves.entries[i]);
                assert(!(t._(KING, BLACK) & t.getAttackMap(WHITE)));
            }
        }
#endif
        return inCheck;
    }

    // ----- Evaluation method, see eval.h for implementation
#ifdef TRAIN
    int evaluate(const TuningTapWeights&, const TuningTableWeights&, bool) const;
    int evaluate() const { return 0; }
    auto evaluateFast(int phase) const { return 0; }
#else
    int evaluate() const;
#endif

#ifdef TRAIN
    auto evaluateFast(const TuningTapWeights& tapWeights, const TuningTableWeights& tableWeights, bool detailDump, int phase) const
    {
        Weight score[2][2]{};
        iterate([&]<Piece p, Color c>(Square s)
        {
            score[c][0] += (rawTableWeights[p * 128 + (s ^ 56 * c) + 0] - RAWTABLE_MID + tapWeights.values[p * 2 + 0]) * RAWTABLE_SCALE;
            score[c][1] += (rawTableWeights[p * 128 + (s ^ 56 * c) + 64] - RAWTABLE_MID + tapWeights.values[p * 2 + 1]) * RAWTABLE_SCALE;
        });
        auto ret = score[0][0] + (score[0][1] - score[0][0]) * phase / 256 -
                   score[1][0] - (score[1][1] - score[1][0]) * phase / 256;
        if (detailDump)
            printf("|- + evlFst cp  %5d | %5d (=%5d)\n",
            (score[0][0] + (score[0][1] - score[0][0]) * phase / 256) / 40,
            (score[1][0] + (score[1][1] - score[1][0]) * phase / 256) / 40, ret / 44);

        return ret;
    }
#else
    auto evaluateFast(int phase) const
    {
        Weight score[2][2]{};
        iterate([&]<Piece p, Color c>(Square s)
        {
#ifdef USE_CONSTEXPR_TABLES
            score[c][0] += (rawTableWeights[p * 128 + (s ^ 56 * c)] - RAWTABLE_MID + iTapWeights.entries[p * 2]) * RAWTABLE_SCALE;
            score[c][1] += (rawTableWeights[p * 128 + (s ^ 56 * c) + 64] - RAWTABLE_MID + iTapWeights.entries[p * 2 + 1]) * RAWTABLE_SCALE;
#else
            score[c][0] += (rawTableWeights[p * 128 + (s ^ 56 * c)] - RAWTABLE_MID + iTapWeights[p * 2]) * RAWTABLE_SCALE;
            score[c][1] += (rawTableWeights[p * 128 + (s ^ 56 * c) + 64] - RAWTABLE_MID + iTapWeights[p * 2 + 1]) * RAWTABLE_SCALE;
#endif
        });

        // convert from .12 fixed to centipawn base-100
        return score[0][0] + (score[0][1] - score[0][0]) * phase / 256 -
               score[1][0] - (score[1][1] - score[1][0]) * phase / 256;
    }
#endif

    // ----- Convert FEN string into Position and vice versa, see fen.h for implementation
    ptrdiff_t initFromFEN(const string_view&, u16* pliesTo50MRPtr = nullptr, u16* fullmoveNumPtr = nullptr);        //@-
    string generateFEN(bool isFlipped = false, int fullmoveNumber = 1);                                             //@-

    // ----- Debugging dump's, see dump.h for implementation
#ifdef DUMP
    void dump(int fullmoveNumber = 1) const;
    void _dump(int fullmoveNumber = 1);
    Piece _getPieceSymbol(File j, Rank i);
#endif
};
static_assert(sizeof(Position) == 64);

//////////////////////
Position rootPosition;
//////////////////////
