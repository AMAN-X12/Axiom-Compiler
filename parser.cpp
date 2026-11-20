#include "lexer.cpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

// ---------------------------------------------------------------- parser state
vector<Token>  toks;        // tokens from the lexer
vector<string> sym;         // the grammar symbol of each token     
size_t pos = 0;             // index of the lookahead token
bool   showTree = false;    // print the parse tree while parsing? (long: toggle with menu option 5)
int    depth = 0;           // current depth, used only to indent the tree

struct SyntaxError { int line, col; string msg; };

// NEW: every syntax error found is stored here instead of stopping at the first one.
vector<SyntaxError> syntaxErrors;

// NEW: positions (line, column) of the lexical errors, so the same mistake is not reported twice.
vector<pair<int,int>> lexPositions;

// NEW: remember an error (ignores a second error reported at exactly the same place).
void record(const SyntaxError& e) {
    for (const auto& p : lexPositions) if (p.first == e.line && p.second == e.col) return;   // lexer already reported this spot
    if (!syntaxErrors.empty() && syntaxErrors.back().line == e.line && syntaxErrors.back().col == e.col) return;
    syntaxErrors.push_back(e);
}

// The grammar symbol of a token: names are ID, numbers are NUMBER, ... and every
// keyword / operator is just its own text (chk, :=, ;; ...). End of file is "$".
string symbolOf(const Token& t) {
    switch (t.type) {
        case TokenType::END_OF_FILE:  return "$";
        case TokenType::IDENTIFIER:   return "ID";
        case TokenType::NUMBER:       return "NUMBER";
        case TokenType::DOTTEDNUMBER: return "DOTTEDNUMBER";
        case TokenType::WORD:         return "WORD";
        case TokenType::CHAR:         return "CHAR";
        case TokenType::HEADER:       return "HEADER";
        case TokenType::INVALID:      return "NUMBER";   // malformed number: already reported by the lexer, parse it as a number so it is not reported twice
        default:                      return t.lexeme;
    }
}

string look() { return sym[pos]; }                       // the lookahead symbol

// Prints the non-terminal's name when its function starts (builds the tree view).
struct Rule {
    Rule(const char* name) { if (showTree) cout << string(depth * 2, ' ') << name << "\n"; depth++; }
    ~Rule() { depth--; }
};

// Describes the current token for error messages.
string found() {
    const Token& t = toks[pos];
    if (t.type == TokenType::END_OF_FILE) return "end of file";
    return "'" + t.lexeme + "'";
}

[[noreturn]] void fail(const string& expected) {
    throw SyntaxError{toks[pos].line, toks[pos].col, "expected " + expected + " but found " + found()};
}

// Consume the lookahead if it is `x`, otherwise report a syntax error.
void match(const string& x) {
    if (look() != x) {
        if (x == ";;" && pos > 0) {   // a missing ;; is reported right after the previous token
            const Token& p = toks[pos - 1];
            throw SyntaxError{p.line, p.col + (int)p.lexeme.size(), "expected ';;' but found " + found()};
        }
        fail(x == "ID" ? "a name" : x == "HEADER" ? "a library name like <iostream>" : "'" + x + "'");
    }
    if (showTree) cout << string(depth * 2, ' ') << (x == "ID" || x == "NUMBER" ? x + " " + toks[pos].lexeme : x) << "\n";
    pos++;
}

// ---------------------------------------------------------------- lookahead tests
bool isOneOf(const string& s, initializer_list<const char*> list) {
    for (const char* x : list) if (s == x) return true;
    return false;
}
bool isType()      { return isOneOf(look(), {"number","dottednumber","char","flag","word","non","vector","align","pair","join"}); }
bool isExprStart() { return isOneOf(look(), {"ID","NUMBER","DOTTEDNUMBER","WORD","CHAR","true","false","Null","(","not","-","~"}); }
bool isAssignOp()  { return isOneOf(look(), {":=","+:=","-:=","*:=","/:=","%:="}); }

// ---------------------------------------------------------------- NEW: error recovery helpers
// When a rule fails, the error is recorded and the parser skips ahead to a "safe" place
// (end of the statement, the start of the next statement, or the start of the next function),
// then keeps parsing so that later errors are found too. The grammar rules themselves are unchanged.

bool isTypeSym(const string& s) {
    return isOneOf(s, {"number","dottednumber","char","flag","word","non","vector","align","pair","join"});
}

// can this symbol begin a statement?
bool isStmtStart(const string& s) {
    return isTypeSym(s) || isOneOf(s, {"make","ID","chk","choose","begin","try","return","print","input","inc","dec","break","continue","["});
}

// keywords that clearly begin a new statement (ID, '[' and inc/dec/break/continue are left out
// on purpose: they can also appear in the middle of a broken statement)
bool isStmtKeyword(const string& s) {
    return isTypeSym(s) || isOneOf(s, {"make","chk","choose","begin","try","return","print","input"});
}

// does a top-level item (function or struct) begin at token i?
bool atItemStart(size_t i) {
    if (i >= sym.size()) return false;
    if (sym[i] == "make") return i + 2 < sym.size() && sym[i + 1] == "ID" && sym[i + 2] == "[";
    if (!isTypeSym(sym[i])) return false;
    size_t j = i + 1;
    if (isOneOf(sym[i], {"vector","align","pair","join"}) && j < sym.size() && sym[j] == "<") {   // skip < ... >
        int d = 0;
        for (; j < sym.size() && sym[j] != "$"; j++) {
            if (sym[j] == "<") d++;
            else if (sym[j] == ">") d--;
            else if (sym[j] == ">>") d -= 2;
            if (d <= 0) { j++; break; }
        }
    }
    return j < sym.size() && (sym[j] == "func" || sym[j] == "mainClass");
}

// After an error inside a statement that began at token `start`: skip to a safe place.
void syncStmt(size_t start, const string& msg) {
    bool missingSemi = msg.rfind("expected ';;'", 0) == 0;
    // a loop whose header is broken: skip the whole loop up to its endfor / endwhile
    // (an error inside the loop body never gets here, it is recovered inside the body)
    if (sym[start] == "begin" && msg.find("'endfor'") == string::npos && msg.find("'endwhile'") == string::npos) {
        int d = 0;
        while (look() != "$") {
            string t = look();
            if (t == "begin") d++;
            else if (t == "endfor" || t == "endwhile") { if (d == 0) { pos++; break; } d--; }
            pos++;
        }
        return;
    }
    // "expected ;;": the next statement usually begins right here, so do not skip it
    if (missingSemi && pos > start && isStmtStart(look())) return;
    while (look() != "$") {
        string t = look();
        if (t == ";;") { pos++; break; }                                            // end of the broken statement
        if (isOneOf(t, {"]","endfor","endwhile","case","default"})) break;          // end of the enclosing block / clause
        if (pos > start && (isStmtKeyword(t) || t == "[")) break;                   // the next statement (or a block) starts here
        pos++;
    }
    if (pos == start && look() != "$") pos++;                                       // always make progress
}

// After an error inside a function / struct that began at token `start`: skip to the next one.
void syncItem(size_t start) {
    if (pos == start && look() != "$") pos++;                                       // always make progress
    while (look() != "$" && !atItemStart(pos)) pos++;
}

// Forward declarations (the rules call each other).
void Program(); void UseList(); void UseDecl(); void ItemList(); void Item(); void ItemTail(); void StructDecl();
void FieldList(); void Params(); void ParamsTail(); void Param(); void Type(); void TypeTail(); void Block();
void StmtList(); void Stmt(); void VarDecl(); void VarTail(); void IdStmt(); void IdTail(); void Access();
void IdEnd(); void AssignOp(); void PrintStmt(); void InputStmt(); void IfStmt(); void ElsePart(); void ElseBody();
void SwitchStmt(); void CaseList(); void CaseClause(); void LoopStmt(); void LoopKind(); void ForInit();
void ForCond(); void ForStep(); void TryStmt(); void ReturnStmt(); void ReturnVal(); void Args(); void ArgsTail();
void Expr(); void TernaryOpt(); void OrExpr(); void OrRest(); void AndExpr(); void AndRest(); void BitExpr();
void BitRest(); void BitOp(); void EqExpr(); void EqRest(); void RelExpr(); void RelRest(); void RelOp();
void AddExpr(); void AddRest(); void AddOp(); void MulExpr(); void MulRest(); void MulOp(); void Unary();
void Primary(); void PrimaryTail(); void ParenBody();

// ================================================================ THE GRAMMAR RULES
// Each function below is one rule of the grammar. The rule is written above it.
// An "| ε" alternative becomes an `if`: if the lookahead can start the rule, use it, else do nothing.

// Program → UseList ItemList
void Program() { Rule r("Program"); UseList(); ItemList(); }

// UseList → UseDecl UseList | ε
// (recovery: a bad USE line is recorded, then we skip to the next USE or the first function)
void UseList() {
    Rule r("UseList");
    if (look() == "USE") {
        try { UseDecl(); }
        catch (const SyntaxError& e) {
            record(e);
            while (look() != "$" && look() != "USE" && !atItemStart(pos)) pos++;
        }
        UseList();
    }
}

// UseDecl → USE HEADER
void UseDecl() { Rule r("UseDecl"); match("USE"); match("HEADER"); }

// ItemList → Item ItemList | ε
// (recovery: a broken function / struct is recorded, then we skip to the next one;
//  a stray token between items is reported and skipped)
void ItemList() {
    Rule r("ItemList");
    if (isType() || look() == "make") {
        size_t start = pos;
        try { Item(); }
        catch (const SyntaxError& e) { record(e); syncItem(start); }
        ItemList();
    } else if (look() != "$") {
        record(SyntaxError{toks[pos].line, toks[pos].col, "expected a function, struct or end of file but found " + found()});
        syncItem(pos);
        ItemList();
    }
}

// Item → Type ItemTail | StructDecl
void Item() { Rule r("Item"); if (isType()) { Type(); ItemTail(); } else StructDecl(); }

// ItemTail → func ID ( Params ) Block | mainClass ( ) Block
void ItemTail() {
    Rule r("ItemTail");
    if (look() == "func") { match("func"); match("ID"); match("("); Params(); match(")"); Block(); }
    else if (look() == "mainClass") { match("mainClass"); match("("); match(")"); Block(); }
    else fail("'func' or 'mainClass'");
}

// StructDecl → make ID [ FieldList ] ;;
void StructDecl() { Rule r("StructDecl"); match("make"); match("ID"); match("["); FieldList(); match("]"); match(";;"); }

// FieldList → Type ID ;; FieldList | ε
void FieldList() { Rule r("FieldList"); if (isType()) { Type(); match("ID"); match(";;"); FieldList(); } }

// Params → Param ParamsTail | ε
void Params() { Rule r("Params"); if (isType()) { Param(); ParamsTail(); } }

// ParamsTail → , Param ParamsTail | ε
void ParamsTail() { Rule r("ParamsTail"); if (look() == ",") { match(","); Param(); ParamsTail(); } }

// Param → Type ID
void Param() { Rule r("Param"); Type(); match("ID"); }

// closing '>' of a type; a '>>' token is split in two so vector<vector<number>> works
void closeAngle() {
    if (look() == ">>") { sym[pos] = ">"; if (showTree) cout << string(depth * 2, ' ') << ">\n"; }  // use half of '>>'
    else match(">");
}

// Type → number | dottednumber | char | flag | word | non | vector < Type > | align < Type >
//      | pair < Type , Type > | join < Type TypeTail >
void Type() {
    Rule r("Type");
    string t = look();
    if (isOneOf(t, {"number","dottednumber","char","flag","word","non"})) match(t);
    else if (t == "vector" || t == "align") { match(t); match("<"); Type(); closeAngle(); }
    else if (t == "pair") { match("pair"); match("<"); Type(); match(","); Type(); closeAngle(); }
    else if (t == "join") { match("join"); match("<"); Type(); TypeTail(); closeAngle(); }
    else fail("a type");
}

// TypeTail → , Type TypeTail | ε
void TypeTail() { Rule r("TypeTail"); if (look() == ",") { match(","); Type(); TypeTail(); } }

// Block → [ StmtList ]
void Block() { Rule r("Block"); match("["); StmtList(); match("]"); }

// StmtList → Stmt StmtList | ε
// (recovery: a broken statement is recorded, then we skip to the end of it and go on with the next one)
void StmtList() {
    Rule r("StmtList");
    if (isType() || isOneOf(look(), {"make","ID","chk","choose","begin","try","return","print","input","inc","dec","break","continue","["})) {
        size_t start = pos;
        try { Stmt(); }
        catch (const SyntaxError& e) {
            record(e);
            syncStmt(start, e.msg);
        }
        StmtList();
    }
    else if (look() == "otherwise" && !syntaxErrors.empty()) {
        // recovery only: the 'chk' before this 'otherwise' was broken, so skip the leftover keyword
        pos++;
        StmtList();
    }
}

// Stmt → VarDecl | IdStmt | IfStmt | SwitchStmt | LoopStmt | TryStmt | ReturnStmt | PrintStmt | InputStmt
//      | inc ID ;; | dec ID ;; | break ;; | continue ;; | Block
void Stmt() {
    Rule r("Stmt");
    string t = look();
    if (isType() || t == "make") VarDecl();
    else if (t == "ID")       IdStmt();
    else if (t == "chk")      IfStmt();
    else if (t == "choose")   SwitchStmt();
    else if (t == "begin")    LoopStmt();
    else if (t == "try")      TryStmt();
    else if (t == "return")   ReturnStmt();
    else if (t == "print")    PrintStmt();
    else if (t == "input")    InputStmt();
    else if (t == "inc" || t == "dec") { match(t); match("ID"); match(";;"); }
    else if (t == "break" || t == "continue") { match(t); match(";;"); }
    else if (t == "[")        Block();
    else fail("a statement");
}

// VarDecl → Type ID VarTail ;; | make ID ID ;;
void VarDecl() {
    Rule r("VarDecl");
    if (look() == "make") { match("make"); match("ID"); match("ID"); match(";;"); }
    else { Type(); match("ID"); VarTail(); match(";;"); }
}

// VarTail → := Expr | ( Expr ) | ε          (a value, or the size of an array)
void VarTail() {
    Rule r("VarTail");
    if (look() == ":=") { match(":="); Expr(); }
    else if (look() == "(") { match("("); Expr(); match(")"); }
}

// IdStmt → ID IdTail
void IdStmt() { Rule r("IdStmt"); match("ID"); IdTail(); }

// IdTail → ( Args ) ;; | Access IdEnd ;;
void IdTail() {
    Rule r("IdTail");
    if (look() == "(") { match("("); Args(); match(")"); match(";;"); }
    else { Access(); IdEnd(); match(";;"); }
}

// Access → { Expr } Access | . ID Access | ε          (array element or struct field)
void Access() {
    Rule r("Access");
    if (look() == "{") { match("{"); Expr(); match("}"); Access(); }
    else if (look() == ".") { match("."); match("ID"); Access(); }
}

// IdEnd → AssignOp Expr | inc | dec
void IdEnd() {
    Rule r("IdEnd");
    if (look() == "inc" || look() == "dec") match(look());
    else if (isAssignOp()) { AssignOp(); Expr(); }
    else fail("':=', '+:=', 'inc' or 'dec'");
}

// AssignOp → := | +:= | -:= | *:= | /:= | %:=
void AssignOp() { Rule r("AssignOp"); match(look()); }

// PrintStmt → print ( Args ) ;;
void PrintStmt() { Rule r("PrintStmt"); match("print"); match("("); Args(); match(")"); match(";;"); }

// InputStmt → input ( ID ) ;;
void InputStmt() { Rule r("InputStmt"); match("input"); match("("); match("ID"); match(")"); match(";;"); }

// IfStmt → chk ( Expr ) Block ElsePart
void IfStmt() { Rule r("IfStmt"); match("chk"); match("("); Expr(); match(")"); Block(); ElsePart(); }

// ElsePart → otherwise ElseBody | ε
void ElsePart() { Rule r("ElsePart"); if (look() == "otherwise") { match("otherwise"); ElseBody(); } }

// ElseBody → Block | IfStmt          (otherwise chk ... is an else-if)
void ElseBody() {
    Rule r("ElseBody");
    if (look() == "[") Block();
    else if (look() == "chk") IfStmt();
    else fail("'[' or 'chk'");
}

// SwitchStmt → choose ( Expr ) [ CaseList ]
void SwitchStmt() { Rule r("SwitchStmt"); match("choose"); match("("); Expr(); match(")"); match("["); CaseList(); match("]"); }

// CaseList → CaseClause CaseList | ε
void CaseList() { Rule r("CaseList"); if (look() == "case" || look() == "default") { CaseClause(); CaseList(); } }

// CaseClause → case Expr : StmtList | default : StmtList
void CaseClause() {
    Rule r("CaseClause");
    if (look() == "case") { match("case"); Expr(); match(":"); StmtList(); }
    else { match("default"); match(":"); StmtList(); }
}

// LoopStmt → begin LoopKind
void LoopStmt() { Rule r("LoopStmt"); match("begin"); LoopKind(); }

// recovery: a broken part of a loop header is recorded, then we skip to the next ';;' or ')'
// (not consuming it), so every part of the header is checked and the loop body is still parsed.
void skipHeaderPart() {
    while (look() != "$" && look() != ";;" && look() != ")" && look() != "]" && !isStmtKeyword(look())) pos++;
}

// LoopKind → for ( ForInit ;; ForCond ;; ForStep ) StmtList endfor | while ( Expr ) StmtList endwhile
void LoopKind() {
    Rule r("LoopKind");
    if (look() == "for") {
        match("for"); match("(");
        try { ForInit(); } catch (const SyntaxError& e) { record(e); skipHeaderPart(); }
        match(";;");
        try { ForCond(); } catch (const SyntaxError& e) { record(e); skipHeaderPart(); }
        match(";;");
        try { ForStep(); } catch (const SyntaxError& e) { record(e); skipHeaderPart(); }
        match(")");
        StmtList(); match("endfor");
    } else if (look() == "while") {
        match("while"); match("(");
        try { Expr(); } catch (const SyntaxError& e) { record(e); skipHeaderPart(); }
        match(")"); StmtList(); match("endwhile");
    } else fail("'for' or 'while'");
}

// ForInit → Type ID := Expr | ID := Expr | ε
void ForInit() {
    Rule r("ForInit");
    if (isType()) { Type(); match("ID"); match(":="); Expr(); }
    else if (look() == "ID") { match("ID"); match(":="); Expr(); }
}

// ForCond → Expr | ε
void ForCond() { Rule r("ForCond"); if (isExprStart()) Expr(); }

// ForStep → inc ID | dec ID | ID IdEnd | ε
void ForStep() {
    Rule r("ForStep");
    if (look() == "inc" || look() == "dec") { match(look()); match("ID"); }
    else if (look() == "ID") { match("ID"); IdEnd(); }
}

// TryStmt → try Block catch Block
void TryStmt() { Rule r("TryStmt"); match("try"); Block(); match("catch"); Block(); }

// ReturnStmt → return ReturnVal ;;
void ReturnStmt() { Rule r("ReturnStmt"); match("return"); ReturnVal(); match(";;"); }

// ReturnVal → Expr | ε
void ReturnVal() { Rule r("ReturnVal"); if (isExprStart()) Expr(); }

// Args → Expr ArgsTail | ε
void Args() { Rule r("Args"); if (isExprStart()) { Expr(); ArgsTail(); } }

// ArgsTail → , Expr ArgsTail | ε
void ArgsTail() { Rule r("ArgsTail"); if (look() == ",") { match(","); Expr(); ArgsTail(); } }

// ---------------------------------------------------------------- expressions
// One rule per precedence level: lowest first (?: , or, and, bit, chkEqual, relational, + -, * /, unary).
// The "...Rest" rules repeat the operator, which makes the operators left-associative.

// Expr → OrExpr TernaryOpt
void Expr() { Rule r("Expr"); OrExpr(); TernaryOpt(); }

// TernaryOpt → ? Expr : Expr | ε
void TernaryOpt() { Rule r("TernaryOpt"); if (look() == "?") { match("?"); Expr(); match(":"); Expr(); } }

// OrExpr → AndExpr OrRest        OrRest → or AndExpr OrRest | ε
void OrExpr() { Rule r("OrExpr"); AndExpr(); OrRest(); }
void OrRest() { Rule r("OrRest"); if (look() == "or") { match("or"); AndExpr(); OrRest(); } }

// AndExpr → BitExpr AndRest      AndRest → and BitExpr AndRest | ε
void AndExpr() { Rule r("AndExpr"); BitExpr(); AndRest(); }
void AndRest() { Rule r("AndRest"); if (look() == "and") { match("and"); BitExpr(); AndRest(); } }

// BitExpr → EqExpr BitRest       BitRest → BitOp EqExpr BitRest | ε       BitOp → & | ^ | << | >>
void BitExpr() { Rule r("BitExpr"); EqExpr(); BitRest(); }
void BitRest() { Rule r("BitRest"); if (isOneOf(look(), {"&","^","<<",">>"})) { BitOp(); EqExpr(); BitRest(); } }
void BitOp()   { Rule r("BitOp"); match(look()); }

// EqExpr → RelExpr EqRest        EqRest → chkEqual RelExpr EqRest | ε
void EqExpr() { Rule r("EqExpr"); RelExpr(); EqRest(); }
void EqRest() { Rule r("EqRest"); if (look() == "chkEqual") { match("chkEqual"); RelExpr(); EqRest(); } }

// RelExpr → AddExpr RelRest      RelRest → RelOp AddExpr RelRest | ε      RelOp → < | > | <= | >=
void RelExpr() { Rule r("RelExpr"); AddExpr(); RelRest(); }
void RelRest() { Rule r("RelRest"); if (isOneOf(look(), {"<",">","<=",">="})) { RelOp(); AddExpr(); RelRest(); } }
void RelOp()   { Rule r("RelOp"); match(look()); }

// AddExpr → MulExpr AddRest      AddRest → AddOp MulExpr AddRest | ε      AddOp → + | -
void AddExpr() { Rule r("AddExpr"); MulExpr(); AddRest(); }
void AddRest() { Rule r("AddRest"); if (look() == "+" || look() == "-") { AddOp(); MulExpr(); AddRest(); } }
void AddOp()   { Rule r("AddOp"); match(look()); }

// MulExpr → Unary MulRest        MulRest → MulOp Unary MulRest | ε        MulOp → * | / | %
void MulExpr() { Rule r("MulExpr"); Unary(); MulRest(); }
void MulRest() { Rule r("MulRest"); if (isOneOf(look(), {"*","/","%"})) { MulOp(); Unary(); MulRest(); } }
void MulOp()   { Rule r("MulOp"); match(look()); }

// Unary → not Unary | - Unary | ~ Unary | Primary
void Unary() {
    Rule r("Unary");
    if (isOneOf(look(), {"not","-","~"})) { match(look()); Unary(); }
    else Primary();
}

// Primary → NUMBER | DOTTEDNUMBER | WORD | CHAR | true | false | Null | ID PrimaryTail | ( ParenBody
void Primary() {
    Rule r("Primary");
    string t = look();
    if (isOneOf(t, {"NUMBER","DOTTEDNUMBER","WORD","CHAR","true","false","Null"})) match(t);
    else if (t == "ID") { match("ID"); PrimaryTail(); }
    else if (t == "(") { match("("); ParenBody(); }
    else fail("an expression");
}

// PrimaryTail → ( Args ) | Access          (a function call, or array / struct access)
void PrimaryTail() {
    Rule r("PrimaryTail");
    if (look() == "(") { match("("); Args(); match(")"); }
    else Access();
}

// ParenBody → ) | Expr ArgsTail )          ( )  empty list,  ( e )  grouping,  ( e, e )  list
void ParenBody() {
    Rule r("ParenBody");
    if (look() == ")") match(")");
    else { Expr(); ArgsTail(); match(")"); }
}

// ================================================================ driver
// Parses the tokens. Returns true if the program follows the grammar.
// Now reports ALL syntax errors found, not just the first one.
bool parseProgram(const vector<Token>& tokens) {
    toks = tokens; pos = 0; depth = 0;
    sym.clear();
    syntaxErrors.clear();
    for (const Token& t : toks) sym.push_back(symbolOf(t));
    try {
        Program();
        if (look() != "$") fail("a function, struct or end of file");     // leftover tokens
    } catch (const SyntaxError& e) {
        record(e);
    }
    if (!syntaxErrors.empty()) {
        for (const SyntaxError& e : syntaxErrors)
            cout << "\nSyntax error at line " << e.line << ", column " << e.col << ": " << e.msg << "\n";
        cout << "\n" << syntaxErrors.size() << " syntax error" << (syntaxErrors.size() == 1 ? "" : "s") << " found.\n";
        return false;
    }
    cout << "\nSyntax OK: the program follows the Axiom grammar.\n";
    return true;
}

// Lexer first, then parser. Lexical errors do NOT stop the parser any more:
// both kinds of errors are reported in one run.
void analyze(const string& source) {
    Lexer lexer(source);
    vector<Token> tokens = lexer.tokenize(false);          // false = drop comments
    bool lexErrors = !lexer.errors().empty();
    lexPositions.clear();
    for (const LexError& e : lexer.errors()) lexPositions.push_back({e.line, e.col});
    if (lexErrors) {
        cout << "\nLexical errors (" << lexer.errors().size() << "):\n";
        for (const LexError& e : lexer.errors()) cout << "  line " << e.line << ", column " << e.col << ": " << e.message << "\n";
    }
    cout << "\nParse tree:\n";
    parseProgram(tokens);
    if (lexErrors)
        cout << "\n errrors in program \n";
}

const char* DEMO_OK =
    "USE <iostream>\n"
    "\n"
    "number func fact(number n) [\n"
    "    chk (n <= 1) [ return 1;; ]\n"
    "    return n * fact(n - 1);;\n"
    "]\n"
    "\n"
    "non mainClass() [\n"
    "    number sum := 0;;\n"
    "    begin for (number i := 0 ;; i < 5 ;; inc i)\n"
    "        sum +:= i;;\n"
    "    endfor\n"
    "    chk (sum > 5 and not (sum chkEqual 0)) [\n"
    "        print(\"Big\");;\n"
    "    ] otherwise [\n"
    "        print(\"Small\");;\n"
    "    ]\n"
    "]\n";

// Demo with SEVERAL errors, so the multi-error report can be seen.
const char* DEMO_ERR =
    "non mainClass() [\n"
    "    number x := 5\n"
    "    print(x);;\n"
    "    number y := ;;\n"
    "    chk (x > 1 [\n"
    "        print(x);;\n"
    "    ]\n"
    "    y := 3 +;;\n"
    "]\n";

string readFile(const string& path, bool& ok) {
    ifstream in(path, ios::binary);
    ok = (bool)in;
    stringstream ss;
    if (ok) ss << in.rdbuf();
    return ss.str();
}

#ifndef AXIOM_NO_MAIN
int main(int argc, char** argv) {
    if (argc >= 2) {                                        // command line:  parser file.axm
        bool ok;
        string src = readFile(argv[1], ok);
        if (!ok) { cerr << "cannot open " << argv[1] << "\n"; return 2; }
        analyze(src);
        return 0;
    }
    string choice;
    while (true) {
        cout << "\n=========== AXIOM PARSER ===========\n"
             << "  1) Type / paste Axiom code and parse it\n"
             << "  2) Parse a file (e.g. sample.axm)\n"
             << "  3) Demo: valid program\n"
             << "  4) Demo: program with a syntax error\n"
             << "  5) Show parse tree: " << (showTree ? "ON " : "OFF") << "  (press 5 to toggle)\n"
             << "  0) Exit\nChoose: ";
        if (!getline(cin, choice)) break;
        if (choice == "0") break;
        else if (choice == "1") {
            cout << "Enter Axiom code. Finish with a line containing only END\n";
            string line, src;
            while (getline(cin, line)) { if (line == "END" || line == "END\r") break; src += line + "\n"; }
            analyze(src);
        } else if (choice == "2") {
            cout << "File name: ";
            string path; getline(cin, path);
            bool ok; string src = readFile(path, ok);
            if (!ok) cout << "Cannot open '" << path << "'\n"; else analyze(src);
        } else if (choice == "3") { cout << "\n--- source ---\n" << DEMO_OK << "\n"; analyze(DEMO_OK); }
        else if (choice == "4")   { cout << "\n--- source ---\n" << DEMO_ERR << "\n"; analyze(DEMO_ERR); }
        else if (choice == "5")   showTree = !showTree;
        else if (!choice.empty()) cout << "Please choose 0-5.\n";
    }
    cout << "Bye.\n";
    return 0;
}
#endif