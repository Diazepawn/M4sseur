
//#define USE_CONSTEXPR_TABLES

#define RAWTABLE_MID 42
#define RAWTABLE_SCALE 512

using Weight = int; // 1.19.12 fixed point (1.0 = 4096)

#ifdef TRAIN // use high precision conversations in training mode
  int fixed(flt v) { return int(v * 4096. + (.5 - (v < 0))); }
  int fixMul(Weight w, auto mul) { return int((w * mul) / (is_same_v<decltype(mul), flt> /*SAME_TYPE(mul, flt)*/ ? 1 : 4096)); }
#else
  int fixed(flt v) { return int(v * 4096.); }                       //@-
  int fixMul(Weight w, auto mul) { return int((w * mul) / 4096); }  //@-
#endif

enum /* @TapWeightName */ : int
{
    // ---- Weights for evaluation params; internally, we are using 1.19.12 fixed point (1.0 = 4096)
    TAP_EVAL_PAWN = 0, TAP_EVAL_KNIGHT, TAP_EVAL_BISHOP, TAP_EVAL_ROOK, TAP_EVAL_QUEEN, TAP_EVAL_KING,
    TAP_THREATATTK_NONE1, TAP_THREATATTK_NONE2,
    TAP_THREATATTK_SMALL, TAP_THREATATTK_MED, TAP_THREATATTK_BIG, TAP_THREATATTK_HUGE,
    TAP_BISHPAIR_BONUS,
    TAP_PAWN_SUPPORTS_ANY, TAP_PAWN_DOUBLE_PEN, TAP_PAWN_FORK, TAP_PAWN_PASSED,
    TAP_MOB_MOVESALL, TAP_KING_SAFETY_PAWNPROT,

    // ----------------------------------------------------------------------------------------------------
    TAP_WEIGHTS_LAST,
    TAP_WEIGHTS_SIZE = 38 /*TAP_WEIGHTS_LAST * 2*/
};
using TapWeightName = int;
static_assert(TAP_WEIGHTS_SIZE == TAP_WEIGHTS_LAST * 2);

/* HOLY!
const char rawTableWeights[] = "********'*((),-((*****-)(**+,*+()+*,-++(*+,,/.,(24/2/4-)****************+++++***********++*))***-,+**+++221/..1144444444********&(&'(((((&)**+)(()++++,))+++,,,))+,.-0+,&/-/140.&'0-,/+)&&&&/&)&(&()))&&&()**((&(**++*(((*+++++))*,,,++(((++*)(((((*)((&&')(((&&'*))))'(++++++-**++++++++++,-++***,.--+*)------*(+)),/+&(*&'''+)))()))))))***))))*+++*)))*++++***+++++*************)**))))))))((()*+++'(&))****&&())***''()*+)*(()+,,-)(*,,-+./+,-////,----./+,-*******(****)))*****))))*****)))********++******++++****++++++++*))+)((&')++++**)))*****))))****((((****))++,...('**)/,.(*,+....((('*((((((((('')(++++++),,.---,*,,./-/-(*+..-,+),--/,,*),,,,,,,)-+&+(,+++)&')++))(&&(((&*('&&&&)))((()'),*)(*,(,*))))'(&,+)&'*+&(((((((()++++*)(*+,,,+))),,,,+)),,,,,,*+++++..+)++++-,+&')))+*)";
*/

// PANIC FIXES...
// '+'=41 '*'=42  '+'=43  ','=44  '-'=45
//                                                      abcdefgh
const char rawTableWeights[] = "********'*((),-((*****,)(**+,*+()+*,-++(*+,,/.,(24/2/4-)****************+++++***********++*))***-,+**+++221/..1144444444********&(&'(((((&)**+)(()++++,))+++,,,))+,.-0+,&/-/140.&'0-,/+)&&&&/&)&(&()))&&&()**((&(**++*(((*+++++))*,,,++(((++*)(((((*)((&&')(((&&'*))))'(++++++-**++++++++++,-++***,.--+*)------*(+)),/+&(*&'''+)))()))))))***))))*+++*)))*++++***+++++*************)**))))))))((()*+++'(&))****&&())***''()*+)*(()+,,-)(*,,-+./+,-////,----./+,-*******(****)))*****))))*****)))********++******++++****++++++++*))+)((&')++++**)))*****))))****((((****))++,...('**)/,.(*,+....((('*((((((((('')(++++++),,.---,*,,./-/-(*+..-,+),--/,,*),,,,,,,)-+&+(,+++)&')++))(&&(((&*('&&&&)))((()'),*)(*,(,*))))'(&,+)&'*+&(((((((()++++*)(*+,,,+))),,,,+)),,,,,,*+++++..+)++++-,+&')))+*)";
//                              |       |       |       |       |       |       |       |
//                              1bcdefgh2       3       4       5       6       7       8

enum /* @TapTableName */ : int
{
    // ---- Weights for tables
    TAP_TABLE_PAWN = 0, TAP_TABLE_KNIGHT, TAP_TABLE_BISHOP, TAP_TABLE_ROOK, TAP_TABLE_QUEEN, TAP_TABLE_KING,
    // -----------------------------------------------------------------------------------------------------
    TAP_TABLE_SIZE,
    TAP_COMPRESSED_TABLE_SIZE = 768 /*TAP_TABLE_LAST * (64) * 2*/
};
using TapTableName = int;

#ifdef USE_CONSTEXPR_TABLES

// ---- Note that this array is only used to generate the fixed point version (iTapWeights)
// ---- Order in this array is: W_MIDDLEGAME1, W_ENDGAME1,   W_MIDDLEGAME2, ...
// -----------------------------------------------------------------------------------------------------
constexpr flt fTapWeights[] = {   /* MIDDLEGAME, ENDGAME, ... */
    /*   TAP_EVAL_PAWN                             TAP_EVAL_KNIGHT                          TAP_EVAL_BISHOP                         TAP_EVAL_ROOK                             TAP_EVAL_QUEEN                           TAP_EVAL_KING  */
         .76/RAWTABLE_SCALE, 1.04/RAWTABLE_SCALE,   3.2/RAWTABLE_SCALE, 2.7/RAWTABLE_SCALE, 3.4/RAWTABLE_SCALE, 3./RAWTABLE_SCALE,  4.8/RAWTABLE_SCALE, 5.2/RAWTABLE_SCALE,   9.6/RAWTABLE_SCALE, 9.1/RAWTABLE_SCALE,  .0, .0,
    /*  TAP_THREATATTK_NONE1    TAP_THREATATTK_NONE2                                                  */
        0.0, 0.0,               0.0, 0.0,
    /*  TAP_THREATATTK_SMALL    TAP_THREATATTK_MED    TAP_THREATATTK_BIG   TAP_THREATATTK_HUGE        */
        .015, .025,             .08, .06,             .09, .12,            .12, .22,
    /* TAP_BISHPAIR_BONUS                                                                             */
       .12, .22,
    /* TAP_PAWN_SUPPORTS_ANY   TAP_PAWN_DOUBLE_PEN     TAP_PAWN_FORK         TAP_PAWN_PASSED (x2/x4)  */
       .008, .02,              -.12, -.25,             .38, .38,             .09,.12,
    /* TAP_MOB_MOVESALL  */
       .008, .006,
    /* TAP_KING_SAFETY_PAWNPROT (2x)                                                                  */
       .08,.0
};
static_assert(sizeof(fTapWeights) / sizeof(*fTapWeights) == TAP_WEIGHTS_SIZE);

struct TapWeights
{
    constexpr TapWeights() :
        entries() {
        for (auto i = 0; i < TAP_WEIGHTS_SIZE; i++)
            entries[i] = Weight(fTapWeights[i] * 4096.);
    }
    Weight entries[TAP_WEIGHTS_SIZE];
};

// -------------------------------- Fixed point weights; generated at compile time
constexpr TapWeights iTapWeights{};

#else
/* // works very well, maybe weakness late mid endgame
const Weight iTapWeights[] = {
        6,    8,      25,   21,      27,   24,      38,   41,
       76,   72,       0,    0,       0,    0,       0,    0,
     // attk sml      attk med        attk bug      attk huge
       60,  100,     327,  245,     368,  490,     490,  901,
     // bishpair     pwn sup any   pawn dbl pen   pawn fork
      490,  900,      32,   81,    -491,-1024,    1556, 1556,
     // pwn passed    mob all       king saf pwn prot
      368,  490,      32,   24,     327,    0,
};
*/
/* version before panic fixes
const Weight iTapWeights[] = {
        6,    8,      25,   21,      27,   24,      38,   41,
     //                               attk micro     attk mini
       76,   72,       0,    0,        0,   0,       0, 0,
     // attk sml      attk med        attk big      attk huge
       41,  76,       200,  200,      300, 500,     500,  500,
     // bishpair     pwn sup any   pawn dbl pen   pawn fork
      500,  900,      38,  76,     -500,-700,     1000, 1000,
     // pwn passed    mob all       king saf pwn prot
      300,  500,      32,  24,      250,  0
};
*/
/* don't plays e2e4 d7d5 
const Weight iTapWeights[] = {
        6,    8,      25,   21,      27,   24,      38,   41,
     //                               attk micro     attk mini
       76,   72,       0,    0,        0,   0,       0, 0,
     // attk sml      attk med        attk big      attk huge
       41,  72,       900,  300,      2000, 500,    500,  500,
     // bishpair     pwn sup any   pawn dbl pen   pawn fork
      500,  900,      32,  72,     -500,-1000,     1000, 1000,
     // pwn passed    mob all       king saf pwn prot
      300,  500,      32,  25,      200,  0
};
*/
/*
const Weight iTapWeights[] = {
        6,    9,      25,   21,      27,   24,      38,   41,
     //                               attk micro     attk mini
       76,   72,       0,    0,        0,   0,       0, 0,
     // attk sml      attk med        attk big      attk huge
       60,  72,       600,  400,      1500, 500,    500,  400,
     // bishpair     pwn sup any   pawn dbl pen   pawn fork
      400,  900,      32,  72,     -500,-1000,     1000, 1000,
     // pwn passed    mob all       king saf pwn prot
      300,  500,      25,  25,      200,  0
};
*/
/*const Weight iTapWeights[] = {
        6,    9,      25,   21,      27,   24,      38,   41,
     //                               attk micro     attk mini
       76,   72,       0,    0,        0,   0,       0, 0,
     // attk sml      attk med        attk big      attk huge
       41,  32,       600,  300,      900, 500,    900,  900,
     // bishpair     pwn sup any   pawn dbl pen   pawn fork
      400,  900,      40,  64,     -500,-1000,     1000, 1000,
     // pwn passed    mob all       king saf pwn prot
      300,  500,      32,  24,      200,  0
};
*/
const Weight iTapWeights[] = {
        6,    9,      25,   21,      27,   24,      38,   41,
     //                               attk micro     attk mini
       76,   72,       0,    0,        0,   0,       0, 0,
     // attk sml      attk med        attk big      attk huge
       42,  50,       800,  300,      1650, 600,     500,  500,
     // bishpair     pwn sup any   pawn dbl pen   pawn fork
      450,  800,      36,  50,     -500,-1000,     1000, 1000,
     // pwn passed    mob all       king saf pwn prot
      300,  500,      34,  24,      200,  0
};

#endif

#ifndef TRAIN

auto tapWeight(TapWeightName val, int phase)                                        //@---
{
#ifdef USE_CONSTEXPR_TABLES
    return iTapWeights.entries[val * 2] + (iTapWeights.entries[val * 2 + 1] - iTapWeights.entries[val * 2]) * phase / 256;
#else
    return iTapWeights[val * 2] + (iTapWeights[val * 2 + 1] - iTapWeights[val * 2]) * phase / 256;
#endif
}                                                                                   //---@

void tapWeight2(TapWeightName val, Weight c[], int cnt = 1)
{
#ifdef USE_CONSTEXPR_TABLES
    c[0] += iTapWeights.entries[val * 2 + 0] * cnt;
    c[1] += iTapWeights.entries[val * 2 + 1] * cnt;
#else
    c[0] += iTapWeights[val * 2 + 0] * cnt;
    c[1] += iTapWeights[val * 2 + 1] * cnt;
#endif
}

#else

#define tapWeight(n, p) _tapWeight(tapWeights.values, n, p)
#define tapWeight2(n, w, cnt) _tapWeight2(tapWeights.values, n, w, cnt)

Weight _tapWeight(const Weight weights[], TapWeightName val, int phase)
{
    return weights[val * 2] + (weights[val * 2 + 1] - weights[val * 2]) * phase / 256;
}

void _tapWeight2(const Weight weights[], TapWeightName val, Weight c[], int cnt)
{
    c[0] += weights[val * 2] * cnt;
    c[1] += weights[val * 2 + 1] * cnt;
}

struct TuningTapWeights
{
    Weight values[TAP_WEIGHTS_SIZE];
    int scoreBase;
    int scoreFast;

    TuningTapWeights() = default;
    TuningTapWeights(const Weight w[]) { memcpy(values, w, sizeof(iTapWeights)); }
    TuningTapWeights(const flt w[]) { transform(w, w + TAP_WEIGHTS_SIZE, values, [](flt v) {return fixed(v); }); }
};

struct TuningTableWeights
{
    Weight values[768];

    TuningTableWeights() = default;
    TuningTableWeights(const Weight w[768]) { memcpy(values, w, 768 * 4); }
    TuningTableWeights(const flt w[768]) { transform(w, w + 768, values, [](flt v) {return fixed(v); }); }
    TuningTableWeights(const char w[768]) { transform(w, w + 768, values, [](char v) { return (v - RAWTABLE_MID) * RAWTABLE_SCALE; }); }
};

#endif //TRAIN
