// Axiom Lexical Analyzer - Compiler Construction
// Table-driven DFAs taken from the "Axiom Regex and DFA" specification.
#pragma once
#include <string>
#include <vector>

enum class TokenType {
    KEYWORD, IDENTIFIER, NUMBER, DOTTEDNUMBER, WORD, CHAR,
    OPERATOR, PUNCTUATION, HEADER, COMMENT, INVALID, END_OF_FILE
};

std::string tokenTypeName(TokenType t);

struct Token {
    TokenType   type;
    std::string lexeme;
    int         line;
    int         col;
    std::string detail;   // sub-class, e.g. ASSIGN, RELATIONAL, STATEMENT_END
};

struct LexError {
    int         line;
    int         col;
    std::string message;
};

class Lexer {
public:
    explicit Lexer(std::string source);

    // Scans the whole input. Always ends with an END_OF_FILE token.
    // Comments are kept in the stream unless keepComments is false.
    std::vector<Token> tokenize(bool keepComments = true);

    const std::vector<LexError>& errors() const { return errors_; }

    static bool isKeyword(const std::string& s);

private:
    std::string src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
    std::vector<LexError> errors_;

    void advance(size_t n);
    void error(int line, int col, const std::string& msg);
};
