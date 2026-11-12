
// Generated from CypherParser.g4 by ANTLR 4.13.2


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
      "script", "query", "regularQuery", "singleQuery", "clause", "readingClause", 
      "updatingClause", "primitiveResultStatement", "standaloneCall", "returnSt", 
      "withSt", "skipSt", "limitSt", "projectionBody", "projectionItems", 
      "projectionItem", "orderItem", "orderSt", "matchSt", "unwindSt", "readingStatement", 
      "updatingStatement", "deleteSt", "removeSt", "foreachSt", "removeItem", 
      "queryCallSt", "parenExpressionChain", "yieldItems", "yieldItem", 
      "mergeSt", "mergeAction", "setSt", "setItem", "nodeLabels", "createSt", 
      "patternWhere", "where", "pattern", "expression", "xorExpression", 
      "andExpression", "notExpression", "comparisonExpression", "comparisonSigns", 
      "addSubExpression", "multDivExpression", "powerExpression", "unaryAddSubExpression", 
      "atomicExpression", "listExpression", "stringExpression", "stringExpPrefix", 
      "nullExpression", "propertyOrLabelExpression", "propertyExpression", 
      "postfix", "labelCast", "patternPart", "shortestPathPattern", "allShortestPathsPattern", 
      "patternElem", "patternElemChain", "properties", "nodePattern", "atom", 
      "lhs", "relationshipPattern", "relationDetail", "relationshipTypes", 
      "unionSt", "subqueryExist", "invocationName", "functionInvocation", 
      "parenthesizedExpression", "filterWith", "patternComprehension", "relationshipsChainPattern", 
      "listComprehension", "reduceExpression", "filterExpression", "countAll", 
      "expressionChain", "caseExpression", "parameter", "literal", "rangeLit", 
      "boolLit", "numLit", "stringLit", "charLit", "listLit", "mapLit", 
      "mapPair", "name", "symbol", "reservedWord"
    },
    std::vector<std::string>{
      "", "'='", "'+='", "'<='", "'>='", "'>'", "'<'", "'<>'", "'..'", "';'", 
      "'.'", "','", "'('", "')'", "'{'", "'}'", "'['", "']'", "'-'", "'+'", 
      "'/'", "'%'", "'^'", "'*'", "'`'", "':'", "'::'", "'|'", "'$'", "'CALL'", 
      "'YIELD'", "'FILTER'", "'EXTRACT'", "'COUNT'", "'ANY'", "'NONE'", 
      "'SINGLE'", "'REDUCE'", "'SHORTESTPATH'", "'ALLSHORTESTPATHS'", "'ALL'", 
      "'ASC'", "'ASCENDING'", "'BY'", "'CREATE'", "'DELETE'", "'DESC'", 
      "'DESCENDING'", "'DETACH'", "'EXISTS'", "'EXPLAIN'", "'PROFILE'", 
      "'LIMIT'", "'MATCH'", "'MERGE'", "'ON'", "'OPTIONAL'", "'ORDER'", 
      "'REMOVE'", "'RETURN'", "'SET'", "'SKIP'", "'WHERE'", "'WITH'", "'UNION'", 
      "'UNWIND'", "'AND'", "'AS'", "'CONTAINS'", "'DISTINCT'", "'ENDS'", 
      "'IN'", "'IS'", "'NOT'", "'OR'", "'STARTS'", "'XOR'", "'FALSE'", "'TRUE'", 
      "'NULL'", "'CONSTRAINT'", "'DO'", "'FOR'", "'REQUIRE'", "'UNIQUE'", 
      "'CASE'", "'WHEN'", "'THEN'", "'ELSE'", "'END'", "'MANDATORY'", "'SCALAR'", 
      "'OF'", "'ADD'", "'DROP'", "'FOREACH'"
    },
    std::vector<std::string>{
      "", "ASSIGN", "ADD_ASSIGN", "LE", "GE", "GT", "LT", "NOT_EQUAL", "RANGE", 
      "SEMI", "DOT", "COMMA", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACK", 
      "RBRACK", "SUB", "PLUS", "DIV", "MOD", "CARET", "MULT", "ESC", "COLON", 
      "COLONCOLON", "STICK", "DOLLAR", "CALL", "YIELD", "FILTER", "EXTRACT", 
      "COUNT", "ANY", "NONE", "SINGLE", "REDUCE", "SHORTESTPATH", "ALLSHORTESTPATHS", 
      "ALL", "ASC", "ASCENDING", "BY", "CREATE", "DELETE", "DESC", "DESCENDING", 
      "DETACH", "EXISTS", "EXPLAIN", "PROFILE", "LIMIT", "MATCH", "MERGE", 
      "ON", "OPTIONAL", "ORDER", "REMOVE", "RETURN", "SET", "SKIP_W", "WHERE", 
      "WITH", "UNION", "UNWIND", "AND", "AS", "CONTAINS", "DISTINCT", "ENDS", 
      "IN", "IS", "NOT", "OR", "STARTS", "XOR", "FALSE", "TRUE", "NULL_W", 
      "CONSTRAINT", "DO", "FOR", "REQUIRE", "UNIQUE", "CASE", "WHEN", "THEN", 
      "ELSE", "END", "MANDATORY", "SCALAR", "OF", "ADD", "DROP", "FOREACH", 
      "ID", "ESC_LITERAL", "CHAR_LITERAL", "STRING_LITERAL", "DIGIT", "FLOAT", 
      "WS", "COMMENT", "LINE_COMMENT", "ERRCHAR", "Letter"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,106,897,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
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
  	84,2,85,7,85,2,86,7,86,2,87,7,87,2,88,7,88,2,89,7,89,2,90,7,90,2,91,7,
  	91,2,92,7,92,2,93,7,93,2,94,7,94,2,95,7,95,2,96,7,96,1,0,1,0,3,0,197,
  	8,0,1,0,1,0,1,1,3,1,202,8,1,1,1,1,1,3,1,206,8,1,1,2,1,2,5,2,210,8,2,10,
  	2,12,2,213,9,2,1,3,5,3,216,8,3,10,3,12,3,219,9,3,1,3,3,3,222,8,3,1,4,
  	1,4,1,4,3,4,227,8,4,1,5,1,5,1,5,3,5,232,8,5,1,6,1,6,1,6,1,6,1,6,1,6,3,
  	6,240,8,6,1,7,1,7,1,8,1,8,1,8,3,8,247,8,8,1,8,1,8,1,8,3,8,252,8,8,3,8,
  	254,8,8,1,9,1,9,1,9,1,10,1,10,1,10,3,10,262,8,10,1,11,1,11,1,11,1,12,
  	1,12,1,12,1,13,3,13,271,8,13,1,13,1,13,3,13,275,8,13,1,13,3,13,278,8,
  	13,1,13,3,13,281,8,13,1,14,1,14,3,14,285,8,14,1,14,1,14,5,14,289,8,14,
  	10,14,12,14,292,9,14,1,15,1,15,1,15,3,15,297,8,15,1,16,1,16,3,16,301,
  	8,16,1,17,1,17,1,17,1,17,1,17,5,17,308,8,17,10,17,12,17,311,9,17,1,18,
  	3,18,314,8,18,1,18,1,18,1,18,1,19,1,19,1,19,1,19,1,19,1,20,1,20,1,20,
  	3,20,327,8,20,1,21,1,21,1,21,1,21,1,21,3,21,334,8,21,1,22,3,22,337,8,
  	22,1,22,1,22,1,22,1,23,1,23,1,23,1,23,5,23,346,8,23,10,23,12,23,349,9,
  	23,1,24,1,24,1,24,1,24,1,24,1,24,1,24,4,24,358,8,24,11,24,12,24,359,1,
  	24,1,24,1,25,1,25,1,25,1,25,3,25,368,8,25,1,26,1,26,1,26,1,26,1,26,3,
  	26,375,8,26,1,27,1,27,3,27,379,8,27,1,27,1,27,1,28,1,28,1,28,5,28,386,
  	8,28,10,28,12,28,389,9,28,1,28,3,28,392,8,28,1,29,1,29,1,29,3,29,397,
  	8,29,1,29,1,29,1,30,1,30,1,30,5,30,404,8,30,10,30,12,30,407,9,30,1,31,
  	1,31,1,31,1,31,1,32,1,32,1,32,1,32,5,32,417,8,32,10,32,12,32,420,9,32,
  	1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,3,33,433,8,33,
  	1,34,1,34,4,34,437,8,34,11,34,12,34,438,1,35,1,35,1,35,1,36,1,36,3,36,
  	446,8,36,1,37,1,37,1,37,1,38,1,38,1,38,5,38,454,8,38,10,38,12,38,457,
  	9,38,1,39,1,39,1,39,5,39,462,8,39,10,39,12,39,465,9,39,1,40,1,40,1,40,
  	5,40,470,8,40,10,40,12,40,473,9,40,1,41,1,41,1,41,5,41,478,8,41,10,41,
  	12,41,481,9,41,1,42,5,42,484,8,42,10,42,12,42,487,9,42,1,42,1,42,1,43,
  	1,43,1,43,1,43,5,43,495,8,43,10,43,12,43,498,9,43,1,44,1,44,1,45,1,45,
  	1,45,5,45,505,8,45,10,45,12,45,508,9,45,1,46,1,46,1,46,5,46,513,8,46,
  	10,46,12,46,516,9,46,1,47,1,47,1,47,5,47,521,8,47,10,47,12,47,524,9,47,
  	1,48,3,48,527,8,48,1,48,1,48,1,49,1,49,1,49,1,49,5,49,535,8,49,10,49,
  	12,49,538,9,49,1,50,1,50,1,50,1,51,1,51,1,51,1,52,1,52,1,52,1,52,1,52,
  	3,52,551,8,52,1,53,1,53,3,53,555,8,53,1,53,1,53,1,54,1,54,3,54,561,8,
  	54,1,55,1,55,5,55,565,8,55,10,55,12,55,568,9,55,1,55,1,55,3,55,572,8,
  	55,1,56,1,56,1,56,1,56,3,56,578,8,56,1,56,1,56,3,56,582,8,56,1,56,3,56,
  	585,8,56,1,56,3,56,588,8,56,1,57,1,57,1,57,5,57,593,8,57,10,57,12,57,
  	596,9,57,1,58,1,58,1,58,3,58,601,8,58,1,58,1,58,1,58,3,58,606,8,58,1,
  	59,1,59,1,59,1,59,1,59,1,60,1,60,1,60,1,60,1,60,1,61,1,61,5,61,620,8,
  	61,10,61,12,61,623,9,61,1,61,1,61,1,61,1,61,3,61,629,8,61,1,62,1,62,1,
  	62,1,63,1,63,3,63,636,8,63,1,64,1,64,3,64,640,8,64,1,64,3,64,643,8,64,
  	1,64,3,64,646,8,64,1,64,1,64,1,65,1,65,1,65,1,65,1,65,1,65,1,65,1,65,
  	1,65,1,65,1,65,1,65,1,65,3,65,663,8,65,1,66,1,66,1,66,1,67,1,67,1,67,
  	3,67,671,8,67,1,67,1,67,3,67,675,8,67,1,67,1,67,3,67,679,8,67,1,67,1,
  	67,3,67,683,8,67,3,67,685,8,67,1,68,1,68,3,68,689,8,68,1,68,3,68,692,
  	8,68,1,68,3,68,695,8,68,1,68,3,68,698,8,68,1,68,1,68,1,69,1,69,1,69,1,
  	69,3,69,706,8,69,1,69,5,69,709,8,69,10,69,12,69,712,9,69,1,70,1,70,3,
  	70,716,8,70,1,70,1,70,1,71,1,71,1,71,1,71,3,71,724,8,71,1,71,1,71,1,72,
  	1,72,1,72,5,72,731,8,72,10,72,12,72,734,9,72,1,73,1,73,1,73,3,73,739,
  	8,73,1,73,3,73,742,8,73,1,73,1,73,1,74,1,74,1,74,1,74,1,75,1,75,1,75,
  	1,75,1,75,1,76,1,76,3,76,757,8,76,1,76,1,76,3,76,761,8,76,1,76,1,76,1,
  	76,1,76,1,77,1,77,4,77,769,8,77,11,77,12,77,770,1,78,1,78,1,78,1,78,3,
  	78,777,8,78,1,78,1,78,1,79,1,79,1,79,1,79,1,79,1,79,1,79,1,79,1,79,1,
  	79,1,79,1,79,1,79,1,80,1,80,1,80,1,80,3,80,798,8,80,1,81,1,81,1,81,1,
  	81,1,81,1,82,1,82,1,82,5,82,808,8,82,10,82,12,82,811,9,82,1,83,1,83,3,
  	83,815,8,83,1,83,1,83,1,83,1,83,1,83,4,83,822,8,83,11,83,12,83,823,1,
  	83,1,83,3,83,828,8,83,1,83,1,83,1,84,1,84,1,84,3,84,835,8,84,1,85,1,85,
  	1,85,1,85,1,85,1,85,1,85,3,85,844,8,85,1,86,1,86,1,86,3,86,849,8,86,1,
  	86,1,86,1,86,3,86,854,8,86,3,86,856,8,86,1,87,1,87,1,88,1,88,1,89,1,89,
  	1,90,1,90,1,91,1,91,3,91,868,8,91,1,91,1,91,1,92,1,92,1,92,1,92,5,92,
  	876,8,92,10,92,12,92,879,9,92,3,92,881,8,92,1,92,1,92,1,93,1,93,1,93,
  	1,93,1,94,1,94,3,94,891,8,94,1,95,1,95,1,96,1,96,1,96,0,0,97,0,2,4,6,
  	8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,
  	56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,
  	102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,
  	138,140,142,144,146,148,150,152,154,156,158,160,162,164,166,168,170,172,
  	174,176,178,180,182,184,186,188,190,192,0,11,1,0,50,51,2,0,41,42,46,47,
  	2,0,44,44,53,53,1,0,1,2,2,0,1,1,3,7,1,0,18,19,2,0,20,21,23,23,2,0,34,
  	36,40,40,1,0,77,78,2,0,31,36,95,97,2,0,40,49,52,94,935,0,194,1,0,0,0,
  	2,205,1,0,0,0,4,207,1,0,0,0,6,217,1,0,0,0,8,226,1,0,0,0,10,231,1,0,0,
  	0,12,239,1,0,0,0,14,241,1,0,0,0,16,243,1,0,0,0,18,255,1,0,0,0,20,258,
  	1,0,0,0,22,263,1,0,0,0,24,266,1,0,0,0,26,270,1,0,0,0,28,284,1,0,0,0,30,
  	293,1,0,0,0,32,298,1,0,0,0,34,302,1,0,0,0,36,313,1,0,0,0,38,318,1,0,0,
  	0,40,326,1,0,0,0,42,333,1,0,0,0,44,336,1,0,0,0,46,341,1,0,0,0,48,350,
  	1,0,0,0,50,367,1,0,0,0,52,369,1,0,0,0,54,376,1,0,0,0,56,382,1,0,0,0,58,
  	396,1,0,0,0,60,400,1,0,0,0,62,408,1,0,0,0,64,412,1,0,0,0,66,432,1,0,0,
  	0,68,436,1,0,0,0,70,440,1,0,0,0,72,443,1,0,0,0,74,447,1,0,0,0,76,450,
  	1,0,0,0,78,458,1,0,0,0,80,466,1,0,0,0,82,474,1,0,0,0,84,485,1,0,0,0,86,
  	490,1,0,0,0,88,499,1,0,0,0,90,501,1,0,0,0,92,509,1,0,0,0,94,517,1,0,0,
  	0,96,526,1,0,0,0,98,530,1,0,0,0,100,539,1,0,0,0,102,542,1,0,0,0,104,550,
  	1,0,0,0,106,552,1,0,0,0,108,558,1,0,0,0,110,562,1,0,0,0,112,587,1,0,0,
  	0,114,589,1,0,0,0,116,600,1,0,0,0,118,607,1,0,0,0,120,612,1,0,0,0,122,
  	628,1,0,0,0,124,630,1,0,0,0,126,635,1,0,0,0,128,637,1,0,0,0,130,662,1,
  	0,0,0,132,664,1,0,0,0,134,684,1,0,0,0,136,686,1,0,0,0,138,701,1,0,0,0,
  	140,713,1,0,0,0,142,719,1,0,0,0,144,727,1,0,0,0,146,735,1,0,0,0,148,745,
  	1,0,0,0,150,749,1,0,0,0,152,754,1,0,0,0,154,766,1,0,0,0,156,772,1,0,0,
  	0,158,780,1,0,0,0,160,793,1,0,0,0,162,799,1,0,0,0,164,804,1,0,0,0,166,
  	812,1,0,0,0,168,831,1,0,0,0,170,843,1,0,0,0,172,845,1,0,0,0,174,857,1,
  	0,0,0,176,859,1,0,0,0,178,861,1,0,0,0,180,863,1,0,0,0,182,865,1,0,0,0,
  	184,871,1,0,0,0,186,884,1,0,0,0,188,890,1,0,0,0,190,892,1,0,0,0,192,894,
  	1,0,0,0,194,196,3,2,1,0,195,197,5,9,0,0,196,195,1,0,0,0,196,197,1,0,0,
  	0,197,198,1,0,0,0,198,199,5,0,0,1,199,1,1,0,0,0,200,202,7,0,0,0,201,200,
  	1,0,0,0,201,202,1,0,0,0,202,203,1,0,0,0,203,206,3,4,2,0,204,206,3,16,
  	8,0,205,201,1,0,0,0,205,204,1,0,0,0,206,3,1,0,0,0,207,211,3,6,3,0,208,
  	210,3,140,70,0,209,208,1,0,0,0,210,213,1,0,0,0,211,209,1,0,0,0,211,212,
  	1,0,0,0,212,5,1,0,0,0,213,211,1,0,0,0,214,216,3,8,4,0,215,214,1,0,0,0,
  	216,219,1,0,0,0,217,215,1,0,0,0,217,218,1,0,0,0,218,221,1,0,0,0,219,217,
  	1,0,0,0,220,222,3,14,7,0,221,220,1,0,0,0,221,222,1,0,0,0,222,7,1,0,0,
  	0,223,227,3,10,5,0,224,227,3,12,6,0,225,227,3,52,26,0,226,223,1,0,0,0,
  	226,224,1,0,0,0,226,225,1,0,0,0,227,9,1,0,0,0,228,232,3,36,18,0,229,232,
  	3,38,19,0,230,232,3,20,10,0,231,228,1,0,0,0,231,229,1,0,0,0,231,230,1,
  	0,0,0,232,11,1,0,0,0,233,240,3,70,35,0,234,240,3,60,30,0,235,240,3,44,
  	22,0,236,240,3,64,32,0,237,240,3,46,23,0,238,240,3,48,24,0,239,233,1,
  	0,0,0,239,234,1,0,0,0,239,235,1,0,0,0,239,236,1,0,0,0,239,237,1,0,0,0,
  	239,238,1,0,0,0,240,13,1,0,0,0,241,242,3,18,9,0,242,15,1,0,0,0,243,244,
  	5,29,0,0,244,246,3,144,72,0,245,247,3,54,27,0,246,245,1,0,0,0,246,247,
  	1,0,0,0,247,253,1,0,0,0,248,251,5,30,0,0,249,252,5,23,0,0,250,252,3,56,
  	28,0,251,249,1,0,0,0,251,250,1,0,0,0,252,254,1,0,0,0,253,248,1,0,0,0,
  	253,254,1,0,0,0,254,17,1,0,0,0,255,256,5,59,0,0,256,257,3,26,13,0,257,
  	19,1,0,0,0,258,259,5,63,0,0,259,261,3,26,13,0,260,262,3,74,37,0,261,260,
  	1,0,0,0,261,262,1,0,0,0,262,21,1,0,0,0,263,264,5,61,0,0,264,265,3,78,
  	39,0,265,23,1,0,0,0,266,267,5,52,0,0,267,268,3,78,39,0,268,25,1,0,0,0,
  	269,271,5,69,0,0,270,269,1,0,0,0,270,271,1,0,0,0,271,272,1,0,0,0,272,
  	274,3,28,14,0,273,275,3,34,17,0,274,273,1,0,0,0,274,275,1,0,0,0,275,277,
  	1,0,0,0,276,278,3,22,11,0,277,276,1,0,0,0,277,278,1,0,0,0,278,280,1,0,
  	0,0,279,281,3,24,12,0,280,279,1,0,0,0,280,281,1,0,0,0,281,27,1,0,0,0,
  	282,285,5,23,0,0,283,285,3,30,15,0,284,282,1,0,0,0,284,283,1,0,0,0,285,
  	290,1,0,0,0,286,287,5,11,0,0,287,289,3,30,15,0,288,286,1,0,0,0,289,292,
  	1,0,0,0,290,288,1,0,0,0,290,291,1,0,0,0,291,29,1,0,0,0,292,290,1,0,0,
  	0,293,296,3,78,39,0,294,295,5,67,0,0,295,297,3,190,95,0,296,294,1,0,0,
  	0,296,297,1,0,0,0,297,31,1,0,0,0,298,300,3,78,39,0,299,301,7,1,0,0,300,
  	299,1,0,0,0,300,301,1,0,0,0,301,33,1,0,0,0,302,303,5,57,0,0,303,304,5,
  	43,0,0,304,309,3,32,16,0,305,306,5,11,0,0,306,308,3,32,16,0,307,305,1,
  	0,0,0,308,311,1,0,0,0,309,307,1,0,0,0,309,310,1,0,0,0,310,35,1,0,0,0,
  	311,309,1,0,0,0,312,314,5,56,0,0,313,312,1,0,0,0,313,314,1,0,0,0,314,
  	315,1,0,0,0,315,316,5,53,0,0,316,317,3,72,36,0,317,37,1,0,0,0,318,319,
  	5,65,0,0,319,320,3,78,39,0,320,321,5,67,0,0,321,322,3,190,95,0,322,39,
  	1,0,0,0,323,327,3,36,18,0,324,327,3,38,19,0,325,327,3,52,26,0,326,323,
  	1,0,0,0,326,324,1,0,0,0,326,325,1,0,0,0,327,41,1,0,0,0,328,334,3,70,35,
  	0,329,334,3,60,30,0,330,334,3,44,22,0,331,334,3,64,32,0,332,334,3,46,
  	23,0,333,328,1,0,0,0,333,329,1,0,0,0,333,330,1,0,0,0,333,331,1,0,0,0,
  	333,332,1,0,0,0,334,43,1,0,0,0,335,337,5,48,0,0,336,335,1,0,0,0,336,337,
  	1,0,0,0,337,338,1,0,0,0,338,339,5,45,0,0,339,340,3,164,82,0,340,45,1,
  	0,0,0,341,342,5,58,0,0,342,347,3,50,25,0,343,344,5,11,0,0,344,346,3,50,
  	25,0,345,343,1,0,0,0,346,349,1,0,0,0,347,345,1,0,0,0,347,348,1,0,0,0,
  	348,47,1,0,0,0,349,347,1,0,0,0,350,351,5,95,0,0,351,352,5,12,0,0,352,
  	353,3,190,95,0,353,354,5,71,0,0,354,355,3,78,39,0,355,357,5,27,0,0,356,
  	358,3,12,6,0,357,356,1,0,0,0,358,359,1,0,0,0,359,357,1,0,0,0,359,360,
  	1,0,0,0,360,361,1,0,0,0,361,362,5,13,0,0,362,49,1,0,0,0,363,364,3,190,
  	95,0,364,365,3,68,34,0,365,368,1,0,0,0,366,368,3,110,55,0,367,363,1,0,
  	0,0,367,366,1,0,0,0,368,51,1,0,0,0,369,370,5,29,0,0,370,371,3,144,72,
  	0,371,374,3,54,27,0,372,373,5,30,0,0,373,375,3,56,28,0,374,372,1,0,0,
  	0,374,375,1,0,0,0,375,53,1,0,0,0,376,378,5,12,0,0,377,379,3,164,82,0,
  	378,377,1,0,0,0,378,379,1,0,0,0,379,380,1,0,0,0,380,381,5,13,0,0,381,
  	55,1,0,0,0,382,387,3,58,29,0,383,384,5,11,0,0,384,386,3,58,29,0,385,383,
  	1,0,0,0,386,389,1,0,0,0,387,385,1,0,0,0,387,388,1,0,0,0,388,391,1,0,0,
  	0,389,387,1,0,0,0,390,392,3,74,37,0,391,390,1,0,0,0,391,392,1,0,0,0,392,
  	57,1,0,0,0,393,394,3,190,95,0,394,395,5,67,0,0,395,397,1,0,0,0,396,393,
  	1,0,0,0,396,397,1,0,0,0,397,398,1,0,0,0,398,399,3,190,95,0,399,59,1,0,
  	0,0,400,401,5,54,0,0,401,405,3,116,58,0,402,404,3,62,31,0,403,402,1,0,
  	0,0,404,407,1,0,0,0,405,403,1,0,0,0,405,406,1,0,0,0,406,61,1,0,0,0,407,
  	405,1,0,0,0,408,409,5,55,0,0,409,410,7,2,0,0,410,411,3,64,32,0,411,63,
  	1,0,0,0,412,413,5,60,0,0,413,418,3,66,33,0,414,415,5,11,0,0,415,417,3,
  	66,33,0,416,414,1,0,0,0,417,420,1,0,0,0,418,416,1,0,0,0,418,419,1,0,0,
  	0,419,65,1,0,0,0,420,418,1,0,0,0,421,422,3,110,55,0,422,423,5,1,0,0,423,
  	424,3,78,39,0,424,433,1,0,0,0,425,426,3,190,95,0,426,427,7,3,0,0,427,
  	428,3,78,39,0,428,433,1,0,0,0,429,430,3,190,95,0,430,431,3,68,34,0,431,
  	433,1,0,0,0,432,421,1,0,0,0,432,425,1,0,0,0,432,429,1,0,0,0,433,67,1,
  	0,0,0,434,435,5,25,0,0,435,437,3,188,94,0,436,434,1,0,0,0,437,438,1,0,
  	0,0,438,436,1,0,0,0,438,439,1,0,0,0,439,69,1,0,0,0,440,441,5,44,0,0,441,
  	442,3,76,38,0,442,71,1,0,0,0,443,445,3,76,38,0,444,446,3,74,37,0,445,
  	444,1,0,0,0,445,446,1,0,0,0,446,73,1,0,0,0,447,448,5,62,0,0,448,449,3,
  	78,39,0,449,75,1,0,0,0,450,455,3,116,58,0,451,452,5,11,0,0,452,454,3,
  	116,58,0,453,451,1,0,0,0,454,457,1,0,0,0,455,453,1,0,0,0,455,456,1,0,
  	0,0,456,77,1,0,0,0,457,455,1,0,0,0,458,463,3,80,40,0,459,460,5,74,0,0,
  	460,462,3,80,40,0,461,459,1,0,0,0,462,465,1,0,0,0,463,461,1,0,0,0,463,
  	464,1,0,0,0,464,79,1,0,0,0,465,463,1,0,0,0,466,471,3,82,41,0,467,468,
  	5,76,0,0,468,470,3,82,41,0,469,467,1,0,0,0,470,473,1,0,0,0,471,469,1,
  	0,0,0,471,472,1,0,0,0,472,81,1,0,0,0,473,471,1,0,0,0,474,479,3,84,42,
  	0,475,476,5,66,0,0,476,478,3,84,42,0,477,475,1,0,0,0,478,481,1,0,0,0,
  	479,477,1,0,0,0,479,480,1,0,0,0,480,83,1,0,0,0,481,479,1,0,0,0,482,484,
  	5,73,0,0,483,482,1,0,0,0,484,487,1,0,0,0,485,483,1,0,0,0,485,486,1,0,
  	0,0,486,488,1,0,0,0,487,485,1,0,0,0,488,489,3,86,43,0,489,85,1,0,0,0,
  	490,496,3,90,45,0,491,492,3,88,44,0,492,493,3,90,45,0,493,495,1,0,0,0,
  	494,491,1,0,0,0,495,498,1,0,0,0,496,494,1,0,0,0,496,497,1,0,0,0,497,87,
  	1,0,0,0,498,496,1,0,0,0,499,500,7,4,0,0,500,89,1,0,0,0,501,506,3,92,46,
  	0,502,503,7,5,0,0,503,505,3,92,46,0,504,502,1,0,0,0,505,508,1,0,0,0,506,
  	504,1,0,0,0,506,507,1,0,0,0,507,91,1,0,0,0,508,506,1,0,0,0,509,514,3,
  	94,47,0,510,511,7,6,0,0,511,513,3,94,47,0,512,510,1,0,0,0,513,516,1,0,
  	0,0,514,512,1,0,0,0,514,515,1,0,0,0,515,93,1,0,0,0,516,514,1,0,0,0,517,
  	522,3,96,48,0,518,519,5,22,0,0,519,521,3,96,48,0,520,518,1,0,0,0,521,
  	524,1,0,0,0,522,520,1,0,0,0,522,523,1,0,0,0,523,95,1,0,0,0,524,522,1,
  	0,0,0,525,527,7,5,0,0,526,525,1,0,0,0,526,527,1,0,0,0,527,528,1,0,0,0,
  	528,529,3,98,49,0,529,97,1,0,0,0,530,536,3,108,54,0,531,535,3,102,51,
  	0,532,535,3,100,50,0,533,535,3,106,53,0,534,531,1,0,0,0,534,532,1,0,0,
  	0,534,533,1,0,0,0,535,538,1,0,0,0,536,534,1,0,0,0,536,537,1,0,0,0,537,
  	99,1,0,0,0,538,536,1,0,0,0,539,540,5,71,0,0,540,541,3,108,54,0,541,101,
  	1,0,0,0,542,543,3,104,52,0,543,544,3,108,54,0,544,103,1,0,0,0,545,546,
  	5,75,0,0,546,551,5,63,0,0,547,548,5,70,0,0,548,551,5,63,0,0,549,551,5,
  	68,0,0,550,545,1,0,0,0,550,547,1,0,0,0,550,549,1,0,0,0,551,105,1,0,0,
  	0,552,554,5,72,0,0,553,555,5,73,0,0,554,553,1,0,0,0,554,555,1,0,0,0,555,
  	556,1,0,0,0,556,557,5,79,0,0,557,107,1,0,0,0,558,560,3,110,55,0,559,561,
  	3,68,34,0,560,559,1,0,0,0,560,561,1,0,0,0,561,109,1,0,0,0,562,566,3,130,
  	65,0,563,565,3,112,56,0,564,563,1,0,0,0,565,568,1,0,0,0,566,564,1,0,0,
  	0,566,567,1,0,0,0,567,571,1,0,0,0,568,566,1,0,0,0,569,570,5,26,0,0,570,
  	572,3,114,57,0,571,569,1,0,0,0,571,572,1,0,0,0,572,111,1,0,0,0,573,574,
  	5,10,0,0,574,588,3,188,94,0,575,584,5,16,0,0,576,578,3,78,39,0,577,576,
  	1,0,0,0,577,578,1,0,0,0,578,579,1,0,0,0,579,581,5,8,0,0,580,582,3,78,
  	39,0,581,580,1,0,0,0,581,582,1,0,0,0,582,585,1,0,0,0,583,585,3,78,39,
  	0,584,577,1,0,0,0,584,583,1,0,0,0,585,586,1,0,0,0,586,588,5,17,0,0,587,
  	573,1,0,0,0,587,575,1,0,0,0,588,113,1,0,0,0,589,594,3,188,94,0,590,591,
  	5,10,0,0,591,593,3,188,94,0,592,590,1,0,0,0,593,596,1,0,0,0,594,592,1,
  	0,0,0,594,595,1,0,0,0,595,115,1,0,0,0,596,594,1,0,0,0,597,598,3,190,95,
  	0,598,599,5,1,0,0,599,601,1,0,0,0,600,597,1,0,0,0,600,601,1,0,0,0,601,
  	605,1,0,0,0,602,606,3,118,59,0,603,606,3,120,60,0,604,606,3,122,61,0,
  	605,602,1,0,0,0,605,603,1,0,0,0,605,604,1,0,0,0,606,117,1,0,0,0,607,608,
  	5,38,0,0,608,609,5,12,0,0,609,610,3,122,61,0,610,611,5,13,0,0,611,119,
  	1,0,0,0,612,613,5,39,0,0,613,614,5,12,0,0,614,615,3,122,61,0,615,616,
  	5,13,0,0,616,121,1,0,0,0,617,621,3,128,64,0,618,620,3,124,62,0,619,618,
  	1,0,0,0,620,623,1,0,0,0,621,619,1,0,0,0,621,622,1,0,0,0,622,629,1,0,0,
  	0,623,621,1,0,0,0,624,625,5,12,0,0,625,626,3,122,61,0,626,627,5,13,0,
  	0,627,629,1,0,0,0,628,617,1,0,0,0,628,624,1,0,0,0,629,123,1,0,0,0,630,
  	631,3,134,67,0,631,632,3,128,64,0,632,125,1,0,0,0,633,636,3,184,92,0,
  	634,636,3,168,84,0,635,633,1,0,0,0,635,634,1,0,0,0,636,127,1,0,0,0,637,
  	639,5,12,0,0,638,640,3,190,95,0,639,638,1,0,0,0,639,640,1,0,0,0,640,642,
  	1,0,0,0,641,643,3,68,34,0,642,641,1,0,0,0,642,643,1,0,0,0,643,645,1,0,
  	0,0,644,646,3,126,63,0,645,644,1,0,0,0,645,646,1,0,0,0,646,647,1,0,0,
  	0,647,648,5,13,0,0,648,129,1,0,0,0,649,663,3,170,85,0,650,663,3,168,84,
  	0,651,663,3,166,83,0,652,663,3,162,81,0,653,663,3,156,78,0,654,663,3,
  	158,79,0,655,663,3,152,76,0,656,663,3,150,75,0,657,663,3,154,77,0,658,
  	663,3,148,74,0,659,663,3,146,73,0,660,663,3,190,95,0,661,663,3,142,71,
  	0,662,649,1,0,0,0,662,650,1,0,0,0,662,651,1,0,0,0,662,652,1,0,0,0,662,
  	653,1,0,0,0,662,654,1,0,0,0,662,655,1,0,0,0,662,656,1,0,0,0,662,657,1,
  	0,0,0,662,658,1,0,0,0,662,659,1,0,0,0,662,660,1,0,0,0,662,661,1,0,0,0,
  	663,131,1,0,0,0,664,665,3,190,95,0,665,666,5,1,0,0,666,133,1,0,0,0,667,
  	668,5,6,0,0,668,670,5,18,0,0,669,671,3,136,68,0,670,669,1,0,0,0,670,671,
  	1,0,0,0,671,672,1,0,0,0,672,674,5,18,0,0,673,675,5,5,0,0,674,673,1,0,
  	0,0,674,675,1,0,0,0,675,685,1,0,0,0,676,678,5,18,0,0,677,679,3,136,68,
  	0,678,677,1,0,0,0,678,679,1,0,0,0,679,680,1,0,0,0,680,682,5,18,0,0,681,
  	683,5,5,0,0,682,681,1,0,0,0,682,683,1,0,0,0,683,685,1,0,0,0,684,667,1,
  	0,0,0,684,676,1,0,0,0,685,135,1,0,0,0,686,688,5,16,0,0,687,689,3,190,
  	95,0,688,687,1,0,0,0,688,689,1,0,0,0,689,691,1,0,0,0,690,692,3,138,69,
  	0,691,690,1,0,0,0,691,692,1,0,0,0,692,694,1,0,0,0,693,695,3,172,86,0,
  	694,693,1,0,0,0,694,695,1,0,0,0,695,697,1,0,0,0,696,698,3,126,63,0,697,
  	696,1,0,0,0,697,698,1,0,0,0,698,699,1,0,0,0,699,700,5,17,0,0,700,137,
  	1,0,0,0,701,702,5,25,0,0,702,710,3,188,94,0,703,705,5,27,0,0,704,706,
  	5,25,0,0,705,704,1,0,0,0,705,706,1,0,0,0,706,707,1,0,0,0,707,709,3,188,
  	94,0,708,703,1,0,0,0,709,712,1,0,0,0,710,708,1,0,0,0,710,711,1,0,0,0,
  	711,139,1,0,0,0,712,710,1,0,0,0,713,715,5,64,0,0,714,716,5,40,0,0,715,
  	714,1,0,0,0,715,716,1,0,0,0,716,717,1,0,0,0,717,718,3,6,3,0,718,141,1,
  	0,0,0,719,720,5,49,0,0,720,723,5,14,0,0,721,724,3,4,2,0,722,724,3,72,
  	36,0,723,721,1,0,0,0,723,722,1,0,0,0,724,725,1,0,0,0,725,726,5,15,0,0,
  	726,143,1,0,0,0,727,732,3,190,95,0,728,729,5,10,0,0,729,731,3,190,95,
  	0,730,728,1,0,0,0,731,734,1,0,0,0,732,730,1,0,0,0,732,733,1,0,0,0,733,
  	145,1,0,0,0,734,732,1,0,0,0,735,736,3,144,72,0,736,738,5,12,0,0,737,739,
  	5,69,0,0,738,737,1,0,0,0,738,739,1,0,0,0,739,741,1,0,0,0,740,742,3,164,
  	82,0,741,740,1,0,0,0,741,742,1,0,0,0,742,743,1,0,0,0,743,744,5,13,0,0,
  	744,147,1,0,0,0,745,746,5,12,0,0,746,747,3,78,39,0,747,748,5,13,0,0,748,
  	149,1,0,0,0,749,750,7,7,0,0,750,751,5,12,0,0,751,752,3,160,80,0,752,753,
  	5,13,0,0,753,151,1,0,0,0,754,756,5,16,0,0,755,757,3,132,66,0,756,755,
  	1,0,0,0,756,757,1,0,0,0,757,758,1,0,0,0,758,760,3,154,77,0,759,761,3,
  	74,37,0,760,759,1,0,0,0,760,761,1,0,0,0,761,762,1,0,0,0,762,763,5,27,
  	0,0,763,764,3,78,39,0,764,765,5,17,0,0,765,153,1,0,0,0,766,768,3,128,
  	64,0,767,769,3,124,62,0,768,767,1,0,0,0,769,770,1,0,0,0,770,768,1,0,0,
  	0,770,771,1,0,0,0,771,155,1,0,0,0,772,773,5,16,0,0,773,776,3,160,80,0,
  	774,775,5,27,0,0,775,777,3,78,39,0,776,774,1,0,0,0,776,777,1,0,0,0,777,
  	778,1,0,0,0,778,779,5,17,0,0,779,157,1,0,0,0,780,781,5,37,0,0,781,782,
  	5,12,0,0,782,783,3,190,95,0,783,784,5,1,0,0,784,785,3,78,39,0,785,786,
  	5,11,0,0,786,787,3,190,95,0,787,788,5,71,0,0,788,789,3,78,39,0,789,790,
  	5,27,0,0,790,791,3,78,39,0,791,792,5,13,0,0,792,159,1,0,0,0,793,794,3,
  	190,95,0,794,795,5,71,0,0,795,797,3,78,39,0,796,798,3,74,37,0,797,796,
  	1,0,0,0,797,798,1,0,0,0,798,161,1,0,0,0,799,800,5,33,0,0,800,801,5,12,
  	0,0,801,802,5,23,0,0,802,803,5,13,0,0,803,163,1,0,0,0,804,809,3,78,39,
  	0,805,806,5,11,0,0,806,808,3,78,39,0,807,805,1,0,0,0,808,811,1,0,0,0,
  	809,807,1,0,0,0,809,810,1,0,0,0,810,165,1,0,0,0,811,809,1,0,0,0,812,814,
  	5,85,0,0,813,815,3,78,39,0,814,813,1,0,0,0,814,815,1,0,0,0,815,821,1,
  	0,0,0,816,817,5,86,0,0,817,818,3,78,39,0,818,819,5,87,0,0,819,820,3,78,
  	39,0,820,822,1,0,0,0,821,816,1,0,0,0,822,823,1,0,0,0,823,821,1,0,0,0,
  	823,824,1,0,0,0,824,827,1,0,0,0,825,826,5,88,0,0,826,828,3,78,39,0,827,
  	825,1,0,0,0,827,828,1,0,0,0,828,829,1,0,0,0,829,830,5,89,0,0,830,167,
  	1,0,0,0,831,834,5,28,0,0,832,835,3,190,95,0,833,835,3,176,88,0,834,832,
  	1,0,0,0,834,833,1,0,0,0,835,169,1,0,0,0,836,844,3,174,87,0,837,844,3,
  	176,88,0,838,844,5,79,0,0,839,844,3,178,89,0,840,844,3,180,90,0,841,844,
  	3,182,91,0,842,844,3,184,92,0,843,836,1,0,0,0,843,837,1,0,0,0,843,838,
  	1,0,0,0,843,839,1,0,0,0,843,840,1,0,0,0,843,841,1,0,0,0,843,842,1,0,0,
  	0,844,171,1,0,0,0,845,848,5,23,0,0,846,849,3,176,88,0,847,849,5,96,0,
  	0,848,846,1,0,0,0,848,847,1,0,0,0,848,849,1,0,0,0,849,855,1,0,0,0,850,
  	853,5,8,0,0,851,854,3,176,88,0,852,854,5,96,0,0,853,851,1,0,0,0,853,852,
  	1,0,0,0,853,854,1,0,0,0,854,856,1,0,0,0,855,850,1,0,0,0,855,856,1,0,0,
  	0,856,173,1,0,0,0,857,858,7,8,0,0,858,175,1,0,0,0,859,860,5,100,0,0,860,
  	177,1,0,0,0,861,862,5,99,0,0,862,179,1,0,0,0,863,864,5,98,0,0,864,181,
  	1,0,0,0,865,867,5,16,0,0,866,868,3,164,82,0,867,866,1,0,0,0,867,868,1,
  	0,0,0,868,869,1,0,0,0,869,870,5,17,0,0,870,183,1,0,0,0,871,880,5,14,0,
  	0,872,877,3,186,93,0,873,874,5,11,0,0,874,876,3,186,93,0,875,873,1,0,
  	0,0,876,879,1,0,0,0,877,875,1,0,0,0,877,878,1,0,0,0,878,881,1,0,0,0,879,
  	877,1,0,0,0,880,872,1,0,0,0,880,881,1,0,0,0,881,882,1,0,0,0,882,883,5,
  	15,0,0,883,185,1,0,0,0,884,885,3,188,94,0,885,886,5,25,0,0,886,887,3,
  	78,39,0,887,187,1,0,0,0,888,891,3,190,95,0,889,891,3,192,96,0,890,888,
  	1,0,0,0,890,889,1,0,0,0,891,189,1,0,0,0,892,893,7,9,0,0,893,191,1,0,0,
  	0,894,895,7,10,0,0,895,193,1,0,0,0,104,196,201,205,211,217,221,226,231,
  	239,246,251,253,261,270,274,277,280,284,290,296,300,309,313,326,333,336,
  	347,359,367,374,378,387,391,396,405,418,432,438,445,455,463,471,479,485,
  	496,506,514,522,526,534,536,550,554,560,566,571,577,581,584,587,594,600,
  	605,621,628,635,639,642,645,662,670,674,678,682,684,688,691,694,697,705,
  	710,715,723,732,738,741,756,760,770,776,797,809,814,823,827,834,843,848,
  	853,855,867,877,880,890
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
    setState(194);
    query();
    setState(196);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SEMI) {
      setState(195);
      match(CypherParser::SEMI);
    }
    setState(198);
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

tree::TerminalNode* CypherParser::QueryContext::PROFILE() {
  return getToken(CypherParser::PROFILE, 0);
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
    setState(205);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 2, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(201);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::EXPLAIN

      || _la == CypherParser::PROFILE) {
        setState(200);
        _la = _input->LA(1);
        if (!(_la == CypherParser::EXPLAIN

        || _la == CypherParser::PROFILE)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
      }
      setState(203);
      regularQuery();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(204);
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
    setState(207);
    singleQuery();
    setState(211);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::UNION) {
      setState(208);
      unionSt();
      setState(213);
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

std::vector<CypherParser::ClauseContext *> CypherParser::SingleQueryContext::clause() {
  return getRuleContexts<CypherParser::ClauseContext>();
}

CypherParser::ClauseContext* CypherParser::SingleQueryContext::clause(size_t i) {
  return getRuleContext<CypherParser::ClauseContext>(i);
}

CypherParser::PrimitiveResultStatementContext* CypherParser::SingleQueryContext::primitiveResultStatement() {
  return getRuleContext<CypherParser::PrimitiveResultStatementContext>(0);
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
    setState(217);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -7682806712222351360) != 0) || _la == CypherParser::UNWIND

    || _la == CypherParser::FOREACH) {
      setState(214);
      clause();
      setState(219);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(221);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RETURN) {
      setState(220);
      primitiveResultStatement();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ClauseContext ------------------------------------------------------------------

CypherParser::ClauseContext::ClauseContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ReadingClauseContext* CypherParser::ClauseContext::readingClause() {
  return getRuleContext<CypherParser::ReadingClauseContext>(0);
}

CypherParser::UpdatingClauseContext* CypherParser::ClauseContext::updatingClause() {
  return getRuleContext<CypherParser::UpdatingClauseContext>(0);
}

CypherParser::QueryCallStContext* CypherParser::ClauseContext::queryCallSt() {
  return getRuleContext<CypherParser::QueryCallStContext>(0);
}


size_t CypherParser::ClauseContext::getRuleIndex() const {
  return CypherParser::RuleClause;
}


std::any CypherParser::ClauseContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitClause(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ClauseContext* CypherParser::clause() {
  ClauseContext *_localctx = _tracker.createInstance<ClauseContext>(_ctx, getState());
  enterRule(_localctx, 8, CypherParser::RuleClause);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(226);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL:
      case CypherParser::WITH:
      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 1);
        setState(223);
        readingClause();
        break;
      }

      case CypherParser::CREATE:
      case CypherParser::DELETE:
      case CypherParser::DETACH:
      case CypherParser::MERGE:
      case CypherParser::REMOVE:
      case CypherParser::SET:
      case CypherParser::FOREACH: {
        enterOuterAlt(_localctx, 2);
        setState(224);
        updatingClause();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(225);
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

//----------------- ReadingClauseContext ------------------------------------------------------------------

CypherParser::ReadingClauseContext::ReadingClauseContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::MatchStContext* CypherParser::ReadingClauseContext::matchSt() {
  return getRuleContext<CypherParser::MatchStContext>(0);
}

CypherParser::UnwindStContext* CypherParser::ReadingClauseContext::unwindSt() {
  return getRuleContext<CypherParser::UnwindStContext>(0);
}

CypherParser::WithStContext* CypherParser::ReadingClauseContext::withSt() {
  return getRuleContext<CypherParser::WithStContext>(0);
}


size_t CypherParser::ReadingClauseContext::getRuleIndex() const {
  return CypherParser::RuleReadingClause;
}


std::any CypherParser::ReadingClauseContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitReadingClause(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ReadingClauseContext* CypherParser::readingClause() {
  ReadingClauseContext *_localctx = _tracker.createInstance<ReadingClauseContext>(_ctx, getState());
  enterRule(_localctx, 10, CypherParser::RuleReadingClause);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(231);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(228);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(229);
        unwindSt();
        break;
      }

      case CypherParser::WITH: {
        enterOuterAlt(_localctx, 3);
        setState(230);
        withSt();
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

//----------------- UpdatingClauseContext ------------------------------------------------------------------

CypherParser::UpdatingClauseContext::UpdatingClauseContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::CreateStContext* CypherParser::UpdatingClauseContext::createSt() {
  return getRuleContext<CypherParser::CreateStContext>(0);
}

CypherParser::MergeStContext* CypherParser::UpdatingClauseContext::mergeSt() {
  return getRuleContext<CypherParser::MergeStContext>(0);
}

CypherParser::DeleteStContext* CypherParser::UpdatingClauseContext::deleteSt() {
  return getRuleContext<CypherParser::DeleteStContext>(0);
}

CypherParser::SetStContext* CypherParser::UpdatingClauseContext::setSt() {
  return getRuleContext<CypherParser::SetStContext>(0);
}

CypherParser::RemoveStContext* CypherParser::UpdatingClauseContext::removeSt() {
  return getRuleContext<CypherParser::RemoveStContext>(0);
}

CypherParser::ForeachStContext* CypherParser::UpdatingClauseContext::foreachSt() {
  return getRuleContext<CypherParser::ForeachStContext>(0);
}


size_t CypherParser::UpdatingClauseContext::getRuleIndex() const {
  return CypherParser::RuleUpdatingClause;
}


std::any CypherParser::UpdatingClauseContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitUpdatingClause(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::UpdatingClauseContext* CypherParser::updatingClause() {
  UpdatingClauseContext *_localctx = _tracker.createInstance<UpdatingClauseContext>(_ctx, getState());
  enterRule(_localctx, 12, CypherParser::RuleUpdatingClause);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(239);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(233);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(234);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(235);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(236);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(237);
        removeSt();
        break;
      }

      case CypherParser::FOREACH: {
        enterOuterAlt(_localctx, 6);
        setState(238);
        foreachSt();
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

//----------------- PrimitiveResultStatementContext ------------------------------------------------------------------

CypherParser::PrimitiveResultStatementContext::PrimitiveResultStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CypherParser::ReturnStContext* CypherParser::PrimitiveResultStatementContext::returnSt() {
  return getRuleContext<CypherParser::ReturnStContext>(0);
}


size_t CypherParser::PrimitiveResultStatementContext::getRuleIndex() const {
  return CypherParser::RulePrimitiveResultStatement;
}


std::any CypherParser::PrimitiveResultStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPrimitiveResultStatement(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PrimitiveResultStatementContext* CypherParser::primitiveResultStatement() {
  PrimitiveResultStatementContext *_localctx = _tracker.createInstance<PrimitiveResultStatementContext>(_ctx, getState());
  enterRule(_localctx, 14, CypherParser::RulePrimitiveResultStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(241);
    returnSt();
   
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
  enterRule(_localctx, 16, CypherParser::RuleStandaloneCall);
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
    setState(243);
    match(CypherParser::CALL);
    setState(244);
    invocationName();
    setState(246);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LPAREN) {
      setState(245);
      parenExpressionChain();
    }
    setState(253);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(248);
      match(CypherParser::YIELD);
      setState(251);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::MULT: {
          setState(249);
          match(CypherParser::MULT);
          break;
        }

        case CypherParser::FILTER:
        case CypherParser::EXTRACT:
        case CypherParser::COUNT:
        case CypherParser::ANY:
        case CypherParser::NONE:
        case CypherParser::SINGLE:
        case CypherParser::FOREACH:
        case CypherParser::ID:
        case CypherParser::ESC_LITERAL: {
          setState(250);
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
  enterRule(_localctx, 18, CypherParser::RuleReturnSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(255);
    match(CypherParser::RETURN);
    setState(256);
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
  enterRule(_localctx, 20, CypherParser::RuleWithSt);
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
    setState(258);
    match(CypherParser::WITH);
    setState(259);
    projectionBody();
    setState(261);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(260);
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
  enterRule(_localctx, 22, CypherParser::RuleSkipSt);

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
    match(CypherParser::SKIP_W);
    setState(264);
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
  enterRule(_localctx, 24, CypherParser::RuleLimitSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(266);
    match(CypherParser::LIMIT);
    setState(267);
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
  enterRule(_localctx, 26, CypherParser::RuleProjectionBody);
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
    setState(270);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(269);
      match(CypherParser::DISTINCT);
    }
    setState(272);
    projectionItems();
    setState(274);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ORDER) {
      setState(273);
      orderSt();
    }
    setState(277);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SKIP_W) {
      setState(276);
      skipSt();
    }
    setState(280);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LIMIT) {
      setState(279);
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
  enterRule(_localctx, 28, CypherParser::RuleProjectionItems);
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
    setState(284);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MULT: {
        setState(282);
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
      case CypherParser::REDUCE:
      case CypherParser::ALL:
      case CypherParser::EXISTS:
      case CypherParser::NOT:
      case CypherParser::FALSE:
      case CypherParser::TRUE:
      case CypherParser::NULL_W:
      case CypherParser::CASE:
      case CypherParser::FOREACH:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL:
      case CypherParser::CHAR_LITERAL:
      case CypherParser::STRING_LITERAL:
      case CypherParser::DIGIT: {
        setState(283);
        projectionItem();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(290);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(286);
      match(CypherParser::COMMA);
      setState(287);
      projectionItem();
      setState(292);
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
  enterRule(_localctx, 30, CypherParser::RuleProjectionItem);
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
    setState(293);
    expression();
    setState(296);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::AS) {
      setState(294);
      match(CypherParser::AS);
      setState(295);
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
  enterRule(_localctx, 32, CypherParser::RuleOrderItem);
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
    setState(298);
    expression();
    setState(300);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 217703302299648) != 0)) {
      setState(299);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 217703302299648) != 0))) {
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
  enterRule(_localctx, 34, CypherParser::RuleOrderSt);
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
    setState(302);
    match(CypherParser::ORDER);
    setState(303);
    match(CypherParser::BY);
    setState(304);
    orderItem();
    setState(309);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(305);
      match(CypherParser::COMMA);
      setState(306);
      orderItem();
      setState(311);
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
  enterRule(_localctx, 36, CypherParser::RuleMatchSt);
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
    setState(313);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::OPTIONAL) {
      setState(312);
      match(CypherParser::OPTIONAL);
    }
    setState(315);
    match(CypherParser::MATCH);
    setState(316);
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
  enterRule(_localctx, 38, CypherParser::RuleUnwindSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(318);
    match(CypherParser::UNWIND);
    setState(319);
    expression();
    setState(320);
    match(CypherParser::AS);
    setState(321);
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
  enterRule(_localctx, 40, CypherParser::RuleReadingStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(326);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(323);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(324);
        unwindSt();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(325);
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
  enterRule(_localctx, 42, CypherParser::RuleUpdatingStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(333);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(328);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(329);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(330);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(331);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(332);
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
  enterRule(_localctx, 44, CypherParser::RuleDeleteSt);
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
    setState(336);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DETACH) {
      setState(335);
      match(CypherParser::DETACH);
    }
    setState(338);
    match(CypherParser::DELETE);
    setState(339);
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
  enterRule(_localctx, 46, CypherParser::RuleRemoveSt);
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
    setState(341);
    match(CypherParser::REMOVE);
    setState(342);
    removeItem();
    setState(347);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(343);
      match(CypherParser::COMMA);
      setState(344);
      removeItem();
      setState(349);
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

//----------------- ForeachStContext ------------------------------------------------------------------

CypherParser::ForeachStContext::ForeachStContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ForeachStContext::FOREACH() {
  return getToken(CypherParser::FOREACH, 0);
}

tree::TerminalNode* CypherParser::ForeachStContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

CypherParser::SymbolContext* CypherParser::ForeachStContext::symbol() {
  return getRuleContext<CypherParser::SymbolContext>(0);
}

tree::TerminalNode* CypherParser::ForeachStContext::IN() {
  return getToken(CypherParser::IN, 0);
}

CypherParser::ExpressionContext* CypherParser::ForeachStContext::expression() {
  return getRuleContext<CypherParser::ExpressionContext>(0);
}

tree::TerminalNode* CypherParser::ForeachStContext::STICK() {
  return getToken(CypherParser::STICK, 0);
}

tree::TerminalNode* CypherParser::ForeachStContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}

std::vector<CypherParser::UpdatingClauseContext *> CypherParser::ForeachStContext::updatingClause() {
  return getRuleContexts<CypherParser::UpdatingClauseContext>();
}

CypherParser::UpdatingClauseContext* CypherParser::ForeachStContext::updatingClause(size_t i) {
  return getRuleContext<CypherParser::UpdatingClauseContext>(i);
}


size_t CypherParser::ForeachStContext::getRuleIndex() const {
  return CypherParser::RuleForeachSt;
}


std::any CypherParser::ForeachStContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitForeachSt(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ForeachStContext* CypherParser::foreachSt() {
  ForeachStContext *_localctx = _tracker.createInstance<ForeachStContext>(_ctx, getState());
  enterRule(_localctx, 48, CypherParser::RuleForeachSt);
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
    setState(350);
    match(CypherParser::FOREACH);
    setState(351);
    match(CypherParser::LPAREN);
    setState(352);
    symbol();
    setState(353);
    match(CypherParser::IN);
    setState(354);
    expression();
    setState(355);
    match(CypherParser::STICK);
    setState(357); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(356);
      updatingClause();
      setState(359); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (((((_la - 44) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 44)) & 2251799813768211) != 0));
    setState(361);
    match(CypherParser::RPAREN);
   
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
  enterRule(_localctx, 50, CypherParser::RuleRemoveItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(367);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(363);
      symbol();
      setState(364);
      nodeLabels();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(366);
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
  enterRule(_localctx, 52, CypherParser::RuleQueryCallSt);
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
    setState(369);
    match(CypherParser::CALL);
    setState(370);
    invocationName();
    setState(371);
    parenExpressionChain();
    setState(374);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(372);
      match(CypherParser::YIELD);
      setState(373);
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
  enterRule(_localctx, 54, CypherParser::RuleParenExpressionChain);
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
    setState(376);
    match(CypherParser::LPAREN);
    setState(378);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 73) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 73)) & 264245361) != 0)) {
      setState(377);
      expressionChain();
    }
    setState(380);
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
  enterRule(_localctx, 56, CypherParser::RuleYieldItems);
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
    setState(382);
    yieldItem();
    setState(387);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(383);
      match(CypherParser::COMMA);
      setState(384);
      yieldItem();
      setState(389);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(391);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(390);
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
  enterRule(_localctx, 58, CypherParser::RuleYieldItem);

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
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx)) {
    case 1: {
      setState(393);
      symbol();
      setState(394);
      match(CypherParser::AS);
      break;
    }

    default:
      break;
    }
    setState(398);
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
  enterRule(_localctx, 60, CypherParser::RuleMergeSt);
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
    setState(400);
    match(CypherParser::MERGE);
    setState(401);
    patternPart();
    setState(405);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::ON) {
      setState(402);
      mergeAction();
      setState(407);
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
  enterRule(_localctx, 62, CypherParser::RuleMergeAction);
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
    setState(408);
    match(CypherParser::ON);
    setState(409);
    _la = _input->LA(1);
    if (!(_la == CypherParser::CREATE

    || _la == CypherParser::MATCH)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(410);
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
  enterRule(_localctx, 64, CypherParser::RuleSetSt);
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
    setState(412);
    match(CypherParser::SET);
    setState(413);
    setItem();
    setState(418);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(414);
      match(CypherParser::COMMA);
      setState(415);
      setItem();
      setState(420);
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
  enterRule(_localctx, 66, CypherParser::RuleSetItem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(432);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(421);
      propertyExpression();
      setState(422);
      match(CypherParser::ASSIGN);
      setState(423);
      expression();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(425);
      symbol();
      setState(426);
      _la = _input->LA(1);
      if (!(_la == CypherParser::ASSIGN

      || _la == CypherParser::ADD_ASSIGN)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(427);
      expression();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(429);
      symbol();
      setState(430);
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
  enterRule(_localctx, 68, CypherParser::RuleNodeLabels);
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
    setState(436); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(434);
      match(CypherParser::COLON);
      setState(435);
      name();
      setState(438); 
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
  enterRule(_localctx, 70, CypherParser::RuleCreateSt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(440);
    match(CypherParser::CREATE);
    setState(441);
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
  enterRule(_localctx, 72, CypherParser::RulePatternWhere);
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
    setState(443);
    pattern();
    setState(445);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(444);
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
  enterRule(_localctx, 74, CypherParser::RuleWhere);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(447);
    match(CypherParser::WHERE);
    setState(448);
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
  enterRule(_localctx, 76, CypherParser::RulePattern);
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
    patternPart();
    setState(455);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(451);
      match(CypherParser::COMMA);
      setState(452);
      patternPart();
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
  enterRule(_localctx, 78, CypherParser::RuleExpression);
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
    xorExpression();
    setState(463);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::OR) {
      setState(459);
      match(CypherParser::OR);
      setState(460);
      xorExpression();
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
  enterRule(_localctx, 80, CypherParser::RuleXorExpression);
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
    setState(466);
    andExpression();
    setState(471);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::XOR) {
      setState(467);
      match(CypherParser::XOR);
      setState(468);
      andExpression();
      setState(473);
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
  enterRule(_localctx, 82, CypherParser::RuleAndExpression);
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
    setState(474);
    notExpression();
    setState(479);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::AND) {
      setState(475);
      match(CypherParser::AND);
      setState(476);
      notExpression();
      setState(481);
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

std::vector<tree::TerminalNode *> CypherParser::NotExpressionContext::NOT() {
  return getTokens(CypherParser::NOT);
}

tree::TerminalNode* CypherParser::NotExpressionContext::NOT(size_t i) {
  return getToken(CypherParser::NOT, i);
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
  enterRule(_localctx, 84, CypherParser::RuleNotExpression);
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
    setState(485);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::NOT) {
      setState(482);
      match(CypherParser::NOT);
      setState(487);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(488);
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
  enterRule(_localctx, 86, CypherParser::RuleComparisonExpression);
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
    addSubExpression();
    setState(496);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 250) != 0)) {
      setState(491);
      comparisonSigns();
      setState(492);
      addSubExpression();
      setState(498);
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
  enterRule(_localctx, 88, CypherParser::RuleComparisonSigns);
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
    setState(499);
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
  enterRule(_localctx, 90, CypherParser::RuleAddSubExpression);
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
    setState(501);
    multDivExpression();
    setState(506);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(502);
      _la = _input->LA(1);
      if (!(_la == CypherParser::SUB

      || _la == CypherParser::PLUS)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(503);
      multDivExpression();
      setState(508);
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
  enterRule(_localctx, 92, CypherParser::RuleMultDivExpression);
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
    setState(509);
    powerExpression();
    setState(514);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 11534336) != 0)) {
      setState(510);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 11534336) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(511);
      powerExpression();
      setState(516);
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
  enterRule(_localctx, 94, CypherParser::RulePowerExpression);
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
    setState(517);
    unaryAddSubExpression();
    setState(522);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::CARET) {
      setState(518);
      match(CypherParser::CARET);
      setState(519);
      unaryAddSubExpression();
      setState(524);
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
  enterRule(_localctx, 96, CypherParser::RuleUnaryAddSubExpression);
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
    setState(526);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(525);
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
    setState(528);
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
  enterRule(_localctx, 98, CypherParser::RuleAtomicExpression);
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
    setState(530);
    propertyOrLabelExpression();
    setState(536);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 157) != 0)) {
      setState(534);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::CONTAINS:
        case CypherParser::ENDS:
        case CypherParser::STARTS: {
          setState(531);
          stringExpression();
          break;
        }

        case CypherParser::IN: {
          setState(532);
          listExpression();
          break;
        }

        case CypherParser::IS: {
          setState(533);
          nullExpression();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(538);
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
  enterRule(_localctx, 100, CypherParser::RuleListExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(539);
    match(CypherParser::IN);
    setState(540);
    propertyOrLabelExpression();
   
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
  enterRule(_localctx, 102, CypherParser::RuleStringExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(542);
    stringExpPrefix();
    setState(543);
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
  enterRule(_localctx, 104, CypherParser::RuleStringExpPrefix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(550);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::STARTS: {
        enterOuterAlt(_localctx, 1);
        setState(545);
        match(CypherParser::STARTS);
        setState(546);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::ENDS: {
        enterOuterAlt(_localctx, 2);
        setState(547);
        match(CypherParser::ENDS);
        setState(548);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::CONTAINS: {
        enterOuterAlt(_localctx, 3);
        setState(549);
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
  enterRule(_localctx, 106, CypherParser::RuleNullExpression);
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
    match(CypherParser::IS);
    setState(554);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::NOT) {
      setState(553);
      match(CypherParser::NOT);
    }
    setState(556);
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
  enterRule(_localctx, 108, CypherParser::RulePropertyOrLabelExpression);
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
    setState(558);
    propertyExpression();
    setState(560);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(559);
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

std::vector<CypherParser::PostfixContext *> CypherParser::PropertyExpressionContext::postfix() {
  return getRuleContexts<CypherParser::PostfixContext>();
}

CypherParser::PostfixContext* CypherParser::PropertyExpressionContext::postfix(size_t i) {
  return getRuleContext<CypherParser::PostfixContext>(i);
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
  enterRule(_localctx, 110, CypherParser::RulePropertyExpression);
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
    setState(562);
    atom();
    setState(566);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT

    || _la == CypherParser::LBRACK) {
      setState(563);
      postfix();
      setState(568);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(571);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLONCOLON) {
      setState(569);
      match(CypherParser::COLONCOLON);
      setState(570);
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

//----------------- PostfixContext ------------------------------------------------------------------

CypherParser::PostfixContext::PostfixContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::PostfixContext::DOT() {
  return getToken(CypherParser::DOT, 0);
}

CypherParser::NameContext* CypherParser::PostfixContext::name() {
  return getRuleContext<CypherParser::NameContext>(0);
}

tree::TerminalNode* CypherParser::PostfixContext::LBRACK() {
  return getToken(CypherParser::LBRACK, 0);
}

tree::TerminalNode* CypherParser::PostfixContext::RBRACK() {
  return getToken(CypherParser::RBRACK, 0);
}

tree::TerminalNode* CypherParser::PostfixContext::RANGE() {
  return getToken(CypherParser::RANGE, 0);
}

std::vector<CypherParser::ExpressionContext *> CypherParser::PostfixContext::expression() {
  return getRuleContexts<CypherParser::ExpressionContext>();
}

CypherParser::ExpressionContext* CypherParser::PostfixContext::expression(size_t i) {
  return getRuleContext<CypherParser::ExpressionContext>(i);
}


size_t CypherParser::PostfixContext::getRuleIndex() const {
  return CypherParser::RulePostfix;
}


std::any CypherParser::PostfixContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitPostfix(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::PostfixContext* CypherParser::postfix() {
  PostfixContext *_localctx = _tracker.createInstance<PostfixContext>(_ctx, getState());
  enterRule(_localctx, 112, CypherParser::RulePostfix);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(587);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::DOT: {
        enterOuterAlt(_localctx, 1);
        setState(573);
        match(CypherParser::DOT);
        setState(574);
        name();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 2);
        setState(575);
        match(CypherParser::LBRACK);
        setState(584);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 58, _ctx)) {
        case 1: {
          setState(577);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 73) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 73)) & 264245361) != 0)) {
            setState(576);
            expression();
          }
          setState(579);
          match(CypherParser::RANGE);
          setState(581);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 73) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 73)) & 264245361) != 0)) {
            setState(580);
            expression();
          }
          break;
        }

        case 2: {
          setState(583);
          expression();
          break;
        }

        default:
          break;
        }
        setState(586);
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
  enterRule(_localctx, 114, CypherParser::RuleLabelCast);
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
    setState(589);
    name();
    setState(594);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(590);
      match(CypherParser::DOT);
      setState(591);
      name();
      setState(596);
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

CypherParser::ShortestPathPatternContext* CypherParser::PatternPartContext::shortestPathPattern() {
  return getRuleContext<CypherParser::ShortestPathPatternContext>(0);
}

CypherParser::AllShortestPathsPatternContext* CypherParser::PatternPartContext::allShortestPathsPattern() {
  return getRuleContext<CypherParser::AllShortestPathsPatternContext>(0);
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
  enterRule(_localctx, 116, CypherParser::RulePatternPart);
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
    setState(600);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 95) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 95)) & 7) != 0)) {
      setState(597);
      symbol();
      setState(598);
      match(CypherParser::ASSIGN);
    }
    setState(605);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::SHORTESTPATH: {
        setState(602);
        shortestPathPattern();
        break;
      }

      case CypherParser::ALLSHORTESTPATHS: {
        setState(603);
        allShortestPathsPattern();
        break;
      }

      case CypherParser::LPAREN: {
        setState(604);
        patternElem();
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

//----------------- ShortestPathPatternContext ------------------------------------------------------------------

CypherParser::ShortestPathPatternContext::ShortestPathPatternContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ShortestPathPatternContext::SHORTESTPATH() {
  return getToken(CypherParser::SHORTESTPATH, 0);
}

tree::TerminalNode* CypherParser::ShortestPathPatternContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

CypherParser::PatternElemContext* CypherParser::ShortestPathPatternContext::patternElem() {
  return getRuleContext<CypherParser::PatternElemContext>(0);
}

tree::TerminalNode* CypherParser::ShortestPathPatternContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}


size_t CypherParser::ShortestPathPatternContext::getRuleIndex() const {
  return CypherParser::RuleShortestPathPattern;
}


std::any CypherParser::ShortestPathPatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitShortestPathPattern(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ShortestPathPatternContext* CypherParser::shortestPathPattern() {
  ShortestPathPatternContext *_localctx = _tracker.createInstance<ShortestPathPatternContext>(_ctx, getState());
  enterRule(_localctx, 118, CypherParser::RuleShortestPathPattern);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(607);
    match(CypherParser::SHORTESTPATH);
    setState(608);
    match(CypherParser::LPAREN);
    setState(609);
    patternElem();
    setState(610);
    match(CypherParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AllShortestPathsPatternContext ------------------------------------------------------------------

CypherParser::AllShortestPathsPatternContext::AllShortestPathsPatternContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::AllShortestPathsPatternContext::ALLSHORTESTPATHS() {
  return getToken(CypherParser::ALLSHORTESTPATHS, 0);
}

tree::TerminalNode* CypherParser::AllShortestPathsPatternContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

CypherParser::PatternElemContext* CypherParser::AllShortestPathsPatternContext::patternElem() {
  return getRuleContext<CypherParser::PatternElemContext>(0);
}

tree::TerminalNode* CypherParser::AllShortestPathsPatternContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}


size_t CypherParser::AllShortestPathsPatternContext::getRuleIndex() const {
  return CypherParser::RuleAllShortestPathsPattern;
}


std::any CypherParser::AllShortestPathsPatternContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitAllShortestPathsPattern(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::AllShortestPathsPatternContext* CypherParser::allShortestPathsPattern() {
  AllShortestPathsPatternContext *_localctx = _tracker.createInstance<AllShortestPathsPatternContext>(_ctx, getState());
  enterRule(_localctx, 120, CypherParser::RuleAllShortestPathsPattern);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(612);
    match(CypherParser::ALLSHORTESTPATHS);
    setState(613);
    match(CypherParser::LPAREN);
    setState(614);
    patternElem();
    setState(615);
    match(CypherParser::RPAREN);
   
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
  enterRule(_localctx, 122, CypherParser::RulePatternElem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(628);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(617);
      nodePattern();
      setState(621);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::LT

      || _la == CypherParser::SUB) {
        setState(618);
        patternElemChain();
        setState(623);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(624);
      match(CypherParser::LPAREN);
      setState(625);
      patternElem();
      setState(626);
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
  enterRule(_localctx, 124, CypherParser::RulePatternElemChain);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(630);
    relationshipPattern();
    setState(631);
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
  enterRule(_localctx, 126, CypherParser::RuleProperties);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(635);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 1);
        setState(633);
        mapLit();
        break;
      }

      case CypherParser::DOLLAR: {
        enterOuterAlt(_localctx, 2);
        setState(634);
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
  enterRule(_localctx, 128, CypherParser::RuleNodePattern);
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
    setState(637);
    match(CypherParser::LPAREN);
    setState(639);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 95) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 95)) & 7) != 0)) {
      setState(638);
      symbol();
    }
    setState(642);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(641);
      nodeLabels();
    }
    setState(645);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(644);
      properties();
    }
    setState(647);
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

CypherParser::ReduceExpressionContext* CypherParser::AtomContext::reduceExpression() {
  return getRuleContext<CypherParser::ReduceExpressionContext>(0);
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
  enterRule(_localctx, 130, CypherParser::RuleAtom);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(662);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(649);
      literal();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(650);
      parameter();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(651);
      caseExpression();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(652);
      countAll();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(653);
      listComprehension();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(654);
      reduceExpression();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(655);
      patternComprehension();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(656);
      filterWith();
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(657);
      relationshipsChainPattern();
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(658);
      parenthesizedExpression();
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(659);
      functionInvocation();
      break;
    }

    case 12: {
      enterOuterAlt(_localctx, 12);
      setState(660);
      symbol();
      break;
    }

    case 13: {
      enterOuterAlt(_localctx, 13);
      setState(661);
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
  enterRule(_localctx, 132, CypherParser::RuleLhs);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(664);
    symbol();
    setState(665);
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
  enterRule(_localctx, 134, CypherParser::RuleRelationshipPattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(684);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LT: {
        enterOuterAlt(_localctx, 1);
        setState(667);
        match(CypherParser::LT);
        setState(668);
        match(CypherParser::SUB);
        setState(670);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(669);
          relationDetail();
        }
        setState(672);
        match(CypherParser::SUB);
        setState(674);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(673);
          match(CypherParser::GT);
        }
        break;
      }

      case CypherParser::SUB: {
        enterOuterAlt(_localctx, 2);
        setState(676);
        match(CypherParser::SUB);
        setState(678);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(677);
          relationDetail();
        }
        setState(680);
        match(CypherParser::SUB);
        setState(682);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(681);
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
  enterRule(_localctx, 136, CypherParser::RuleRelationDetail);
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
    setState(686);
    match(CypherParser::LBRACK);
    setState(688);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 95) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 95)) & 7) != 0)) {
      setState(687);
      symbol();
    }
    setState(691);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(690);
      relationshipTypes();
    }
    setState(694);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::MULT) {
      setState(693);
      rangeLit();
    }
    setState(697);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(696);
      properties();
    }
    setState(699);
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
  enterRule(_localctx, 138, CypherParser::RuleRelationshipTypes);
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
    setState(701);
    match(CypherParser::COLON);
    setState(702);
    name();
    setState(710);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::STICK) {
      setState(703);
      match(CypherParser::STICK);
      setState(705);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::COLON) {
        setState(704);
        match(CypherParser::COLON);
      }
      setState(707);
      name();
      setState(712);
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
  enterRule(_localctx, 140, CypherParser::RuleUnionSt);
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
    setState(713);
    match(CypherParser::UNION);
    setState(715);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ALL) {
      setState(714);
      match(CypherParser::ALL);
    }
    setState(717);
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
  enterRule(_localctx, 142, CypherParser::RuleSubqueryExist);

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
    match(CypherParser::EXISTS);
    setState(720);
    match(CypherParser::LBRACE);
    setState(723);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx)) {
    case 1: {
      setState(721);
      regularQuery();
      break;
    }

    case 2: {
      setState(722);
      patternWhere();
      break;
    }

    default:
      break;
    }
    setState(725);
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
  enterRule(_localctx, 144, CypherParser::RuleInvocationName);
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
    setState(727);
    symbol();
    setState(732);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(728);
      match(CypherParser::DOT);
      setState(729);
      symbol();
      setState(734);
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
  enterRule(_localctx, 146, CypherParser::RuleFunctionInvocation);
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
    setState(735);
    invocationName();
    setState(736);
    match(CypherParser::LPAREN);
    setState(738);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(737);
      match(CypherParser::DISTINCT);
    }
    setState(741);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 73) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 73)) & 264245361) != 0)) {
      setState(740);
      expressionChain();
    }
    setState(743);
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
  enterRule(_localctx, 148, CypherParser::RuleParenthesizedExpression);

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
    match(CypherParser::LPAREN);
    setState(746);
    expression();
    setState(747);
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
  enterRule(_localctx, 150, CypherParser::RuleFilterWith);
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
    setState(749);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1219770712064) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(750);
    match(CypherParser::LPAREN);
    setState(751);
    filterExpression();
    setState(752);
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
  enterRule(_localctx, 152, CypherParser::RulePatternComprehension);
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
    setState(754);
    match(CypherParser::LBRACK);
    setState(756);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 95) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 95)) & 7) != 0)) {
      setState(755);
      lhs();
    }
    setState(758);
    relationshipsChainPattern();
    setState(760);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(759);
      where();
    }
    setState(762);
    match(CypherParser::STICK);
    setState(763);
    expression();
    setState(764);
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
  enterRule(_localctx, 154, CypherParser::RuleRelationshipsChainPattern);

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
    setState(766);
    nodePattern();
    setState(768); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(767);
              patternElemChain();
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(770); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 88, _ctx);
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
  enterRule(_localctx, 156, CypherParser::RuleListComprehension);
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
    setState(772);
    match(CypherParser::LBRACK);
    setState(773);
    filterExpression();
    setState(776);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::STICK) {
      setState(774);
      match(CypherParser::STICK);
      setState(775);
      expression();
    }
    setState(778);
    match(CypherParser::RBRACK);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ReduceExpressionContext ------------------------------------------------------------------

CypherParser::ReduceExpressionContext::ReduceExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::REDUCE() {
  return getToken(CypherParser::REDUCE, 0);
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::LPAREN() {
  return getToken(CypherParser::LPAREN, 0);
}

std::vector<CypherParser::SymbolContext *> CypherParser::ReduceExpressionContext::symbol() {
  return getRuleContexts<CypherParser::SymbolContext>();
}

CypherParser::SymbolContext* CypherParser::ReduceExpressionContext::symbol(size_t i) {
  return getRuleContext<CypherParser::SymbolContext>(i);
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::ASSIGN() {
  return getToken(CypherParser::ASSIGN, 0);
}

std::vector<CypherParser::ExpressionContext *> CypherParser::ReduceExpressionContext::expression() {
  return getRuleContexts<CypherParser::ExpressionContext>();
}

CypherParser::ExpressionContext* CypherParser::ReduceExpressionContext::expression(size_t i) {
  return getRuleContext<CypherParser::ExpressionContext>(i);
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::COMMA() {
  return getToken(CypherParser::COMMA, 0);
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::IN() {
  return getToken(CypherParser::IN, 0);
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::STICK() {
  return getToken(CypherParser::STICK, 0);
}

tree::TerminalNode* CypherParser::ReduceExpressionContext::RPAREN() {
  return getToken(CypherParser::RPAREN, 0);
}


size_t CypherParser::ReduceExpressionContext::getRuleIndex() const {
  return CypherParser::RuleReduceExpression;
}


std::any CypherParser::ReduceExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CypherParserVisitor*>(visitor))
    return parserVisitor->visitReduceExpression(this);
  else
    return visitor->visitChildren(this);
}

CypherParser::ReduceExpressionContext* CypherParser::reduceExpression() {
  ReduceExpressionContext *_localctx = _tracker.createInstance<ReduceExpressionContext>(_ctx, getState());
  enterRule(_localctx, 158, CypherParser::RuleReduceExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(780);
    match(CypherParser::REDUCE);
    setState(781);
    match(CypherParser::LPAREN);
    setState(782);
    symbol();
    setState(783);
    match(CypherParser::ASSIGN);
    setState(784);
    expression();
    setState(785);
    match(CypherParser::COMMA);
    setState(786);
    symbol();
    setState(787);
    match(CypherParser::IN);
    setState(788);
    expression();
    setState(789);
    match(CypherParser::STICK);
    setState(790);
    expression();
    setState(791);
    match(CypherParser::RPAREN);
   
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
  enterRule(_localctx, 160, CypherParser::RuleFilterExpression);
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
    setState(793);
    symbol();
    setState(794);
    match(CypherParser::IN);
    setState(795);
    expression();
    setState(797);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(796);
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
  enterRule(_localctx, 162, CypherParser::RuleCountAll);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(799);
    match(CypherParser::COUNT);
    setState(800);
    match(CypherParser::LPAREN);
    setState(801);
    match(CypherParser::MULT);
    setState(802);
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
  enterRule(_localctx, 164, CypherParser::RuleExpressionChain);
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
    setState(804);
    expression();
    setState(809);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(805);
      match(CypherParser::COMMA);
      setState(806);
      expression();
      setState(811);
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
  enterRule(_localctx, 166, CypherParser::RuleCaseExpression);
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
    setState(812);
    match(CypherParser::CASE);
    setState(814);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 73) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 73)) & 264245361) != 0)) {
      setState(813);
      expression();
    }
    setState(821); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(816);
      match(CypherParser::WHEN);
      setState(817);
      expression();
      setState(818);
      match(CypherParser::THEN);
      setState(819);
      expression();
      setState(823); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == CypherParser::WHEN);
    setState(827);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ELSE) {
      setState(825);
      match(CypherParser::ELSE);
      setState(826);
      expression();
    }
    setState(829);
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
  enterRule(_localctx, 168, CypherParser::RuleParameter);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(831);
    match(CypherParser::DOLLAR);
    setState(834);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FILTER:
      case CypherParser::EXTRACT:
      case CypherParser::COUNT:
      case CypherParser::ANY:
      case CypherParser::NONE:
      case CypherParser::SINGLE:
      case CypherParser::FOREACH:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL: {
        setState(832);
        symbol();
        break;
      }

      case CypherParser::DIGIT: {
        setState(833);
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
  enterRule(_localctx, 170, CypherParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(843);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FALSE:
      case CypherParser::TRUE: {
        enterOuterAlt(_localctx, 1);
        setState(836);
        boolLit();
        break;
      }

      case CypherParser::DIGIT: {
        enterOuterAlt(_localctx, 2);
        setState(837);
        numLit();
        break;
      }

      case CypherParser::NULL_W: {
        enterOuterAlt(_localctx, 3);
        setState(838);
        match(CypherParser::NULL_W);
        break;
      }

      case CypherParser::STRING_LITERAL: {
        enterOuterAlt(_localctx, 4);
        setState(839);
        stringLit();
        break;
      }

      case CypherParser::CHAR_LITERAL: {
        enterOuterAlt(_localctx, 5);
        setState(840);
        charLit();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 6);
        setState(841);
        listLit();
        break;
      }

      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 7);
        setState(842);
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
  enterRule(_localctx, 172, CypherParser::RuleRangeLit);
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
    setState(845);
    match(CypherParser::MULT);
    setState(848);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::DIGIT: {
        setState(846);
        numLit();
        break;
      }

      case CypherParser::ID: {
        setState(847);
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
    setState(855);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RANGE) {
      setState(850);
      match(CypherParser::RANGE);
      setState(853);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::DIGIT: {
          setState(851);
          numLit();
          break;
        }

        case CypherParser::ID: {
          setState(852);
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
  enterRule(_localctx, 174, CypherParser::RuleBoolLit);
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
    setState(857);
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
  enterRule(_localctx, 176, CypherParser::RuleNumLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(859);
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
  enterRule(_localctx, 178, CypherParser::RuleStringLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(861);
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
  enterRule(_localctx, 180, CypherParser::RuleCharLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(863);
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
  enterRule(_localctx, 182, CypherParser::RuleListLit);
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
    setState(865);
    match(CypherParser::LBRACK);
    setState(867);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 73) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 73)) & 264245361) != 0)) {
      setState(866);
      expressionChain();
    }
    setState(869);
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
  enterRule(_localctx, 184, CypherParser::RuleMapLit);
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
    setState(871);
    match(CypherParser::LBRACE);
    setState(880);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -3378663940685824) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 17179869183) != 0)) {
      setState(872);
      mapPair();
      setState(877);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::COMMA) {
        setState(873);
        match(CypherParser::COMMA);
        setState(874);
        mapPair();
        setState(879);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(882);
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
  enterRule(_localctx, 186, CypherParser::RuleMapPair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(884);
    name();
    setState(885);
    match(CypherParser::COLON);
    setState(886);
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
  enterRule(_localctx, 188, CypherParser::RuleName);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(890);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FILTER:
      case CypherParser::EXTRACT:
      case CypherParser::COUNT:
      case CypherParser::ANY:
      case CypherParser::NONE:
      case CypherParser::SINGLE:
      case CypherParser::FOREACH:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL: {
        enterOuterAlt(_localctx, 1);
        setState(888);
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
        setState(889);
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

tree::TerminalNode* CypherParser::SymbolContext::FOREACH() {
  return getToken(CypherParser::FOREACH, 0);
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
  enterRule(_localctx, 190, CypherParser::RuleSymbol);
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
    setState(892);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 95) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 95)) & 7) != 0))) {
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
  enterRule(_localctx, 192, CypherParser::RuleReservedWord);
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
    setState(894);
    _la = _input->LA(1);
    if (!(((((_la - 40) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 40)) & 36028797018960895) != 0))) {
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
