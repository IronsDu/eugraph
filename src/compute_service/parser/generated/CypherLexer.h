
// Generated from /mnt/f/code/eugraph/grammar/CypherLexer.g4 by ANTLR 4.13.2

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
    COUNT = 33, ANY = 34, NONE = 35, SINGLE = 36, ALL = 37, ASC = 38, ASCENDING = 39, 
    BY = 40, CREATE = 41, DELETE = 42, DESC = 43, DESCENDING = 44, DETACH = 45, 
    EXISTS = 46, LIMIT = 47, MATCH = 48, MERGE = 49, ON = 50, OPTIONAL = 51, 
    ORDER = 52, REMOVE = 53, RETURN = 54, SET = 55, SKIP_W = 56, WHERE = 57, 
    WITH = 58, UNION = 59, UNWIND = 60, AND = 61, AS = 62, CONTAINS = 63, 
    DISTINCT = 64, ENDS = 65, IN = 66, IS = 67, NOT = 68, OR = 69, STARTS = 70, 
    XOR = 71, FALSE = 72, TRUE = 73, NULL_W = 74, CONSTRAINT = 75, DO = 76, 
    FOR = 77, REQUIRE = 78, UNIQUE = 79, CASE = 80, WHEN = 81, THEN = 82, 
    ELSE = 83, END = 84, MANDATORY = 85, SCALAR = 86, OF = 87, ADD = 88, 
    DROP = 89, ID = 90, ESC_LITERAL = 91, CHAR_LITERAL = 92, STRING_LITERAL = 93, 
    DIGIT = 94, FLOAT = 95, WS = 96, COMMENT = 97, LINE_COMMENT = 98, ERRCHAR = 99, 
    Letter = 100
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

