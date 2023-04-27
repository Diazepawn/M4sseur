
Mask moveUp(Mask m) { return m << 8; }
Mask moveDown(Mask m) { return m >> 8; }
Mask moveRight(Mask m) { return m << 1 & 0xfefefefefefefefeull; }
Mask moveLeft(Mask m) { return m >> 1 & 0x7f7f7f7f7f7f7f7full; }
Mask moveUpRight(Mask m) { return moveUp(moveRight(m)); }
Mask moveUpLeft(Mask m) { return moveUp(moveLeft(m)); }
Mask moveDownRight(Mask m) { return moveDown(moveRight(m)); }
Mask moveDownLeft(Mask m) { return moveDown(moveLeft(m)); }


template<Piece p>
Mask iteratePieceMoves(Mask pieceSrcMask, Mask blockersOther, Mask blockersOwn = 0)
{
    Mask m = 0, i = ~(blockersOwn | blockersOther), t;

    if (!pieceSrcMask)
        return 0;
 
    IFC (p == KNIGHT)
        // https://www.chessprogramming.org/Knight_Pattern
        // for knights, InputIsMask can be true and more than one knight can be evaluated at once
        m = // clip 2 squares right for -10 +6
            (pieceSrcMask << 10 | pieceSrcMask >> 6) & 0xfcfcfcfcfcfcfcfcull |
            // clip 1 squares right for -17 +15
            (pieceSrcMask << 17 | pieceSrcMask >> 15) & 0xfefefefefefefefeull |
            // clip 1 squares left for -15 +17
            (pieceSrcMask << 15 | pieceSrcMask >> 17) & 0x7f7f7f7f7f7f7f7full |
            // clip 2 squares left for -6 +10
            (pieceSrcMask << 6 | pieceSrcMask >> 10) & 0x3f3f3f3f3f3f3f3full;
    IFC (p == KING)
        m = moveUp(pieceSrcMask) | moveUpRight(pieceSrcMask) | moveRight(pieceSrcMask) | moveDownRight(pieceSrcMask) |
            moveDown(pieceSrcMask) | moveDownLeft(pieceSrcMask) | moveLeft(pieceSrcMask) | moveUpLeft(pieceSrcMask);
    IFC (p == BISHOP || p == QUEEN)
    {
      #define RAY(func) t = func(pieceSrcMask); t |= func(t&i); t |= func(t&i); t |= func(t&i); t |= func(t&i); t |= func(t&i); t |= func(t&i); m |= t;
        RAY(moveUpRight) RAY(moveDownRight) RAY(moveDownLeft) RAY(moveUpLeft)
    }
    IFC (p == ROOK || p == QUEEN)
    {
        RAY(moveUp) RAY(moveRight) RAY(moveDown) RAY(moveLeft)
    }

    return m & ~blockersOwn;
}
