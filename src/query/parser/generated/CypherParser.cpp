
// Generated from /mnt/f/code/eugraph/grammar/CypherParser.g4 by ANTLR 4.13.2


#include "CypherParserVisitor.h"

#include "CypherParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct CypherParserStaticData final {
  CypherParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  CypherParserStaticData(const CypherParserStaticData&) = delete;
  CypherParserStaticData(CypherParserStaticData&&) = delete;
  CypherParserStaticData& operator=(const CypherParserStaticData&) = delete;
  CypherParserStaticData& operator=(CypherParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag cypherparserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<CypherParserStaticData> cypherparserParserStaticData = nullptr;

void cypherparserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (cypherparserParserStaticData != nullptr) {
    return;
  }
#else
  assert(cypherparserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<CypherParserStaticData>(
    std::vector<std::string>{
      "script", "query", "regularQuery", "singleQuery", "standaloneCall", 
      "returnSt", "withSt", "skipSt", "limitSt", "projectionBody", "projectionItems", 
      "projectionItem", "orderItem", "orderSt", "singlePartQ", "multiPartQ", 
      "matchSt", "unwindSt", "readingStatement", "updatingStatement", "deleteSt", 
      "removeSt", "removeItem", "queryCallSt", "parenExpressionChain", "yieldItems", 
      "yieldItem", "mergeSt", "mergeAction", "setSt", "setItem", "nodeLabels", 
      "createSt", "patternWhere", "where", "pattern", "expression", "xorExpression", 
      "andExpression", "notExpression", "comparisonExpression", "comparisonSigns", 
      "addSubExpression", "multDivExpression", "powerExpression", "unaryAddSubExpression", 
      "atomicExpression", "listExpression", "stringExpression", "stringExpPrefix", 
      "nullExpression", "propertyOrLabelExpression", "propertyExpression", 
      "labelCast", "patternPart", "patternElem", "patternElemChain", "properties", 
      "nodePattern", "atom", "lhs", "relationshipPattern", "relationDetail", 
      "relationshipTypes", "unionSt", "subqueryExist", "invocationName", 
      "functionInvocation", "parenthesizedExpression", "filterWith", "patternComprehension", 
      "relationshipsChainPattern", "listComprehension", "filterExpression", 
      "countAll", "expressionChain", "caseExpression", "parameter", "literal", 
      "rangeLit", "boolLit", "numLit", "stringLit", "charLit", "listLit", 
      "mapLit", "mapPair", "name", "symbol", "reservedWord"
    },
    std::vector<std::string>{
      "", "'='", "'+='", "'<='", "'>='", "'>'", "'<'", "'<>'", "'..'", "';'", 
      "'.'", "','", "'('", "')'", "'{'", "'}'", "'['", "']'", "'-'", "'+'", 
      "'/'", "'%'", "'^'", "'*'", "'`'", "':'", "'::'", "'|'", "'$'", "'CALL'", 
      "'YIELD'", "'FILTER'", "'EXTRACT'", "'COUNT'", "'ANY'", "'NONE'", 
      "'SINGLE'", "'ALL'", "'ASC'", "'ASCENDING'", "'BY'", "'CREATE'", "'DELETE'", 
      "'DESC'", "'DESCENDING'", "'DETACH'", "'EXISTS'", "'EXPLAIN'", "'LIMIT'", 
      "'MATCH'", "'MERGE'", "'ON'", "'OPTIONAL'", "'ORDER'", "'REMOVE'", 
      "'RETURN'", "'SET'", "'SKIP'", "'WHERE'", "'WITH'", "'UNION'", "'UNWIND'", 
      "'AND'", "'AS'", "'CONTAINS'", "'DISTINCT'", "'ENDS'", "'IN'", "'IS'", 
      "'NOT'", "'OR'", "'STARTS'", "'XOR'", "'FALSE'", "'TRUE'", "'NULL'", 
      "'CONSTRAINT'", "'DO'", "'FOR'", "'REQUIRE'", "'UNIQUE'", "'CASE'", 
      "'WHEN'", "'THEN'", "'ELSE'", "'END'", "'MANDATORY'", "'SCALAR'", 
      "'OF'", "'ADD'", "'DROP'"
    },
    std::vector<std::string>{
      "", "ASSIGN", "ADD_ASSIGN", "LE", "GE", "GT", "LT", "NOT_EQUAL", "RANGE", 
      "SEMI", "DOT", "COMMA", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACK", 
      "RBRACK", "SUB", "PLUS", "DIV", "MOD", "CARET", "MULT", "ESC", "COLON", 
      "COLONCOLON", "STICK", "DOLLAR", "CALL", "YIELD", "FILTER", "EXTRACT", 
      "COUNT", "ANY", "NONE", "SINGLE", "ALL", "ASC", "ASCENDING", "BY", 
      "CREATE", "DELETE", "DESC", "DESCENDING", "DETACH", "EXISTS", "EXPLAIN", 
      "LIMIT", "MATCH", "MERGE", "ON", "OPTIONAL", "ORDER", "REMOVE", "RETURN", 
      "SET", "SKIP_W", "WHERE", "WITH", "UNION", "UNWIND", "AND", "AS", 
      "CONTAINS", "DISTINCT", "ENDS", "IN", "IS", "NOT", "OR", "STARTS", 
      "XOR", "FALSE", "TRUE", "NULL_W", "CONSTRAINT", "DO", "FOR", "REQUIRE", 
      "UNIQUE", "CASE", "WHEN", "THEN", "ELSE", "END", "MANDATORY", "SCALAR", 
      "OF", "ADD", "DROP", "ID", "ESC_LITERAL", "CHAR_LITERAL", "STRING_LITERAL", 
      "DIGIT", "FLOAT", "WS", "COMMENT", "LINE_COMMENT", "ERRCHAR", "Letter"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,101,849,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,2,42,7,
  	42,2,43,7,43,2,44,7,44,2,45,7,45,2,46,7,46,2,47,7,47,2,48,7,48,2,49,7,
  	49,2,50,7,50,2,51,7,51,2,52,7,52,2,53,7,53,2,54,7,54,2,55,7,55,2,56,7,
  	56,2,57,7,57,2,58,7,58,2,59,7,59,2,60,7,60,2,61,7,61,2,62,7,62,2,63,7,
  	63,2,64,7,64,2,65,7,65,2,66,7,66,2,67,7,67,2,68,7,68,2,69,7,69,2,70,7,
  	70,2,71,7,71,2,72,7,72,2,73,7,73,2,74,7,74,2,75,7,75,2,76,7,76,2,77,7,
  	77,2,78,7,78,2,79,7,79,2,80,7,80,2,81,7,81,2,82,7,82,2,83,7,83,2,84,7,
  	84,2,85,7,85,2,86,7,86,2,87,7,87,2,88,7,88,2,89,7,89,1,0,1,0,3,0,183,
  	8,0,1,0,1,0,1,1,3,1,188,8,1,1,1,1,1,3,1,192,8,1,1,2,1,2,5,2,196,8,2,10,
  	2,12,2,199,9,2,1,3,1,3,3,3,203,8,3,1,4,1,4,1,4,3,4,208,8,4,1,4,1,4,1,
  	4,3,4,213,8,4,3,4,215,8,4,1,5,1,5,1,5,1,6,1,6,1,6,3,6,223,8,6,1,7,1,7,
  	1,7,1,8,1,8,1,8,1,9,3,9,232,8,9,1,9,1,9,3,9,236,8,9,1,9,3,9,239,8,9,1,
  	9,3,9,242,8,9,1,10,1,10,3,10,246,8,10,1,10,1,10,5,10,250,8,10,10,10,12,
  	10,253,9,10,1,11,1,11,1,11,3,11,258,8,11,1,12,1,12,3,12,262,8,12,1,13,
  	1,13,1,13,1,13,1,13,5,13,269,8,13,10,13,12,13,272,9,13,1,14,5,14,275,
  	8,14,10,14,12,14,278,9,14,1,14,1,14,4,14,282,8,14,11,14,12,14,283,1,14,
  	3,14,287,8,14,3,14,289,8,14,1,15,5,15,292,8,15,10,15,12,15,295,9,15,1,
  	15,5,15,298,8,15,10,15,12,15,301,9,15,1,15,4,15,304,8,15,11,15,12,15,
  	305,1,15,1,15,1,16,3,16,311,8,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,
  	17,1,18,1,18,1,18,3,18,324,8,18,1,19,1,19,1,19,1,19,1,19,3,19,331,8,19,
  	1,20,3,20,334,8,20,1,20,1,20,1,20,1,21,1,21,1,21,1,21,5,21,343,8,21,10,
  	21,12,21,346,9,21,1,22,1,22,1,22,1,22,3,22,352,8,22,1,23,1,23,1,23,1,
  	23,1,23,3,23,359,8,23,1,24,1,24,3,24,363,8,24,1,24,1,24,1,25,1,25,1,25,
  	5,25,370,8,25,10,25,12,25,373,9,25,1,25,3,25,376,8,25,1,26,1,26,1,26,
  	3,26,381,8,26,1,26,1,26,1,27,1,27,1,27,5,27,388,8,27,10,27,12,27,391,
  	9,27,1,28,1,28,1,28,1,28,1,29,1,29,1,29,1,29,5,29,401,8,29,10,29,12,29,
  	404,9,29,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,3,30,
  	417,8,30,1,31,1,31,4,31,421,8,31,11,31,12,31,422,1,32,1,32,1,32,1,33,
  	1,33,3,33,430,8,33,1,34,1,34,1,34,1,35,1,35,1,35,5,35,438,8,35,10,35,
  	12,35,441,9,35,1,36,1,36,1,36,5,36,446,8,36,10,36,12,36,449,9,36,1,37,
  	1,37,1,37,5,37,454,8,37,10,37,12,37,457,9,37,1,38,1,38,1,38,5,38,462,
  	8,38,10,38,12,38,465,9,38,1,39,3,39,468,8,39,1,39,1,39,1,40,1,40,1,40,
  	1,40,5,40,476,8,40,10,40,12,40,479,9,40,1,41,1,41,1,42,1,42,1,42,5,42,
  	486,8,42,10,42,12,42,489,9,42,1,43,1,43,1,43,5,43,494,8,43,10,43,12,43,
  	497,9,43,1,44,1,44,1,44,5,44,502,8,44,10,44,12,44,505,9,44,1,45,3,45,
  	508,8,45,1,45,1,45,1,46,1,46,1,46,1,46,5,46,516,8,46,10,46,12,46,519,
  	9,46,1,47,1,47,1,47,1,47,3,47,525,8,47,1,47,1,47,3,47,529,8,47,1,47,3,
  	47,532,8,47,1,47,3,47,535,8,47,1,48,1,48,1,48,1,49,1,49,1,49,1,49,1,49,
  	3,49,545,8,49,1,50,1,50,3,50,549,8,50,1,50,1,50,1,51,1,51,3,51,555,8,
  	51,1,52,1,52,1,52,5,52,560,8,52,10,52,12,52,563,9,52,1,52,1,52,3,52,567,
  	8,52,1,53,1,53,1,53,5,53,572,8,53,10,53,12,53,575,9,53,1,54,1,54,1,54,
  	3,54,580,8,54,1,54,1,54,1,55,1,55,5,55,586,8,55,10,55,12,55,589,9,55,
  	1,55,1,55,1,55,1,55,3,55,595,8,55,1,56,1,56,1,56,1,57,1,57,3,57,602,8,
  	57,1,58,1,58,3,58,606,8,58,1,58,3,58,609,8,58,1,58,3,58,612,8,58,1,58,
  	1,58,1,59,1,59,1,59,1,59,1,59,1,59,1,59,1,59,1,59,1,59,1,59,1,59,3,59,
  	628,8,59,1,60,1,60,1,60,1,61,1,61,1,61,3,61,636,8,61,1,61,1,61,3,61,640,
  	8,61,1,61,1,61,3,61,644,8,61,1,61,1,61,3,61,648,8,61,3,61,650,8,61,1,
  	62,1,62,3,62,654,8,62,1,62,3,62,657,8,62,1,62,3,62,660,8,62,1,62,3,62,
  	663,8,62,1,62,1,62,1,63,1,63,1,63,1,63,3,63,671,8,63,1,63,5,63,674,8,
  	63,10,63,12,63,677,9,63,1,64,1,64,3,64,681,8,64,1,64,1,64,1,65,1,65,1,
  	65,1,65,3,65,689,8,65,1,65,1,65,1,66,1,66,1,66,5,66,696,8,66,10,66,12,
  	66,699,9,66,1,67,1,67,1,67,3,67,704,8,67,1,67,3,67,707,8,67,1,67,1,67,
  	1,68,1,68,1,68,1,68,1,69,1,69,1,69,1,69,1,69,1,70,1,70,3,70,722,8,70,
  	1,70,1,70,3,70,726,8,70,1,70,1,70,1,70,1,70,1,71,1,71,4,71,734,8,71,11,
  	71,12,71,735,1,72,1,72,1,72,1,72,3,72,742,8,72,1,72,1,72,1,73,1,73,1,
  	73,1,73,3,73,750,8,73,1,74,1,74,1,74,1,74,1,74,1,75,1,75,1,75,5,75,760,
  	8,75,10,75,12,75,763,9,75,1,76,1,76,3,76,767,8,76,1,76,1,76,1,76,1,76,
  	1,76,4,76,774,8,76,11,76,12,76,775,1,76,1,76,3,76,780,8,76,1,76,1,76,
  	1,77,1,77,1,77,3,77,787,8,77,1,78,1,78,1,78,1,78,1,78,1,78,1,78,3,78,
  	796,8,78,1,79,1,79,1,79,3,79,801,8,79,1,79,1,79,1,79,3,79,806,8,79,3,
  	79,808,8,79,1,80,1,80,1,81,1,81,1,82,1,82,1,83,1,83,1,84,1,84,3,84,820,
  	8,84,1,84,1,84,1,85,1,85,1,85,1,85,5,85,828,8,85,10,85,12,85,831,9,85,
  	3,85,833,8,85,1,85,1,85,1,86,1,86,1,86,1,86,1,87,1,87,3,87,843,8,87,1,
  	88,1,88,1,89,1,89,1,89,0,0,90,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,
  	30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,
  	76,78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,114,116,
  	118,120,122,124,126,128,130,132,134,136,138,140,142,144,146,148,150,152,
  	154,156,158,160,162,164,166,168,170,172,174,176,178,0,10,2,0,38,39,43,
  	44,2,0,41,41,49,49,1,0,1,2,2,0,1,1,3,7,1,0,18,19,2,0,20,21,23,23,1,0,
  	34,37,1,0,73,74,2,0,31,36,91,92,2,0,37,46,48,90,887,0,180,1,0,0,0,2,191,
  	1,0,0,0,4,193,1,0,0,0,6,202,1,0,0,0,8,204,1,0,0,0,10,216,1,0,0,0,12,219,
  	1,0,0,0,14,224,1,0,0,0,16,227,1,0,0,0,18,231,1,0,0,0,20,245,1,0,0,0,22,
  	254,1,0,0,0,24,259,1,0,0,0,26,263,1,0,0,0,28,276,1,0,0,0,30,293,1,0,0,
  	0,32,310,1,0,0,0,34,315,1,0,0,0,36,323,1,0,0,0,38,330,1,0,0,0,40,333,
  	1,0,0,0,42,338,1,0,0,0,44,351,1,0,0,0,46,353,1,0,0,0,48,360,1,0,0,0,50,
  	366,1,0,0,0,52,380,1,0,0,0,54,384,1,0,0,0,56,392,1,0,0,0,58,396,1,0,0,
  	0,60,416,1,0,0,0,62,420,1,0,0,0,64,424,1,0,0,0,66,427,1,0,0,0,68,431,
  	1,0,0,0,70,434,1,0,0,0,72,442,1,0,0,0,74,450,1,0,0,0,76,458,1,0,0,0,78,
  	467,1,0,0,0,80,471,1,0,0,0,82,480,1,0,0,0,84,482,1,0,0,0,86,490,1,0,0,
  	0,88,498,1,0,0,0,90,507,1,0,0,0,92,511,1,0,0,0,94,534,1,0,0,0,96,536,
  	1,0,0,0,98,544,1,0,0,0,100,546,1,0,0,0,102,552,1,0,0,0,104,556,1,0,0,
  	0,106,568,1,0,0,0,108,579,1,0,0,0,110,594,1,0,0,0,112,596,1,0,0,0,114,
  	601,1,0,0,0,116,603,1,0,0,0,118,627,1,0,0,0,120,629,1,0,0,0,122,649,1,
  	0,0,0,124,651,1,0,0,0,126,666,1,0,0,0,128,678,1,0,0,0,130,684,1,0,0,0,
  	132,692,1,0,0,0,134,700,1,0,0,0,136,710,1,0,0,0,138,714,1,0,0,0,140,719,
  	1,0,0,0,142,731,1,0,0,0,144,737,1,0,0,0,146,745,1,0,0,0,148,751,1,0,0,
  	0,150,756,1,0,0,0,152,764,1,0,0,0,154,783,1,0,0,0,156,795,1,0,0,0,158,
  	797,1,0,0,0,160,809,1,0,0,0,162,811,1,0,0,0,164,813,1,0,0,0,166,815,1,
  	0,0,0,168,817,1,0,0,0,170,823,1,0,0,0,172,836,1,0,0,0,174,842,1,0,0,0,
  	176,844,1,0,0,0,178,846,1,0,0,0,180,182,3,2,1,0,181,183,5,9,0,0,182,181,
  	1,0,0,0,182,183,1,0,0,0,183,184,1,0,0,0,184,185,5,0,0,1,185,1,1,0,0,0,
  	186,188,5,47,0,0,187,186,1,0,0,0,187,188,1,0,0,0,188,189,1,0,0,0,189,
  	192,3,4,2,0,190,192,3,8,4,0,191,187,1,0,0,0,191,190,1,0,0,0,192,3,1,0,
  	0,0,193,197,3,6,3,0,194,196,3,128,64,0,195,194,1,0,0,0,196,199,1,0,0,
  	0,197,195,1,0,0,0,197,198,1,0,0,0,198,5,1,0,0,0,199,197,1,0,0,0,200,203,
  	3,28,14,0,201,203,3,30,15,0,202,200,1,0,0,0,202,201,1,0,0,0,203,7,1,0,
  	0,0,204,205,5,29,0,0,205,207,3,132,66,0,206,208,3,48,24,0,207,206,1,0,
  	0,0,207,208,1,0,0,0,208,214,1,0,0,0,209,212,5,30,0,0,210,213,5,23,0,0,
  	211,213,3,50,25,0,212,210,1,0,0,0,212,211,1,0,0,0,213,215,1,0,0,0,214,
  	209,1,0,0,0,214,215,1,0,0,0,215,9,1,0,0,0,216,217,5,55,0,0,217,218,3,
  	18,9,0,218,11,1,0,0,0,219,220,5,59,0,0,220,222,3,18,9,0,221,223,3,68,
  	34,0,222,221,1,0,0,0,222,223,1,0,0,0,223,13,1,0,0,0,224,225,5,57,0,0,
  	225,226,3,72,36,0,226,15,1,0,0,0,227,228,5,48,0,0,228,229,3,72,36,0,229,
  	17,1,0,0,0,230,232,5,65,0,0,231,230,1,0,0,0,231,232,1,0,0,0,232,233,1,
  	0,0,0,233,235,3,20,10,0,234,236,3,26,13,0,235,234,1,0,0,0,235,236,1,0,
  	0,0,236,238,1,0,0,0,237,239,3,14,7,0,238,237,1,0,0,0,238,239,1,0,0,0,
  	239,241,1,0,0,0,240,242,3,16,8,0,241,240,1,0,0,0,241,242,1,0,0,0,242,
  	19,1,0,0,0,243,246,5,23,0,0,244,246,3,22,11,0,245,243,1,0,0,0,245,244,
  	1,0,0,0,246,251,1,0,0,0,247,248,5,11,0,0,248,250,3,22,11,0,249,247,1,
  	0,0,0,250,253,1,0,0,0,251,249,1,0,0,0,251,252,1,0,0,0,252,21,1,0,0,0,
  	253,251,1,0,0,0,254,257,3,72,36,0,255,256,5,63,0,0,256,258,3,176,88,0,
  	257,255,1,0,0,0,257,258,1,0,0,0,258,23,1,0,0,0,259,261,3,72,36,0,260,
  	262,7,0,0,0,261,260,1,0,0,0,261,262,1,0,0,0,262,25,1,0,0,0,263,264,5,
  	53,0,0,264,265,5,40,0,0,265,270,3,24,12,0,266,267,5,11,0,0,267,269,3,
  	24,12,0,268,266,1,0,0,0,269,272,1,0,0,0,270,268,1,0,0,0,270,271,1,0,0,
  	0,271,27,1,0,0,0,272,270,1,0,0,0,273,275,3,36,18,0,274,273,1,0,0,0,275,
  	278,1,0,0,0,276,274,1,0,0,0,276,277,1,0,0,0,277,288,1,0,0,0,278,276,1,
  	0,0,0,279,289,3,10,5,0,280,282,3,38,19,0,281,280,1,0,0,0,282,283,1,0,
  	0,0,283,281,1,0,0,0,283,284,1,0,0,0,284,286,1,0,0,0,285,287,3,10,5,0,
  	286,285,1,0,0,0,286,287,1,0,0,0,287,289,1,0,0,0,288,279,1,0,0,0,288,281,
  	1,0,0,0,289,29,1,0,0,0,290,292,3,36,18,0,291,290,1,0,0,0,292,295,1,0,
  	0,0,293,291,1,0,0,0,293,294,1,0,0,0,294,303,1,0,0,0,295,293,1,0,0,0,296,
  	298,3,38,19,0,297,296,1,0,0,0,298,301,1,0,0,0,299,297,1,0,0,0,299,300,
  	1,0,0,0,300,302,1,0,0,0,301,299,1,0,0,0,302,304,3,12,6,0,303,299,1,0,
  	0,0,304,305,1,0,0,0,305,303,1,0,0,0,305,306,1,0,0,0,306,307,1,0,0,0,307,
  	308,3,28,14,0,308,31,1,0,0,0,309,311,5,52,0,0,310,309,1,0,0,0,310,311,
  	1,0,0,0,311,312,1,0,0,0,312,313,5,49,0,0,313,314,3,66,33,0,314,33,1,0,
  	0,0,315,316,5,61,0,0,316,317,3,72,36,0,317,318,5,63,0,0,318,319,3,176,
  	88,0,319,35,1,0,0,0,320,324,3,32,16,0,321,324,3,34,17,0,322,324,3,46,
  	23,0,323,320,1,0,0,0,323,321,1,0,0,0,323,322,1,0,0,0,324,37,1,0,0,0,325,
  	331,3,64,32,0,326,331,3,54,27,0,327,331,3,40,20,0,328,331,3,58,29,0,329,
  	331,3,42,21,0,330,325,1,0,0,0,330,326,1,0,0,0,330,327,1,0,0,0,330,328,
  	1,0,0,0,330,329,1,0,0,0,331,39,1,0,0,0,332,334,5,45,0,0,333,332,1,0,0,
  	0,333,334,1,0,0,0,334,335,1,0,0,0,335,336,5,42,0,0,336,337,3,150,75,0,
  	337,41,1,0,0,0,338,339,5,54,0,0,339,344,3,44,22,0,340,341,5,11,0,0,341,
  	343,3,44,22,0,342,340,1,0,0,0,343,346,1,0,0,0,344,342,1,0,0,0,344,345,
  	1,0,0,0,345,43,1,0,0,0,346,344,1,0,0,0,347,348,3,176,88,0,348,349,3,62,
  	31,0,349,352,1,0,0,0,350,352,3,104,52,0,351,347,1,0,0,0,351,350,1,0,0,
  	0,352,45,1,0,0,0,353,354,5,29,0,0,354,355,3,132,66,0,355,358,3,48,24,
  	0,356,357,5,30,0,0,357,359,3,50,25,0,358,356,1,0,0,0,358,359,1,0,0,0,
  	359,47,1,0,0,0,360,362,5,12,0,0,361,363,3,150,75,0,362,361,1,0,0,0,362,
  	363,1,0,0,0,363,364,1,0,0,0,364,365,5,13,0,0,365,49,1,0,0,0,366,371,3,
  	52,26,0,367,368,5,11,0,0,368,370,3,52,26,0,369,367,1,0,0,0,370,373,1,
  	0,0,0,371,369,1,0,0,0,371,372,1,0,0,0,372,375,1,0,0,0,373,371,1,0,0,0,
  	374,376,3,68,34,0,375,374,1,0,0,0,375,376,1,0,0,0,376,51,1,0,0,0,377,
  	378,3,176,88,0,378,379,5,63,0,0,379,381,1,0,0,0,380,377,1,0,0,0,380,381,
  	1,0,0,0,381,382,1,0,0,0,382,383,3,176,88,0,383,53,1,0,0,0,384,385,5,50,
  	0,0,385,389,3,108,54,0,386,388,3,56,28,0,387,386,1,0,0,0,388,391,1,0,
  	0,0,389,387,1,0,0,0,389,390,1,0,0,0,390,55,1,0,0,0,391,389,1,0,0,0,392,
  	393,5,51,0,0,393,394,7,1,0,0,394,395,3,58,29,0,395,57,1,0,0,0,396,397,
  	5,56,0,0,397,402,3,60,30,0,398,399,5,11,0,0,399,401,3,60,30,0,400,398,
  	1,0,0,0,401,404,1,0,0,0,402,400,1,0,0,0,402,403,1,0,0,0,403,59,1,0,0,
  	0,404,402,1,0,0,0,405,406,3,104,52,0,406,407,5,1,0,0,407,408,3,72,36,
  	0,408,417,1,0,0,0,409,410,3,176,88,0,410,411,7,2,0,0,411,412,3,72,36,
  	0,412,417,1,0,0,0,413,414,3,176,88,0,414,415,3,62,31,0,415,417,1,0,0,
  	0,416,405,1,0,0,0,416,409,1,0,0,0,416,413,1,0,0,0,417,61,1,0,0,0,418,
  	419,5,25,0,0,419,421,3,174,87,0,420,418,1,0,0,0,421,422,1,0,0,0,422,420,
  	1,0,0,0,422,423,1,0,0,0,423,63,1,0,0,0,424,425,5,41,0,0,425,426,3,70,
  	35,0,426,65,1,0,0,0,427,429,3,70,35,0,428,430,3,68,34,0,429,428,1,0,0,
  	0,429,430,1,0,0,0,430,67,1,0,0,0,431,432,5,58,0,0,432,433,3,72,36,0,433,
  	69,1,0,0,0,434,439,3,108,54,0,435,436,5,11,0,0,436,438,3,108,54,0,437,
  	435,1,0,0,0,438,441,1,0,0,0,439,437,1,0,0,0,439,440,1,0,0,0,440,71,1,
  	0,0,0,441,439,1,0,0,0,442,447,3,74,37,0,443,444,5,70,0,0,444,446,3,74,
  	37,0,445,443,1,0,0,0,446,449,1,0,0,0,447,445,1,0,0,0,447,448,1,0,0,0,
  	448,73,1,0,0,0,449,447,1,0,0,0,450,455,3,76,38,0,451,452,5,72,0,0,452,
  	454,3,76,38,0,453,451,1,0,0,0,454,457,1,0,0,0,455,453,1,0,0,0,455,456,
  	1,0,0,0,456,75,1,0,0,0,457,455,1,0,0,0,458,463,3,78,39,0,459,460,5,62,
  	0,0,460,462,3,78,39,0,461,459,1,0,0,0,462,465,1,0,0,0,463,461,1,0,0,0,
  	463,464,1,0,0,0,464,77,1,0,0,0,465,463,1,0,0,0,466,468,5,69,0,0,467,466,
  	1,0,0,0,467,468,1,0,0,0,468,469,1,0,0,0,469,470,3,80,40,0,470,79,1,0,
  	0,0,471,477,3,84,42,0,472,473,3,82,41,0,473,474,3,84,42,0,474,476,1,0,
  	0,0,475,472,1,0,0,0,476,479,1,0,0,0,477,475,1,0,0,0,477,478,1,0,0,0,478,
  	81,1,0,0,0,479,477,1,0,0,0,480,481,7,3,0,0,481,83,1,0,0,0,482,487,3,86,
  	43,0,483,484,7,4,0,0,484,486,3,86,43,0,485,483,1,0,0,0,486,489,1,0,0,
  	0,487,485,1,0,0,0,487,488,1,0,0,0,488,85,1,0,0,0,489,487,1,0,0,0,490,
  	495,3,88,44,0,491,492,7,5,0,0,492,494,3,88,44,0,493,491,1,0,0,0,494,497,
  	1,0,0,0,495,493,1,0,0,0,495,496,1,0,0,0,496,87,1,0,0,0,497,495,1,0,0,
  	0,498,503,3,90,45,0,499,500,5,22,0,0,500,502,3,90,45,0,501,499,1,0,0,
  	0,502,505,1,0,0,0,503,501,1,0,0,0,503,504,1,0,0,0,504,89,1,0,0,0,505,
  	503,1,0,0,0,506,508,7,4,0,0,507,506,1,0,0,0,507,508,1,0,0,0,508,509,1,
  	0,0,0,509,510,3,92,46,0,510,91,1,0,0,0,511,517,3,102,51,0,512,516,3,96,
  	48,0,513,516,3,94,47,0,514,516,3,100,50,0,515,512,1,0,0,0,515,513,1,0,
  	0,0,515,514,1,0,0,0,516,519,1,0,0,0,517,515,1,0,0,0,517,518,1,0,0,0,518,
  	93,1,0,0,0,519,517,1,0,0,0,520,521,5,67,0,0,521,535,3,102,51,0,522,531,
  	5,16,0,0,523,525,3,72,36,0,524,523,1,0,0,0,524,525,1,0,0,0,525,526,1,
  	0,0,0,526,528,5,8,0,0,527,529,3,72,36,0,528,527,1,0,0,0,528,529,1,0,0,
  	0,529,532,1,0,0,0,530,532,3,72,36,0,531,524,1,0,0,0,531,530,1,0,0,0,532,
  	533,1,0,0,0,533,535,5,17,0,0,534,520,1,0,0,0,534,522,1,0,0,0,535,95,1,
  	0,0,0,536,537,3,98,49,0,537,538,3,102,51,0,538,97,1,0,0,0,539,540,5,71,
  	0,0,540,545,5,59,0,0,541,542,5,66,0,0,542,545,5,59,0,0,543,545,5,64,0,
  	0,544,539,1,0,0,0,544,541,1,0,0,0,544,543,1,0,0,0,545,99,1,0,0,0,546,
  	548,5,68,0,0,547,549,5,69,0,0,548,547,1,0,0,0,548,549,1,0,0,0,549,550,
  	1,0,0,0,550,551,5,75,0,0,551,101,1,0,0,0,552,554,3,104,52,0,553,555,3,
  	62,31,0,554,553,1,0,0,0,554,555,1,0,0,0,555,103,1,0,0,0,556,561,3,118,
  	59,0,557,558,5,10,0,0,558,560,3,174,87,0,559,557,1,0,0,0,560,563,1,0,
  	0,0,561,559,1,0,0,0,561,562,1,0,0,0,562,566,1,0,0,0,563,561,1,0,0,0,564,
  	565,5,26,0,0,565,567,3,106,53,0,566,564,1,0,0,0,566,567,1,0,0,0,567,105,
  	1,0,0,0,568,573,3,174,87,0,569,570,5,10,0,0,570,572,3,174,87,0,571,569,
  	1,0,0,0,572,575,1,0,0,0,573,571,1,0,0,0,573,574,1,0,0,0,574,107,1,0,0,
  	0,575,573,1,0,0,0,576,577,3,176,88,0,577,578,5,1,0,0,578,580,1,0,0,0,
  	579,576,1,0,0,0,579,580,1,0,0,0,580,581,1,0,0,0,581,582,3,110,55,0,582,
  	109,1,0,0,0,583,587,3,116,58,0,584,586,3,112,56,0,585,584,1,0,0,0,586,
  	589,1,0,0,0,587,585,1,0,0,0,587,588,1,0,0,0,588,595,1,0,0,0,589,587,1,
  	0,0,0,590,591,5,12,0,0,591,592,3,110,55,0,592,593,5,13,0,0,593,595,1,
  	0,0,0,594,583,1,0,0,0,594,590,1,0,0,0,595,111,1,0,0,0,596,597,3,122,61,
  	0,597,598,3,116,58,0,598,113,1,0,0,0,599,602,3,170,85,0,600,602,3,154,
  	77,0,601,599,1,0,0,0,601,600,1,0,0,0,602,115,1,0,0,0,603,605,5,12,0,0,
  	604,606,3,176,88,0,605,604,1,0,0,0,605,606,1,0,0,0,606,608,1,0,0,0,607,
  	609,3,62,31,0,608,607,1,0,0,0,608,609,1,0,0,0,609,611,1,0,0,0,610,612,
  	3,114,57,0,611,610,1,0,0,0,611,612,1,0,0,0,612,613,1,0,0,0,613,614,5,
  	13,0,0,614,117,1,0,0,0,615,628,3,156,78,0,616,628,3,154,77,0,617,628,
  	3,152,76,0,618,628,3,148,74,0,619,628,3,144,72,0,620,628,3,140,70,0,621,
  	628,3,138,69,0,622,628,3,142,71,0,623,628,3,136,68,0,624,628,3,134,67,
  	0,625,628,3,176,88,0,626,628,3,130,65,0,627,615,1,0,0,0,627,616,1,0,0,
  	0,627,617,1,0,0,0,627,618,1,0,0,0,627,619,1,0,0,0,627,620,1,0,0,0,627,
  	621,1,0,0,0,627,622,1,0,0,0,627,623,1,0,0,0,627,624,1,0,0,0,627,625,1,
  	0,0,0,627,626,1,0,0,0,628,119,1,0,0,0,629,630,3,176,88,0,630,631,5,1,
  	0,0,631,121,1,0,0,0,632,633,5,6,0,0,633,635,5,18,0,0,634,636,3,124,62,
  	0,635,634,1,0,0,0,635,636,1,0,0,0,636,637,1,0,0,0,637,639,5,18,0,0,638,
  	640,5,5,0,0,639,638,1,0,0,0,639,640,1,0,0,0,640,650,1,0,0,0,641,643,5,
  	18,0,0,642,644,3,124,62,0,643,642,1,0,0,0,643,644,1,0,0,0,644,645,1,0,
  	0,0,645,647,5,18,0,0,646,648,5,5,0,0,647,646,1,0,0,0,647,648,1,0,0,0,
  	648,650,1,0,0,0,649,632,1,0,0,0,649,641,1,0,0,0,650,123,1,0,0,0,651,653,
  	5,16,0,0,652,654,3,176,88,0,653,652,1,0,0,0,653,654,1,0,0,0,654,656,1,
  	0,0,0,655,657,3,126,63,0,656,655,1,0,0,0,656,657,1,0,0,0,657,659,1,0,
  	0,0,658,660,3,158,79,0,659,658,1,0,0,0,659,660,1,0,0,0,660,662,1,0,0,
  	0,661,663,3,114,57,0,662,661,1,0,0,0,662,663,1,0,0,0,663,664,1,0,0,0,
  	664,665,5,17,0,0,665,125,1,0,0,0,666,667,5,25,0,0,667,675,3,174,87,0,
  	668,670,5,27,0,0,669,671,5,25,0,0,670,669,1,0,0,0,670,671,1,0,0,0,671,
  	672,1,0,0,0,672,674,3,174,87,0,673,668,1,0,0,0,674,677,1,0,0,0,675,673,
  	1,0,0,0,675,676,1,0,0,0,676,127,1,0,0,0,677,675,1,0,0,0,678,680,5,60,
  	0,0,679,681,5,37,0,0,680,679,1,0,0,0,680,681,1,0,0,0,681,682,1,0,0,0,
  	682,683,3,6,3,0,683,129,1,0,0,0,684,685,5,46,0,0,685,688,5,14,0,0,686,
  	689,3,4,2,0,687,689,3,66,33,0,688,686,1,0,0,0,688,687,1,0,0,0,689,690,
  	1,0,0,0,690,691,5,15,0,0,691,131,1,0,0,0,692,697,3,176,88,0,693,694,5,
  	10,0,0,694,696,3,176,88,0,695,693,1,0,0,0,696,699,1,0,0,0,697,695,1,0,
  	0,0,697,698,1,0,0,0,698,133,1,0,0,0,699,697,1,0,0,0,700,701,3,132,66,
  	0,701,703,5,12,0,0,702,704,5,65,0,0,703,702,1,0,0,0,703,704,1,0,0,0,704,
  	706,1,0,0,0,705,707,3,150,75,0,706,705,1,0,0,0,706,707,1,0,0,0,707,708,
  	1,0,0,0,708,709,5,13,0,0,709,135,1,0,0,0,710,711,5,12,0,0,711,712,3,72,
  	36,0,712,713,5,13,0,0,713,137,1,0,0,0,714,715,7,6,0,0,715,716,5,12,0,
  	0,716,717,3,146,73,0,717,718,5,13,0,0,718,139,1,0,0,0,719,721,5,16,0,
  	0,720,722,3,120,60,0,721,720,1,0,0,0,721,722,1,0,0,0,722,723,1,0,0,0,
  	723,725,3,142,71,0,724,726,3,68,34,0,725,724,1,0,0,0,725,726,1,0,0,0,
  	726,727,1,0,0,0,727,728,5,27,0,0,728,729,3,72,36,0,729,730,5,17,0,0,730,
  	141,1,0,0,0,731,733,3,116,58,0,732,734,3,112,56,0,733,732,1,0,0,0,734,
  	735,1,0,0,0,735,733,1,0,0,0,735,736,1,0,0,0,736,143,1,0,0,0,737,738,5,
  	16,0,0,738,741,3,146,73,0,739,740,5,27,0,0,740,742,3,72,36,0,741,739,
  	1,0,0,0,741,742,1,0,0,0,742,743,1,0,0,0,743,744,5,17,0,0,744,145,1,0,
  	0,0,745,746,3,176,88,0,746,747,5,67,0,0,747,749,3,72,36,0,748,750,3,68,
  	34,0,749,748,1,0,0,0,749,750,1,0,0,0,750,147,1,0,0,0,751,752,5,33,0,0,
  	752,753,5,12,0,0,753,754,5,23,0,0,754,755,5,13,0,0,755,149,1,0,0,0,756,
  	761,3,72,36,0,757,758,5,11,0,0,758,760,3,72,36,0,759,757,1,0,0,0,760,
  	763,1,0,0,0,761,759,1,0,0,0,761,762,1,0,0,0,762,151,1,0,0,0,763,761,1,
  	0,0,0,764,766,5,81,0,0,765,767,3,72,36,0,766,765,1,0,0,0,766,767,1,0,
  	0,0,767,773,1,0,0,0,768,769,5,82,0,0,769,770,3,72,36,0,770,771,5,83,0,
  	0,771,772,3,72,36,0,772,774,1,0,0,0,773,768,1,0,0,0,774,775,1,0,0,0,775,
  	773,1,0,0,0,775,776,1,0,0,0,776,779,1,0,0,0,777,778,5,84,0,0,778,780,
  	3,72,36,0,779,777,1,0,0,0,779,780,1,0,0,0,780,781,1,0,0,0,781,782,5,85,
  	0,0,782,153,1,0,0,0,783,786,5,28,0,0,784,787,3,176,88,0,785,787,3,162,
  	81,0,786,784,1,0,0,0,786,785,1,0,0,0,787,155,1,0,0,0,788,796,3,160,80,
  	0,789,796,3,162,81,0,790,796,5,75,0,0,791,796,3,164,82,0,792,796,3,166,
  	83,0,793,796,3,168,84,0,794,796,3,170,85,0,795,788,1,0,0,0,795,789,1,
  	0,0,0,795,790,1,0,0,0,795,791,1,0,0,0,795,792,1,0,0,0,795,793,1,0,0,0,
  	795,794,1,0,0,0,796,157,1,0,0,0,797,800,5,23,0,0,798,801,3,162,81,0,799,
  	801,5,91,0,0,800,798,1,0,0,0,800,799,1,0,0,0,800,801,1,0,0,0,801,807,
  	1,0,0,0,802,805,5,8,0,0,803,806,3,162,81,0,804,806,5,91,0,0,805,803,1,
  	0,0,0,805,804,1,0,0,0,805,806,1,0,0,0,806,808,1,0,0,0,807,802,1,0,0,0,
  	807,808,1,0,0,0,808,159,1,0,0,0,809,810,7,7,0,0,810,161,1,0,0,0,811,812,
  	5,95,0,0,812,163,1,0,0,0,813,814,5,94,0,0,814,165,1,0,0,0,815,816,5,93,
  	0,0,816,167,1,0,0,0,817,819,5,16,0,0,818,820,3,150,75,0,819,818,1,0,0,
  	0,819,820,1,0,0,0,820,821,1,0,0,0,821,822,5,17,0,0,822,169,1,0,0,0,823,
  	832,5,14,0,0,824,829,3,172,86,0,825,826,5,11,0,0,826,828,3,172,86,0,827,
  	825,1,0,0,0,828,831,1,0,0,0,829,827,1,0,0,0,829,830,1,0,0,0,830,833,1,
  	0,0,0,831,829,1,0,0,0,832,824,1,0,0,0,832,833,1,0,0,0,833,834,1,0,0,0,
  	834,835,5,15,0,0,835,171,1,0,0,0,836,837,3,174,87,0,837,838,5,25,0,0,
  	838,839,3,72,36,0,839,173,1,0,0,0,840,843,3,176,88,0,841,843,3,178,89,
  	0,842,840,1,0,0,0,842,841,1,0,0,0,843,175,1,0,0,0,844,845,7,8,0,0,845,
  	177,1,0,0,0,846,847,7,9,0,0,847,179,1,0,0,0,105,182,187,191,197,202,207,
  	212,214,222,231,235,238,241,245,251,257,261,270,276,283,286,288,293,299,
  	305,310,323,330,333,344,351,358,362,371,375,380,389,402,416,422,429,439,
  	447,455,463,467,477,487,495,503,507,515,517,524,528,531,534,544,548,554,
  	561,566,573,579,587,594,601,605,608,611,627,635,639,643,647,649,653,656,
  	659,662,670,675,680,688,697,703,706,721,725,735,741,749,761,766,775,779,
  	786,795,800,805,807,819,829,832,842
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  cypherparserParserStaticData = std::move(staticData);
}

}

CypherParser::CypherParser(TokenStream *input) : CypherParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

CypherParser::CypherParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  CypherParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *cypherparserParserStaticData->atn, cypherparserParserStaticData->decisionToDFA, cypherparserParserStaticData->sharedContextCache, options);
}

CypherParser::~CypherParser() {
  delete _interpreter;
}

const atn::ATN& CypherParser::getATN() const {
  return *cypherparserParserStaticData->atn;
}

std::string CypherParser::getGrammarFileName() const {
  return "CypherParser.g4";
}

const std::vector<std::string>& CypherParser::getRuleNames() const {
  return cypherparserParserStaticData->ruleNames;
}

const dfa::Vocabulary& CypherParser::getVocabulary() const {
  return cypherparserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView CypherParser::getSerializedATN() const {
  return cypherparserParserStaticData->serializedATN;
}


//----------------- ScriptContext ------------------------------------------------------------------

CypherParser::ScriptContext::ScriptContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::QueryContext* CypherParser::ScriptContext::query() {
  return getRuleContext<CypherParser::QueryContext>(0);
}

tree::TerminalNode* CypherParser::ScriptContext::EOF() {
  return getToken(CypherParser::EOF, 0);
}

tree::TerminalNode* CypherParser::ScriptContext::SEMI() {
  return getToken(CypherParser::SEMI, 0);
}


size_t CypherParser::ScriptContext::getRuleIndex() const {
  return CypherParser::RuleScript;
}


std::any CypherParser::ScriptContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitScript(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ScriptContext* CypherParser::script() {
  ScriptContext *_localctx = _tracker.createInstance<ScriptContext>(_ctx, getState());
  enterRule(_localctx, 0, CypherParser::RuleScript);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(180);
    query();
    setState(182);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SEMI) {
      setState(181);
      match(CypherParser::SEMI);
    }
    setState(184);
    match(CypherParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- QueryContext ------------------------------------------------------------------

CypherParser::QueryContext::QueryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::RegularQueryContext* CypherParser::QueryContext::regularQuery() {
  return getRuleContext<CypherParser::RegularQueryContext>(0);
}

tree::TerminalNode* CypherParser::QueryContext::EXPLAIN() {
  return getToken(CypherParser::EXPLAIN, 0);
}

CypherParser::StandaloneCallContext* CypherParser::QueryContext::standaloneCall() {
  return getRuleContext<CypherParser::StandaloneCallContext>(0);
}


size_t CypherParser::QueryContext::getRuleIndex() const {
  return CypherParser::RuleQuery;
}


std::any CypherParser::QueryContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitQuery(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::QueryContext* CypherParser::query() {
  QueryContext *_localctx = _tracker.createInstance<QueryContext>(_ctx, getState());
  enterRule(_localctx, 2, CypherParser::RuleQuery);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(191);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 2, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(187);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::EXPLAIN) {
        setState(186);
        match(CypherParser::EXPLAIN);
      }
      setState(189);
      regularQuery();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(190);
      standaloneCall();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RegularQueryContext ------------------------------------------------------------------

CypherParser::RegularQueryContext::RegularQueryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SingleQueryContext* CypherParser::RegularQueryContext::singleQuery() {
  return getRuleContext<CypherParser::SingleQueryContext>(0);
}

std::vector<CypherParser::UnionStContext *> CypherParser::RegularQueryContext::unionSt() {
  return getRuleContexts<CypherParser::UnionStContext>();
}

CypherParser::UnionStContext* CypherParser::RegularQueryContext::unionSt(size_t i) {
  return getRuleContext<CypherParser::UnionStContext>(i);
}


size_t CypherParser::RegularQueryContext::getRuleIndex() const {
  return CypherParser::RuleRegularQuery;
}


std::any CypherParser::RegularQueryContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRegularQuery(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RegularQueryContext* CypherParser::regularQuery() {
  RegularQueryContext *_localctx = _tracker.createInstance<RegularQueryContext>(_ctx, getState());
  enterRule(_localctx, 4, CypherParser::RuleRegularQuery);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(193);
    singleQuery();
    setState(197);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::UNION) {
      setState(194);
      unionSt();
      setState(199);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SingleQueryContext ------------------------------------------------------------------

CypherParser::SingleQueryContext::SingleQueryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SinglePartQContext* CypherParser::SingleQueryContext::singlePartQ() {
  return getRuleContext<CypherParser::SinglePartQContext>(0);
}

CypherParser::MultiPartQContext* CypherParser::SingleQueryContext::multiPartQ() {
  return getRuleContext<CypherParser::MultiPartQContext>(0);
}


size_t CypherParser::SingleQueryContext::getRuleIndex() const {
  return CypherParser::RuleSingleQuery;
}


std::any CypherParser::SingleQueryContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSingleQuery(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SingleQueryContext* CypherParser::singleQuery() {
  SingleQueryContext *_localctx = _tracker.createInstance<SingleQueryContext>(_ctx, getState());
  enterRule(_localctx, 6, CypherParser::RuleSingleQuery);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(202);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 4, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(200);
      singlePartQ();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(201);
      multiPartQ();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StandaloneCallContext ------------------------------------------------------------------

CypherParser::StandaloneCallContext::StandaloneCallContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::StandaloneCallContext::CALL() {
  return getToken(CypherParser::CALL, 0);
}

CypherParser::InvocationNameContext* CypherParser::StandaloneCallContext::invocationName() {
  return getRuleContext<CypherParser::InvocationNameContext>(0);
}

CypherParser::ParenExpressionChainContext* CypherParser::StandaloneCallContext::parenExpressionChain() {
  return getRuleContext<CypherParser::ParenExpressionChainContext>(0);
}

tree::TerminalNode* CypherParser::StandaloneCallContext::YIELD() {
  return getToken(CypherParser::YIELD, 0);
}

tree::TerminalNode* CypherParser::StandaloneCallContext::MULT() {
  return getToken(CypherParser::MULT, 0);
}

CypherParser::YieldItemsContext* CypherParser::StandaloneCallContext::yieldItems() {
  return getRuleContext<CypherParser::YieldItemsContext>(0);
}


size_t CypherParser::StandaloneCallContext::getRuleIndex() const {
  return CypherParser::RuleStandaloneCall;
}


std::any CypherParser::StandaloneCallContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitStandaloneCall(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::StandaloneCallContext* CypherParser::standaloneCall() {
  StandaloneCallContext *_localctx = _tracker.createInstance<StandaloneCallContext>(_ctx, getState());
  enterRule(_localctx, 8, CypherParser::RuleStandaloneCall);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(204);
    match(CypherParser::CALL);
    setState(205);
    invocationName();
    setState(207);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LPAREN) {
      setState(206);
      parenExpressionChain();
    }
    setState(214);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(209);
      match(CypherParser::YIELD);
      setState(212);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::MULT: {
          setState(210);
          match(CypherParser::MULT);
          break;
        }

        case CypherParser::FILTER:
        case CypherParser::EXTRACT:
        case CypherParser::COUNT:
        case CypherParser::ANY:
        case CypherParser::NONE:
        case CypherParser::SINGLE:
        case CypherParser::ID:
        case CypherParser::ESC_LITERAL: {
          setState(211);
          yieldItems();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ReturnStContext ------------------------------------------------------------------

CypherParser::ReturnStContext::ReturnStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ReturnStContext::RETURN() {
  return getToken(CypherParser::RETURN, 0);
}

CypherParser::ProjectionBodyContext* CypherParser::ReturnStContext::projectionBody() {
  return getRuleContext<CypherParser::ProjectionBodyContext>(0);
}


size_t CypherParser::ReturnStContext::getRuleIndex() const {
  return CypherParser::RuleReturnSt;
}


std::any CypherParser::ReturnStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitReturnSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ReturnStContext* CypherParser::returnSt() {
  ReturnStContext *_localctx = _tracker.createInstance<ReturnStContext>(_ctx, getState());
  enterRule(_localctx, 10, CypherParser::RuleReturnSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(216);
    match(CypherParser::RETURN);
    setState(217);
    projectionBody();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WithStContext ------------------------------------------------------------------

CypherParser::WithStContext::WithStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::WithStContext::WITH() {
  return getToken(CypherParser::WITH, 0);
}

CypherParser::ProjectionBodyContext* CypherParser::WithStContext::projectionBody() {
  return getRuleContext<CypherParser::ProjectionBodyContext>(0);
}

CypherParser::WhereContext* CypherParser::WithStContext::where() {
  return getRuleContext<CypherParser::WhereContext>(0);
}


size_t CypherParser::WithStContext::getRuleIndex() const {
  return CypherParser::RuleWithSt;
}


std::any CypherParser::WithStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitWithSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::WithStContext* CypherParser::withSt() {
  WithStContext *_localctx = _tracker.createInstance<WithStContext>(_ctx, getState());
  enterRule(_localctx, 12, CypherParser::RuleWithSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(219);
    match(CypherParser::WITH);
    setState(220);
    projectionBody();
    setState(222);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(221);
      where();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SkipStContext ------------------------------------------------------------------

CypherParser::SkipStContext::SkipStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::SkipStContext::SKIP_W() {
  return getToken(CypherParser::SKIP_W, 0);
}

CypherParser::ExpressionContext* CypherParser::SkipStContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}


size_t CypherParser::SkipStContext::getRuleIndex() const {
  return CypherParser::RuleSkipSt;
}


std::any CypherParser::SkipStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSkipSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SkipStContext* CypherParser::skipSt() {
  SkipStContext *_localctx = _tracker.createInstance<SkipStContext>(_ctx, getState());
  enterRule(_localctx, 14, CypherParser::RuleSkipSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(224);
    match(CypherParser::SKIP_W);
    setState(225);
    expression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LimitStContext ------------------------------------------------------------------

CypherParser::LimitStContext::LimitStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::LimitStContext::LIMIT() {
  return getToken(CypherParser::LIMIT, 0);
}

CypherParser::ExpressionContext* CypherParser::LimitStContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}


size_t CypherParser::LimitStContext::getRuleIndex() const {
  return CypherParser::RuleLimitSt;
}


std::any CypherParser::LimitStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitLimitSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::LimitStContext* CypherParser::limitSt() {
  LimitStContext *_localctx = _tracker.createInstance<LimitStContext>(_ctx, getState());
  enterRule(_localctx, 16, CypherParser::RuleLimitSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(227);
    match(CypherParser::LIMIT);
    setState(228);
    expression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ProjectionBodyContext ------------------------------------------------------------------

CypherParser::ProjectionBodyContext::ProjectionBodyContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ProjectionItemsContext* CypherParser::ProjectionBodyContext::projectionItems() {
  return getRuleContext<CypherParser::ProjectionItemsContext>(0);
}

tree::TerminalNode* CypherParser::ProjectionBodyContext::DISTINCT() {
  return getToken(CypherParser::DISTINCT, 0);
}

CypherParser::OrderStContext* CypherParser::ProjectionBodyContext::orderSt() {
  return getRuleContext<CypherParser::OrderStContext>(0);
}

CypherParser::SkipStContext* CypherParser::ProjectionBodyContext::skipSt() {
  return getRuleContext<CypherParser::SkipStContext>(0);
}

CypherParser::LimitStContext* CypherParser::ProjectionBodyContext::limitSt() {
  return getRuleContext<CypherParser::LimitStContext>(0);
}


size_t CypherParser::ProjectionBodyContext::getRuleIndex() const {
  return CypherParser::RuleProjectionBody;
}


std::any CypherParser::ProjectionBodyContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitProjectionBody(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ProjectionBodyContext* CypherParser::projectionBody() {
  ProjectionBodyContext *_localctx = _tracker.createInstance<ProjectionBodyContext>(_ctx, getState());
  enterRule(_localctx, 18, CypherParser::RuleProjectionBody);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(231);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(230);
      match(CypherParser::DISTINCT);
    }
    setState(233);
    projectionItems();
    setState(235);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ORDER) {
      setState(234);
      orderSt();
    }
    setState(238);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SKIP_W) {
      setState(237);
      skipSt();
    }
    setState(241);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LIMIT) {
      setState(240);
      limitSt();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ProjectionItemsContext ------------------------------------------------------------------

CypherParser::ProjectionItemsContext::ProjectionItemsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ProjectionItemsContext::MULT() {
  return getToken(CypherParser::MULT, 0);
}

std::vector<CypherParser::ProjectionItemContext *> CypherParser::ProjectionItemsContext::projectionItem() {
  return getRuleContexts<CypherParser::ProjectionItemContext>();
}

CypherParser::ProjectionItemContext* CypherParser::ProjectionItemsContext::projectionItem(size_t i) {
  return getRuleContext<CypherParser::ProjectionItemContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::ProjectionItemsContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::ProjectionItemsContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::ProjectionItemsContext::getRuleIndex() const {
  return CypherParser::RuleProjectionItems;
}


std::any CypherParser::ProjectionItemsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitProjectionItems(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ProjectionItemsContext* CypherParser::projectionItems() {
  ProjectionItemsContext *_localctx = _tracker.createInstance<ProjectionItemsContext>(_ctx, getState());
  enterRule(_localctx, 20, CypherParser::RuleProjectionItems);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(245);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MULT: {
        setState(243);
        match(CypherParser::MULT);
        break;
      }

      case CypherParser::LPAREN:
      case CypherParser::LBRACE:
      case CypherParser::LBRACK:
      case CypherParser::SUB:
      case CypherParser::PLUS:
      case CypherParser::DOLLAR:
      case CypherParser::FILTER:
      case CypherParser::EXTRACT:
      case CypherParser::COUNT:
      case CypherParser::ANY:
      case CypherParser::NONE:
      case CypherParser::SINGLE:
      case CypherParser::ALL:
      case CypherParser::EXISTS:
      case CypherParser::NOT:
      case CypherParser::FALSE:
      case CypherParser::TRUE:
      case CypherParser::NULL_W:
      case CypherParser::CASE:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL:
      case CypherParser::CHAR_LITERAL:
      case CypherParser::STRING_LITERAL:
      case CypherParser::DIGIT: {
        setState(244);
        projectionItem();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(251);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(247);
      match(CypherParser::COMMA);
      setState(248);
      projectionItem();
      setState(253);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ProjectionItemContext ------------------------------------------------------------------

CypherParser::ProjectionItemContext::ProjectionItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ExpressionContext* CypherParser::ProjectionItemContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

tree::TerminalNode* CypherParser::ProjectionItemContext::AS() {
  return getToken(CypherParser::AS, 0);
}

CypherParser::SymbolContext* CypherParser::ProjectionItemContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}


size_t CypherParser::ProjectionItemContext::getRuleIndex() const {
  return CypherParser::RuleProjectionItem;
}


std::any CypherParser::ProjectionItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitProjectionItem(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ProjectionItemContext* CypherParser::projectionItem() {
  ProjectionItemContext *_localctx = _tracker.createInstance<ProjectionItemContext>(_ctx, getState());
  enterRule(_localctx, 22, CypherParser::RuleProjectionItem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(254);
    expression();
    setState(257);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::AS) {
      setState(255);
      match(CypherParser::AS);
      setState(256);
      symbol();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- OrderItemContext ------------------------------------------------------------------

CypherParser::OrderItemContext::OrderItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ExpressionContext* CypherParser::OrderItemContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

tree::TerminalNode* CypherParser::OrderItemContext::ASCENDING() {
  return getToken(CypherParser::ASCENDING, 0);
}

tree::TerminalNode* CypherParser::OrderItemContext::ASC() {
  return getToken(CypherParser::ASC, 0);
}

tree::TerminalNode* CypherParser::OrderItemContext::DESCENDING() {
  return getToken(CypherParser::DESCENDING, 0);
}

tree::TerminalNode* CypherParser::OrderItemContext::DESC() {
  return getToken(CypherParser::DESC, 0);
}


size_t CypherParser::OrderItemContext::getRuleIndex() const {
  return CypherParser::RuleOrderItem;
}


std::any CypherParser::OrderItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitOrderItem(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::OrderItemContext* CypherParser::orderItem() {
  OrderItemContext *_localctx = _tracker.createInstance<OrderItemContext>(_ctx, getState());
  enterRule(_localctx, 24, CypherParser::RuleOrderItem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(259);
    expression();
    setState(261);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 27212912787456) != 0)) {
      setState(260);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 27212912787456) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- OrderStContext ------------------------------------------------------------------

CypherParser::OrderStContext::OrderStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::OrderStContext::ORDER() {
  return getToken(CypherParser::ORDER, 0);
}

tree::TerminalNode* CypherParser::OrderStContext::BY() {
  return getToken(CypherParser::BY, 0);
}

std::vector<CypherParser::OrderItemContext *> CypherParser::OrderStContext::orderItem() {
  return getRuleContexts<CypherParser::OrderItemContext>();
}

CypherParser::OrderItemContext* CypherParser::OrderStContext::orderItem(size_t i) {
  return getRuleContext<CypherParser::OrderItemContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::OrderStContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::OrderStContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::OrderStContext::getRuleIndex() const {
  return CypherParser::RuleOrderSt;
}


std::any CypherParser::OrderStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitOrderSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::OrderStContext* CypherParser::orderSt() {
  OrderStContext *_localctx = _tracker.createInstance<OrderStContext>(_ctx, getState());
  enterRule(_localctx, 26, CypherParser::RuleOrderSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(263);
    match(CypherParser::ORDER);
    setState(264);
    match(CypherParser::BY);
    setState(265);
    orderItem();
    setState(270);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(266);
      match(CypherParser::COMMA);
      setState(267);
      orderItem();
      setState(272);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SinglePartQContext ------------------------------------------------------------------

CypherParser::SinglePartQContext::SinglePartQContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ReturnStContext* CypherParser::SinglePartQContext::returnSt() {
  return getRuleContext<CypherParser::ReturnStContext>(0);
}

std::vector<CypherParser::ReadingStatementContext *> CypherParser::SinglePartQContext::readingStatement() {
  return getRuleContexts<CypherParser::ReadingStatementContext>();
}

CypherParser::ReadingStatementContext* CypherParser::SinglePartQContext::readingStatement(size_t i) {
  return getRuleContext<CypherParser::ReadingStatementContext>(i);
}

std::vector<CypherParser::UpdatingStatementContext *> CypherParser::SinglePartQContext::updatingStatement() {
  return getRuleContexts<CypherParser::UpdatingStatementContext>();
}

CypherParser::UpdatingStatementContext* CypherParser::SinglePartQContext::updatingStatement(size_t i) {
  return getRuleContext<CypherParser::UpdatingStatementContext>(i);
}


size_t CypherParser::SinglePartQContext::getRuleIndex() const {
  return CypherParser::RuleSinglePartQ;
}


std::any CypherParser::SinglePartQContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSinglePartQ(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SinglePartQContext* CypherParser::singlePartQ() {
  SinglePartQContext *_localctx = _tracker.createInstance<SinglePartQContext>(_ctx, getState());
  enterRule(_localctx, 28, CypherParser::RuleSinglePartQ);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(276);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2310909559331356672) != 0)) {
      setState(273);
      readingStatement();
      setState(278);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(288);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::RETURN: {
        setState(279);
        returnSt();
        break;
      }

      case CypherParser::CREATE:
      case CypherParser::DELETE:
      case CypherParser::DETACH:
      case CypherParser::MERGE:
      case CypherParser::REMOVE:
      case CypherParser::SET: {
        setState(281); 
        _errHandler->sync(this);
        _la = _input->LA(1);
        do {
          setState(280);
          updatingStatement();
          setState(283); 
          _errHandler->sync(this);
          _la = _input->LA(1);
        } while ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 91239673896108032) != 0));
        setState(286);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::RETURN) {
          setState(285);
          returnSt();
        }
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MultiPartQContext ------------------------------------------------------------------

CypherParser::MultiPartQContext::MultiPartQContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SinglePartQContext* CypherParser::MultiPartQContext::singlePartQ() {
  return getRuleContext<CypherParser::SinglePartQContext>(0);
}

std::vector<CypherParser::ReadingStatementContext *> CypherParser::MultiPartQContext::readingStatement() {
  return getRuleContexts<CypherParser::ReadingStatementContext>();
}

CypherParser::ReadingStatementContext* CypherParser::MultiPartQContext::readingStatement(size_t i) {
  return getRuleContext<CypherParser::ReadingStatementContext>(i);
}

std::vector<CypherParser::WithStContext *> CypherParser::MultiPartQContext::withSt() {
  return getRuleContexts<CypherParser::WithStContext>();
}

CypherParser::WithStContext* CypherParser::MultiPartQContext::withSt(size_t i) {
  return getRuleContext<CypherParser::WithStContext>(i);
}

std::vector<CypherParser::UpdatingStatementContext *> CypherParser::MultiPartQContext::updatingStatement() {
  return getRuleContexts<CypherParser::UpdatingStatementContext>();
}

CypherParser::UpdatingStatementContext* CypherParser::MultiPartQContext::updatingStatement(size_t i) {
  return getRuleContext<CypherParser::UpdatingStatementContext>(i);
}


size_t CypherParser::MultiPartQContext::getRuleIndex() const {
  return CypherParser::RuleMultiPartQ;
}


std::any CypherParser::MultiPartQContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMultiPartQ(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MultiPartQContext* CypherParser::multiPartQ() {
  MultiPartQContext *_localctx = _tracker.createInstance<MultiPartQContext>(_ctx, getState());
  enterRule(_localctx, 30, CypherParser::RuleMultiPartQ);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(293);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2310909559331356672) != 0)) {
      setState(290);
      readingStatement();
      setState(295);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(303); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(299);
              _errHandler->sync(this);
              _la = _input->LA(1);
              while ((((_la & ~ 0x3fULL) == 0) &&
                ((1ULL << _la) & 91239673896108032) != 0)) {
                setState(296);
                updatingStatement();
                setState(301);
                _errHandler->sync(this);
                _la = _input->LA(1);
              }
              setState(302);
              withSt();
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(305); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
    setState(307);
    singlePartQ();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MatchStContext ------------------------------------------------------------------

CypherParser::MatchStContext::MatchStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::MatchStContext::MATCH() {
  return getToken(CypherParser::MATCH, 0);
}

CypherParser::PatternWhereContext* CypherParser::MatchStContext::patternWhere() {
  return getRuleContext<CypherParser::PatternWhereContext>(0);
}

tree::TerminalNode* CypherParser::MatchStContext::OPTIONAL() {
  return getToken(CypherParser::OPTIONAL, 0);
}


size_t CypherParser::MatchStContext::getRuleIndex() const {
  return CypherParser::RuleMatchSt;
}


std::any CypherParser::MatchStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMatchSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MatchStContext* CypherParser::matchSt() {
  MatchStContext *_localctx = _tracker.createInstance<MatchStContext>(_ctx, getState());
  enterRule(_localctx, 32, CypherParser::RuleMatchSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(310);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::OPTIONAL) {
      setState(309);
      match(CypherParser::OPTIONAL);
    }
    setState(312);
    match(CypherParser::MATCH);
    setState(313);
    patternWhere();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnwindStContext ------------------------------------------------------------------

CypherParser::UnwindStContext::UnwindStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::UnwindStContext::UNWIND() {
  return getToken(CypherParser::UNWIND, 0);
}

CypherParser::ExpressionContext* CypherParser::UnwindStContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

tree::TerminalNode* CypherParser::UnwindStContext::AS() {
  return getToken(CypherParser::AS, 0);
}

CypherParser::SymbolContext* CypherParser::UnwindStContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}


size_t CypherParser::UnwindStContext::getRuleIndex() const {
  return CypherParser::RuleUnwindSt;
}


std::any CypherParser::UnwindStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitUnwindSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::UnwindStContext* CypherParser::unwindSt() {
  UnwindStContext *_localctx = _tracker.createInstance<UnwindStContext>(_ctx, getState());
  enterRule(_localctx, 34, CypherParser::RuleUnwindSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(315);
    match(CypherParser::UNWIND);
    setState(316);
    expression();
    setState(317);
    match(CypherParser::AS);
    setState(318);
    symbol();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ReadingStatementContext ------------------------------------------------------------------

CypherParser::ReadingStatementContext::ReadingStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::MatchStContext* CypherParser::ReadingStatementContext::matchSt() {
  return getRuleContext<CypherParser::MatchStContext>(0);
}

CypherParser::UnwindStContext* CypherParser::ReadingStatementContext::unwindSt() {
  return getRuleContext<CypherParser::UnwindStContext>(0);
}

CypherParser::QueryCallStContext* CypherParser::ReadingStatementContext::queryCallSt() {
  return getRuleContext<CypherParser::QueryCallStContext>(0);
}


size_t CypherParser::ReadingStatementContext::getRuleIndex() const {
  return CypherParser::RuleReadingStatement;
}


std::any CypherParser::ReadingStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitReadingStatement(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ReadingStatementContext* CypherParser::readingStatement() {
  ReadingStatementContext *_localctx = _tracker.createInstance<ReadingStatementContext>(_ctx, getState());
  enterRule(_localctx, 36, CypherParser::RuleReadingStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(323);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(320);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(321);
        unwindSt();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(322);
        queryCallSt();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UpdatingStatementContext ------------------------------------------------------------------

CypherParser::UpdatingStatementContext::UpdatingStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::CreateStContext* CypherParser::UpdatingStatementContext::createSt() {
  return getRuleContext<CypherParser::CreateStContext>(0);
}

CypherParser::MergeStContext* CypherParser::UpdatingStatementContext::mergeSt() {
  return getRuleContext<CypherParser::MergeStContext>(0);
}

CypherParser::DeleteStContext* CypherParser::UpdatingStatementContext::deleteSt() {
  return getRuleContext<CypherParser::DeleteStContext>(0);
}

CypherParser::SetStContext* CypherParser::UpdatingStatementContext::setSt() {
  return getRuleContext<CypherParser::SetStContext>(0);
}

CypherParser::RemoveStContext* CypherParser::UpdatingStatementContext::removeSt() {
  return getRuleContext<CypherParser::RemoveStContext>(0);
}


size_t CypherParser::UpdatingStatementContext::getRuleIndex() const {
  return CypherParser::RuleUpdatingStatement;
}


std::any CypherParser::UpdatingStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitUpdatingStatement(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::UpdatingStatementContext* CypherParser::updatingStatement() {
  UpdatingStatementContext *_localctx = _tracker.createInstance<UpdatingStatementContext>(_ctx, getState());
  enterRule(_localctx, 38, CypherParser::RuleUpdatingStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(330);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(325);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(326);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(327);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(328);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(329);
        removeSt();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeleteStContext ------------------------------------------------------------------

CypherParser::DeleteStContext::DeleteStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::DeleteStContext::DELETE() {
  return getToken(CypherParser::DELETE, 0);
}

CypherParser::ExpressionChainContext* CypherParser::DeleteStContext::expressionChain() {
  return getRuleContext<CypherParser::ExpressionChainContext>(0);
}

tree::TerminalNode* CypherParser::DeleteStContext::DETACH() {
  return getToken(CypherParser::DETACH, 0);
}


size_t CypherParser::DeleteStContext::getRuleIndex() const {
  return CypherParser::RuleDeleteSt;
}


std::any CypherParser::DeleteStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitDeleteSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::DeleteStContext* CypherParser::deleteSt() {
  DeleteStContext *_localctx = _tracker.createInstance<DeleteStContext>(_ctx, getState());
  enterRule(_localctx, 40, CypherParser::RuleDeleteSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(333);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DETACH) {
      setState(332);
      match(CypherParser::DETACH);
    }
    setState(335);
    match(CypherParser::DELETE);
    setState(336);
    expressionChain();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RemoveStContext ------------------------------------------------------------------

CypherParser::RemoveStContext::RemoveStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::RemoveStContext::REMOVE() {
  return getToken(CypherParser::REMOVE, 0);
}

std::vector<CypherParser::RemoveItemContext *> CypherParser::RemoveStContext::removeItem() {
  return getRuleContexts<CypherParser::RemoveItemContext>();
}

CypherParser::RemoveItemContext* CypherParser::RemoveStContext::removeItem(size_t i) {
  return getRuleContext<CypherParser::RemoveItemContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::RemoveStContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::RemoveStContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::RemoveStContext::getRuleIndex() const {
  return CypherParser::RuleRemoveSt;
}


std::any CypherParser::RemoveStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRemoveSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RemoveStContext* CypherParser::removeSt() {
  RemoveStContext *_localctx = _tracker.createInstance<RemoveStContext>(_ctx, getState());
  enterRule(_localctx, 42, CypherParser::RuleRemoveSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(338);
    match(CypherParser::REMOVE);
    setState(339);
    removeItem();
    setState(344);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(340);
      match(CypherParser::COMMA);
      setState(341);
      removeItem();
      setState(346);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RemoveItemContext ------------------------------------------------------------------

CypherParser::RemoveItemContext::RemoveItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SymbolContext* CypherParser::RemoveItemContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

CypherParser::NodeLabelsContext* CypherParser::RemoveItemContext::nodeLabels() {
  return getRuleContext<CypherParser::NodeLabelsContext>(0);
}

CypherParser::PropertyExpressionContext* CypherParser::RemoveItemContext::propertyExpression() {
  return getRuleContext<CypherParser::PropertyExpressionContext>(0);
}


size_t CypherParser::RemoveItemContext::getRuleIndex() const {
  return CypherParser::RuleRemoveItem;
}


std::any CypherParser::RemoveItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRemoveItem(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RemoveItemContext* CypherParser::removeItem() {
  RemoveItemContext *_localctx = _tracker.createInstance<RemoveItemContext>(_ctx, getState());
  enterRule(_localctx, 44, CypherParser::RuleRemoveItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(351);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(347);
      symbol();
      setState(348);
      nodeLabels();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(350);
      propertyExpression();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- QueryCallStContext ------------------------------------------------------------------

CypherParser::QueryCallStContext::QueryCallStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::QueryCallStContext::CALL() {
  return getToken(CypherParser::CALL, 0);
}

CypherParser::InvocationNameContext* CypherParser::QueryCallStContext::invocationName() {
  return getRuleContext<CypherParser::InvocationNameContext>(0);
}

CypherParser::ParenExpressionChainContext* CypherParser::QueryCallStContext::parenExpressionChain() {
  return getRuleContext<CypherParser::ParenExpressionChainContext>(0);
}

tree::TerminalNode* CypherParser::QueryCallStContext::YIELD() {
  return getToken(CypherParser::YIELD, 0);
}

CypherParser::YieldItemsContext* CypherParser::QueryCallStContext::yieldItems() {
  return getRuleContext<CypherParser::YieldItemsContext>(0);
}


size_t CypherParser::QueryCallStContext::getRuleIndex() const {
  return CypherParser::RuleQueryCallSt;
}


std::any CypherParser::QueryCallStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitQueryCallSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::QueryCallStContext* CypherParser::queryCallSt() {
  QueryCallStContext *_localctx = _tracker.createInstance<QueryCallStContext>(_ctx, getState());
  enterRule(_localctx, 46, CypherParser::RuleQueryCallSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(353);
    match(CypherParser::CALL);
    setState(354);
    invocationName();
    setState(355);
    parenExpressionChain();
    setState(358);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(356);
      match(CypherParser::YIELD);
      setState(357);
      yieldItems();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParenExpressionChainContext ------------------------------------------------------------------

CypherParser::ParenExpressionChainContext::ParenExpressionChainContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ParenExpressionChainContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

tree::TerminalNode* CypherParser::ParenExpressionChainContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}

CypherParser::ExpressionChainContext* CypherParser::ParenExpressionChainContext::expressionChain() {
  return getRuleContext<CypherParser::ExpressionChainContext>(0);
}


size_t CypherParser::ParenExpressionChainContext::getRuleIndex() const {
  return CypherParser::RuleParenExpressionChain;
}


std::any CypherParser::ParenExpressionChainContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitParenExpressionChain(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ParenExpressionChainContext* CypherParser::parenExpressionChain() {
  ParenExpressionChainContext *_localctx = _tracker.createInstance<ParenExpressionChainContext>(_ctx, getState());
  enterRule(_localctx, 48, CypherParser::RuleParenExpressionChain);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(360);
    match(CypherParser::LPAREN);
    setState(362);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 130027633) != 0)) {
      setState(361);
      expressionChain();
    }
    setState(364);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- YieldItemsContext ------------------------------------------------------------------

CypherParser::YieldItemsContext::YieldItemsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::YieldItemContext *> CypherParser::YieldItemsContext::yieldItem() {
  return getRuleContexts<CypherParser::YieldItemContext>();
}

CypherParser::YieldItemContext* CypherParser::YieldItemsContext::yieldItem(size_t i) {
  return getRuleContext<CypherParser::YieldItemContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::YieldItemsContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::YieldItemsContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}

CypherParser::WhereContext* CypherParser::YieldItemsContext::where() {
  return getRuleContext<CypherParser::WhereContext>(0);
}


size_t CypherParser::YieldItemsContext::getRuleIndex() const {
  return CypherParser::RuleYieldItems;
}


std::any CypherParser::YieldItemsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitYieldItems(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::YieldItemsContext* CypherParser::yieldItems() {
  YieldItemsContext *_localctx = _tracker.createInstance<YieldItemsContext>(_ctx, getState());
  enterRule(_localctx, 50, CypherParser::RuleYieldItems);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(366);
    yieldItem();
    setState(371);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(367);
      match(CypherParser::COMMA);
      setState(368);
      yieldItem();
      setState(373);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(375);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(374);
      where();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- YieldItemContext ------------------------------------------------------------------

CypherParser::YieldItemContext::YieldItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::SymbolContext *> CypherParser::YieldItemContext::symbol() {
  return getRuleContexts<CypherParser::SymbolContext>();
}

CypherParser::SymbolContext* CypherParser::YieldItemContext::symbol(size_t i) {
  return getRuleContext<CypherParser::SymbolContext>(i);
}

tree::TerminalNode* CypherParser::YieldItemContext::AS() {
  return getToken(CypherParser::AS, 0);
}


size_t CypherParser::YieldItemContext::getRuleIndex() const {
  return CypherParser::RuleYieldItem;
}


std::any CypherParser::YieldItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitYieldItem(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::YieldItemContext* CypherParser::yieldItem() {
  YieldItemContext *_localctx = _tracker.createInstance<YieldItemContext>(_ctx, getState());
  enterRule(_localctx, 52, CypherParser::RuleYieldItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(380);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx)) {
    case 1: {
      setState(377);
      symbol();
      setState(378);
      match(CypherParser::AS);
      break;
    }

    default:
      break;
    }
    setState(382);
    symbol();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MergeStContext ------------------------------------------------------------------

CypherParser::MergeStContext::MergeStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::MergeStContext::MERGE() {
  return getToken(CypherParser::MERGE, 0);
}

CypherParser::PatternPartContext* CypherParser::MergeStContext::patternPart() {
  return getRuleContext<CypherParser::PatternPartContext>(0);
}

std::vector<CypherParser::MergeActionContext *> CypherParser::MergeStContext::mergeAction() {
  return getRuleContexts<CypherParser::MergeActionContext>();
}

CypherParser::MergeActionContext* CypherParser::MergeStContext::mergeAction(size_t i) {
  return getRuleContext<CypherParser::MergeActionContext>(i);
}


size_t CypherParser::MergeStContext::getRuleIndex() const {
  return CypherParser::RuleMergeSt;
}


std::any CypherParser::MergeStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMergeSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MergeStContext* CypherParser::mergeSt() {
  MergeStContext *_localctx = _tracker.createInstance<MergeStContext>(_ctx, getState());
  enterRule(_localctx, 54, CypherParser::RuleMergeSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(384);
    match(CypherParser::MERGE);
    setState(385);
    patternPart();
    setState(389);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::ON) {
      setState(386);
      mergeAction();
      setState(391);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MergeActionContext ------------------------------------------------------------------

CypherParser::MergeActionContext::MergeActionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::MergeActionContext::ON() {
  return getToken(CypherParser::ON, 0);
}

CypherParser::SetStContext* CypherParser::MergeActionContext::setSt() {
  return getRuleContext<CypherParser::SetStContext>(0);
}

tree::TerminalNode* CypherParser::MergeActionContext::MATCH() {
  return getToken(CypherParser::MATCH, 0);
}

tree::TerminalNode* CypherParser::MergeActionContext::CREATE() {
  return getToken(CypherParser::CREATE, 0);
}


size_t CypherParser::MergeActionContext::getRuleIndex() const {
  return CypherParser::RuleMergeAction;
}


std::any CypherParser::MergeActionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMergeAction(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MergeActionContext* CypherParser::mergeAction() {
  MergeActionContext *_localctx = _tracker.createInstance<MergeActionContext>(_ctx, getState());
  enterRule(_localctx, 56, CypherParser::RuleMergeAction);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(392);
    match(CypherParser::ON);
    setState(393);
    _la = _input->LA(1);
    if (!(_la == CypherParser::CREATE

    || _la == CypherParser::MATCH)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(394);
    setSt();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SetStContext ------------------------------------------------------------------

CypherParser::SetStContext::SetStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::SetStContext::SET() {
  return getToken(CypherParser::SET, 0);
}

std::vector<CypherParser::SetItemContext *> CypherParser::SetStContext::setItem() {
  return getRuleContexts<CypherParser::SetItemContext>();
}

CypherParser::SetItemContext* CypherParser::SetStContext::setItem(size_t i) {
  return getRuleContext<CypherParser::SetItemContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::SetStContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::SetStContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::SetStContext::getRuleIndex() const {
  return CypherParser::RuleSetSt;
}


std::any CypherParser::SetStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSetSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SetStContext* CypherParser::setSt() {
  SetStContext *_localctx = _tracker.createInstance<SetStContext>(_ctx, getState());
  enterRule(_localctx, 58, CypherParser::RuleSetSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(396);
    match(CypherParser::SET);
    setState(397);
    setItem();
    setState(402);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(398);
      match(CypherParser::COMMA);
      setState(399);
      setItem();
      setState(404);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SetItemContext ------------------------------------------------------------------

CypherParser::SetItemContext::SetItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::PropertyExpressionContext* CypherParser::SetItemContext::propertyExpression() {
  return getRuleContext<CypherParser::PropertyExpressionContext>(0);
}

tree::TerminalNode* CypherParser::SetItemContext::ASSIGN() {
  return getToken(CypherParser::ASSIGN, 0);
}

CypherParser::ExpressionContext* CypherParser::SetItemContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

CypherParser::SymbolContext* CypherParser::SetItemContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

tree::TerminalNode* CypherParser::SetItemContext::ADD_ASSIGN() {
  return getToken(CypherParser::ADD_ASSIGN, 0);
}

CypherParser::NodeLabelsContext* CypherParser::SetItemContext::nodeLabels() {
  return getRuleContext<CypherParser::NodeLabelsContext>(0);
}


size_t CypherParser::SetItemContext::getRuleIndex() const {
  return CypherParser::RuleSetItem;
}


std::any CypherParser::SetItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSetItem(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SetItemContext* CypherParser::setItem() {
  SetItemContext *_localctx = _tracker.createInstance<SetItemContext>(_ctx, getState());
  enterRule(_localctx, 60, CypherParser::RuleSetItem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(416);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(405);
      propertyExpression();
      setState(406);
      match(CypherParser::ASSIGN);
      setState(407);
      expression();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(409);
      symbol();
      setState(410);
      _la = _input->LA(1);
      if (!(_la == CypherParser::ASSIGN

      || _la == CypherParser::ADD_ASSIGN)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(411);
      expression();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(413);
      symbol();
      setState(414);
      nodeLabels();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NodeLabelsContext ------------------------------------------------------------------

CypherParser::NodeLabelsContext::NodeLabelsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> CypherParser::NodeLabelsContext::COLON() {
  return getTokens(CypherParser::COLON);
}

tree::TerminalNode* CypherParser::NodeLabelsContext::COLON(size_t i) {
  return getToken(CypherParser::COLON, i);
}

std::vector<CypherParser::NameContext *> CypherParser::NodeLabelsContext::name() {
  return getRuleContexts<CypherParser::NameContext>();
}

CypherParser::NameContext* CypherParser::NodeLabelsContext::name(size_t i) {
  return getRuleContext<CypherParser::NameContext>(i);
}


size_t CypherParser::NodeLabelsContext::getRuleIndex() const {
  return CypherParser::RuleNodeLabels;
}


std::any CypherParser::NodeLabelsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitNodeLabels(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::NodeLabelsContext* CypherParser::nodeLabels() {
  NodeLabelsContext *_localctx = _tracker.createInstance<NodeLabelsContext>(_ctx, getState());
  enterRule(_localctx, 62, CypherParser::RuleNodeLabels);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(420); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(418);
      match(CypherParser::COLON);
      setState(419);
      name();
      setState(422); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == CypherParser::COLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CreateStContext ------------------------------------------------------------------

CypherParser::CreateStContext::CreateStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::CreateStContext::CREATE() {
  return getToken(CypherParser::CREATE, 0);
}

CypherParser::PatternContext* CypherParser::CreateStContext::pattern() {
  return getRuleContext<CypherParser::PatternContext>(0);
}


size_t CypherParser::CreateStContext::getRuleIndex() const {
  return CypherParser::RuleCreateSt;
}


std::any CypherParser::CreateStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitCreateSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::CreateStContext* CypherParser::createSt() {
  CreateStContext *_localctx = _tracker.createInstance<CreateStContext>(_ctx, getState());
  enterRule(_localctx, 64, CypherParser::RuleCreateSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(424);
    match(CypherParser::CREATE);
    setState(425);
    pattern();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PatternWhereContext ------------------------------------------------------------------

CypherParser::PatternWhereContext::PatternWhereContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::PatternContext* CypherParser::PatternWhereContext::pattern() {
  return getRuleContext<CypherParser::PatternContext>(0);
}

CypherParser::WhereContext* CypherParser::PatternWhereContext::where() {
  return getRuleContext<CypherParser::WhereContext>(0);
}


size_t CypherParser::PatternWhereContext::getRuleIndex() const {
  return CypherParser::RulePatternWhere;
}


std::any CypherParser::PatternWhereContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPatternWhere(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PatternWhereContext* CypherParser::patternWhere() {
  PatternWhereContext *_localctx = _tracker.createInstance<PatternWhereContext>(_ctx, getState());
  enterRule(_localctx, 66, CypherParser::RulePatternWhere);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(427);
    pattern();
    setState(429);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(428);
      where();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- WhereContext ------------------------------------------------------------------

CypherParser::WhereContext::WhereContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::WhereContext::WHERE() {
  return getToken(CypherParser::WHERE, 0);
}

CypherParser::ExpressionContext* CypherParser::WhereContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}


size_t CypherParser::WhereContext::getRuleIndex() const {
  return CypherParser::RuleWhere;
}


std::any CypherParser::WhereContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitWhere(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::WhereContext* CypherParser::where() {
  WhereContext *_localctx = _tracker.createInstance<WhereContext>(_ctx, getState());
  enterRule(_localctx, 68, CypherParser::RuleWhere);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(431);
    match(CypherParser::WHERE);
    setState(432);
    expression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PatternContext ------------------------------------------------------------------

CypherParser::PatternContext::PatternContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::PatternPartContext *> CypherParser::PatternContext::patternPart() {
  return getRuleContexts<CypherParser::PatternPartContext>();
}

CypherParser::PatternPartContext* CypherParser::PatternContext::patternPart(size_t i) {
  return getRuleContext<CypherParser::PatternPartContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::PatternContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::PatternContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::PatternContext::getRuleIndex() const {
  return CypherParser::RulePattern;
}


std::any CypherParser::PatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPattern(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PatternContext* CypherParser::pattern() {
  PatternContext *_localctx = _tracker.createInstance<PatternContext>(_ctx, getState());
  enterRule(_localctx, 70, CypherParser::RulePattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(434);
    patternPart();
    setState(439);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(435);
      match(CypherParser::COMMA);
      setState(436);
      patternPart();
      setState(441);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionContext ------------------------------------------------------------------

CypherParser::ExpressionContext::ExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::XorExpressionContext *> CypherParser::ExpressionContext::xorExpression() {
  return getRuleContexts<CypherParser::XorExpressionContext>();
}

CypherParser::XorExpressionContext* CypherParser::ExpressionContext::xorExpression(size_t i) {
  return getRuleContext<CypherParser::XorExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::ExpressionContext::OR() {
  return getTokens(CypherParser::OR);
}

tree::TerminalNode* CypherParser::ExpressionContext::OR(size_t i) {
  return getToken(CypherParser::OR, i);
}


size_t CypherParser::ExpressionContext::getRuleIndex() const {
  return CypherParser::RuleExpression;
}


std::any CypherParser::ExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ExpressionContext* CypherParser::expression() {
  ExpressionContext *_localctx = _tracker.createInstance<ExpressionContext>(_ctx, getState());
  enterRule(_localctx, 72, CypherParser::RuleExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(442);
    xorExpression();
    setState(447);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::OR) {
      setState(443);
      match(CypherParser::OR);
      setState(444);
      xorExpression();
      setState(449);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- XorExpressionContext ------------------------------------------------------------------

CypherParser::XorExpressionContext::XorExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::AndExpressionContext *> CypherParser::XorExpressionContext::andExpression() {
  return getRuleContexts<CypherParser::AndExpressionContext>();
}

CypherParser::AndExpressionContext* CypherParser::XorExpressionContext::andExpression(size_t i) {
  return getRuleContext<CypherParser::AndExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::XorExpressionContext::XOR() {
  return getTokens(CypherParser::XOR);
}

tree::TerminalNode* CypherParser::XorExpressionContext::XOR(size_t i) {
  return getToken(CypherParser::XOR, i);
}


size_t CypherParser::XorExpressionContext::getRuleIndex() const {
  return CypherParser::RuleXorExpression;
}


std::any CypherParser::XorExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitXorExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::XorExpressionContext* CypherParser::xorExpression() {
  XorExpressionContext *_localctx = _tracker.createInstance<XorExpressionContext>(_ctx, getState());
  enterRule(_localctx, 74, CypherParser::RuleXorExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(450);
    andExpression();
    setState(455);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::XOR) {
      setState(451);
      match(CypherParser::XOR);
      setState(452);
      andExpression();
      setState(457);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AndExpressionContext ------------------------------------------------------------------

CypherParser::AndExpressionContext::AndExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::NotExpressionContext *> CypherParser::AndExpressionContext::notExpression() {
  return getRuleContexts<CypherParser::NotExpressionContext>();
}

CypherParser::NotExpressionContext* CypherParser::AndExpressionContext::notExpression(size_t i) {
  return getRuleContext<CypherParser::NotExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::AndExpressionContext::AND() {
  return getTokens(CypherParser::AND);
}

tree::TerminalNode* CypherParser::AndExpressionContext::AND(size_t i) {
  return getToken(CypherParser::AND, i);
}


size_t CypherParser::AndExpressionContext::getRuleIndex() const {
  return CypherParser::RuleAndExpression;
}


std::any CypherParser::AndExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitAndExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::AndExpressionContext* CypherParser::andExpression() {
  AndExpressionContext *_localctx = _tracker.createInstance<AndExpressionContext>(_ctx, getState());
  enterRule(_localctx, 76, CypherParser::RuleAndExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(458);
    notExpression();
    setState(463);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::AND) {
      setState(459);
      match(CypherParser::AND);
      setState(460);
      notExpression();
      setState(465);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NotExpressionContext ------------------------------------------------------------------

CypherParser::NotExpressionContext::NotExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ComparisonExpressionContext* CypherParser::NotExpressionContext::comparisonExpression() {
  return getRuleContext<CypherParser::ComparisonExpressionContext>(0);
}

tree::TerminalNode* CypherParser::NotExpressionContext::NOT() {
  return getToken(CypherParser::NOT, 0);
}


size_t CypherParser::NotExpressionContext::getRuleIndex() const {
  return CypherParser::RuleNotExpression;
}


std::any CypherParser::NotExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitNotExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::NotExpressionContext* CypherParser::notExpression() {
  NotExpressionContext *_localctx = _tracker.createInstance<NotExpressionContext>(_ctx, getState());
  enterRule(_localctx, 78, CypherParser::RuleNotExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(467);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::NOT) {
      setState(466);
      match(CypherParser::NOT);
    }
    setState(469);
    comparisonExpression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ComparisonExpressionContext ------------------------------------------------------------------

CypherParser::ComparisonExpressionContext::ComparisonExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::AddSubExpressionContext *> CypherParser::ComparisonExpressionContext::addSubExpression() {
  return getRuleContexts<CypherParser::AddSubExpressionContext>();
}

CypherParser::AddSubExpressionContext* CypherParser::ComparisonExpressionContext::addSubExpression(size_t i) {
  return getRuleContext<CypherParser::AddSubExpressionContext>(i);
}

std::vector<CypherParser::ComparisonSignsContext *> CypherParser::ComparisonExpressionContext::comparisonSigns() {
  return getRuleContexts<CypherParser::ComparisonSignsContext>();
}

CypherParser::ComparisonSignsContext* CypherParser::ComparisonExpressionContext::comparisonSigns(size_t i) {
  return getRuleContext<CypherParser::ComparisonSignsContext>(i);
}


size_t CypherParser::ComparisonExpressionContext::getRuleIndex() const {
  return CypherParser::RuleComparisonExpression;
}


std::any CypherParser::ComparisonExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitComparisonExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ComparisonExpressionContext* CypherParser::comparisonExpression() {
  ComparisonExpressionContext *_localctx = _tracker.createInstance<ComparisonExpressionContext>(_ctx, getState());
  enterRule(_localctx, 80, CypherParser::RuleComparisonExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(471);
    addSubExpression();
    setState(477);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 250) != 0)) {
      setState(472);
      comparisonSigns();
      setState(473);
      addSubExpression();
      setState(479);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ComparisonSignsContext ------------------------------------------------------------------

CypherParser::ComparisonSignsContext::ComparisonSignsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ComparisonSignsContext::ASSIGN() {
  return getToken(CypherParser::ASSIGN, 0);
}

tree::TerminalNode* CypherParser::ComparisonSignsContext::LE() {
  return getToken(CypherParser::LE, 0);
}

tree::TerminalNode* CypherParser::ComparisonSignsContext::GE() {
  return getToken(CypherParser::GE, 0);
}

tree::TerminalNode* CypherParser::ComparisonSignsContext::GT() {
  return getToken(CypherParser::GT, 0);
}

tree::TerminalNode* CypherParser::ComparisonSignsContext::LT() {
  return getToken(CypherParser::LT, 0);
}

tree::TerminalNode* CypherParser::ComparisonSignsContext::NOT_EQUAL() {
  return getToken(CypherParser::NOT_EQUAL, 0);
}


size_t CypherParser::ComparisonSignsContext::getRuleIndex() const {
  return CypherParser::RuleComparisonSigns;
}


std::any CypherParser::ComparisonSignsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitComparisonSigns(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ComparisonSignsContext* CypherParser::comparisonSigns() {
  ComparisonSignsContext *_localctx = _tracker.createInstance<ComparisonSignsContext>(_ctx, getState());
  enterRule(_localctx, 82, CypherParser::RuleComparisonSigns);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(480);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 250) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AddSubExpressionContext ------------------------------------------------------------------

CypherParser::AddSubExpressionContext::AddSubExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::MultDivExpressionContext *> CypherParser::AddSubExpressionContext::multDivExpression() {
  return getRuleContexts<CypherParser::MultDivExpressionContext>();
}

CypherParser::MultDivExpressionContext* CypherParser::AddSubExpressionContext::multDivExpression(size_t i) {
  return getRuleContext<CypherParser::MultDivExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::AddSubExpressionContext::PLUS() {
  return getTokens(CypherParser::PLUS);
}

tree::TerminalNode* CypherParser::AddSubExpressionContext::PLUS(size_t i) {
  return getToken(CypherParser::PLUS, i);
}

std::vector<tree::TerminalNode *> CypherParser::AddSubExpressionContext::SUB() {
  return getTokens(CypherParser::SUB);
}

tree::TerminalNode* CypherParser::AddSubExpressionContext::SUB(size_t i) {
  return getToken(CypherParser::SUB, i);
}


size_t CypherParser::AddSubExpressionContext::getRuleIndex() const {
  return CypherParser::RuleAddSubExpression;
}


std::any CypherParser::AddSubExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitAddSubExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::AddSubExpressionContext* CypherParser::addSubExpression() {
  AddSubExpressionContext *_localctx = _tracker.createInstance<AddSubExpressionContext>(_ctx, getState());
  enterRule(_localctx, 84, CypherParser::RuleAddSubExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(482);
    multDivExpression();
    setState(487);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(483);
      _la = _input->LA(1);
      if (!(_la == CypherParser::SUB

      || _la == CypherParser::PLUS)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(484);
      multDivExpression();
      setState(489);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MultDivExpressionContext ------------------------------------------------------------------

CypherParser::MultDivExpressionContext::MultDivExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::PowerExpressionContext *> CypherParser::MultDivExpressionContext::powerExpression() {
  return getRuleContexts<CypherParser::PowerExpressionContext>();
}

CypherParser::PowerExpressionContext* CypherParser::MultDivExpressionContext::powerExpression(size_t i) {
  return getRuleContext<CypherParser::PowerExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::MultDivExpressionContext::MULT() {
  return getTokens(CypherParser::MULT);
}

tree::TerminalNode* CypherParser::MultDivExpressionContext::MULT(size_t i) {
  return getToken(CypherParser::MULT, i);
}

std::vector<tree::TerminalNode *> CypherParser::MultDivExpressionContext::DIV() {
  return getTokens(CypherParser::DIV);
}

tree::TerminalNode* CypherParser::MultDivExpressionContext::DIV(size_t i) {
  return getToken(CypherParser::DIV, i);
}

std::vector<tree::TerminalNode *> CypherParser::MultDivExpressionContext::MOD() {
  return getTokens(CypherParser::MOD);
}

tree::TerminalNode* CypherParser::MultDivExpressionContext::MOD(size_t i) {
  return getToken(CypherParser::MOD, i);
}


size_t CypherParser::MultDivExpressionContext::getRuleIndex() const {
  return CypherParser::RuleMultDivExpression;
}


std::any CypherParser::MultDivExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMultDivExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MultDivExpressionContext* CypherParser::multDivExpression() {
  MultDivExpressionContext *_localctx = _tracker.createInstance<MultDivExpressionContext>(_ctx, getState());
  enterRule(_localctx, 86, CypherParser::RuleMultDivExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(490);
    powerExpression();
    setState(495);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 11534336) != 0)) {
      setState(491);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 11534336) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(492);
      powerExpression();
      setState(497);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PowerExpressionContext ------------------------------------------------------------------

CypherParser::PowerExpressionContext::PowerExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::UnaryAddSubExpressionContext *> CypherParser::PowerExpressionContext::unaryAddSubExpression() {
  return getRuleContexts<CypherParser::UnaryAddSubExpressionContext>();
}

CypherParser::UnaryAddSubExpressionContext* CypherParser::PowerExpressionContext::unaryAddSubExpression(size_t i) {
  return getRuleContext<CypherParser::UnaryAddSubExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::PowerExpressionContext::CARET() {
  return getTokens(CypherParser::CARET);
}

tree::TerminalNode* CypherParser::PowerExpressionContext::CARET(size_t i) {
  return getToken(CypherParser::CARET, i);
}


size_t CypherParser::PowerExpressionContext::getRuleIndex() const {
  return CypherParser::RulePowerExpression;
}


std::any CypherParser::PowerExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPowerExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PowerExpressionContext* CypherParser::powerExpression() {
  PowerExpressionContext *_localctx = _tracker.createInstance<PowerExpressionContext>(_ctx, getState());
  enterRule(_localctx, 88, CypherParser::RulePowerExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(498);
    unaryAddSubExpression();
    setState(503);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::CARET) {
      setState(499);
      match(CypherParser::CARET);
      setState(500);
      unaryAddSubExpression();
      setState(505);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryAddSubExpressionContext ------------------------------------------------------------------

CypherParser::UnaryAddSubExpressionContext::UnaryAddSubExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::AtomicExpressionContext* CypherParser::UnaryAddSubExpressionContext::atomicExpression() {
  return getRuleContext<CypherParser::AtomicExpressionContext>(0);
}

tree::TerminalNode* CypherParser::UnaryAddSubExpressionContext::PLUS() {
  return getToken(CypherParser::PLUS, 0);
}

tree::TerminalNode* CypherParser::UnaryAddSubExpressionContext::SUB() {
  return getToken(CypherParser::SUB, 0);
}


size_t CypherParser::UnaryAddSubExpressionContext::getRuleIndex() const {
  return CypherParser::RuleUnaryAddSubExpression;
}


std::any CypherParser::UnaryAddSubExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitUnaryAddSubExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::UnaryAddSubExpressionContext* CypherParser::unaryAddSubExpression() {
  UnaryAddSubExpressionContext *_localctx = _tracker.createInstance<UnaryAddSubExpressionContext>(_ctx, getState());
  enterRule(_localctx, 90, CypherParser::RuleUnaryAddSubExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(507);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(506);
      _la = _input->LA(1);
      if (!(_la == CypherParser::SUB

      || _la == CypherParser::PLUS)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
    }
    setState(509);
    atomicExpression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AtomicExpressionContext ------------------------------------------------------------------

CypherParser::AtomicExpressionContext::AtomicExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::PropertyOrLabelExpressionContext* CypherParser::AtomicExpressionContext::propertyOrLabelExpression() {
  return getRuleContext<CypherParser::PropertyOrLabelExpressionContext>(0);
}

std::vector<CypherParser::StringExpressionContext *> CypherParser::AtomicExpressionContext::stringExpression() {
  return getRuleContexts<CypherParser::StringExpressionContext>();
}

CypherParser::StringExpressionContext* CypherParser::AtomicExpressionContext::stringExpression(size_t i) {
  return getRuleContext<CypherParser::StringExpressionContext>(i);
}

std::vector<CypherParser::ListExpressionContext *> CypherParser::AtomicExpressionContext::listExpression() {
  return getRuleContexts<CypherParser::ListExpressionContext>();
}

CypherParser::ListExpressionContext* CypherParser::AtomicExpressionContext::listExpression(size_t i) {
  return getRuleContext<CypherParser::ListExpressionContext>(i);
}

std::vector<CypherParser::NullExpressionContext *> CypherParser::AtomicExpressionContext::nullExpression() {
  return getRuleContexts<CypherParser::NullExpressionContext>();
}

CypherParser::NullExpressionContext* CypherParser::AtomicExpressionContext::nullExpression(size_t i) {
  return getRuleContext<CypherParser::NullExpressionContext>(i);
}


size_t CypherParser::AtomicExpressionContext::getRuleIndex() const {
  return CypherParser::RuleAtomicExpression;
}


std::any CypherParser::AtomicExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitAtomicExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::AtomicExpressionContext* CypherParser::atomicExpression() {
  AtomicExpressionContext *_localctx = _tracker.createInstance<AtomicExpressionContext>(_ctx, getState());
  enterRule(_localctx, 92, CypherParser::RuleAtomicExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(511);
    propertyOrLabelExpression();
    setState(517);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 16) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 16)) & 44191571343572993) != 0)) {
      setState(515);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::CONTAINS:
        case CypherParser::ENDS:
        case CypherParser::STARTS: {
          setState(512);
          stringExpression();
          break;
        }

        case CypherParser::LBRACK:
        case CypherParser::IN: {
          setState(513);
          listExpression();
          break;
        }

        case CypherParser::IS: {
          setState(514);
          nullExpression();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(519);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListExpressionContext ------------------------------------------------------------------

CypherParser::ListExpressionContext::ListExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ListExpressionContext::IN() {
  return getToken(CypherParser::IN, 0);
}

CypherParser::PropertyOrLabelExpressionContext* CypherParser::ListExpressionContext::propertyOrLabelExpression() {
  return getRuleContext<CypherParser::PropertyOrLabelExpressionContext>(0);
}

tree::TerminalNode* CypherParser::ListExpressionContext::LBRACK() {
  return getToken(CypherParser::LBRACK, 0);
}

tree::TerminalNode* CypherParser::ListExpressionContext::RBRACK() {
  return getToken(CypherParser::RBRACK, 0);
}

tree::TerminalNode* CypherParser::ListExpressionContext::RANGE() {
  return getToken(CypherParser::RANGE, 0);
}

std::vector<CypherParser::ExpressionContext *> CypherParser::ListExpressionContext::expression() {
  return getRuleContexts<CypherParser::ExpressionContext>();
}

CypherParser::ExpressionContext* CypherParser::ListExpressionContext::expression(size_t i) {
  return getRuleContext<CypherParser::ExpressionContext>(i);
}


size_t CypherParser::ListExpressionContext::getRuleIndex() const {
  return CypherParser::RuleListExpression;
}


std::any CypherParser::ListExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitListExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ListExpressionContext* CypherParser::listExpression() {
  ListExpressionContext *_localctx = _tracker.createInstance<ListExpressionContext>(_ctx, getState());
  enterRule(_localctx, 94, CypherParser::RuleListExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(534);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::IN: {
        enterOuterAlt(_localctx, 1);
        setState(520);
        match(CypherParser::IN);
        setState(521);
        propertyOrLabelExpression();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 2);
        setState(522);
        match(CypherParser::LBRACK);
        setState(531);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 55, _ctx)) {
        case 1: {
          setState(524);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 69)) & 130027633) != 0)) {
            setState(523);
            expression();
          }
          setState(526);
          match(CypherParser::RANGE);
          setState(528);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 69)) & 130027633) != 0)) {
            setState(527);
            expression();
          }
          break;
        }

        case 2: {
          setState(530);
          expression();
          break;
        }

        default:
          break;
        }
        setState(533);
        match(CypherParser::RBRACK);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StringExpressionContext ------------------------------------------------------------------

CypherParser::StringExpressionContext::StringExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::StringExpPrefixContext* CypherParser::StringExpressionContext::stringExpPrefix() {
  return getRuleContext<CypherParser::StringExpPrefixContext>(0);
}

CypherParser::PropertyOrLabelExpressionContext* CypherParser::StringExpressionContext::propertyOrLabelExpression() {
  return getRuleContext<CypherParser::PropertyOrLabelExpressionContext>(0);
}


size_t CypherParser::StringExpressionContext::getRuleIndex() const {
  return CypherParser::RuleStringExpression;
}


std::any CypherParser::StringExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitStringExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::StringExpressionContext* CypherParser::stringExpression() {
  StringExpressionContext *_localctx = _tracker.createInstance<StringExpressionContext>(_ctx, getState());
  enterRule(_localctx, 96, CypherParser::RuleStringExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(536);
    stringExpPrefix();
    setState(537);
    propertyOrLabelExpression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StringExpPrefixContext ------------------------------------------------------------------

CypherParser::StringExpPrefixContext::StringExpPrefixContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::StringExpPrefixContext::STARTS() {
  return getToken(CypherParser::STARTS, 0);
}

tree::TerminalNode* CypherParser::StringExpPrefixContext::WITH() {
  return getToken(CypherParser::WITH, 0);
}

tree::TerminalNode* CypherParser::StringExpPrefixContext::ENDS() {
  return getToken(CypherParser::ENDS, 0);
}

tree::TerminalNode* CypherParser::StringExpPrefixContext::CONTAINS() {
  return getToken(CypherParser::CONTAINS, 0);
}


size_t CypherParser::StringExpPrefixContext::getRuleIndex() const {
  return CypherParser::RuleStringExpPrefix;
}


std::any CypherParser::StringExpPrefixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitStringExpPrefix(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::StringExpPrefixContext* CypherParser::stringExpPrefix() {
  StringExpPrefixContext *_localctx = _tracker.createInstance<StringExpPrefixContext>(_ctx, getState());
  enterRule(_localctx, 98, CypherParser::RuleStringExpPrefix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(544);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::STARTS: {
        enterOuterAlt(_localctx, 1);
        setState(539);
        match(CypherParser::STARTS);
        setState(540);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::ENDS: {
        enterOuterAlt(_localctx, 2);
        setState(541);
        match(CypherParser::ENDS);
        setState(542);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::CONTAINS: {
        enterOuterAlt(_localctx, 3);
        setState(543);
        match(CypherParser::CONTAINS);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NullExpressionContext ------------------------------------------------------------------

CypherParser::NullExpressionContext::NullExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::NullExpressionContext::IS() {
  return getToken(CypherParser::IS, 0);
}

tree::TerminalNode* CypherParser::NullExpressionContext::NULL_W() {
  return getToken(CypherParser::NULL_W, 0);
}

tree::TerminalNode* CypherParser::NullExpressionContext::NOT() {
  return getToken(CypherParser::NOT, 0);
}


size_t CypherParser::NullExpressionContext::getRuleIndex() const {
  return CypherParser::RuleNullExpression;
}


std::any CypherParser::NullExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitNullExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::NullExpressionContext* CypherParser::nullExpression() {
  NullExpressionContext *_localctx = _tracker.createInstance<NullExpressionContext>(_ctx, getState());
  enterRule(_localctx, 100, CypherParser::RuleNullExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(546);
    match(CypherParser::IS);
    setState(548);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::NOT) {
      setState(547);
      match(CypherParser::NOT);
    }
    setState(550);
    match(CypherParser::NULL_W);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PropertyOrLabelExpressionContext ------------------------------------------------------------------

CypherParser::PropertyOrLabelExpressionContext::PropertyOrLabelExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::PropertyExpressionContext* CypherParser::PropertyOrLabelExpressionContext::propertyExpression() {
  return getRuleContext<CypherParser::PropertyExpressionContext>(0);
}

CypherParser::NodeLabelsContext* CypherParser::PropertyOrLabelExpressionContext::nodeLabels() {
  return getRuleContext<CypherParser::NodeLabelsContext>(0);
}


size_t CypherParser::PropertyOrLabelExpressionContext::getRuleIndex() const {
  return CypherParser::RulePropertyOrLabelExpression;
}


std::any CypherParser::PropertyOrLabelExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPropertyOrLabelExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PropertyOrLabelExpressionContext* CypherParser::propertyOrLabelExpression() {
  PropertyOrLabelExpressionContext *_localctx = _tracker.createInstance<PropertyOrLabelExpressionContext>(_ctx, getState());
  enterRule(_localctx, 102, CypherParser::RulePropertyOrLabelExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(552);
    propertyExpression();
    setState(554);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(553);
      nodeLabels();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PropertyExpressionContext ------------------------------------------------------------------

CypherParser::PropertyExpressionContext::PropertyExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::AtomContext* CypherParser::PropertyExpressionContext::atom() {
  return getRuleContext<CypherParser::AtomContext>(0);
}

std::vector<tree::TerminalNode *> CypherParser::PropertyExpressionContext::DOT() {
  return getTokens(CypherParser::DOT);
}

tree::TerminalNode* CypherParser::PropertyExpressionContext::DOT(size_t i) {
  return getToken(CypherParser::DOT, i);
}

std::vector<CypherParser::NameContext *> CypherParser::PropertyExpressionContext::name() {
  return getRuleContexts<CypherParser::NameContext>();
}

CypherParser::NameContext* CypherParser::PropertyExpressionContext::name(size_t i) {
  return getRuleContext<CypherParser::NameContext>(i);
}

tree::TerminalNode* CypherParser::PropertyExpressionContext::COLONCOLON() {
  return getToken(CypherParser::COLONCOLON, 0);
}

CypherParser::LabelCastContext* CypherParser::PropertyExpressionContext::labelCast() {
  return getRuleContext<CypherParser::LabelCastContext>(0);
}


size_t CypherParser::PropertyExpressionContext::getRuleIndex() const {
  return CypherParser::RulePropertyExpression;
}


std::any CypherParser::PropertyExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPropertyExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PropertyExpressionContext* CypherParser::propertyExpression() {
  PropertyExpressionContext *_localctx = _tracker.createInstance<PropertyExpressionContext>(_ctx, getState());
  enterRule(_localctx, 104, CypherParser::RulePropertyExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(556);
    atom();
    setState(561);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(557);
      match(CypherParser::DOT);
      setState(558);
      name();
      setState(563);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(566);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLONCOLON) {
      setState(564);
      match(CypherParser::COLONCOLON);
      setState(565);
      labelCast();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LabelCastContext ------------------------------------------------------------------

CypherParser::LabelCastContext::LabelCastContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::NameContext *> CypherParser::LabelCastContext::name() {
  return getRuleContexts<CypherParser::NameContext>();
}

CypherParser::NameContext* CypherParser::LabelCastContext::name(size_t i) {
  return getRuleContext<CypherParser::NameContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::LabelCastContext::DOT() {
  return getTokens(CypherParser::DOT);
}

tree::TerminalNode* CypherParser::LabelCastContext::DOT(size_t i) {
  return getToken(CypherParser::DOT, i);
}


size_t CypherParser::LabelCastContext::getRuleIndex() const {
  return CypherParser::RuleLabelCast;
}


std::any CypherParser::LabelCastContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitLabelCast(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::LabelCastContext* CypherParser::labelCast() {
  LabelCastContext *_localctx = _tracker.createInstance<LabelCastContext>(_ctx, getState());
  enterRule(_localctx, 106, CypherParser::RuleLabelCast);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(568);
    name();
    setState(573);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(569);
      match(CypherParser::DOT);
      setState(570);
      name();
      setState(575);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PatternPartContext ------------------------------------------------------------------

CypherParser::PatternPartContext::PatternPartContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::PatternElemContext* CypherParser::PatternPartContext::patternElem() {
  return getRuleContext<CypherParser::PatternElemContext>(0);
}

CypherParser::SymbolContext* CypherParser::PatternPartContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

tree::TerminalNode* CypherParser::PatternPartContext::ASSIGN() {
  return getToken(CypherParser::ASSIGN, 0);
}


size_t CypherParser::PatternPartContext::getRuleIndex() const {
  return CypherParser::RulePatternPart;
}


std::any CypherParser::PatternPartContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPatternPart(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PatternPartContext* CypherParser::patternPart() {
  PatternPartContext *_localctx = _tracker.createInstance<PatternPartContext>(_ctx, getState());
  enterRule(_localctx, 108, CypherParser::RulePatternPart);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(579);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 3458764513820540991) != 0)) {
      setState(576);
      symbol();
      setState(577);
      match(CypherParser::ASSIGN);
    }
    setState(581);
    patternElem();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PatternElemContext ------------------------------------------------------------------

CypherParser::PatternElemContext::PatternElemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::NodePatternContext* CypherParser::PatternElemContext::nodePattern() {
  return getRuleContext<CypherParser::NodePatternContext>(0);
}

std::vector<CypherParser::PatternElemChainContext *> CypherParser::PatternElemContext::patternElemChain() {
  return getRuleContexts<CypherParser::PatternElemChainContext>();
}

CypherParser::PatternElemChainContext* CypherParser::PatternElemContext::patternElemChain(size_t i) {
  return getRuleContext<CypherParser::PatternElemChainContext>(i);
}

tree::TerminalNode* CypherParser::PatternElemContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

CypherParser::PatternElemContext* CypherParser::PatternElemContext::patternElem() {
  return getRuleContext<CypherParser::PatternElemContext>(0);
}

tree::TerminalNode* CypherParser::PatternElemContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}


size_t CypherParser::PatternElemContext::getRuleIndex() const {
  return CypherParser::RulePatternElem;
}


std::any CypherParser::PatternElemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPatternElem(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PatternElemContext* CypherParser::patternElem() {
  PatternElemContext *_localctx = _tracker.createInstance<PatternElemContext>(_ctx, getState());
  enterRule(_localctx, 110, CypherParser::RulePatternElem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(594);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 65, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(583);
      nodePattern();
      setState(587);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::LT

      || _la == CypherParser::SUB) {
        setState(584);
        patternElemChain();
        setState(589);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(590);
      match(CypherParser::LPAREN);
      setState(591);
      patternElem();
      setState(592);
      match(CypherParser::RPAREN);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PatternElemChainContext ------------------------------------------------------------------

CypherParser::PatternElemChainContext::PatternElemChainContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::RelationshipPatternContext* CypherParser::PatternElemChainContext::relationshipPattern() {
  return getRuleContext<CypherParser::RelationshipPatternContext>(0);
}

CypherParser::NodePatternContext* CypherParser::PatternElemChainContext::nodePattern() {
  return getRuleContext<CypherParser::NodePatternContext>(0);
}


size_t CypherParser::PatternElemChainContext::getRuleIndex() const {
  return CypherParser::RulePatternElemChain;
}


std::any CypherParser::PatternElemChainContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPatternElemChain(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PatternElemChainContext* CypherParser::patternElemChain() {
  PatternElemChainContext *_localctx = _tracker.createInstance<PatternElemChainContext>(_ctx, getState());
  enterRule(_localctx, 112, CypherParser::RulePatternElemChain);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(596);
    relationshipPattern();
    setState(597);
    nodePattern();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PropertiesContext ------------------------------------------------------------------

CypherParser::PropertiesContext::PropertiesContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::MapLitContext* CypherParser::PropertiesContext::mapLit() {
  return getRuleContext<CypherParser::MapLitContext>(0);
}

CypherParser::ParameterContext* CypherParser::PropertiesContext::parameter() {
  return getRuleContext<CypherParser::ParameterContext>(0);
}


size_t CypherParser::PropertiesContext::getRuleIndex() const {
  return CypherParser::RuleProperties;
}


std::any CypherParser::PropertiesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitProperties(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PropertiesContext* CypherParser::properties() {
  PropertiesContext *_localctx = _tracker.createInstance<PropertiesContext>(_ctx, getState());
  enterRule(_localctx, 114, CypherParser::RuleProperties);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(601);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 1);
        setState(599);
        mapLit();
        break;
      }

      case CypherParser::DOLLAR: {
        enterOuterAlt(_localctx, 2);
        setState(600);
        parameter();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NodePatternContext ------------------------------------------------------------------

CypherParser::NodePatternContext::NodePatternContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::NodePatternContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

tree::TerminalNode* CypherParser::NodePatternContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}

CypherParser::SymbolContext* CypherParser::NodePatternContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

CypherParser::NodeLabelsContext* CypherParser::NodePatternContext::nodeLabels() {
  return getRuleContext<CypherParser::NodeLabelsContext>(0);
}

CypherParser::PropertiesContext* CypherParser::NodePatternContext::properties() {
  return getRuleContext<CypherParser::PropertiesContext>(0);
}


size_t CypherParser::NodePatternContext::getRuleIndex() const {
  return CypherParser::RuleNodePattern;
}


std::any CypherParser::NodePatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitNodePattern(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::NodePatternContext* CypherParser::nodePattern() {
  NodePatternContext *_localctx = _tracker.createInstance<NodePatternContext>(_ctx, getState());
  enterRule(_localctx, 116, CypherParser::RuleNodePattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(603);
    match(CypherParser::LPAREN);
    setState(605);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 3458764513820540991) != 0)) {
      setState(604);
      symbol();
    }
    setState(608);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(607);
      nodeLabels();
    }
    setState(611);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(610);
      properties();
    }
    setState(613);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AtomContext ------------------------------------------------------------------

CypherParser::AtomContext::AtomContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::LiteralContext* CypherParser::AtomContext::literal() {
  return getRuleContext<CypherParser::LiteralContext>(0);
}

CypherParser::ParameterContext* CypherParser::AtomContext::parameter() {
  return getRuleContext<CypherParser::ParameterContext>(0);
}

CypherParser::CaseExpressionContext* CypherParser::AtomContext::caseExpression() {
  return getRuleContext<CypherParser::CaseExpressionContext>(0);
}

CypherParser::CountAllContext* CypherParser::AtomContext::countAll() {
  return getRuleContext<CypherParser::CountAllContext>(0);
}

CypherParser::ListComprehensionContext* CypherParser::AtomContext::listComprehension() {
  return getRuleContext<CypherParser::ListComprehensionContext>(0);
}

CypherParser::PatternComprehensionContext* CypherParser::AtomContext::patternComprehension() {
  return getRuleContext<CypherParser::PatternComprehensionContext>(0);
}

CypherParser::FilterWithContext* CypherParser::AtomContext::filterWith() {
  return getRuleContext<CypherParser::FilterWithContext>(0);
}

CypherParser::RelationshipsChainPatternContext* CypherParser::AtomContext::relationshipsChainPattern() {
  return getRuleContext<CypherParser::RelationshipsChainPatternContext>(0);
}

CypherParser::ParenthesizedExpressionContext* CypherParser::AtomContext::parenthesizedExpression() {
  return getRuleContext<CypherParser::ParenthesizedExpressionContext>(0);
}

CypherParser::FunctionInvocationContext* CypherParser::AtomContext::functionInvocation() {
  return getRuleContext<CypherParser::FunctionInvocationContext>(0);
}

CypherParser::SymbolContext* CypherParser::AtomContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

CypherParser::SubqueryExistContext* CypherParser::AtomContext::subqueryExist() {
  return getRuleContext<CypherParser::SubqueryExistContext>(0);
}


size_t CypherParser::AtomContext::getRuleIndex() const {
  return CypherParser::RuleAtom;
}


std::any CypherParser::AtomContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitAtom(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::AtomContext* CypherParser::atom() {
  AtomContext *_localctx = _tracker.createInstance<AtomContext>(_ctx, getState());
  enterRule(_localctx, 118, CypherParser::RuleAtom);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(627);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 70, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(615);
      literal();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(616);
      parameter();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(617);
      caseExpression();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(618);
      countAll();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(619);
      listComprehension();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(620);
      patternComprehension();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(621);
      filterWith();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(622);
      relationshipsChainPattern();
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(623);
      parenthesizedExpression();
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(624);
      functionInvocation();
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(625);
      symbol();
      break;
    }

    case 12: {
      enterOuterAlt(_localctx, 12);
      setState(626);
      subqueryExist();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LhsContext ------------------------------------------------------------------

CypherParser::LhsContext::LhsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SymbolContext* CypherParser::LhsContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

tree::TerminalNode* CypherParser::LhsContext::ASSIGN() {
  return getToken(CypherParser::ASSIGN, 0);
}


size_t CypherParser::LhsContext::getRuleIndex() const {
  return CypherParser::RuleLhs;
}


std::any CypherParser::LhsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitLhs(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::LhsContext* CypherParser::lhs() {
  LhsContext *_localctx = _tracker.createInstance<LhsContext>(_ctx, getState());
  enterRule(_localctx, 120, CypherParser::RuleLhs);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(629);
    symbol();
    setState(630);
    match(CypherParser::ASSIGN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RelationshipPatternContext ------------------------------------------------------------------

CypherParser::RelationshipPatternContext::RelationshipPatternContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::RelationshipPatternContext::LT() {
  return getToken(CypherParser::LT, 0);
}

std::vector<tree::TerminalNode *> CypherParser::RelationshipPatternContext::SUB() {
  return getTokens(CypherParser::SUB);
}

tree::TerminalNode* CypherParser::RelationshipPatternContext::SUB(size_t i) {
  return getToken(CypherParser::SUB, i);
}

CypherParser::RelationDetailContext* CypherParser::RelationshipPatternContext::relationDetail() {
  return getRuleContext<CypherParser::RelationDetailContext>(0);
}

tree::TerminalNode* CypherParser::RelationshipPatternContext::GT() {
  return getToken(CypherParser::GT, 0);
}


size_t CypherParser::RelationshipPatternContext::getRuleIndex() const {
  return CypherParser::RuleRelationshipPattern;
}


std::any CypherParser::RelationshipPatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRelationshipPattern(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RelationshipPatternContext* CypherParser::relationshipPattern() {
  RelationshipPatternContext *_localctx = _tracker.createInstance<RelationshipPatternContext>(_ctx, getState());
  enterRule(_localctx, 122, CypherParser::RuleRelationshipPattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(649);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LT: {
        enterOuterAlt(_localctx, 1);
        setState(632);
        match(CypherParser::LT);
        setState(633);
        match(CypherParser::SUB);
        setState(635);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(634);
          relationDetail();
        }
        setState(637);
        match(CypherParser::SUB);
        setState(639);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(638);
          match(CypherParser::GT);
        }
        break;
      }

      case CypherParser::SUB: {
        enterOuterAlt(_localctx, 2);
        setState(641);
        match(CypherParser::SUB);
        setState(643);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(642);
          relationDetail();
        }
        setState(645);
        match(CypherParser::SUB);
        setState(647);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(646);
          match(CypherParser::GT);
        }
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RelationDetailContext ------------------------------------------------------------------

CypherParser::RelationDetailContext::RelationDetailContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::RelationDetailContext::LBRACK() {
  return getToken(CypherParser::LBRACK, 0);
}

tree::TerminalNode* CypherParser::RelationDetailContext::RBRACK() {
  return getToken(CypherParser::RBRACK, 0);
}

CypherParser::SymbolContext* CypherParser::RelationDetailContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

CypherParser::RelationshipTypesContext* CypherParser::RelationDetailContext::relationshipTypes() {
  return getRuleContext<CypherParser::RelationshipTypesContext>(0);
}

CypherParser::RangeLitContext* CypherParser::RelationDetailContext::rangeLit() {
  return getRuleContext<CypherParser::RangeLitContext>(0);
}

CypherParser::PropertiesContext* CypherParser::RelationDetailContext::properties() {
  return getRuleContext<CypherParser::PropertiesContext>(0);
}


size_t CypherParser::RelationDetailContext::getRuleIndex() const {
  return CypherParser::RuleRelationDetail;
}


std::any CypherParser::RelationDetailContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRelationDetail(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RelationDetailContext* CypherParser::relationDetail() {
  RelationDetailContext *_localctx = _tracker.createInstance<RelationDetailContext>(_ctx, getState());
  enterRule(_localctx, 124, CypherParser::RuleRelationDetail);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(651);
    match(CypherParser::LBRACK);
    setState(653);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 3458764513820540991) != 0)) {
      setState(652);
      symbol();
    }
    setState(656);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(655);
      relationshipTypes();
    }
    setState(659);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::MULT) {
      setState(658);
      rangeLit();
    }
    setState(662);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(661);
      properties();
    }
    setState(664);
    match(CypherParser::RBRACK);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RelationshipTypesContext ------------------------------------------------------------------

CypherParser::RelationshipTypesContext::RelationshipTypesContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> CypherParser::RelationshipTypesContext::COLON() {
  return getTokens(CypherParser::COLON);
}

tree::TerminalNode* CypherParser::RelationshipTypesContext::COLON(size_t i) {
  return getToken(CypherParser::COLON, i);
}

std::vector<CypherParser::NameContext *> CypherParser::RelationshipTypesContext::name() {
  return getRuleContexts<CypherParser::NameContext>();
}

CypherParser::NameContext* CypherParser::RelationshipTypesContext::name(size_t i) {
  return getRuleContext<CypherParser::NameContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::RelationshipTypesContext::STICK() {
  return getTokens(CypherParser::STICK);
}

tree::TerminalNode* CypherParser::RelationshipTypesContext::STICK(size_t i) {
  return getToken(CypherParser::STICK, i);
}


size_t CypherParser::RelationshipTypesContext::getRuleIndex() const {
  return CypherParser::RuleRelationshipTypes;
}


std::any CypherParser::RelationshipTypesContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRelationshipTypes(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RelationshipTypesContext* CypherParser::relationshipTypes() {
  RelationshipTypesContext *_localctx = _tracker.createInstance<RelationshipTypesContext>(_ctx, getState());
  enterRule(_localctx, 126, CypherParser::RuleRelationshipTypes);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(666);
    match(CypherParser::COLON);
    setState(667);
    name();
    setState(675);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::STICK) {
      setState(668);
      match(CypherParser::STICK);
      setState(670);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::COLON) {
        setState(669);
        match(CypherParser::COLON);
      }
      setState(672);
      name();
      setState(677);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnionStContext ------------------------------------------------------------------

CypherParser::UnionStContext::UnionStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::UnionStContext::UNION() {
  return getToken(CypherParser::UNION, 0);
}

CypherParser::SingleQueryContext* CypherParser::UnionStContext::singleQuery() {
  return getRuleContext<CypherParser::SingleQueryContext>(0);
}

tree::TerminalNode* CypherParser::UnionStContext::ALL() {
  return getToken(CypherParser::ALL, 0);
}


size_t CypherParser::UnionStContext::getRuleIndex() const {
  return CypherParser::RuleUnionSt;
}


std::any CypherParser::UnionStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitUnionSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::UnionStContext* CypherParser::unionSt() {
  UnionStContext *_localctx = _tracker.createInstance<UnionStContext>(_ctx, getState());
  enterRule(_localctx, 128, CypherParser::RuleUnionSt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(678);
    match(CypherParser::UNION);
    setState(680);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ALL) {
      setState(679);
      match(CypherParser::ALL);
    }
    setState(682);
    singleQuery();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SubqueryExistContext ------------------------------------------------------------------

CypherParser::SubqueryExistContext::SubqueryExistContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::SubqueryExistContext::EXISTS() {
  return getToken(CypherParser::EXISTS, 0);
}

tree::TerminalNode* CypherParser::SubqueryExistContext::LBRACE() {
  return getToken(CypherParser::LBRACE, 0);
}

tree::TerminalNode* CypherParser::SubqueryExistContext::RBRACE() {
  return getToken(CypherParser::RBRACE, 0);
}

CypherParser::RegularQueryContext* CypherParser::SubqueryExistContext::regularQuery() {
  return getRuleContext<CypherParser::RegularQueryContext>(0);
}

CypherParser::PatternWhereContext* CypherParser::SubqueryExistContext::patternWhere() {
  return getRuleContext<CypherParser::PatternWhereContext>(0);
}


size_t CypherParser::SubqueryExistContext::getRuleIndex() const {
  return CypherParser::RuleSubqueryExist;
}


std::any CypherParser::SubqueryExistContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSubqueryExist(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SubqueryExistContext* CypherParser::subqueryExist() {
  SubqueryExistContext *_localctx = _tracker.createInstance<SubqueryExistContext>(_ctx, getState());
  enterRule(_localctx, 130, CypherParser::RuleSubqueryExist);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(684);
    match(CypherParser::EXISTS);
    setState(685);
    match(CypherParser::LBRACE);
    setState(688);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CALL:
      case CypherParser::CREATE:
      case CypherParser::DELETE:
      case CypherParser::DETACH:
      case CypherParser::MATCH:
      case CypherParser::MERGE:
      case CypherParser::OPTIONAL:
      case CypherParser::REMOVE:
      case CypherParser::RETURN:
      case CypherParser::SET:
      case CypherParser::WITH:
      case CypherParser::UNWIND: {
        setState(686);
        regularQuery();
        break;
      }

      case CypherParser::LPAREN:
      case CypherParser::FILTER:
      case CypherParser::EXTRACT:
      case CypherParser::COUNT:
      case CypherParser::ANY:
      case CypherParser::NONE:
      case CypherParser::SINGLE:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL: {
        setState(687);
        patternWhere();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(690);
    match(CypherParser::RBRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- InvocationNameContext ------------------------------------------------------------------

CypherParser::InvocationNameContext::InvocationNameContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::SymbolContext *> CypherParser::InvocationNameContext::symbol() {
  return getRuleContexts<CypherParser::SymbolContext>();
}

CypherParser::SymbolContext* CypherParser::InvocationNameContext::symbol(size_t i) {
  return getRuleContext<CypherParser::SymbolContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::InvocationNameContext::DOT() {
  return getTokens(CypherParser::DOT);
}

tree::TerminalNode* CypherParser::InvocationNameContext::DOT(size_t i) {
  return getToken(CypherParser::DOT, i);
}


size_t CypherParser::InvocationNameContext::getRuleIndex() const {
  return CypherParser::RuleInvocationName;
}


std::any CypherParser::InvocationNameContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitInvocationName(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::InvocationNameContext* CypherParser::invocationName() {
  InvocationNameContext *_localctx = _tracker.createInstance<InvocationNameContext>(_ctx, getState());
  enterRule(_localctx, 132, CypherParser::RuleInvocationName);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(692);
    symbol();
    setState(697);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(693);
      match(CypherParser::DOT);
      setState(694);
      symbol();
      setState(699);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FunctionInvocationContext ------------------------------------------------------------------

CypherParser::FunctionInvocationContext::FunctionInvocationContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::InvocationNameContext* CypherParser::FunctionInvocationContext::invocationName() {
  return getRuleContext<CypherParser::InvocationNameContext>(0);
}

tree::TerminalNode* CypherParser::FunctionInvocationContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

tree::TerminalNode* CypherParser::FunctionInvocationContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}

tree::TerminalNode* CypherParser::FunctionInvocationContext::DISTINCT() {
  return getToken(CypherParser::DISTINCT, 0);
}

CypherParser::ExpressionChainContext* CypherParser::FunctionInvocationContext::expressionChain() {
  return getRuleContext<CypherParser::ExpressionChainContext>(0);
}


size_t CypherParser::FunctionInvocationContext::getRuleIndex() const {
  return CypherParser::RuleFunctionInvocation;
}


std::any CypherParser::FunctionInvocationContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitFunctionInvocation(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::FunctionInvocationContext* CypherParser::functionInvocation() {
  FunctionInvocationContext *_localctx = _tracker.createInstance<FunctionInvocationContext>(_ctx, getState());
  enterRule(_localctx, 134, CypherParser::RuleFunctionInvocation);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(700);
    invocationName();
    setState(701);
    match(CypherParser::LPAREN);
    setState(703);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(702);
      match(CypherParser::DISTINCT);
    }
    setState(706);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 130027633) != 0)) {
      setState(705);
      expressionChain();
    }
    setState(708);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParenthesizedExpressionContext ------------------------------------------------------------------

CypherParser::ParenthesizedExpressionContext::ParenthesizedExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ParenthesizedExpressionContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

CypherParser::ExpressionContext* CypherParser::ParenthesizedExpressionContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

tree::TerminalNode* CypherParser::ParenthesizedExpressionContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}


size_t CypherParser::ParenthesizedExpressionContext::getRuleIndex() const {
  return CypherParser::RuleParenthesizedExpression;
}


std::any CypherParser::ParenthesizedExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitParenthesizedExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ParenthesizedExpressionContext* CypherParser::parenthesizedExpression() {
  ParenthesizedExpressionContext *_localctx = _tracker.createInstance<ParenthesizedExpressionContext>(_ctx, getState());
  enterRule(_localctx, 136, CypherParser::RuleParenthesizedExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(710);
    match(CypherParser::LPAREN);
    setState(711);
    expression();
    setState(712);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FilterWithContext ------------------------------------------------------------------

CypherParser::FilterWithContext::FilterWithContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::FilterWithContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

CypherParser::FilterExpressionContext* CypherParser::FilterWithContext::filterExpression() {
  return getRuleContext<CypherParser::FilterExpressionContext>(0);
}

tree::TerminalNode* CypherParser::FilterWithContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}

tree::TerminalNode* CypherParser::FilterWithContext::ALL() {
  return getToken(CypherParser::ALL, 0);
}

tree::TerminalNode* CypherParser::FilterWithContext::ANY() {
  return getToken(CypherParser::ANY, 0);
}

tree::TerminalNode* CypherParser::FilterWithContext::NONE() {
  return getToken(CypherParser::NONE, 0);
}

tree::TerminalNode* CypherParser::FilterWithContext::SINGLE() {
  return getToken(CypherParser::SINGLE, 0);
}


size_t CypherParser::FilterWithContext::getRuleIndex() const {
  return CypherParser::RuleFilterWith;
}


std::any CypherParser::FilterWithContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitFilterWith(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::FilterWithContext* CypherParser::filterWith() {
  FilterWithContext *_localctx = _tracker.createInstance<FilterWithContext>(_ctx, getState());
  enterRule(_localctx, 138, CypherParser::RuleFilterWith);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(714);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 257698037760) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(715);
    match(CypherParser::LPAREN);
    setState(716);
    filterExpression();
    setState(717);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PatternComprehensionContext ------------------------------------------------------------------

CypherParser::PatternComprehensionContext::PatternComprehensionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::PatternComprehensionContext::LBRACK() {
  return getToken(CypherParser::LBRACK, 0);
}

CypherParser::RelationshipsChainPatternContext* CypherParser::PatternComprehensionContext::relationshipsChainPattern() {
  return getRuleContext<CypherParser::RelationshipsChainPatternContext>(0);
}

tree::TerminalNode* CypherParser::PatternComprehensionContext::STICK() {
  return getToken(CypherParser::STICK, 0);
}

CypherParser::ExpressionContext* CypherParser::PatternComprehensionContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

tree::TerminalNode* CypherParser::PatternComprehensionContext::RBRACK() {
  return getToken(CypherParser::RBRACK, 0);
}

CypherParser::LhsContext* CypherParser::PatternComprehensionContext::lhs() {
  return getRuleContext<CypherParser::LhsContext>(0);
}

CypherParser::WhereContext* CypherParser::PatternComprehensionContext::where() {
  return getRuleContext<CypherParser::WhereContext>(0);
}


size_t CypherParser::PatternComprehensionContext::getRuleIndex() const {
  return CypherParser::RulePatternComprehension;
}


std::any CypherParser::PatternComprehensionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPatternComprehension(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PatternComprehensionContext* CypherParser::patternComprehension() {
  PatternComprehensionContext *_localctx = _tracker.createInstance<PatternComprehensionContext>(_ctx, getState());
  enterRule(_localctx, 140, CypherParser::RulePatternComprehension);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(719);
    match(CypherParser::LBRACK);
    setState(721);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 3458764513820540991) != 0)) {
      setState(720);
      lhs();
    }
    setState(723);
    relationshipsChainPattern();
    setState(725);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(724);
      where();
    }
    setState(727);
    match(CypherParser::STICK);
    setState(728);
    expression();
    setState(729);
    match(CypherParser::RBRACK);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RelationshipsChainPatternContext ------------------------------------------------------------------

CypherParser::RelationshipsChainPatternContext::RelationshipsChainPatternContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::NodePatternContext* CypherParser::RelationshipsChainPatternContext::nodePattern() {
  return getRuleContext<CypherParser::NodePatternContext>(0);
}

std::vector<CypherParser::PatternElemChainContext *> CypherParser::RelationshipsChainPatternContext::patternElemChain() {
  return getRuleContexts<CypherParser::PatternElemChainContext>();
}

CypherParser::PatternElemChainContext* CypherParser::RelationshipsChainPatternContext::patternElemChain(size_t i) {
  return getRuleContext<CypherParser::PatternElemChainContext>(i);
}


size_t CypherParser::RelationshipsChainPatternContext::getRuleIndex() const {
  return CypherParser::RuleRelationshipsChainPattern;
}


std::any CypherParser::RelationshipsChainPatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRelationshipsChainPattern(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RelationshipsChainPatternContext* CypherParser::relationshipsChainPattern() {
  RelationshipsChainPatternContext *_localctx = _tracker.createInstance<RelationshipsChainPatternContext>(_ctx, getState());
  enterRule(_localctx, 142, CypherParser::RuleRelationshipsChainPattern);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(731);
    nodePattern();
    setState(733); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(732);
              patternElemChain();
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(735); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 89, _ctx);
    } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListComprehensionContext ------------------------------------------------------------------

CypherParser::ListComprehensionContext::ListComprehensionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ListComprehensionContext::LBRACK() {
  return getToken(CypherParser::LBRACK, 0);
}

CypherParser::FilterExpressionContext* CypherParser::ListComprehensionContext::filterExpression() {
  return getRuleContext<CypherParser::FilterExpressionContext>(0);
}

tree::TerminalNode* CypherParser::ListComprehensionContext::RBRACK() {
  return getToken(CypherParser::RBRACK, 0);
}

tree::TerminalNode* CypherParser::ListComprehensionContext::STICK() {
  return getToken(CypherParser::STICK, 0);
}

CypherParser::ExpressionContext* CypherParser::ListComprehensionContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}


size_t CypherParser::ListComprehensionContext::getRuleIndex() const {
  return CypherParser::RuleListComprehension;
}


std::any CypherParser::ListComprehensionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitListComprehension(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ListComprehensionContext* CypherParser::listComprehension() {
  ListComprehensionContext *_localctx = _tracker.createInstance<ListComprehensionContext>(_ctx, getState());
  enterRule(_localctx, 144, CypherParser::RuleListComprehension);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(737);
    match(CypherParser::LBRACK);
    setState(738);
    filterExpression();
    setState(741);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::STICK) {
      setState(739);
      match(CypherParser::STICK);
      setState(740);
      expression();
    }
    setState(743);
    match(CypherParser::RBRACK);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FilterExpressionContext ------------------------------------------------------------------

CypherParser::FilterExpressionContext::FilterExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SymbolContext* CypherParser::FilterExpressionContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

tree::TerminalNode* CypherParser::FilterExpressionContext::IN() {
  return getToken(CypherParser::IN, 0);
}

CypherParser::ExpressionContext* CypherParser::FilterExpressionContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

CypherParser::WhereContext* CypherParser::FilterExpressionContext::where() {
  return getRuleContext<CypherParser::WhereContext>(0);
}


size_t CypherParser::FilterExpressionContext::getRuleIndex() const {
  return CypherParser::RuleFilterExpression;
}


std::any CypherParser::FilterExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitFilterExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::FilterExpressionContext* CypherParser::filterExpression() {
  FilterExpressionContext *_localctx = _tracker.createInstance<FilterExpressionContext>(_ctx, getState());
  enterRule(_localctx, 146, CypherParser::RuleFilterExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(745);
    symbol();
    setState(746);
    match(CypherParser::IN);
    setState(747);
    expression();
    setState(749);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(748);
      where();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CountAllContext ------------------------------------------------------------------

CypherParser::CountAllContext::CountAllContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::CountAllContext::COUNT() {
  return getToken(CypherParser::COUNT, 0);
}

tree::TerminalNode* CypherParser::CountAllContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

tree::TerminalNode* CypherParser::CountAllContext::MULT() {
  return getToken(CypherParser::MULT, 0);
}

tree::TerminalNode* CypherParser::CountAllContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}


size_t CypherParser::CountAllContext::getRuleIndex() const {
  return CypherParser::RuleCountAll;
}


std::any CypherParser::CountAllContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitCountAll(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::CountAllContext* CypherParser::countAll() {
  CountAllContext *_localctx = _tracker.createInstance<CountAllContext>(_ctx, getState());
  enterRule(_localctx, 148, CypherParser::RuleCountAll);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(751);
    match(CypherParser::COUNT);
    setState(752);
    match(CypherParser::LPAREN);
    setState(753);
    match(CypherParser::MULT);
    setState(754);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionChainContext ------------------------------------------------------------------

CypherParser::ExpressionChainContext::ExpressionChainContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CypherParser::ExpressionContext *> CypherParser::ExpressionChainContext::expression() {
  return getRuleContexts<CypherParser::ExpressionContext>();
}

CypherParser::ExpressionContext* CypherParser::ExpressionChainContext::expression(size_t i) {
  return getRuleContext<CypherParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::ExpressionChainContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::ExpressionChainContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::ExpressionChainContext::getRuleIndex() const {
  return CypherParser::RuleExpressionChain;
}


std::any CypherParser::ExpressionChainContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitExpressionChain(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ExpressionChainContext* CypherParser::expressionChain() {
  ExpressionChainContext *_localctx = _tracker.createInstance<ExpressionChainContext>(_ctx, getState());
  enterRule(_localctx, 150, CypherParser::RuleExpressionChain);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(756);
    expression();
    setState(761);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(757);
      match(CypherParser::COMMA);
      setState(758);
      expression();
      setState(763);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CaseExpressionContext ------------------------------------------------------------------

CypherParser::CaseExpressionContext::CaseExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::CaseExpressionContext::CASE() {
  return getToken(CypherParser::CASE, 0);
}

tree::TerminalNode* CypherParser::CaseExpressionContext::END() {
  return getToken(CypherParser::END, 0);
}

std::vector<CypherParser::ExpressionContext *> CypherParser::CaseExpressionContext::expression() {
  return getRuleContexts<CypherParser::ExpressionContext>();
}

CypherParser::ExpressionContext* CypherParser::CaseExpressionContext::expression(size_t i) {
  return getRuleContext<CypherParser::ExpressionContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::CaseExpressionContext::WHEN() {
  return getTokens(CypherParser::WHEN);
}

tree::TerminalNode* CypherParser::CaseExpressionContext::WHEN(size_t i) {
  return getToken(CypherParser::WHEN, i);
}

std::vector<tree::TerminalNode *> CypherParser::CaseExpressionContext::THEN() {
  return getTokens(CypherParser::THEN);
}

tree::TerminalNode* CypherParser::CaseExpressionContext::THEN(size_t i) {
  return getToken(CypherParser::THEN, i);
}

tree::TerminalNode* CypherParser::CaseExpressionContext::ELSE() {
  return getToken(CypherParser::ELSE, 0);
}


size_t CypherParser::CaseExpressionContext::getRuleIndex() const {
  return CypherParser::RuleCaseExpression;
}


std::any CypherParser::CaseExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitCaseExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::CaseExpressionContext* CypherParser::caseExpression() {
  CaseExpressionContext *_localctx = _tracker.createInstance<CaseExpressionContext>(_ctx, getState());
  enterRule(_localctx, 152, CypherParser::RuleCaseExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(764);
    match(CypherParser::CASE);
    setState(766);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 130027633) != 0)) {
      setState(765);
      expression();
    }
    setState(773); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(768);
      match(CypherParser::WHEN);
      setState(769);
      expression();
      setState(770);
      match(CypherParser::THEN);
      setState(771);
      expression();
      setState(775); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == CypherParser::WHEN);
    setState(779);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ELSE) {
      setState(777);
      match(CypherParser::ELSE);
      setState(778);
      expression();
    }
    setState(781);
    match(CypherParser::END);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParameterContext ------------------------------------------------------------------

CypherParser::ParameterContext::ParameterContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ParameterContext::DOLLAR() {
  return getToken(CypherParser::DOLLAR, 0);
}

CypherParser::SymbolContext* CypherParser::ParameterContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

CypherParser::NumLitContext* CypherParser::ParameterContext::numLit() {
  return getRuleContext<CypherParser::NumLitContext>(0);
}


size_t CypherParser::ParameterContext::getRuleIndex() const {
  return CypherParser::RuleParameter;
}


std::any CypherParser::ParameterContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitParameter(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ParameterContext* CypherParser::parameter() {
  ParameterContext *_localctx = _tracker.createInstance<ParameterContext>(_ctx, getState());
  enterRule(_localctx, 154, CypherParser::RuleParameter);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(783);
    match(CypherParser::DOLLAR);
    setState(786);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FILTER:
      case CypherParser::EXTRACT:
      case CypherParser::COUNT:
      case CypherParser::ANY:
      case CypherParser::NONE:
      case CypherParser::SINGLE:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL: {
        setState(784);
        symbol();
        break;
      }

      case CypherParser::DIGIT: {
        setState(785);
        numLit();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LiteralContext ------------------------------------------------------------------

CypherParser::LiteralContext::LiteralContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::BoolLitContext* CypherParser::LiteralContext::boolLit() {
  return getRuleContext<CypherParser::BoolLitContext>(0);
}

CypherParser::NumLitContext* CypherParser::LiteralContext::numLit() {
  return getRuleContext<CypherParser::NumLitContext>(0);
}

tree::TerminalNode* CypherParser::LiteralContext::NULL_W() {
  return getToken(CypherParser::NULL_W, 0);
}

CypherParser::StringLitContext* CypherParser::LiteralContext::stringLit() {
  return getRuleContext<CypherParser::StringLitContext>(0);
}

CypherParser::CharLitContext* CypherParser::LiteralContext::charLit() {
  return getRuleContext<CypherParser::CharLitContext>(0);
}

CypherParser::ListLitContext* CypherParser::LiteralContext::listLit() {
  return getRuleContext<CypherParser::ListLitContext>(0);
}

CypherParser::MapLitContext* CypherParser::LiteralContext::mapLit() {
  return getRuleContext<CypherParser::MapLitContext>(0);
}


size_t CypherParser::LiteralContext::getRuleIndex() const {
  return CypherParser::RuleLiteral;
}


std::any CypherParser::LiteralContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitLiteral(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::LiteralContext* CypherParser::literal() {
  LiteralContext *_localctx = _tracker.createInstance<LiteralContext>(_ctx, getState());
  enterRule(_localctx, 156, CypherParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(795);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FALSE:
      case CypherParser::TRUE: {
        enterOuterAlt(_localctx, 1);
        setState(788);
        boolLit();
        break;
      }

      case CypherParser::DIGIT: {
        enterOuterAlt(_localctx, 2);
        setState(789);
        numLit();
        break;
      }

      case CypherParser::NULL_W: {
        enterOuterAlt(_localctx, 3);
        setState(790);
        match(CypherParser::NULL_W);
        break;
      }

      case CypherParser::STRING_LITERAL: {
        enterOuterAlt(_localctx, 4);
        setState(791);
        stringLit();
        break;
      }

      case CypherParser::CHAR_LITERAL: {
        enterOuterAlt(_localctx, 5);
        setState(792);
        charLit();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 6);
        setState(793);
        listLit();
        break;
      }

      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 7);
        setState(794);
        mapLit();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RangeLitContext ------------------------------------------------------------------

CypherParser::RangeLitContext::RangeLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::RangeLitContext::MULT() {
  return getToken(CypherParser::MULT, 0);
}

std::vector<CypherParser::NumLitContext *> CypherParser::RangeLitContext::numLit() {
  return getRuleContexts<CypherParser::NumLitContext>();
}

CypherParser::NumLitContext* CypherParser::RangeLitContext::numLit(size_t i) {
  return getRuleContext<CypherParser::NumLitContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::RangeLitContext::ID() {
  return getTokens(CypherParser::ID);
}

tree::TerminalNode* CypherParser::RangeLitContext::ID(size_t i) {
  return getToken(CypherParser::ID, i);
}

tree::TerminalNode* CypherParser::RangeLitContext::RANGE() {
  return getToken(CypherParser::RANGE, 0);
}


size_t CypherParser::RangeLitContext::getRuleIndex() const {
  return CypherParser::RuleRangeLit;
}


std::any CypherParser::RangeLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitRangeLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::RangeLitContext* CypherParser::rangeLit() {
  RangeLitContext *_localctx = _tracker.createInstance<RangeLitContext>(_ctx, getState());
  enterRule(_localctx, 158, CypherParser::RuleRangeLit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(797);
    match(CypherParser::MULT);
    setState(800);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::DIGIT: {
        setState(798);
        numLit();
        break;
      }

      case CypherParser::ID: {
        setState(799);
        match(CypherParser::ID);
        break;
      }

      case CypherParser::RANGE:
      case CypherParser::LBRACE:
      case CypherParser::RBRACK:
      case CypherParser::DOLLAR: {
        break;
      }

    default:
      break;
    }
    setState(807);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RANGE) {
      setState(802);
      match(CypherParser::RANGE);
      setState(805);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::DIGIT: {
          setState(803);
          numLit();
          break;
        }

        case CypherParser::ID: {
          setState(804);
          match(CypherParser::ID);
          break;
        }

        case CypherParser::LBRACE:
        case CypherParser::RBRACK:
        case CypherParser::DOLLAR: {
          break;
        }

      default:
        break;
      }
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BoolLitContext ------------------------------------------------------------------

CypherParser::BoolLitContext::BoolLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::BoolLitContext::TRUE() {
  return getToken(CypherParser::TRUE, 0);
}

tree::TerminalNode* CypherParser::BoolLitContext::FALSE() {
  return getToken(CypherParser::FALSE, 0);
}


size_t CypherParser::BoolLitContext::getRuleIndex() const {
  return CypherParser::RuleBoolLit;
}


std::any CypherParser::BoolLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitBoolLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::BoolLitContext* CypherParser::boolLit() {
  BoolLitContext *_localctx = _tracker.createInstance<BoolLitContext>(_ctx, getState());
  enterRule(_localctx, 160, CypherParser::RuleBoolLit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(809);
    _la = _input->LA(1);
    if (!(_la == CypherParser::FALSE

    || _la == CypherParser::TRUE)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NumLitContext ------------------------------------------------------------------

CypherParser::NumLitContext::NumLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::NumLitContext::DIGIT() {
  return getToken(CypherParser::DIGIT, 0);
}


size_t CypherParser::NumLitContext::getRuleIndex() const {
  return CypherParser::RuleNumLit;
}


std::any CypherParser::NumLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitNumLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::NumLitContext* CypherParser::numLit() {
  NumLitContext *_localctx = _tracker.createInstance<NumLitContext>(_ctx, getState());
  enterRule(_localctx, 162, CypherParser::RuleNumLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(811);
    match(CypherParser::DIGIT);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StringLitContext ------------------------------------------------------------------

CypherParser::StringLitContext::StringLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::StringLitContext::STRING_LITERAL() {
  return getToken(CypherParser::STRING_LITERAL, 0);
}


size_t CypherParser::StringLitContext::getRuleIndex() const {
  return CypherParser::RuleStringLit;
}


std::any CypherParser::StringLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitStringLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::StringLitContext* CypherParser::stringLit() {
  StringLitContext *_localctx = _tracker.createInstance<StringLitContext>(_ctx, getState());
  enterRule(_localctx, 164, CypherParser::RuleStringLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(813);
    match(CypherParser::STRING_LITERAL);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CharLitContext ------------------------------------------------------------------

CypherParser::CharLitContext::CharLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::CharLitContext::CHAR_LITERAL() {
  return getToken(CypherParser::CHAR_LITERAL, 0);
}


size_t CypherParser::CharLitContext::getRuleIndex() const {
  return CypherParser::RuleCharLit;
}


std::any CypherParser::CharLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitCharLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::CharLitContext* CypherParser::charLit() {
  CharLitContext *_localctx = _tracker.createInstance<CharLitContext>(_ctx, getState());
  enterRule(_localctx, 166, CypherParser::RuleCharLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(815);
    match(CypherParser::CHAR_LITERAL);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ListLitContext ------------------------------------------------------------------

CypherParser::ListLitContext::ListLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ListLitContext::LBRACK() {
  return getToken(CypherParser::LBRACK, 0);
}

tree::TerminalNode* CypherParser::ListLitContext::RBRACK() {
  return getToken(CypherParser::RBRACK, 0);
}

CypherParser::ExpressionChainContext* CypherParser::ListLitContext::expressionChain() {
  return getRuleContext<CypherParser::ExpressionChainContext>(0);
}


size_t CypherParser::ListLitContext::getRuleIndex() const {
  return CypherParser::RuleListLit;
}


std::any CypherParser::ListLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitListLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ListLitContext* CypherParser::listLit() {
  ListLitContext *_localctx = _tracker.createInstance<ListLitContext>(_ctx, getState());
  enterRule(_localctx, 168, CypherParser::RuleListLit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(817);
    match(CypherParser::LBRACK);
    setState(819);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 130027633) != 0)) {
      setState(818);
      expressionChain();
    }
    setState(821);
    match(CypherParser::RBRACK);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MapLitContext ------------------------------------------------------------------

CypherParser::MapLitContext::MapLitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::MapLitContext::LBRACE() {
  return getToken(CypherParser::LBRACE, 0);
}

tree::TerminalNode* CypherParser::MapLitContext::RBRACE() {
  return getToken(CypherParser::RBRACE, 0);
}

std::vector<CypherParser::MapPairContext *> CypherParser::MapLitContext::mapPair() {
  return getRuleContexts<CypherParser::MapPairContext>();
}

CypherParser::MapPairContext* CypherParser::MapLitContext::mapPair(size_t i) {
  return getRuleContext<CypherParser::MapPairContext>(i);
}

std::vector<tree::TerminalNode *> CypherParser::MapLitContext::COMMA() {
  return getTokens(CypherParser::COMMA);
}

tree::TerminalNode* CypherParser::MapLitContext::COMMA(size_t i) {
  return getToken(CypherParser::COMMA, i);
}


size_t CypherParser::MapLitContext::getRuleIndex() const {
  return CypherParser::RuleMapLit;
}


std::any CypherParser::MapLitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMapLit(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MapLitContext* CypherParser::mapLit() {
  MapLitContext *_localctx = _tracker.createInstance<MapLitContext>(_ctx, getState());
  enterRule(_localctx, 170, CypherParser::RuleMapLit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(823);
    match(CypherParser::LBRACE);
    setState(832);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 4611686018427322367) != 0)) {
      setState(824);
      mapPair();
      setState(829);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::COMMA) {
        setState(825);
        match(CypherParser::COMMA);
        setState(826);
        mapPair();
        setState(831);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(834);
    match(CypherParser::RBRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MapPairContext ------------------------------------------------------------------

CypherParser::MapPairContext::MapPairContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::NameContext* CypherParser::MapPairContext::name() {
  return getRuleContext<CypherParser::NameContext>(0);
}

tree::TerminalNode* CypherParser::MapPairContext::COLON() {
  return getToken(CypherParser::COLON, 0);
}

CypherParser::ExpressionContext* CypherParser::MapPairContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}


size_t CypherParser::MapPairContext::getRuleIndex() const {
  return CypherParser::RuleMapPair;
}


std::any CypherParser::MapPairContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitMapPair(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::MapPairContext* CypherParser::mapPair() {
  MapPairContext *_localctx = _tracker.createInstance<MapPairContext>(_ctx, getState());
  enterRule(_localctx, 172, CypherParser::RuleMapPair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(836);
    name();
    setState(837);
    match(CypherParser::COLON);
    setState(838);
    expression();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NameContext ------------------------------------------------------------------

CypherParser::NameContext::NameContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::SymbolContext* CypherParser::NameContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

CypherParser::ReservedWordContext* CypherParser::NameContext::reservedWord() {
  return getRuleContext<CypherParser::ReservedWordContext>(0);
}


size_t CypherParser::NameContext::getRuleIndex() const {
  return CypherParser::RuleName;
}


std::any CypherParser::NameContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitName(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::NameContext* CypherParser::name() {
  NameContext *_localctx = _tracker.createInstance<NameContext>(_ctx, getState());
  enterRule(_localctx, 174, CypherParser::RuleName);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(842);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FILTER:
      case CypherParser::EXTRACT:
      case CypherParser::COUNT:
      case CypherParser::ANY:
      case CypherParser::NONE:
      case CypherParser::SINGLE:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL: {
        enterOuterAlt(_localctx, 1);
        setState(840);
        symbol();
        break;
      }

      case CypherParser::ALL:
      case CypherParser::ASC:
      case CypherParser::ASCENDING:
      case CypherParser::BY:
      case CypherParser::CREATE:
      case CypherParser::DELETE:
      case CypherParser::DESC:
      case CypherParser::DESCENDING:
      case CypherParser::DETACH:
      case CypherParser::EXISTS:
      case CypherParser::LIMIT:
      case CypherParser::MATCH:
      case CypherParser::MERGE:
      case CypherParser::ON:
      case CypherParser::OPTIONAL:
      case CypherParser::ORDER:
      case CypherParser::REMOVE:
      case CypherParser::RETURN:
      case CypherParser::SET:
      case CypherParser::SKIP_W:
      case CypherParser::WHERE:
      case CypherParser::WITH:
      case CypherParser::UNION:
      case CypherParser::UNWIND:
      case CypherParser::AND:
      case CypherParser::AS:
      case CypherParser::CONTAINS:
      case CypherParser::DISTINCT:
      case CypherParser::ENDS:
      case CypherParser::IN:
      case CypherParser::IS:
      case CypherParser::NOT:
      case CypherParser::OR:
      case CypherParser::STARTS:
      case CypherParser::XOR:
      case CypherParser::FALSE:
      case CypherParser::TRUE:
      case CypherParser::NULL_W:
      case CypherParser::CONSTRAINT:
      case CypherParser::DO:
      case CypherParser::FOR:
      case CypherParser::REQUIRE:
      case CypherParser::UNIQUE:
      case CypherParser::CASE:
      case CypherParser::WHEN:
      case CypherParser::THEN:
      case CypherParser::ELSE:
      case CypherParser::END:
      case CypherParser::MANDATORY:
      case CypherParser::SCALAR:
      case CypherParser::OF:
      case CypherParser::ADD:
      case CypherParser::DROP: {
        enterOuterAlt(_localctx, 2);
        setState(841);
        reservedWord();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- SymbolContext ------------------------------------------------------------------

CypherParser::SymbolContext::SymbolContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::SymbolContext::ESC_LITERAL() {
  return getToken(CypherParser::ESC_LITERAL, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::ID() {
  return getToken(CypherParser::ID, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::COUNT() {
  return getToken(CypherParser::COUNT, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::FILTER() {
  return getToken(CypherParser::FILTER, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::EXTRACT() {
  return getToken(CypherParser::EXTRACT, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::ANY() {
  return getToken(CypherParser::ANY, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::NONE() {
  return getToken(CypherParser::NONE, 0);
}

tree::TerminalNode* CypherParser::SymbolContext::SINGLE() {
  return getToken(CypherParser::SINGLE, 0);
}


size_t CypherParser::SymbolContext::getRuleIndex() const {
  return CypherParser::RuleSymbol;
}


std::any CypherParser::SymbolContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitSymbol(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::SymbolContext* CypherParser::symbol() {
  SymbolContext *_localctx = _tracker.createInstance<SymbolContext>(_ctx, getState());
  enterRule(_localctx, 176, CypherParser::RuleSymbol);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(844);
    _la = _input->LA(1);
    if (!(((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 3458764513820540991) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ReservedWordContext ------------------------------------------------------------------

CypherParser::ReservedWordContext::ReservedWordContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ReservedWordContext::ALL() {
  return getToken(CypherParser::ALL, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ASC() {
  return getToken(CypherParser::ASC, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ASCENDING() {
  return getToken(CypherParser::ASCENDING, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::BY() {
  return getToken(CypherParser::BY, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::CREATE() {
  return getToken(CypherParser::CREATE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DELETE() {
  return getToken(CypherParser::DELETE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DESC() {
  return getToken(CypherParser::DESC, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DESCENDING() {
  return getToken(CypherParser::DESCENDING, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DETACH() {
  return getToken(CypherParser::DETACH, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::EXISTS() {
  return getToken(CypherParser::EXISTS, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::LIMIT() {
  return getToken(CypherParser::LIMIT, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::MATCH() {
  return getToken(CypherParser::MATCH, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::MERGE() {
  return getToken(CypherParser::MERGE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ON() {
  return getToken(CypherParser::ON, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::OPTIONAL() {
  return getToken(CypherParser::OPTIONAL, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ORDER() {
  return getToken(CypherParser::ORDER, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::REMOVE() {
  return getToken(CypherParser::REMOVE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::RETURN() {
  return getToken(CypherParser::RETURN, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::SET() {
  return getToken(CypherParser::SET, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::SKIP_W() {
  return getToken(CypherParser::SKIP_W, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::WHERE() {
  return getToken(CypherParser::WHERE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::WITH() {
  return getToken(CypherParser::WITH, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::UNION() {
  return getToken(CypherParser::UNION, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::UNWIND() {
  return getToken(CypherParser::UNWIND, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::AND() {
  return getToken(CypherParser::AND, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::AS() {
  return getToken(CypherParser::AS, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::CONTAINS() {
  return getToken(CypherParser::CONTAINS, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DISTINCT() {
  return getToken(CypherParser::DISTINCT, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ENDS() {
  return getToken(CypherParser::ENDS, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::IN() {
  return getToken(CypherParser::IN, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::IS() {
  return getToken(CypherParser::IS, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::NOT() {
  return getToken(CypherParser::NOT, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::OR() {
  return getToken(CypherParser::OR, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::STARTS() {
  return getToken(CypherParser::STARTS, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::XOR() {
  return getToken(CypherParser::XOR, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::FALSE() {
  return getToken(CypherParser::FALSE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::TRUE() {
  return getToken(CypherParser::TRUE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::NULL_W() {
  return getToken(CypherParser::NULL_W, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::CONSTRAINT() {
  return getToken(CypherParser::CONSTRAINT, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DO() {
  return getToken(CypherParser::DO, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::FOR() {
  return getToken(CypherParser::FOR, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::REQUIRE() {
  return getToken(CypherParser::REQUIRE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::UNIQUE() {
  return getToken(CypherParser::UNIQUE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::CASE() {
  return getToken(CypherParser::CASE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::WHEN() {
  return getToken(CypherParser::WHEN, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::THEN() {
  return getToken(CypherParser::THEN, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ELSE() {
  return getToken(CypherParser::ELSE, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::END() {
  return getToken(CypherParser::END, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::MANDATORY() {
  return getToken(CypherParser::MANDATORY, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::SCALAR() {
  return getToken(CypherParser::SCALAR, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::OF() {
  return getToken(CypherParser::OF, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::ADD() {
  return getToken(CypherParser::ADD, 0);
}

tree::TerminalNode* CypherParser::ReservedWordContext::DROP() {
  return getToken(CypherParser::DROP, 0);
}


size_t CypherParser::ReservedWordContext::getRuleIndex() const {
  return CypherParser::RuleReservedWord;
}


std::any CypherParser::ReservedWordContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitReservedWord(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ReservedWordContext* CypherParser::reservedWord() {
  ReservedWordContext *_localctx = _tracker.createInstance<ReservedWordContext>(_ctx, getState());
  enterRule(_localctx, 178, CypherParser::RuleReservedWord);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(846);
    _la = _input->LA(1);
    if (!(((((_la - 37) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 37)) & 18014398509480959) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

void CypherParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  cypherparserParserInitialize();
#else
  ::antlr4::internal::call_once(cypherparserParserOnceFlag, cypherparserParserInitialize);
#endif
}
