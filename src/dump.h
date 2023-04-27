#ifdef DUMP

void dumpDataAsCode(void* vptr, int size)
{
    assert((size & 7) == 0);

    printf("constexpr u64 xxx[%d] = {\n", size / 8);
    u64* uptr = (u64*)vptr;
    while (size > 0)
    {
        for (int i = 0; i < 8; i++)
        {
            u64 data = *uptr++;
            printf("0x%llxull, ", (long long)data);

            size -= 8;
            if (size <= 0)
                break;
        }
        printf("\n");
    }
    printf("};\n");
}

void dumpBitboard(Mask bitboard)
{
    printf("------- Dumping bitboard:  -----\n");
    using llu = long long unsigned int;
    printf("Value:   %016llx\n", (llu)bitboard);

    char strBuf[1024];
    for (int i = 7; i >= 0; i--)
    {
        char* s = strBuf;
        printf(" %d ", i + 1);
        for (int j = 0; j < 8; j++)
        {
            *s++ = ' ';
            Rank actualRank = (Rank)i;
            *s++ = (mkmsk(j | actualRank << 3u) & bitboard) ? 'X' : '.';
        }
        *s++ = 0;
        printf("%s\n", strBuf);
    }
    printf(" ------------------\n");
    printf("    A B C D E F G H\n\n");
}

void MoveList::dump(bool flipped)
{
    printf("Legal moves: %d .....\n", cnt);
    for (int i = 0; i < cnt; i++)
    {
        printf("%s  ", convertMoveToText(alignMove(entries[i], flipped)).data());
        if ((i % 10) == 9)
            printf("\n");
    }
    printf("\n");
}

Piece Position::_getPieceSymbol(File j, Rank i)
{
    Square square = mksq(j, i);
    Mask mask = mkmsk(square);

    if (square == kingsSquare[0])
        return KING;
    else if (square == kingsSquare[1])
        return KING + 0x8000;

    if ((colorsMask[0] & mask) == 0 && (colorsMask[1] & mask) == 0)
    {
        assert((pieceMask[PAWN] & mask) == 0);
        assert((pieceMask[KNIGHT] & mask) == 0);
        assert((pieceMask[BISHOP] & mask) == 0);
        assert((pieceMask[ROOK] & mask) == 0);
        assert((pieceMask[QUEEN] & mask) == 0);

        return NONE;
    }

    for (int i = 0; i < KING; i++)
    {
        if ((pieceMask[i] & mask) != 0)
        {
            if ((colorsMask[0] & mask) != 0)
                return i;
            else if ((colorsMask[1] & mask) != 0)
                return 0x8000 | i;
            else
                return 0x8000 | NONE;
        }
    }

    return NONE;
}

void Position::dump(int fullmoveNumber) const
{
    (const_cast<Position*>(this))->_dump(fullmoveNumber); // don't do this at home, kids
}

void Position::_dump(int fullmoveNumber)
{
#ifndef NDEBUG
    Position posOrig = *this;
#endif

    printf("********************************\n");
    printf("** Dumping current position:  **\n");
    printf("********************************\n");

    printf("FEN: %s\n", generateFEN(false, fullmoveNumber).data());

    bool toggleBack = false;
    if (flipped)
    {
        toggleBack = true;
        flipSides();
    }

    using llu = long long unsigned int;
    printf("White:   %016llx Black:    %016llx\n", (llu)colorsMask[0], (llu)colorsMask[1]);
    printf("Pawns:   %016llx Knights:  %016llx\n", (llu)pieceMask[PAWN], (llu)pieceMask[KNIGHT]);
    printf("Bishops: %016llx Rooks:    %016llx\n", (llu)pieceMask[BISHOP], (llu)pieceMask[ROOK]);
    Rank epF = enPassant ? (enPassant & 7u) : 0u;
    Rank epR = enPassant ? (enPassant >> 3) : 0u;
    printf("Queens:  %016llx Kings: %x %x EP: %c%c\n", (llu)pieceMask[QUEEN], (u32)kingsSquare[0], (u32)kingsSquare[1], (epR == 0)? '-' : ('a' + epF), (epR == 0) ? ' ' : ('1' + epR));
    printf("Castling: %x %x Flags: %04x Turn: %s\n\n", (u32)castling[0], (u32)castling[1], (u32)flags, toggleBack ?"Black":"White");

    char strBuf[1024];
    for (int i = 7; i >= 0; i--)
    {
        char* s = strBuf;
        printf(" %d ", i + 1);
        for (int j = 0; j < 8; j++)
        {
            *s++ = ' ';
            int p = _getPieceSymbol((File)j, (Rank)i);
            bool black = p & 0x8000;
            p &= 0x7fff;
            switch (p)
            {
            case KING: *s++ = black ? 'k' : 'K'; break;
            case PAWN: *s++ = black ? 'p' : 'P'; break;
            case KNIGHT: *s++ = black ? 'n' : 'N'; break;
            case BISHOP: *s++ = black ? 'b' : 'B'; break;
            case ROOK: *s++ = black ? 'r' : 'R'; break;
            case QUEEN: *s++ = black ? 'q' : 'Q'; break;
            case NONE: *s++ = black ? 'X' : '.'; break; // X = error
            }
        }
        *s++ = 0;
        printf("%s\n", strBuf);
    }
    printf(" ------------------\n");
    //        printf("    A B C D E F G H\n\n");
    printf("    A B C D E F G H\n\n");

    if (toggleBack)
        flipSides();

#ifndef NDEBUG
    assert(!(posOrig != *this));
#endif
}


#else
#endif
