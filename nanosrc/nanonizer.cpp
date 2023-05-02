
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <stack>
#include <algorithm>

/////////////////////////////////////////
// Activate for TCEC build!
/////////////////////////////////////////
#if 1
  #define RESET_NUM_THREADS "103"s
  #define RESET_TTSIZE "0x100000000ull"s
#endif
/////////////////////////////////////////


using namespace std;
using namespace chrono;
namespace fs = filesystem;
fs::path nanoOutPath;
char lineBuffer[65536];

using StringVec = vector<string>;
using StringSet = unordered_set<string>;
using StringMap = unordered_map<string, string>;

// There are four types of macros:
// 1. Directive                // i.e. "#define TRACK_PV"
// 2. Object - like macros     // i.e. "#define QSEARCH_MAX_DEPTH 12"
// 3. Function - like macros.  // i.e. "#define PLUS(a,b) a + b + varOutOfScore"
// 4. Chain macros             // i.e. "#define MULTPLUS(a,b,c) a * PLUS(b,c)"
// (5.) Multi - line macros -> will be resolved to 1-4

StringSet globalDefines_Directive;
StringVec globalStdIncludes;
StringMap globalReplacenents; // i.e. key="u32" value="uint32_t"
StringVec phase1Out;
StringVec phase2Out;

StringSet noManglingWords{ "auto", "static", "const", "inline", "std", "chrono", "memset", "getchar", "strncmp", "now", "string",
                           "__builtin_popcountll", "__builtin_bswap64", "__builtin_ctzll", "time_point", "mt19937", "continue",
                            "sort", "stable_sort", "duration", "milli", "this_thread", "atoi", "sleep_for", "join", "using",
                            "namespace", "strlen", "high_resolution_clock", "double", "atomic", "vector", "load", "store", "main",
                            "setbuf", "stdout", "milliseconds", "abs", "jthread", "min", "max", "long", "char", "sizeof", "swap",
                            "operator", "memory_order_relaxed", "printf", "this", "break", "while", "define", "void", "bool",
                            "template", "struct", "uint16_t", "uint8_t", "int16_t", "uint64_t", "if", "else", "for", "uint32_t",
                            "int", "return", "count", "data", "constexpr", "steady_clock", "CLOCKS_PER_SEC", "clock", "goto",
                            "int8_t", "fill", "usleep", "short" };

// --- Try to optimize for lzma compression
//          char shortNameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefgyijklmnozqrswuvthxp";
          char shortNameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWYZ_abcdefgyijklmnozqrswuvtXhxp";
constexpr auto shortNameChars_size = sizeof(shortNameChars) - 1;

          char shortNameCharsL2a[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefgyijklmnozqrswuvthxp";
constexpr auto shortNameCharsL2a_size = sizeof(shortNameCharsL2a) - 1;

          char shortNameCharsL2b[] = /*"wxyz";*/  /*"xphtHdlz"*/ "pxhtHdlz";
constexpr auto shortNameCharsL2b_size = sizeof(shortNameCharsL2b) - 1;

StringSet shortnamesUsed;

#if 1
// panic fixes to reduce file size...
static string makeShortname(int index)
{
    if (index < shortNameChars_size)
        return string(&shortNameChars[shortNameChars_size - index - 1], 1);
    index -= shortNameChars_size;

/*    for (int i = 0; i < 1; i++)
    {
        if (index < shortNameChars_size)
        {
            string s = string(&shortNameChars[index], 1) +
                       string(&shortNameChars[(index + i) % shortNameChars_size], 1);
            shortnamesUsed.insert(s);
            return s;
        }
        index -= shortNameChars_size;
    }
  */
    if (index < shortNameCharsL2a_size * shortNameCharsL2b_size)
    {
        for (;;)
        {
            string s = string(&shortNameCharsL2b[index / shortNameCharsL2a_size], 1) +
                       string(&shortNameCharsL2a[shortNameCharsL2a_size - (index % shortNameCharsL2a_size) - 1], 1);
            if (!shortnamesUsed.contains(s))
                return s;
            index++;
        }
    }
    else
    {
        assert(false);
        return string();
    }
}
#else
static string makeShortname(int index)
{
    if (index < shortNameChars_size)
        return string(&shortNameChars[shortNameChars_size - index - 1], 1);

    index -= shortNameChars_size;
    if (index < shortNameCharsL2a_size * shortNameCharsL2b_size)
        return
        string(&shortNameCharsL2b[index / shortNameCharsL2a_size], 1) +
        string(&shortNameCharsL2a[shortNameCharsL2a_size - (index % shortNameCharsL2a_size) - 1], 1);
    else
    {
        assert(false);
        return string();
    }
}
#endif

struct WordAppearance
{
    string word;
    string shortName;
    vector<string> alsoUsedBy;
    int count = 1;
};

vector<WordAppearance> wordsPresent;

static void replaceShortname(string useNewShortname, string fromThis)
{
    auto itReplace = find_if(wordsPresent.begin(), wordsPresent.end(), [&useNewShortname](auto& v) { return v.word == useNewShortname; });
    assert(itReplace != wordsPresent.end());

    auto itFrom = find_if(wordsPresent.begin(), wordsPresent.end(), [&fromThis](auto& v) { return v.word == fromThis; });
    assert(itFrom != wordsPresent.end());

    itFrom->alsoUsedBy.push_back(itReplace->word);
    itFrom->count += itReplace->count;

    wordsPresent.erase(itReplace);
}

string findShortWord(const string& word)
{
    auto it = wordsPresent.begin();
    for (; it != wordsPresent.end(); it++)
    {
        if (it->word == word)
            return it->shortName;

        auto it2 = it->alsoUsedBy.begin();
        for (; it2 != it->alsoUsedBy.end(); it2++)
        {
            if (*it2 == word)
                return it->shortName;
        }
    }

    return ""s;
}


const StringMap hardReplacements = {  { "negaMax", "_"}, { "iterativeM4ssaging", "setStartpos" },
                                      { "QSEARCH","p" }, { "ROOT", "pieceSrcMask" } };


static void addReplacementShortnames()
{
    // HACK: make sure to double check the context (and test!) when adding replacements. Bugs may
    // appear silently; when somthing isn't working, deactivate this function in order to find out
    // whether it is the cause.  Happy debugging me!
    /////////////////////////////////////////////////////////////////////////////////////////////////
    // good candidates: "m", "c", "p", "i", "s", "t", "score", "moves", "ttData", "_", "_c", "phase"
    //                  "pieceSrcMask", "depth", "pos", "val", "cnt", "strPtr", "maskDest", "move",
    //                  "alpha", "beta", "pieceSrc", "kingsSquare", "colorsMask", "pieceMask", "o",
    //                  "entries", "func", "dir", "inCheck", "enPassant", "QSEARCH",
    //                  "add", "destMask", "castling", "maskSrc"
    /////////////////////////////////////////////////////////////////////////////////////////////////
    replaceShortname("flags", "score");
    replaceShortname("flipped", "alpha");
    replaceShortname("ep", "func");
    replaceShortname("posDest", "ttData");
    replaceShortname("pieceSrc", "t");
    replaceShortname("pieceDest", "m");

    // search
    replaceShortname("iterate", "dir");
    replaceShortname("alphaOrig", "maskDest");
    replaceShortname("confirmedBestMove", "maskSrc");
    replaceShortname("confirmedBestDeep", "kingsSquare");
    replaceShortname("smoves", "func");

    // getPinnedMask
    replaceShortname("itersL", "c");
    replaceShortname("itersR", "score");
    replaceShortname("itersD", "moves");
    replaceShortname("itersU", "alpha");
    replaceShortname("numIters", "func");
    replaceShortname("file", "depth");
    replaceShortname("rank", "cnt");
    replaceShortname("straightDiagMask", "maskSrc");
    replaceShortname("o", "maskDest");

    replaceShortname("attackMap", "alpha");
    replaceShortname("getPhase", "entries");

    replaceShortname("strLiteral", "m");

    replaceShortname("sw", "alpha");
    replaceShortname("mw", "beta");

    replaceShortname("deepestSearch", "alpha");
    replaceShortname("bestMv", "beta");

    replaceShortname("scorePieces", "func");

}

static void setPaths(const char* srcPath, const char* outPath)
{
    // make sure we are in the right directory
    for (int count = 0; !fs::exists(fs::current_path() / srcPath); count++)
    {
        assert(count < 5);
        fs::current_path(fs::current_path().parent_path());
    }
    fs::current_path(fs::current_path() / srcPath);

    nanoOutPath = fs::current_path() / outPath;
    if (!fs::exists(nanoOutPath))
        fs::create_directory(nanoOutPath);

    cout << "Current path: " << fs::current_path() << "\nNanoOutPath: " << nanoOutPath << "\n";
}

inline bool isWordChar(const char c)
{
    return isalnum(c) || c == '_';
}

string getWord(const char* ptrStart)
{
    const char* ptrEnd = ptrStart;
    while (isWordChar(*ptrEnd))
        ptrEnd++;
    return string(ptrStart, ptrEnd);
}

string findReplacementWord(const string& word)
{
    string res;

    auto i = hardReplacements.find(word);
    if (i != hardReplacements.end())
    {
        return i->second;
    }

    auto it = globalReplacenents.find(word);
    if (it != globalReplacenents.end())
    {
        res = it->second;
    }

    return res;
}

void squeezePhase2()
{
    StringVec newVec;
    newVec.push_back(phase2Out[0]);

    int numEntries = (int)phase2Out.size();
    int currentId = 0;
    for (auto i = 1; i < numEntries; i++)
    {
        if (phase2Out[i][0] == '#' || newVec[currentId][0] == '#' || (newVec[currentId].size() + phase2Out[i].size()) >= 4095)
        {
            currentId++;
            newVec.push_back(string());
        }

        if (!newVec[currentId].empty() && isWordChar(phase2Out[i][0]) && isWordChar(newVec[currentId][newVec[currentId].size() - 1]))
            newVec[currentId] += " "s;

        newVec[currentId] += phase2Out[i];
    }

    phase2Out = newVec;
}

void phase2(const char* outFile)
{
    for (const auto& s : phase1Out)
    {
        strcpy(lineBuffer, s.data());
        size_t lineSize = strlen(lineBuffer);

        // ---- search for words
        int firstWordPos = -1;
        int inLiteral = 0;
        for (auto i = 0; i <= lineSize; i++)
        {
            if (lineBuffer[i] == '"')
                inLiteral = 1 - inLiteral;
            if (inLiteral)
                continue;

            if (isWordChar(lineBuffer[i]))
            {
                if (isdigit(lineBuffer[i]) && lineBuffer[i + 1] == 's')
                {
                    // skip time literals like "2s"
                    i++;
                    continue;
                }

                if (firstWordPos < 0 && !isdigit(lineBuffer[i]))
                    firstWordPos = i;
            }
            else if (firstWordPos >= 0)
            {
                // found word
                int lastWordPos = i;
                string word = string(lineBuffer + firstWordPos, lastWordPos - firstWordPos);
/*                if (word == "1s")
                {
                    int t = 0;
                }
                */
                // ---- mangling...
                string replaceWith = findShortWord(word);
                if (!replaceWith.empty())
                {
                    // replace!
                    int shrinkSize = (int)replaceWith.size() - (int)word.size();
                    if (shrinkSize > 0) // make line longer?
                        memmove(lineBuffer + firstWordPos + shrinkSize, lineBuffer + firstWordPos, lineSize - firstWordPos + 1);
                    else if (shrinkSize < 0) // make line shorter?
                        memmove(lineBuffer + firstWordPos, lineBuffer + firstWordPos - shrinkSize, lineSize - firstWordPos + shrinkSize + 1);

                    strncpy(lineBuffer + firstWordPos, replaceWith.data(), replaceWith.size());
                    i = firstWordPos + (int)replaceWith.size() - 1;
                    lineSize = strlen(lineBuffer);
                }
                firstWordPos = -1;
            }
        }

        string line(lineBuffer);
        phase2Out.push_back(line);
//        cout << line << "|\n";
    }

    squeezePhase2();

    // write to outFile
    ofstream ostrm(nanoOutPath / outFile, ios::binary);
    assert(!ostrm.fail());
    for (const auto& l : phase2Out)
        ostrm << l << '\n';
}

void phase1(const char* inFile, const char* outFile, stack<ifstream>& in)
{
    in.push(ifstream(inFile, ios::binary));
    assert(!in.top().fail());
    
    int NNskip = 0;
    int commentSkip = 0;
    map<string, int> ifdefStatus;
    stack<string> lastIfdef;
    int smartEnum = -1;

    while (in.top().getline(lineBuffer, 65535))
    {
        char* strPtr, *strPtr2;
        size_t lineSize = strlen(lineBuffer);

        for (;;)
        {
            // ---- no tabs (add whitespaces instead), no CRLF (insert 0, will later replaced by LF)
            for (auto i = 0; i < lineSize; i++)
                if (lineBuffer[i] == '\t')
                    lineBuffer[i] = ' ';
                else if (lineBuffer[i] == '\n' || lineBuffer[i] == '\r')
                    lineBuffer[i] = '\0';

            // ---- connect lines that end with '\'
            bool connect = false;
            int pos = (int)lineSize - 1;
            for (; pos >= 0; pos--)
                if (lineBuffer[pos] == '\\')
                {
                    connect = true;
                    break;
                }
                else if (lineBuffer[pos] != ' ')
                    break;

            if (!connect)
                break;

            in.top().getline(&lineBuffer[pos], 65535 - lineSize);
            lineSize = strlen(lineBuffer);
        }

        // ---- start/end "smart enum"
        if (strstr(lineBuffer, "enum /* @"))
        {
            assert(smartEnum == -1);
            smartEnum = 0;
            continue;
        }
        if (smartEnum >= 0 && strstr(lineBuffer, "};"))
        {
            smartEnum = -1;
            continue;
        }

        // ---- check for string literals and its position
        char* stringLiteralStart{};
        char* stringLiteralEnd{};
        strPtr = lineBuffer;
        for (;*strPtr;strPtr++)
        {
            if (*strPtr == '"' && stringLiteralStart == nullptr)
                stringLiteralStart = strPtr;
            else if (*strPtr == '"')
            {
                stringLiteralEnd = strPtr;
                break;
            }
        }
        if (stringLiteralEnd == nullptr)
            stringLiteralStart = nullptr;

        // ---- skip comments and the three nanonizer internal skips "@---", "---@" and "@-"
        if (strPtr = strstr(lineBuffer, "//"))
        {
            if (!(stringLiteralStart <= strPtr && stringLiteralEnd >= strPtr))
            {
                if (strncmp(strPtr + 2, "@---", 4) == 0)
                {
                    NNskip++; // start skip this section
                    continue;
                }
                else if (strncmp(strPtr + 2, "---@", 4) == 0)
                {
                    NNskip--; // stop skip this section (but skip this line)
                    assert(NNskip >= 0);
                    continue;
                }
                else if (strncmp(strPtr + 2, "@-", 2) == 0)
                    continue; // just skip this line

                // skip everything from '//'
                *strPtr = '\0';
                lineSize = strlen(lineBuffer);
            }
        }
        if (NNskip > 0)
            continue;

        // ---- skip multi-line comments
        bool commentSkipButThis = false;
        for (;;)
        {
            bool redo = false;
            if (commentSkip > 0)
            {
                if (strPtr = strstr(lineBuffer, "*/"))
                {
                    commentSkip--;
                    memmove(lineBuffer, strPtr + 2, (strPtr + 2) - lineBuffer + 1);
                    lineSize = strlen(lineBuffer);
                }
            }

            // special case: '/*' and '*/' within one line: (may occur multiple times)
            if (strPtr = strstr(lineBuffer, "/*"))
            {
                if (!(stringLiteralStart <= strPtr && stringLiteralEnd >= strPtr))
                {
                    if (strPtr2 = strstr(lineBuffer, "*/"))
                    {
                        assert(strPtr < strPtr2);
                        memmove(strPtr, strPtr2 + 2, lineSize - ((strPtr2 + 2) - strPtr) + 1);
                        lineSize = strlen(lineBuffer);
                        redo = true;
                    }
                    else
                    {
                        commentSkip++;
                        commentSkipButThis = true;
                        *strPtr = '\0';
                        lineSize = strlen(lineBuffer);
                    }
                }
            }
            if (!redo)
                break;
        }
        if (commentSkip > 0 && !commentSkipButThis)
            continue;

        // ---- skip "#ifdef" ("#ifndef", "#else", "#endif"...)
        if (strPtr = strstr(lineBuffer, "#ifdef"))
        {
            string macroName = getWord(strPtr + 7);
            lastIfdef.push(macroName);
            ifdefStatus[macroName] = 1;
            continue;
        }
        else if (strPtr = strstr(lineBuffer, "#ifndef"))
        {
            string macroName = getWord(strPtr + 8);
            lastIfdef.push(macroName);
            ifdefStatus[macroName] = 2;
            continue;
        }
        else if (strstr(lineBuffer, "#if 1"))
        {
            lastIfdef.push("1"s);
            ifdefStatus["1"s] = 1;
            continue;
        }
        else if (strstr(lineBuffer, "#if 0"))
        {
            lastIfdef.push("1"s);
            ifdefStatus["1"s] = 2;
            continue;
        }
        else if (strstr(lineBuffer, "#else"))
        {
            auto t = lastIfdef.top();
            ifdefStatus[lastIfdef.top()] = 3 - ifdefStatus[lastIfdef.top()];
            continue;
        }
        else if (strstr(lineBuffer, "#endif"))
        {
            ifdefStatus.erase(lastIfdef.top());
            lastIfdef.pop();
            continue;
        }

        bool skip = false;
        for (const auto& s : ifdefStatus)
        {
            bool macroMustBePresent = s.second == 1;
            bool macroIsPresent = globalDefines_Directive.find(s.first) != globalDefines_Directive.end();
            if (macroMustBePresent != macroIsPresent)
            {
                // some macro condition is not true, don't add this line
                skip = true;
                break;
            }
        }
        if (skip)
            continue;

        // ---- include local headers
        if (strPtr = strstr(lineBuffer, "#include \""))
        {
            string includeFile = getWord(strPtr + 10) + ".h"s;
            phase1(includeFile.data(), outFile, in);
            continue;
        }

        // ---- skip lines with static_assert() or assert()
        if (strstr(lineBuffer, "assert("))
            continue;

        // ---- Convert character literals to values
#if 1
        for (auto i = 0; i < lineSize; i++)
            if (lineBuffer[i] == 0x27 /*'*/ && lineBuffer[i + 1] != 0 && lineBuffer[i + 2] == 0x27 /*'*/)
            {
                string sVal = to_string(lineBuffer[i + 1]);
                lineBuffer[i] = ' ';
                lineBuffer[i + 1] = ' ';
                lineBuffer[i + 2] = ' ';
                strncpy(&lineBuffer[i], sVal.data(), sVal.size());
            }
#endif
        // ---- remove leading spaces
        int pos = 0;
        for (; pos < lineSize; pos++)
            if (lineBuffer[pos] != ' ')
                break;
        if (pos > 0)
        {
            memmove(lineBuffer, &lineBuffer[pos], lineSize - pos + 1);
            lineSize = strlen(lineBuffer);
        }

        // ---- remove trailing spaces
        pos = (int)lineSize - 1;
        for (; pos >= 0; pos--)
            if (lineBuffer[pos] != ' ')
                break;
        if (pos != (int)lineSize - 1)
        {
            lineBuffer[pos + 1] = '\0';
            lineSize = strlen(lineBuffer);
        }

        // ---- remove spaces in-between
        auto firstSpacePos = -1;
        auto firstChar = '\0';
        auto inString = 0;
        for (auto i = 1; i < lineSize; i++)
        {
            auto c = lineBuffer[i];

            if (inString)
            {
                if (c == '"')
                    inString--;
                continue;
            }

            if (c == ' ')
            {
                if (firstSpacePos < 0)
                {
                    firstSpacePos = i;
                    firstChar = lineBuffer[i - 1];
                }
            }
            else if (firstSpacePos >= 0)
            {
                bool isFirstWordChar = isWordChar(firstChar);
                bool isCurWordChar = isWordChar(c);

                skip = true;
                if (isFirstWordChar != isCurWordChar)
                    skip = false;
                else if (!isFirstWordChar && !isCurWordChar)
                    skip = false;

                int lastSpacePos = (int)i - (skip ? 1 : 0);
                if (firstSpacePos != lastSpacePos)
                {
                    memmove(lineBuffer + firstSpacePos, lineBuffer + lastSpacePos, lineSize - lastSpacePos + 1);
                    lineSize = strlen(lineBuffer);
                    i = firstSpacePos;
                }
                firstSpacePos = -1;
            }

            if (lineBuffer[i] == '"' && inString == 0)
            {
                assert(firstSpacePos == -1);
                inString++;
            }
        }
        assert(firstSpacePos == -1);

        // ---- skip lines that contain only whitespaces
        skip = true;
        for (auto i = 0; i < lineSize && skip; i++)
            if (lineBuffer[i] != ' ')
                skip = false;
        if (skip)
            continue;

        // ---- check for typedefs (aka 'using type=..'")
        if (!strncmp(lineBuffer, "using ", 6))
        {
            if (strPtr = strstr(lineBuffer, "="))
            {
                assert(strPtr - lineBuffer - 6 > 0);
                string typenameWord = string(lineBuffer + 6, strPtr - lineBuffer - 6);
                string typenameWordDef = string(strPtr + 1, lineBuffer + lineSize - strPtr - 2);
                if (typenameWord != "u64"s /*&& typenameWord != "u32"s*/)
                {
                    assert(globalReplacenents.find(typenameWord) == globalReplacenents.end());
                    globalReplacenents[typenameWord] = typenameWordDef;
                    continue;
                }
            }
        }

        // ---- check for directive macro defines and value macros
        if (strPtr = strstr(lineBuffer, "#define "))
        {
            string macroName = getWord(strPtr + 8);

            strPtr += 8 + macroName.size();

            // value macro?
            strPtr2 = strPtr + 1;
            while (isdigit(*strPtr2) || *strPtr2 == 'x' || *strPtr2 == 'u' || *strPtr2 == 'l')
                strPtr2++;
            if (*strPtr2 == '\0')
            {
                size_t cnt = strPtr2 - strPtr - 1;
                string valueWord = string(strPtr + 1, cnt);
                if (valueWord.size() > 0)
                {
                    // is value
#ifdef RESET_NUM_THREADS
                    if (macroName == "NUM_THREADS"s)
                        valueWord = RESET_NUM_THREADS;
#endif
#ifdef RESET_TTSIZE
                    if (macroName == "TTSize"s)
                        valueWord = RESET_TTSIZE;
#endif
                    assert(globalReplacenents.find(macroName) == globalReplacenents.end());
                    globalReplacenents[macroName] = valueWord;
                    continue;
                }
            }

            // directive macro?
            skip = false;
            while (*strPtr)
                if (*strPtr++ != ' ')
                    skip = true;
            if (!skip)
            {
                // directive macro?
                globalDefines_Directive.insert(macroName);
                continue;
            }
        }

        bool useUnmodified = false;

        // ---- is std include? (just use it)
        if (!strncmp(lineBuffer, "#include<", 9))
            useUnmodified = true;

        if (!useUnmodified)
        {
            // ---- search for words
            int firstWordPos = -1;
            for (auto i = 0; i <= lineSize; i++)
            {
                if (isWordChar(lineBuffer[i]))
                {
                    if (firstWordPos < 0 && !isdigit(lineBuffer[i]))
                        firstWordPos = i;
                }
                else if (firstWordPos >= 0)
                {
                    // found word
                    int lastWordPos = i;
                    string word = string(lineBuffer + firstWordPos, lastWordPos - firstWordPos);

                    if (smartEnum >= 0)
                    {
                        // ---- parse "smart enum"
                        if (lineBuffer[lastWordPos] == '=')
                        {
                            smartEnum = atoi(lineBuffer + lastWordPos + 1);
                        }
                        string numStr = to_string(smartEnum);
                        assert(globalReplacenents.find(word) == globalReplacenents.end());
                        globalReplacenents[word] = numStr;

                        smartEnum++;
                    }
                    else
                    {
                        // ---- search for words that need to be replaced
                        string replaceWith = findReplacementWord(word);
                        if (!replaceWith.empty())
                        {
                            // replace!
                            int shrinkSize = (int)replaceWith.size() - (int)word.size();
                            if (shrinkSize > 0) // make line longer?
                                memmove(lineBuffer + firstWordPos + shrinkSize, lineBuffer + firstWordPos, lineSize - firstWordPos + 1);
                            else if (shrinkSize < 0) // make line shorter?
                                memmove(lineBuffer + firstWordPos, lineBuffer + firstWordPos - shrinkSize, lineSize - firstWordPos + shrinkSize + 1);

                            strncpy(lineBuffer + firstWordPos, replaceWith.data(), replaceWith.size());
                            i = firstWordPos - 1;
                            lineSize = strlen(lineBuffer);
                        }
                    }
                    firstWordPos = -1;
                }
            }
        }
        if (smartEnum >= 0)
            continue;

        // ---- count used words
        if (!useUnmodified)
        {
            int firstWordPos = -1;
            int inLiteral = 0;
            for (auto i = 0; i <= lineSize; i++)
            {
                if (lineBuffer[i] == '"')
                    inLiteral = 1 - inLiteral;
                if (inLiteral)
                    continue;

                if (isWordChar(lineBuffer[i]))
                {
                    if (firstWordPos < 0)
                        firstWordPos = i;
                }
                else if (firstWordPos >= 0)
                {
                    // found word
                    int lastWordPos = i;
                    string word = string(lineBuffer + firstWordPos, i - firstWordPos);
                    firstWordPos = -1;

                    if (isdigit(word[0]) || noManglingWords.contains(word))
                        continue;

                    auto it = find_if(wordsPresent.begin(), wordsPresent.end(), [&word](auto& v) { return v.word == word; });
                    if (it != wordsPresent.end())
                        it->count++;
                    else
                        wordsPresent.push_back(WordAppearance{ word });
                }
            }
        }

        string line(lineBuffer);
        if (!line.empty())
            phase1Out.push_back(line);
//        cout << line << "|\n";
    }

    in.pop();
    if (in.empty())
    {
        // write to outFile
        ofstream ostrm(nanoOutPath / outFile, ios::binary);
        assert(!ostrm.fail());
        for (const auto& l : phase1Out)
            ostrm << l << '\n';
    }
}

int main()
{
    setPaths("src", "../nanoout");

    globalDefines_Directive.insert("NDEBUG"s);   // remove all debugging stuff
    globalDefines_Directive.insert("__GNUC__"s); // compiler is GCC (no need to add version number)
    globalDefines_Directive.insert("1"s);        // for "#if 1" / "#if 0" macros


    // Phase 1: - Merge files
    //          - Pre-process (=remove) defines and nanonizer-own macros like '//@---',
    //          - Connect multi-line macros
    //          - Remove unneeded whitespaces and empty lines
    //          - KEEP line breaks in order to keep it readable
    stack<ifstream> in;
    phase1("main.cpp", "phase1.cpp", in);

    addReplacementShortnames();

    stable_sort(wordsPresent.begin(), wordsPresent.end(), [](auto& a, auto& b) { return a.count > b.count; });

    auto i = 0;
    for (auto& w : wordsPresent)
        w.shortName = makeShortname(i++);

#if 1
    i = 1;
    for (auto& w : wordsPresent)
    {
        string alsoUsed = w.alsoUsedBy.empty() ? ""s : "also used by: ";
        for (auto& u : w.alsoUsedBy)
            alsoUsed += u + " "s;
        printf("---- %d: %d x %s  (%s)    %s \n", i, w.count, w.word.data(), w.shortName.data(), alsoUsed.data());
        i++;
    }
#endif

    // Phase 2: - Word mangling
    //          - Squeeze lines together where possible
    phase2("phase2.cpp");
}
