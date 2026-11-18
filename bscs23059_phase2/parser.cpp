// Citu syntax analyzer (table-driven LL(1) parser).
// Reads output/tokens.txt (written by the lexer), validates it against the Citu
// grammar, prints the grammar / FIRST / FOLLOW / parsing table / parsing trace,


#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <utility>
#include <vector>

using namespace std;

// ---------- Data structures (Data_Structures documentation) ----------

struct Token {
    string type;   // token type from tokens.txt, e.g. IDENTIFIER, KEYWORD
    string value;  // lexeme
    int line;      // source line number
};

struct TraceStep {
    string stackContents;
    string remainingInput;
    string action;
};

// Node of the parse tree. Terminals carry the matched lexeme in `value`;
// a nonterminal with no children was expanded to epsilon.
struct ParseNode {
    string type;
    string value;
    bool terminal;
    vector<ParseNode*> children;
};

// ---------- Grammar (Citu_Grammar, section 4, rewritten for LL(1)) ----------
// Repetitions { ... } of the EBNF become right-recursive *_TAIL nonterminals.
// An empty alternative means epsilon.

const string START_SYMBOL = "PROGRAM";

const vector<string> nonterminalOrder = {
    "PROGRAM", "STATEMENT_LIST", "STATEMENT_LIST_TAIL", "STATEMENT", "DECLARATION",
    "IDENTIFIER_LIST", "IDENTIFIER_LIST_TAIL", "INPUT_STATEMENT", "OUTPUT_STATEMENT",
    "OUTPUT_VALUE", "ASSIGNMENT_STATEMENT", "IF_STATEMENT", "ELSE_PART",
    "WHILE_STATEMENT", "BLOCK", "CONDITION", "RELATIONAL_OPERATOR",
    "EXPRESSION", "EXPRESSION_TAIL", "TERM", "TERM_TAIL", "FACTOR",
};

const map<string, vector<vector<string>>> grammar = {
    {"PROGRAM", {{"shuru", "STATEMENT_LIST", "khatam"}}},
    {"STATEMENT_LIST", {{"STATEMENT", "STATEMENT_LIST_TAIL"}}},
    {"STATEMENT_LIST_TAIL", {{"STATEMENT", "STATEMENT_LIST_TAIL"}, {}}},
    {"STATEMENT", {{"DECLARATION"}, {"INPUT_STATEMENT"}, {"OUTPUT_STATEMENT"},
                   {"ASSIGNMENT_STATEMENT"}, {"IF_STATEMENT"}, {"WHILE_STATEMENT"}}},
    {"DECLARATION", {{"int", "IDENTIFIER_LIST", ";"}}},
    {"IDENTIFIER_LIST", {{"IDENTIFIER", "IDENTIFIER_LIST_TAIL"}}},
    {"IDENTIFIER_LIST_TAIL", {{",", "IDENTIFIER", "IDENTIFIER_LIST_TAIL"}, {}}},
    {"INPUT_STATEMENT", {{"padho", "(", "IDENTIFIER", ")", ";"}}},
    {"OUTPUT_STATEMENT", {{"likho", "(", "OUTPUT_VALUE", ")", ";"}}},
    {"OUTPUT_VALUE", {{"STRING_LITERAL"}, {"EXPRESSION"}}},
    {"ASSIGNMENT_STATEMENT", {{"IDENTIFIER", "=", "EXPRESSION", ";"}}},
    {"IF_STATEMENT", {{"agar", "(", "CONDITION", ")", "BLOCK", "ELSE_PART"}}},
    {"ELSE_PART", {{"warna", "BLOCK"}, {}}},
    {"WHILE_STATEMENT", {{"jabtak", "(", "CONDITION", ")", "BLOCK"}}},
    {"BLOCK", {{"{", "STATEMENT_LIST", "}"}}},
    {"CONDITION", {{"EXPRESSION", "RELATIONAL_OPERATOR", "EXPRESSION"}}},
    {"RELATIONAL_OPERATOR", {{"=="}, {"!="}, {"<"}, {">"}, {"<="}, {">="}}},
    {"EXPRESSION", {{"TERM", "EXPRESSION_TAIL"}}},
    {"EXPRESSION_TAIL", {{"+", "TERM", "EXPRESSION_TAIL"}, {"-", "TERM", "EXPRESSION_TAIL"}, {}}},
    {"TERM", {{"FACTOR", "TERM_TAIL"}}},
    {"TERM_TAIL", {{"*", "FACTOR", "TERM_TAIL"}, {"/", "FACTOR", "TERM_TAIL"},
                   {"%", "FACTOR", "TERM_TAIL"}, {}}},
    {"FACTOR", {{"IDENTIFIER"}, {"INTEGER_LITERAL"}, {"(", "EXPRESSION", ")"}}},
};

// ---------- Helpers ----------

// Identifiers and literals are matched by token type; every other token
// (keywords, operators, delimiters) is matched by its lexeme.
string terminalOf(const Token& token) {
    if (token.type == "IDENTIFIER" || token.type == "INTEGER_LITERAL" || token.type == "STRING_LITERAL")
        return token.type;
    return token.value;
}

string productionText(const string& lhs, const vector<string>& rhs) {
    if (rhs.empty()) return lhs + " -> epsilon";
    string out = lhs + " ->";
    for (const string& symbol : rhs) out += " " + symbol;
    return out;
}

string describe(const Token& token) {
    return token.value == "$" ? "end of input" : "'" + token.value + "'";
}

// The stack is copied so its contents can be printed; the top is on the right.
string stackToString(stack<string> s) {
    vector<string> items;
    while (!s.empty()) { items.push_back(s.top()); s.pop(); }
    string out;
    for (auto it = items.rbegin(); it != items.rend(); ++it) out += *it + ' ';
    return out;
}

string remainingText(const vector<Token>& tokens, size_t from, size_t maxShown = 6) {
    string out;
    for (size_t i = from; i < tokens.size() && i < from + maxShown; ++i) out += tokens[i].value + ' ';
    if (tokens.size() > from + maxShown) out += "...";
    return out;
}

// ---------- Reading tokens.txt ----------
// Each line is: TOKEN_TYPE | LEXEME | LINE_NUMBER
// The lexeme may itself contain " | " (inside a string literal), so the type is
// taken up to the first separator and the line number after the last one.

bool readTokens(const string& path, vector<Token>& tokens) {
    ifstream in(path);
    if (!in) {
        cerr << "Could not open " << path << ". Run the lexer first.\n";
        return false;
    }
    string text;
    int lineNo = 0;
    while (getline(in, text)) {
        ++lineNo;
        if (text.empty()) continue;
        size_t first = text.find(" | "), last = text.rfind(" | ");
        if (first == string::npos || first == last) {
            cerr << path << ":" << lineNo << ": malformed token line\n";
            return false;
        }
        try {
            tokens.push_back({text.substr(0, first),
                              text.substr(first + 3, last - first - 3),
                              stoi(text.substr(last + 3))});
        } catch (...) {
            cerr << path << ":" << lineNo << ": invalid line number\n";
            return false;
        }
    }
    return true;
}

// ---------- Parse tree output (Intermediate_File_Format, section 3) ----------
// The raw derivation contains helper nodes (*_TAIL, STATEMENT, ...). They are
// folded away so the written tree shows the structure of the program.

struct ShownNode {
    string text;
    vector<ShownNode> children;
};

ShownNode terminalShown(const ParseNode* n, bool valueOnly = false) {
    if (valueOnly) return {n->value, {}};
    if (n->type == "IDENTIFIER" || n->type == "INTEGER_LITERAL" || n->type == "STRING_LITERAL")
        return {n->type, {{n->value, {}}}};
    return {n->type, {}};
}

void append(vector<ShownNode>& target, vector<ShownNode> items) {
    for (ShownNode& item : items) target.push_back(move(item));
}

vector<ShownNode> show(const ParseNode* n) {
    if (n->terminal) return {terminalShown(n)};
    if (n->children.empty()) return {};  // epsilon

    const string& t = n->type;
    if (t == "STATEMENT" || t == "OUTPUT_VALUE" || t == "RELATIONAL_OPERATOR")
        return show(n->children[0]);
    if (t == "FACTOR" && n->children.size() == 1)
        return show(n->children[0]);

    ShownNode out{t, {}};

    if (t == "ELSE_PART") {
        vector<ShownNode> spliced;
        for (const ParseNode* c : n->children) append(spliced, show(c));
        return spliced;
    }
    if (t == "STATEMENT_LIST") {
        // STATEMENT_LIST and STATEMENT_LIST_TAIL both look like: STATEMENT TAIL
        for (const ParseNode* cur = n; cur && !cur->children.empty();
             cur = cur->children.size() > 1 ? cur->children[1] : nullptr)
            append(out.children, show(cur->children[0]));
        return {out};
    }
    if (t == "IDENTIFIER_LIST") {
        out.children.push_back(terminalShown(n->children[0], true));
        const ParseNode* tail = n->children[1];
        while (!tail->children.empty()) {
            append(out.children, show(tail->children[0]));                // ","
            out.children.push_back(terminalShown(tail->children[1], true));
            tail = tail->children[2];
        }
        return {out};
    }
    if (t == "EXPRESSION" || t == "TERM") {
        vector<ShownNode> parts = show(n->children[0]);
        const ParseNode* tail = n->children[1];
        while (!tail->children.empty()) {
            append(parts, show(tail->children[0]));  // operator
            append(parts, show(tail->children[1]));  // operand
            tail = tail->children[2];
        }
        if (parts.size() == 1) return parts;
        out.children = move(parts);
        return {out};
    }

    for (const ParseNode* c : n->children) append(out.children, show(c));
    return {out};
}

void printShown(ostream& out, const ShownNode& n, int depth) {
    out << string(4 * depth, ' ') << "|-- " << n.text << "\n";
    for (const ShownNode& c : n.children) printShown(out, c, depth + 1);
}

bool writeParseTree(const ParseNode* root, const string& path) {
    ofstream out(path);
    if (!out) return false;
    vector<ShownNode> shown = show(root);
    out << shown[0].text << "\n";
    for (const ShownNode& c : shown[0].children) printShown(out, c, 0);
    return true;
}

// ---------- LL(1) parser ----------

class LL1Parser {
    set<string> nullable;
    map<string, set<string>> first, follow;
    map<pair<string, string>, vector<string>> parsingTable;
    vector<TraceStep> parsingTrace;
    deque<ParseNode> nodePool;  // owns every ParseNode; addresses stay valid
    ParseNode* root = nullptr;

    bool isNonterminal(const string& symbol) const { return grammar.count(symbol) != 0; }

    set<string> firstOf(const vector<string>& sequence, bool& canBeEmpty) const {
        set<string> result;
        canBeEmpty = true;
        for (const string& symbol : sequence) {
            if (!isNonterminal(symbol)) {
                result.insert(symbol);
                canBeEmpty = false;
                return result;
            }
            result.insert(first.at(symbol).begin(), first.at(symbol).end());
            if (!nullable.count(symbol)) {
                canBeEmpty = false;
                return result;
            }
        }
        return result;
    }

    static bool addAll(set<string>& target, const set<string>& source) {
        size_t oldSize = target.size();
        target.insert(source.begin(), source.end());
        return target.size() != oldSize;
    }

    void addTableEntry(const string& nt, const string& terminal, const vector<string>& rhs) {
        auto inserted = parsingTable.emplace(make_pair(nt, terminal), rhs);
        if (!inserted.second && inserted.first->second != rhs) {
            cerr << "Grammar is not LL(1): conflict in [" << nt << ", " << terminal << "]\n";
            exit(1);
        }
    }

    ParseNode* newNode(const string& type, bool terminal) {
        nodePool.push_back({type, "", terminal, {}});
        return &nodePool.back();
    }

    string expectedFor(const string& nt) const {
        string out;
        for (const auto& entry : parsingTable)
            if (entry.first.first == nt) out += (out.empty() ? "" : ", ") + entry.first.second;
        return out;
    }

public:
    LL1Parser() {
        for (const string& nt : nonterminalOrder) {
            first[nt];
            follow[nt];
        }

        // FIRST sets and nullable nonterminals.
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& [lhs, alternatives] : grammar)
                for (const vector<string>& rhs : alternatives) {
                    bool canBeEmpty;
                    changed |= addAll(first[lhs], firstOf(rhs, canBeEmpty));
                    if (canBeEmpty) changed |= nullable.insert(lhs).second;
                }
        }

        // FOLLOW sets.
        follow[START_SYMBOL].insert("$");
        changed = true;
        while (changed) {
            changed = false;
            for (const auto& [lhs, alternatives] : grammar)
                for (const vector<string>& rhs : alternatives)
                    for (size_t i = 0; i < rhs.size(); ++i) {
                        if (!isNonterminal(rhs[i])) continue;
                        vector<string> suffix(rhs.begin() + i + 1, rhs.end());
                        bool suffixCanBeEmpty;
                        changed |= addAll(follow[rhs[i]], firstOf(suffix, suffixCanBeEmpty));
                        if (suffixCanBeEmpty) changed |= addAll(follow[rhs[i]], follow[lhs]);
                    }
        }

        // LL(1) parsing table.
        for (const auto& [lhs, alternatives] : grammar)
            for (const vector<string>& rhs : alternatives) {
                bool canBeEmpty;
                for (const string& terminal : firstOf(rhs, canBeEmpty))
                    addTableEntry(lhs, terminal, rhs);
                if (canBeEmpty)
                    for (const string& terminal : follow[lhs])
                        addTableEntry(lhs, terminal, rhs);
            }
    }

    const ParseNode* parseTree() const { return root; }

    void printGrammarAndSets() const {
        cout << "GRAMMAR\n";
        int number = 1;
        for (const string& nt : nonterminalOrder)
            for (const vector<string>& rhs : grammar.at(nt)) {
                cout << (number < 10 ? " " : "") << number << ". " << productionText(nt, rhs) << '\n';
                ++number;
            }

        auto join = [](const set<string>& items, const string& extra = "") {
            string out;
            for (const string& t : items) out += (out.empty() ? "" : ", ") + t;
            if (!extra.empty()) out += (out.empty() ? "" : ", ") + extra;
            return out;
        };

        cout << "\nFIRST / FOLLOW\n";
        for (const string& nt : nonterminalOrder)
            cout << nt << "  FIRST = { " << join(first.at(nt), nullable.count(nt) ? "epsilon" : "")
                 << " }  FOLLOW = { " << join(follow.at(nt)) << " }\n";
    }

    void printTable() const {
        cout << "\nPARSING TABLE (entry: [nonterminal, lookahead] -> production)\n";
        for (const string& nt : nonterminalOrder)
            for (const auto& entry : parsingTable)
                if (entry.first.first == nt)
                    cout << "[" << nt << ", " << entry.first.second << "]  "
                         << productionText(nt, entry.second) << '\n';
    }

    void printTrace() const {
        cout << "\nPARSING TRACE (stack top is on the right)\n";
        cout << "STEP | STACK | INPUT | ACTION\n";
        for (size_t i = 0; i < parsingTrace.size(); ++i)
            cout << i + 1 << " | " << parsingTrace[i].stackContents << "| "
                 << parsingTrace[i].remainingInput << "| " << parsingTrace[i].action << '\n';
    }

    bool parse(vector<Token> tokens) {
        parsingTrace.clear();
        nodePool.clear();
        tokens.push_back({"END", "$", tokens.empty() ? 1 : tokens.back().line});

        stack<string> parseStack;
        stack<ParseNode*> nodeStack;  // runs in parallel with parseStack
        root = newNode(START_SYMBOL, false);
        parseStack.push("$");
        nodeStack.push(nullptr);
        parseStack.push(START_SYMBOL);
        nodeStack.push(root);

        size_t inputIndex = 0;
        while (!parseStack.empty()) {
            string top = parseStack.top();
            ParseNode* node = nodeStack.top();
            const Token& current = tokens[inputIndex];
            string lookahead = terminalOf(current);
            string stackText = stackToString(parseStack);
            string inputText = remainingText(tokens, inputIndex);
            string error;

            if (!isNonterminal(top)) {
                if (top != lookahead) {
                    error = "expected " + (top == "$" ? string("end of input") : "'" + top + "'") +
                            " but found " + describe(current);
                } else {
                    parsingTrace.push_back({stackText, inputText,
                                            top == "$" ? "accept" : "match " + current.value});
                    if (node) node->value = current.value;
                    parseStack.pop();
                    nodeStack.pop();
                    ++inputIndex;
                    continue;
                }
            } else {
                auto it = parsingTable.find({top, lookahead});
                if (it == parsingTable.end()) {
                    error = "unexpected " + describe(current) + " (expected one of: " + expectedFor(top) + ")";
                } else {
                    const vector<string>& rhs = it->second;
                    parsingTrace.push_back({stackText, inputText, productionText(top, rhs)});
                    parseStack.pop();
                    nodeStack.pop();
                    vector<ParseNode*> kids;
                    for (const string& symbol : rhs) {
                        ParseNode* child = newNode(symbol, !isNonterminal(symbol));
                        node->children.push_back(child);
                        kids.push_back(child);
                    }
                    for (size_t i = rhs.size(); i-- > 0;) {
                        parseStack.push(rhs[i]);
                        nodeStack.push(kids[i]);
                    }
                    continue;
                }
            }

            parsingTrace.push_back({stackText, inputText, "ERROR: " + error});
            cerr << "Syntax error at line " << current.line << ": " << error << "\n";
            return false;
        }
        return true;
    }
};

int main(int argc, char* argv[]) {
    const string tokensPath = argc > 1 ? argv[1] : "output/tokens.txt";
    const string treePath = "output/parse_tree.txt";

    LL1Parser parser;
    parser.printGrammarAndSets();
    parser.printTable();

    vector<Token> tokens;
    if (!readTokens(tokensPath, tokens)) return 1;

    bool accepted = parser.parse(tokens);
    parser.printTrace();

    filesystem::create_directories("output");
    if (accepted && writeParseTree(parser.parseTree(), treePath)) {
        cout << "\nAccepted: valid Citu program. Parse tree written to " << treePath << "\n";
        return 0;
    }

    // Remove any older parse tree so later stages cannot read stale output.
    filesystem::remove(treePath);
    cout << "\nRejected: invalid Citu program. " << treePath << " was not generated.\n";
    return 1;
}