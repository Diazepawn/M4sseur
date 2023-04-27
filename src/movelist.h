
struct MoveList
{
    u16 cnt{}, entries[127];

    void _(Move m, bool c = 0)
    {
        assert(cnt < 125);

        if (c)
            entries[cnt++] = m | 0x4000,  // Queen Promo
            entries[cnt++] = m | 0x3000,  // Rook Promo
            entries[cnt++] = m | 0x1000;  // Knight Promo
        else
            entries[cnt++] = m;
    }

#ifdef DUMP
    void dump(bool flipped = false);
#endif
};
