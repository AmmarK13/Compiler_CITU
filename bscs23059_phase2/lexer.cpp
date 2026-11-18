// Citu lexical analyzer.
// Reads a Citu source program and writes output/tokens.txt, one token per line:
//     TOKEN_TYPE | LEXEME | LINE_NUMBER
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <cctype>
#include <filesystem>

using namespace std;

struct Token {
    string type;
    string value;
    int line;
};

// ---------- Lexical rules (Citu_Grammar, section 3) ----------

bool isKeyword(const string& lexeme) {
    static regex re("^(shuru|khatam|int|agar|warna|jabtak|likho|padho)$");
    return regex_match(lexeme, re);
}

bool isIntegerLiteral(const string& lexeme) {
    static regex re("^[0-9]+$");
    return regex_match(lexeme, re);
}

bool isStringLiteral(const string& lexeme) {
    static regex re("^\"[^\"\\n]*\"$");
    return regex_match(lexeme, re);
}

bool isIdentifier(const string& lexeme) {
    static regex re("^[A-Za-z][A-Za-z0-9_]*$");
    return regex_match(lexeme, re);
}

bool isDelimiter(const string& lexeme) {
    static regex re("^[(){},;]$");
    return regex_match(lexeme, re);
}

bool isOperator(const string& lexeme) {
    static regex re("^(==|!=|<=|>=|[-+*/%<>=])$");
    return regex_match(lexeme, re);
}

// Token type names written to tokens.txt.
const map<string, string> keywordTypes = {
    {"shuru", "KEYWORD"}, {"khatam", "KEYWORD"}, {"int", "DATA_TYPE"},
    {"agar", "IF"}, {"warna", "ELSE"}, {"jabtak", "WHILE"},
    {"likho", "OUTPUT"}, {"padho", "INPUT"},
};

const map<string, string> operatorTypes = {
    {"+", "PLUS"}, {"-", "MINUS"}, {"*", "MULTIPLY"}, {"/", "DIVIDE"}, {"%", "MODULUS"},
    {"==", "EQUAL"}, {"!=", "NOT_EQUAL"}, {"<", "LESS_THAN"}, {">", "GREATER_THAN"},
    {"<=", "LESS_EQUAL"}, {">=", "GREATER_EQUAL"}, {"=", "ASSIGN"},
};

const map<string, string> delimiterTypes = {
    {"(", "LEFT_PAREN"}, {")", "RIGHT_PAREN"}, {"{", "LEFT_BRACE"},
    {"}", "RIGHT_BRACE"}, {",", "COMMA"}, {";", "SEMICOLON"},
};

// Rule 1 and 3 of the recognition rules: keywords are checked before identifiers.
string tokenType(const string& lexeme) {
    if (isKeyword(lexeme))        return keywordTypes.at(lexeme);
    if (isIdentifier(lexeme))     return "IDENTIFIER";
    if (isIntegerLiteral(lexeme)) return "INTEGER_LITERAL";
    if (isStringLiteral(lexeme))  return "STRING_LITERAL";
    if (isOperator(lexeme))       return operatorTypes.at(lexeme);
    if (isDelimiter(lexeme))      return delimiterTypes.at(lexeme);
    return "UNKNOWN";
}

// ---------- Scanner ----------

// Splits the source into tokens. Returns false if any lexical error was found;
// every error is reported with its line number.
bool tokenize(const string& src, vector<Token>& tokens) {
    bool ok = true;
    int line = 1;
    size_t i = 0, n = src.size();

    auto fail = [&](const string& message) {
        cerr << "Lexical error at line " << line << ": " << message << "\n";
        ok = false;
    };
    auto isWordChar = [&](size_t k) {
        return k < n && (isalnum((unsigned char)src[k]) || src[k] == '_');
    };

    while (i < n) {
        char c = src[i];

        // Whitespace is skipped (rule 5); newlines advance the line counter.
        if (c == '\n') { ++line; ++i; continue; }
        if (isspace((unsigned char)c)) { ++i; continue; }

        size_t start = i;

        if (isalpha((unsigned char)c)) {
            // Identifier or keyword.
            while (isWordChar(i)) ++i;
        } else if (isdigit((unsigned char)c)) {
            // Integer literal; digits followed by letters (e.g. 1num) are not a valid token.
            while (i < n && isdigit((unsigned char)src[i])) ++i;
            if (isWordChar(i)) {
                while (isWordChar(i)) ++i;
                fail("invalid token '" + src.substr(start, i - start) + "'");
                continue;
            }
        } else if (c == '"') {
            // String literal: must close on the same line.
            ++i;
            while (i < n && src[i] != '"' && src[i] != '\n') ++i;
            if (i >= n || src[i] != '"') {
                fail("unterminated string literal");
                continue;
            }
            ++i;
        } else {
            // Operators and delimiters: longest match first (rule 2).
            string two = src.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=") i += 2;
            else ++i;
        }

        string lexeme = src.substr(start, i - start);
        string type = tokenType(lexeme);
        if (type == "UNKNOWN") {
            fail("invalid character '" + lexeme + "'");
            continue;
        }
        tokens.push_back({type, lexeme, line});
    }
    return ok;
}

void drivingloop(const string& filename) {
    ifstream fp(filename);
    if (!fp) {
        cerr << "Could not open source file: " << filename << "\n";
        exit(1);
    }
    stringstream buffer;
    buffer << fp.rdbuf();

    const string outputPath = "output/tokens.txt";
    filesystem::create_directories("output");

    vector<Token> tokens;
    if (!tokenize(buffer.str(), tokens)) {
        // Remove any older tokens.txt so the parser cannot read stale output.
        filesystem::remove(outputPath);
        cerr << "Lexical analysis failed. " << outputPath << " was not generated.\n";
        exit(1);
    }

    ofstream out(outputPath);
    if (!out) {
        cerr << "Could not open output file: " << outputPath << "\n";
        exit(1);
    }
    for (const Token& t : tokens)
        out << t.type << " | " << t.value << " | " << t.line << "\n";

    cout << "Tokens written to " << outputPath << "\n";
}

// Quick checks of the lexical rules (not called by main).
int test_regexes() {
    auto yn = [](bool b) { return b ? "yes" : "no"; };

    cout << "Keywords: shuru=" << yn(isKeyword("shuru")) << " int=" << yn(isKeyword("int"))
         << " num=" << yn(isKeyword("num")) << " while=" << yn(isKeyword("while")) << "\n";
    cout << "Identifiers: num1=" << yn(isIdentifier("num1")) << " my_var=" << yn(isIdentifier("my_var"))
         << " 1num=" << yn(isIdentifier("1num")) << " _x=" << yn(isIdentifier("_x")) << "\n";
    cout << "Integers: 25=" << yn(isIntegerLiteral("25")) << " 2.5=" << yn(isIntegerLiteral("2.5")) << "\n";
    cout << "Strings: \"Hi\"=" << yn(isStringLiteral("\"Hi\"")) << " Hi=" << yn(isStringLiteral("Hi")) << "\n";
    cout << "Operators: <==" << yn(isOperator("<=")) << " ==" << yn(isOperator("==")) << " !=" << yn(isOperator("!="))
         << " &&=" << yn(isOperator("&&")) << " ++=" << yn(isOperator("++")) << "\n";
    cout << "Delimiters: (=" << yn(isDelimiter("(")) << " ;=" << yn(isDelimiter(";")) << " [=" << yn(isDelimiter("[")) << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <file.citu>\n";
        return 1;
    }

    drivingloop(argv[1]);
    return 0;
}