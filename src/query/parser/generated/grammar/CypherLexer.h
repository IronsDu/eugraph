
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
    EXPLAIN = 50, PROFILE = 51, LIMIT = 52, MATCH = 53, MERGE = 54, ON = 55, 
    OPTIONAL = 56, ORDER = 57, REMOVE = 58, RETURN = 59, SET = 60, SKIP_W = 61, 
    WHERE = 62, WITH = 63, UNION = 64, UNWIND = 65, AND = 66, AS = 67, CONTAINS = 68, 
    DISTINCT = 69, ENDS = 70, IN = 71, IS = 72, NOT = 73, OR = 74, STARTS = 75, 
    XOR = 76, FALSE = 77, TRUE = 78, NULL_W = 79, CONSTRAINT = 80, DO = 81, 
    FOR = 82, REQUIRE = 83, UNIQUE = 84, CASE = 85, WHEN = 86, THEN = 87, 
    ELSE = 88, END = 89, MANDATORY = 90, SCALAR = 91, OF = 92, ADD = 93, 
    DROP = 94, FOREACH = 95, ID = 96, ESC_LITERAL = 97, CHAR_LITERAL = 98, 
    STRING_LITERAL = 99, DIGIT = 100, FLOAT = 101, WS = 102, COMMENT = 103, 
    LINE_COMMENT = 104, ERRCHAR = 105, Letter = 106
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

