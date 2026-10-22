
// Generated from CypherLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  CypherLexer : public antlr4::Lexer {
public:
  enum {
    ASSIGN = 1, ADD_ASSIGN = 2, LE = 3, GE = 4, GT = 5, LT = 6, NOT_EQUAL = 7, 
    RANGE = 8, SEMI = 9, DOT = 10, COMMA = 11, LPAREN = 12, RPAREN = 13, 
    LBRACE = 14, RBRACE = 15, LBRACK = 16, RBRACK = 17, SUB = 18, PLUS = 19, 
    DIV = 20, MOD = 21, CARET = 22, MULT = 23, ESC = 24, COLON = 25, COLONCOLON = 26, 
    STICK = 27, DOLLAR = 28, CALL = 29, YIELD = 30, FILTER = 31, EXTRACT = 32, 
    COUNT = 33, ANY = 34, NONE = 35, SINGLE = 36, REDUCE = 37, SHORTESTPATH = 38, 
    ALLSHORTESTPATHS = 39, ALL = 40, ASC = 41, ASCENDING = 42, BY = 43, 
    CREATE = 44, DELETE = 45, DESC = 46, DESCENDING = 47, DETACH = 48, EXISTS = 49, 
    EXPLAIN = 50, LIMIT = 51, MATCH = 52, MERGE = 53, ON = 54, OPTIONAL = 55, 
    ORDER = 56, REMOVE = 57, RETURN = 58, SET = 59, SKIP_W = 60, WHERE = 61, 
    WITH = 62, UNION = 63, UNWIND = 64, AND = 65, AS = 66, CONTAINS = 67, 
    DISTINCT = 68, ENDS = 69, IN = 70, IS = 71, NOT = 72, OR = 73, STARTS = 74, 
    XOR = 75, FALSE = 76, TRUE = 77, NULL_W = 78, CONSTRAINT = 79, DO = 80, 
    FOR = 81, REQUIRE = 82, UNIQUE = 83, CASE = 84, WHEN = 85, THEN = 86, 
    ELSE = 87, END = 88, MANDATORY = 89, SCALAR = 90, OF = 91, ADD = 92, 
    DROP = 93, FOREACH = 94, ID = 95, ESC_LITERAL = 96, CHAR_LITERAL = 97, 
    STRING_LITERAL = 98, DIGIT = 99, FLOAT = 100, WS = 101, COMMENT = 102, 
    LINE_COMMENT = 103, ERRCHAR = 104, Letter = 105
  };

  enum {
    COMMENTS = 2
  };

  explicit CypherLexer(antlr4::CharStream *input);

  ~CypherLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

