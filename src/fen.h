
ptrdiff_t Position::initFromFEN(const string_view& strView, u16* pliesTo50MRPtr, u16* fullmoveNumPtr)    //@---
{
    // set empty board
    memset(this, 0, sizeof(Position));

    // init iterators/counters
    Rank rank = 7;
    File file = 0;
    auto it = strView.begin();

    // get pieces on board
    for (; it != strView.end() && *it != ' '; it++)
    {
        auto ch = *it;

        assert(rank >= 0 && rank < 8);
        assert(ch != '\n');

        if (ch == '/')
        {
            rank--;
            file = 0;
        }
        else if (std::isdigit(ch))
        {
            file += ch - '0';
        }
        else
        {
            assert(file >= 0 && file < 8);

            int color = std::isupper(ch) ? WHITE : BLACK;
            Mask mask = mkmsk(mksq(file, rank));
            Piece piece = PAWN;

            switch (ch)
            {
                case 'p': case 'P': { piece = PAWN; break; }
                case 'n': case 'N': { piece = KNIGHT; break; }
                case 'b': case 'B': { piece = BISHOP; break; }
                case 'r': case 'R': { piece = ROOK; break; }
                case 'q': case 'Q': { piece = QUEEN; break; }
                case 'k': case 'K': { kingsSquare[color] = mksq(file, rank); colorsMask[color] |= mask; mask = 0;  break; }
                default: { assert(false); break; }
            }

            colorsMask[color] |= mask;
            pieceMask[piece] |= mask;
            file++;
        }
    }

    // skip whitespaces
    for (; it != strView.end() && *it == ' '; it++) {}
    assert(it != strView.end());

    // which turn is it?
    bool isBlacksTurn = *it++ == 'b';

    // skip whitespaces
    for (; it != strView.end() && *it == ' '; it++) {}

    // get castling privileges
    for (; it != strView.end() && *it != ' '; it++)
    {
        auto ch = *it;
        assert(ch != '\n');

        switch (ch)
        {
            case 'k': { castling[BLACK] |= 2; break; }
            case 'K': { castling[WHITE] |= 2; break; }
            case 'q': { castling[BLACK] |= 1; break; }
            case 'Q': { castling[WHITE] |= 1; break; }
        }
    }

    // skip whitespaces
    for (; it != strView.end() && *it == ' '; it++) {}
    assert(it != strView.end());

    // get en passant square, if present
    if (it[0] != '-')
    {
        enPassant = mksq(it[0] - 'a', it[1] - '1') ^ (56 * isBlacksTurn);
        it++;
        it++;
    }
    else
        it++;

    // flip board if it's black's turn
    if (isBlacksTurn)
        flipSides();

    // skip whitespaces
    for (; it != strView.end() && *it == ' '; it++) {}

    if (it == strView.end() || !isdigit(*it))
        return it - strView.begin();

    // get/skip 50-move rule ply counter
    if (int pliesTo50MR = strtol(&it[0], nullptr, 10); pliesTo50MRPtr != nullptr)
        *pliesTo50MRPtr = pliesTo50MR;

    // skip numbers
    for (; it != strView.end() && (*it >= '0' && *it <= '9'); it++) {}
    // skip whitespaces
    for (; it != strView.end() && *it == ' '; it++) {}

    if (it == strView.end() || !isdigit(*it))
        return it - strView.begin();

    // get/skip "fullmove number"
    if (int fullmoveNum = strtol(&it[0], nullptr, 10); fullmoveNumPtr != nullptr)
        *fullmoveNumPtr = fullmoveNum;

    // skip numbers
    for (; it != strView.end() && (*it >= '0' && *it <= '9'); it++) {}
    // skip whitespaces
    for (; it != strView.end() && *it == ' '; it++) {}

    return it - strView.begin();
}


string Position::generateFEN(bool isFlipped, int fullmoveNumber)
{
    string fen;
    fen.reserve(64);

    bool toggleBack = false;
    if (flipped)
    {
        toggleBack = true;
        flipSides();
    }

    for (int rank = 7; rank >= 0; rank--)
    {
        int emptySqCnt = 0;
        for (File file = 0; file < 8; file++)
        {
            Square sq = mksq(file, rank);
            Piece piece = getPiece(mkmsk(sq));
            if (kingsSquare[WHITE] == sq || kingsSquare[BLACK] == sq)
                piece = KING;

            if (piece == NONE)
                emptySqCnt++;
            else
            {
                if (emptySqCnt > 0)
                {
                    fen += '0' + emptySqCnt;
                    emptySqCnt = 0;
                }

                fen += "pnbrqkPNBRQK"[piece + !!(mkmsk(sq) & _c(WHITE)) * 6];
            }
        }
        if (emptySqCnt > 0)
        {
            fen += '0' + emptySqCnt;
            emptySqCnt = 0;
        }
        if (rank != 0)
            fen += '/';
    }

    fen += (toggleBack || isFlipped)? " b " : " w ";

    if (!castling[WHITE] && !castling[BLACK])
        fen += "-";
    else
    {
        if (castling[WHITE] & 2) fen += 'K';
        if (castling[WHITE] & 1) fen += 'Q';
        if (castling[BLACK] & 2) fen += 'k';
        if (castling[BLACK] & 1) fen += 'q';
    }
    fen += ' ';

    if (!enPassant)
        fen += '-';
    else
    {
        fen += 'a' + (enPassant & 0x7);
        fen += '1' + (enPassant >> 3);
    }
    fen += " 0 ";

    fen += to_string(fullmoveNumber);

    if (toggleBack)
        flipSides();

    return fen;
}                                                                                                       //---@
