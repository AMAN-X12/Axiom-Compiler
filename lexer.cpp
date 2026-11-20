#include "lexer.hpp"
#include <cstdio>
#include <unordered_set>

namespace {

constexpr int X = -1;  // dead state

inline bool isL(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool isD(unsigned char c) { return c >= '0' && c <= '9'; }
inline bool inSet(unsigned char c, const char* s) {
    for (; *s; ++s) if ((unsigned char)*s == c) return true;
    return false;
}

// ---------------------------------------------------------------- generic DFA
struct DFA {
    std::vector<std::vector<int>> table;   // table[state][inputClass] -> state or X
    std::vector<bool>             finals;
    int (*classify)(unsigned char);

    // Length of the longest prefix of s[pos..] accepted by this DFA (0 = none).
    size_t longest(const std::string& s, size_t pos) const {
        int st = 0;
        size_t best = 0;
        for (size_t i = pos; i < s.size(); ++i) {
            int nx = table[st][classify((unsigned char)s[i])];
            if (nx == X) break;
            st = nx;
            if (finals[st]) best = i - pos + 1;
        }
        return best;
    }
};

// ------------------------------------------------------------ input classes
int clsIdent(unsigned char c)   { return isL(c) ? 0 : isD(c) ? 1 : 2; }          // L D other
int clsNumber(unsigned char c)  { return isD(c) ? 0 : 1; }                       // D other
int clsDotted(unsigned char c)  { return isD(c) ? 0 : c == '.' ? 1 : 2; }        // D . other
int clsWord(unsigned char c)    { return c == '"' ? 0 : c == '\\' ? 1 : c == '\n' ? 2 : 3; }
int clsChar(unsigned char c)    { return c == '\'' ? 0 : c == '\\' ? 1 : c == '\n' ? 2 : 3; }
int clsComment(unsigned char c) { return c == '/' ? 0 : c == '*' ? 1 : c == '\n' ? 2 : 3; }
int clsPunct(unsigned char c)   { return c == ';' ? 0 : inSet(c, "()[]{},") ? 1 : 2; }
int clsOp(unsigned char c) {   // arith < > : = single other
    if (inSet(c, "+-*/%")) return 0;
    if (c == '<') return 1;
    if (c == '>') return 2;
    if (c == ':') return 3;
    if (c == '=') return 4;
    if (inSet(c, "&^~?.")) return 5;
    return 6;
}
int clsHeader(unsigned char c) { // < > L D (. /) other
    if (c == '<') return 0;
    if (c == '>') return 1;
    if (isL(c))   return 2;
    if (isD(c))   return 3;
    if (c == '.' || c == '/') return 4;
    return 5;
}
int clsWs(unsigned char c) { return inSet(c, " \t\r\n") ? 0 : 1; }

// ------------------------------------------------------------ the DFAs (section 4)
const DFA& dfaIdentifier() {
    static const DFA d{ {{1,X,X},{1,1,X}}, {false,true}, clsIdent };
    return d;
}
const DFA& dfaNumber() {
    static const DFA d{ {{1,X},{1,X}}, {false,true}, clsNumber };
    return d;
}
const DFA& dfaDotted() {
    static const DFA d{ {{1,X,X},{1,2,X},{3,X,X},{3,X,X}}, {false,false,false,true}, clsDotted };
    return d;
}
const DFA& dfaWord() {            //  "   \   NL  other
    static const DFA d{ {{1,X,X,X},
                         {2,3,X,1},
                         {X,X,X,X},
                         {1,1,X,1}}, {false,false,true,false}, clsWord };
    return d;
}
const DFA& dfaChar() {            //  '   \   NL  other
    // Lexically a character literal is a quote, ANY run of characters, a quote.
    // Whether it holds exactly one character is a SEMANTIC check, not a lexical one.
    static const DFA d{ {{1,X,X,X},     // q0
                         {3,2,X,1},     // q1  inside the literal (loops)
                         {1,1,X,1},     // q2  just read a backslash
                         {X,X,X,X}},    // q3  closing quote (accept)
                        {false,false,false,true}, clsChar };
    return d;
}
const DFA& dfaComment() {         //  /   *   NL  other
    static const DFA d{ {{1,X,X,X},   // q0
                         {X,2,X,X},   // q1  '/'
                         {3,4,X,3},   // q2  '/*'          (accept: single-line)
                         {3,3,X,3},   // q3  single-line   (accept)
                         {4,5,4,4},   // q4  inside '/** ...'
                         {4,6,4,4},   // q5  one star
                         {7,6,4,4},   // q6  two+ stars
                         {X,X,X,X}},  // q7  '**/'         (accept: multi-line)
                        {false,false,true,true,false,false,false,true}, clsComment };
    return d;
}
const DFA& dfaPunct() {           //  ;   P   other
    static const DFA d{ {{2,1,X},{X,X,X},{3,X,X},{X,X,X}}, {false,true,false,true}, clsPunct };
    return d;
}
const DFA& dfaOperator() {        // arith < > : = single other
    static const DFA d{ {{1,4,5,10,X,12,X},   // q0
                         {X,X,X,2,X,X,X},     // q1  + - * / %
                         {X,X,X,X,3,X,X},     // q2  arith ':'
                         {X,X,X,X,X,X,X},     // q3  +:=
                         {X,6,X,X,7,X,X},     // q4  <
                         {X,X,8,X,9,X,X},     // q5  >
                         {X,X,X,X,X,X,X},     // q6  <<
                         {X,X,X,X,X,X,X},     // q7  <=
                         {X,X,X,X,X,X,X},     // q8  >>
                         {X,X,X,X,X,X,X},     // q9  >=
                         {X,X,X,X,11,X,X},    // q10 :
                         {X,X,X,X,X,X,X},     // q11 :=
                         {X,X,X,X,X,X,X}},    // q12 & ^ ~ ?
                        {false,true,false,true,true,true,true,true,true,true,true,true,true}, clsOp };
    return d;
}
const DFA& dfaHeader() {          //  <  >  L  D  ./  other
    static const DFA d{ {{1,X,X,X,X,X},
                         {X,X,2,X,X,X},
                         {X,3,2,2,2,X},
                         {X,X,X,X,X,X}}, {false,false,false,true}, clsHeader };
    return d;
}
const DFA& dfaWhitespace() {
    static const DFA d{ {{1,X},{1,X}}, {false,true}, clsWs };
    return d;
}

const std::unordered_set<std::string>& keywordSet() {
    static const std::unordered_set<std::string> k = {
        "USE","mainClass","func",
        "number","dottednumber","char","flag","word","non","vector","pair","align","make","join",
        "chk","otherwise","choose","case","begin","for","while","endfor","endwhile","try","catch",
        "and","or","not","inc","dec","chkEqual",
        "Null","print","input",
        // added: C++ words kept with the same spelling in Axiom
        "return","true","false","break","continue","default"};
    return k;
}

std::string operatorDetail(const std::string& s) {
    if (s == ".") return "MEMBER";
    if (s == ":=") return "ASSIGN";
    if (s.size() == 3) return "COMPOUND_ASSIGN";
    if (s == "<" || s == ">" || s == "<=" || s == ">=") return "RELATIONAL";
    if (s == "<<" || s == ">>") return "SHIFT";
    if (s == ":") return "COLON";
    if (s == "?") return "TERNARY";
    if (s == "&" || s == "^" || s == "~") return "BITWISE";
    return "ARITHMETIC";
}
std::string punctDetail(const std::string& s) {
    if (s == ";;") return "STATEMENT_END";
    switch (s[0]) {
        case '(': return "LPAREN";   case ')': return "RPAREN";
        case '[': return "LBLOCK";   case ']': return "RBLOCK";
        case '{': return "LBRACE";   case '}': return "RBRACE";
        default:  return "COMMA";
    }
}
std::string show(unsigned char c) {
    char buf[16];
    if (c >= 32 && c < 127) std::snprintf(buf, sizeof buf, "'%c'", c);
    else std::snprintf(buf, sizeof buf, "0x%02X", c);
    return buf;
}

} // namespace

// ------------------------------------------------------------------ public API
std::string tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::KEYWORD:      return "KEYWORD";
        case TokenType::IDENTIFIER:   return "IDENTIFIER";
        case TokenType::NUMBER:       return "NUMBER";
        case TokenType::DOTTEDNUMBER: return "DOTTEDNUMBER";
        case TokenType::WORD:         return "WORD";
        case TokenType::CHAR:         return "CHAR";
        case TokenType::OPERATOR:     return "OPERATOR";
        case TokenType::PUNCTUATION:  return "PUNCTUATION";
        case TokenType::HEADER:       return "HEADER";
        case TokenType::COMMENT:      return "COMMENT";
        case TokenType::INVALID:      return "INVALID";
        case TokenType::END_OF_FILE:  return "EOF";
    }
    return "?";
}

Lexer::Lexer(std::string source) : src_(std::move(source)) {}

bool Lexer::isKeyword(const std::string& s) { return keywordSet().count(s) > 0; }

void Lexer::advance(size_t n) {
    for (size_t i = 0; i < n && pos_ < src_.size(); ++i, ++pos_) {
        if (src_[pos_] == '\n') { ++line_; col_ = 1; } else ++col_;
    }
}

void Lexer::error(int line, int col, const std::string& msg) {
    errors_.push_back({line, col, msg});
}

std::vector<Token> Lexer::tokenize(bool keepComments) {
    std::vector<Token> out;
    pos_ = 0; line_ = 1; col_ = 1; errors_.clear();
    bool afterUse = false;   // HEADER DFA is only tried right after USE

    auto emit = [&](TokenType t, size_t len, const std::string& detail) {
        std::string lex = src_.substr(pos_, len);
        if (t != TokenType::COMMENT || keepComments)
            out.push_back({t, lex, line_, col_, detail});
        advance(len);
    };

    while (pos_ < src_.size()) {
        // 1. whitespace: matched, then discarded
        if (size_t w = dfaWhitespace().longest(src_, pos_)) { advance(w); continue; }

        unsigned char c = (unsigned char)src_[pos_];
        int sl = line_, sc = col_;

        // 2. unterminated multi-line comment  (/** with no closing **/)
        if (src_.compare(pos_, 3, "/**") == 0) {
            size_t len = dfaComment().longest(src_, pos_);
            if (len < 5 || src_.compare(pos_ + len - 3, 3, "**/") != 0) {
                error(sl, sc, "unterminated multi-line comment (expected closing '**/')");
                advance(src_.size() - pos_);
                break;
            }
        }

        // 3. run every DFA, keep the longest; earlier entry wins a tie
        struct Cand { TokenType type; size_t len; };
        Cand best{TokenType::END_OF_FILE, 0};
        auto consider = [&](TokenType t, size_t len) { if (len > best.len) best = {t, len}; };

        consider(TokenType::COMMENT,      dfaComment().longest(src_, pos_));
        consider(TokenType::WORD,         dfaWord().longest(src_, pos_));
        consider(TokenType::CHAR,         dfaChar().longest(src_, pos_));
        if (afterUse)
            consider(TokenType::HEADER,   dfaHeader().longest(src_, pos_));
        consider(TokenType::DOTTEDNUMBER, dfaDotted().longest(src_, pos_));
        consider(TokenType::NUMBER,       dfaNumber().longest(src_, pos_));
        consider(TokenType::IDENTIFIER,   dfaIdentifier().longest(src_, pos_));
        consider(TokenType::PUNCTUATION,  dfaPunct().longest(src_, pos_));
        consider(TokenType::OPERATOR,     dfaOperator().longest(src_, pos_));

        // 4. nothing matched -> lexical error, then recover
        if (best.len == 0) {
            std::string msg;
            size_t skip = 1;
            if (c == '"') {
                msg = "unterminated or invalid string literal";
                size_t e = src_.find('\n', pos_);
                skip = (e == std::string::npos ? src_.size() : e) - pos_;
            } else if (c == '\'') {
                msg = "unterminated character literal (missing closing quote)";
            } else if (c == ';') {
                msg = "stray ';' - a statement ends with ';;'";
            } else if (c == '=') {
                msg = "'=' is not an Axiom operator - use ':=' (assign) or 'chkEqual' (equality)";
            } else if (c == '!') {
                msg = "'!' is not an Axiom operator - use 'not'";
            } else if (c == '|') {
                msg = "'|' is not an Axiom operator - use 'or'";
            } else {
                msg = "unexpected character " + show(c);
            }
            error(sl, sc, msg);
            advance(skip);
            continue;
        }

        // 5. malformed number: 9abc, 7AB, 12., 9.39.9
        //    The WHOLE run of letters/digits/dots becomes ONE token (INVALID),
        //    instead of being split into a number plus stray pieces.
        if ((best.type == TokenType::NUMBER || best.type == TokenType::DOTTEDNUMBER) &&
            pos_ + best.len < src_.size() &&
            (isL((unsigned char)src_[pos_ + best.len]) || src_[pos_ + best.len] == '.')) {
            size_t e = pos_ + best.len;
            while (e < src_.size() &&
                   (isL((unsigned char)src_[e]) || isD((unsigned char)src_[e]) || src_[e] == '.')) ++e;
            std::string bad = src_.substr(pos_, e - pos_);
            int dots = 0; bool letters = false;
            for (char ch : bad) { if (ch == '.') ++dots; else if (isL((unsigned char)ch)) letters = true; }
            std::string why = dots >= 2 ? "a number can have only one decimal point"
                            : letters   ? "an identifier cannot start with a digit"
                                        : "a digit must follow the decimal point";
            error(sl, sc, "invalid token '" + bad + "' - " + why);
            out.push_back({TokenType::INVALID, bad, sl, sc, "MALFORMED_NUMBER"});
            advance(e - pos_);
            continue;
        }

        // 6. emit the token
        std::string lex = src_.substr(pos_, best.len);
        switch (best.type) {
            case TokenType::IDENTIFIER:
                if (isKeyword(lex)) emit(TokenType::KEYWORD, best.len, "");
                else                emit(TokenType::IDENTIFIER, best.len, "");
                break;
            case TokenType::OPERATOR:    emit(best.type, best.len, operatorDetail(lex)); break;
            case TokenType::PUNCTUATION: emit(best.type, best.len, punctDetail(lex));    break;
            case TokenType::COMMENT:
                emit(best.type, best.len, (lex.compare(0, 3, "/**") == 0) ? "MULTI_LINE" : "SINGLE_LINE");
                break;
            default: emit(best.type, best.len, ""); break;
        }

        if (best.type != TokenType::COMMENT)
            afterUse = (best.type == TokenType::IDENTIFIER && lex == "USE");
    }

    out.push_back({TokenType::END_OF_FILE, "", line_, col_, ""});
    return out;
}
