// Axiom LEXICAL ANALYZER - interactive demo
//   press RUN on this file, or:   g++ -std=c++17 parser.cpp -o parser
// (lexer.cpp is included below, so only this file is compiled)
#include "lexer.cpp"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
using namespace std;

bool showComments = true;

// Shows a lexeme on one line (newlines inside comments become \n).
string oneLine(const string& s) {
    string r;
    for (char c : s) r += (c == '\n') ? "\\n" : string(1, c);
    return r.size() > 40 ? r.substr(0, 37) + "..." : r;
}

void analyze(const string& source) {
    Lexer lexer(source);
    vector<Token> tokens = lexer.tokenize(showComments);

    map<TokenType, int> counts;              // how many tokens of each kind
    printf("\n%-8s %-14s %-24s %s\n", "LINE:COL", "TOKEN", "LEXEME", "DETAIL");
    printf("-----------------------------------------------------------------\n");
    for (const Token& t : tokens) {
        string where = to_string(t.line) + ":" + to_string(t.col);
        if (t.type == TokenType::END_OF_FILE) { printf("%-8s EOF\n", where.c_str()); continue; }
        counts[t.type]++;
        printf("%-8s %-14s %-24s %s\n", where.c_str(), tokenTypeName(t.type).c_str(), oneLine(t.lexeme).c_str(), t.detail.c_str());
    }

    printf("\nToken counts\n");
    for (auto& c : counts) printf("  %-14s %d\n", tokenTypeName(c.first).c_str(), c.second);

    if (lexer.errors().empty()) { printf("\nNo lexical errors.\n"); return; }
    printf("\nLexical errors: %d\n", (int)lexer.errors().size());
    for (const LexError& e : lexer.errors()) printf("  line %d, column %d: %s\n", e.line, e.col, e.message.c_str());
}

const char* DEMO_OK =
    "USE <iostream>\n"
    "/* reads n and sums 0..n-1\n"
    "number mainClass() [\n"
    "    number sum := 0;;\n"
    "    dottednumber pi := 3.14;;\n"
    "    begin for (number i := 0 ;; i < 10 ;; inc i)\n"
    "        sum +:= i;;\n"
    "    endfor\n"
    "    chk (sum > 10 and not (sum chkEqual 0)) [\n"
    "        print(\"Big\");;\n"
    "    ]\n"
    "]\n";

const char* DEMO_ERR =
    "number x = 5;;\n"
    "number 9abc := 1;;\n"
    "word s := \"unterminated;;\n"
    "flag f := a | b;;\n"
    "number y := 3;\n"
    "dottednumber d := 9.39.9;;\n"
    "/** never closed\n";

string readFile(const string& path, bool& ok) {
    ifstream in(path, ios::binary);
    ok = (bool)in;
    stringstream ss;
    if (ok) ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc >= 2) {                                         // command line:  lexer file.axm
        bool ok;
        string src = readFile(argv[1], ok);
        if (!ok) { cerr << "cannot open " << argv[1] << "\n"; return 2; }
        analyze(src);
        return 0;
    }
    string choice;
    while (true) {
        cout << "\n=========== AXIOM LEXICAL ANALYZER ===========\n"
             << "  1) Type / paste Axiom code and scan it\n"
             << "  2) Scan a file (e.g. sample.axm)\n"
             << "  3) Demo: valid program\n"
             << "  4) Demo: program with lexical errors\n"
             << "  5) Show comment tokens: " << (showComments ? "ON " : "OFF") << "  (press 5 to toggle)\n"
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
        } else if (choice == "3") { cout << "\n--- source ---\n" << DEMO_OK; analyze(DEMO_OK); }
        else if (choice == "4")   { cout << "\n--- source ---\n" << DEMO_ERR; analyze(DEMO_ERR); }
        else if (choice == "5")   showComments = !showComments;
        else if (!choice.empty()) cout << "Please choose 0-5.\n";
    }
    cout << "Bye.\n";
    return 0;
}
