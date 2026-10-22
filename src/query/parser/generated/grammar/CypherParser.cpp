
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
      "labelCast", "patternPart", "shortestPathPattern", "allShortestPathsPattern", 
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
      "'DESCENDING'", "'DETACH'", "'EXISTS'", "'EXPLAIN'", "'LIMIT'", "'MATCH'", 
      "'MERGE'", "'ON'", "'OPTIONAL'", "'ORDER'", "'REMOVE'", "'RETURN'", 
      "'SET'", "'SKIP'", "'WHERE'", "'WITH'", "'UNION'", "'UNWIND'", "'AND'", 
      "'AS'", "'CONTAINS'", "'DISTINCT'", "'ENDS'", "'IN'", "'IS'", "'NOT'", 
      "'OR'", "'STARTS'", "'XOR'", "'FALSE'", "'TRUE'", "'NULL'", "'CONSTRAINT'", 
      "'DO'", "'FOR'", "'REQUIRE'", "'UNIQUE'", "'CASE'", "'WHEN'", "'THEN'", 
      "'ELSE'", "'END'", "'MANDATORY'", "'SCALAR'", "'OF'", "'ADD'", "'DROP'", 
      "'FOREACH'"
    },
    std::vector<std::string>{
      "", "ASSIGN", "ADD_ASSIGN", "LE", "GE", "GT", "LT", "NOT_EQUAL", "RANGE", 
      "SEMI", "DOT", "COMMA", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACK", 
      "RBRACK", "SUB", "PLUS", "DIV", "MOD", "CARET", "MULT", "ESC", "COLON", 
      "COLONCOLON", "STICK", "DOLLAR", "CALL", "YIELD", "FILTER", "EXTRACT", 
      "COUNT", "ANY", "NONE", "SINGLE", "REDUCE", "SHORTESTPATH", "ALLSHORTESTPATHS", 
      "ALL", "ASC", "ASCENDING", "BY", "CREATE", "DELETE", "DESC", "DESCENDING", 
      "DETACH", "EXISTS", "EXPLAIN", "LIMIT", "MATCH", "MERGE", "ON", "OPTIONAL", 
      "ORDER", "REMOVE", "RETURN", "SET", "SKIP_W", "WHERE", "WITH", "UNION", 
      "UNWIND", "AND", "AS", "CONTAINS", "DISTINCT", "ENDS", "IN", "IS", 
      "NOT", "OR", "STARTS", "XOR", "FALSE", "TRUE", "NULL_W", "CONSTRAINT", 
      "DO", "FOR", "REQUIRE", "UNIQUE", "CASE", "WHEN", "THEN", "ELSE", 
      "END", "MANDATORY", "SCALAR", "OF", "ADD", "DROP", "FOREACH", "ID", 
      "ESC_LITERAL", "CHAR_LITERAL", "STRING_LITERAL", "DIGIT", "FLOAT", 
      "WS", "COMMENT", "LINE_COMMENT", "ERRCHAR", "Letter"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,105,893,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
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
  	91,2,92,7,92,2,93,7,93,2,94,7,94,2,95,7,95,1,0,1,0,3,0,195,8,0,1,0,1,
  	0,1,1,3,1,200,8,1,1,1,1,1,3,1,204,8,1,1,2,1,2,5,2,208,8,2,10,2,12,2,211,
  	9,2,1,3,5,3,214,8,3,10,3,12,3,217,9,3,1,3,3,3,220,8,3,1,4,1,4,1,4,3,4,
  	225,8,4,1,5,1,5,1,5,3,5,230,8,5,1,6,1,6,1,6,1,6,1,6,1,6,3,6,238,8,6,1,
  	7,1,7,1,8,1,8,1,8,3,8,245,8,8,1,8,1,8,1,8,3,8,250,8,8,3,8,252,8,8,1,9,
  	1,9,1,9,1,10,1,10,1,10,3,10,260,8,10,1,11,1,11,1,11,1,12,1,12,1,12,1,
  	13,3,13,269,8,13,1,13,1,13,3,13,273,8,13,1,13,3,13,276,8,13,1,13,3,13,
  	279,8,13,1,14,1,14,3,14,283,8,14,1,14,1,14,5,14,287,8,14,10,14,12,14,
  	290,9,14,1,15,1,15,1,15,3,15,295,8,15,1,16,1,16,3,16,299,8,16,1,17,1,
  	17,1,17,1,17,1,17,5,17,306,8,17,10,17,12,17,309,9,17,1,18,3,18,312,8,
  	18,1,18,1,18,1,18,1,19,1,19,1,19,1,19,1,19,1,20,1,20,1,20,3,20,325,8,
  	20,1,21,1,21,1,21,1,21,1,21,3,21,332,8,21,1,22,3,22,335,8,22,1,22,1,22,
  	1,22,1,23,1,23,1,23,1,23,5,23,344,8,23,10,23,12,23,347,9,23,1,24,1,24,
  	1,24,1,24,1,24,1,24,1,24,4,24,356,8,24,11,24,12,24,357,1,24,1,24,1,25,
  	1,25,1,25,1,25,3,25,366,8,25,1,26,1,26,1,26,1,26,1,26,3,26,373,8,26,1,
  	27,1,27,3,27,377,8,27,1,27,1,27,1,28,1,28,1,28,5,28,384,8,28,10,28,12,
  	28,387,9,28,1,28,3,28,390,8,28,1,29,1,29,1,29,3,29,395,8,29,1,29,1,29,
  	1,30,1,30,1,30,5,30,402,8,30,10,30,12,30,405,9,30,1,31,1,31,1,31,1,31,
  	1,32,1,32,1,32,1,32,5,32,415,8,32,10,32,12,32,418,9,32,1,33,1,33,1,33,
  	1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,3,33,431,8,33,1,34,1,34,4,34,
  	435,8,34,11,34,12,34,436,1,35,1,35,1,35,1,36,1,36,3,36,444,8,36,1,37,
  	1,37,1,37,1,38,1,38,1,38,5,38,452,8,38,10,38,12,38,455,9,38,1,39,1,39,
  	1,39,5,39,460,8,39,10,39,12,39,463,9,39,1,40,1,40,1,40,5,40,468,8,40,
  	10,40,12,40,471,9,40,1,41,1,41,1,41,5,41,476,8,41,10,41,12,41,479,9,41,
  	1,42,5,42,482,8,42,10,42,12,42,485,9,42,1,42,1,42,1,43,1,43,1,43,1,43,
  	5,43,493,8,43,10,43,12,43,496,9,43,1,44,1,44,1,45,1,45,1,45,5,45,503,
  	8,45,10,45,12,45,506,9,45,1,46,1,46,1,46,5,46,511,8,46,10,46,12,46,514,
  	9,46,1,47,1,47,1,47,5,47,519,8,47,10,47,12,47,522,9,47,1,48,3,48,525,
  	8,48,1,48,1,48,1,49,1,49,1,49,1,49,5,49,533,8,49,10,49,12,49,536,9,49,
  	1,50,1,50,1,50,1,50,3,50,542,8,50,1,50,1,50,3,50,546,8,50,1,50,3,50,549,
  	8,50,1,50,3,50,552,8,50,1,51,1,51,1,51,1,52,1,52,1,52,1,52,1,52,3,52,
  	562,8,52,1,53,1,53,3,53,566,8,53,1,53,1,53,1,54,1,54,3,54,572,8,54,1,
  	55,1,55,1,55,5,55,577,8,55,10,55,12,55,580,9,55,1,55,1,55,3,55,584,8,
  	55,1,56,1,56,1,56,5,56,589,8,56,10,56,12,56,592,9,56,1,57,1,57,1,57,3,
  	57,597,8,57,1,57,1,57,1,57,3,57,602,8,57,1,58,1,58,1,58,1,58,1,58,1,59,
  	1,59,1,59,1,59,1,59,1,60,1,60,5,60,616,8,60,10,60,12,60,619,9,60,1,60,
  	1,60,1,60,1,60,3,60,625,8,60,1,61,1,61,1,61,1,62,1,62,3,62,632,8,62,1,
  	63,1,63,3,63,636,8,63,1,63,3,63,639,8,63,1,63,3,63,642,8,63,1,63,1,63,
  	1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,1,64,3,64,
  	659,8,64,1,65,1,65,1,65,1,66,1,66,1,66,3,66,667,8,66,1,66,1,66,3,66,671,
  	8,66,1,66,1,66,3,66,675,8,66,1,66,1,66,3,66,679,8,66,3,66,681,8,66,1,
  	67,1,67,3,67,685,8,67,1,67,3,67,688,8,67,1,67,3,67,691,8,67,1,67,3,67,
  	694,8,67,1,67,1,67,1,68,1,68,1,68,1,68,3,68,702,8,68,1,68,5,68,705,8,
  	68,10,68,12,68,708,9,68,1,69,1,69,3,69,712,8,69,1,69,1,69,1,70,1,70,1,
  	70,1,70,3,70,720,8,70,1,70,1,70,1,71,1,71,1,71,5,71,727,8,71,10,71,12,
  	71,730,9,71,1,72,1,72,1,72,3,72,735,8,72,1,72,3,72,738,8,72,1,72,1,72,
  	1,73,1,73,1,73,1,73,1,74,1,74,1,74,1,74,1,74,1,75,1,75,3,75,753,8,75,
  	1,75,1,75,3,75,757,8,75,1,75,1,75,1,75,1,75,1,76,1,76,4,76,765,8,76,11,
  	76,12,76,766,1,77,1,77,1,77,1,77,3,77,773,8,77,1,77,1,77,1,78,1,78,1,
  	78,1,78,1,78,1,78,1,78,1,78,1,78,1,78,1,78,1,78,1,78,1,79,1,79,1,79,1,
  	79,3,79,794,8,79,1,80,1,80,1,80,1,80,1,80,1,81,1,81,1,81,5,81,804,8,81,
  	10,81,12,81,807,9,81,1,82,1,82,3,82,811,8,82,1,82,1,82,1,82,1,82,1,82,
  	4,82,818,8,82,11,82,12,82,819,1,82,1,82,3,82,824,8,82,1,82,1,82,1,83,
  	1,83,1,83,3,83,831,8,83,1,84,1,84,1,84,1,84,1,84,1,84,1,84,3,84,840,8,
  	84,1,85,1,85,1,85,3,85,845,8,85,1,85,1,85,1,85,3,85,850,8,85,3,85,852,
  	8,85,1,86,1,86,1,87,1,87,1,88,1,88,1,89,1,89,1,90,1,90,3,90,864,8,90,
  	1,90,1,90,1,91,1,91,1,91,1,91,5,91,872,8,91,10,91,12,91,875,9,91,3,91,
  	877,8,91,1,91,1,91,1,92,1,92,1,92,1,92,1,93,1,93,3,93,887,8,93,1,94,1,
  	94,1,95,1,95,1,95,0,0,96,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,
  	34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,
  	80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,114,116,118,
  	120,122,124,126,128,130,132,134,136,138,140,142,144,146,148,150,152,154,
  	156,158,160,162,164,166,168,170,172,174,176,178,180,182,184,186,188,190,
  	0,10,2,0,41,42,46,47,2,0,44,44,52,52,1,0,1,2,2,0,1,1,3,7,1,0,18,19,2,
  	0,20,21,23,23,2,0,34,36,40,40,1,0,76,77,2,0,31,36,94,96,2,0,40,49,51,
  	93,932,0,192,1,0,0,0,2,203,1,0,0,0,4,205,1,0,0,0,6,215,1,0,0,0,8,224,
  	1,0,0,0,10,229,1,0,0,0,12,237,1,0,0,0,14,239,1,0,0,0,16,241,1,0,0,0,18,
  	253,1,0,0,0,20,256,1,0,0,0,22,261,1,0,0,0,24,264,1,0,0,0,26,268,1,0,0,
  	0,28,282,1,0,0,0,30,291,1,0,0,0,32,296,1,0,0,0,34,300,1,0,0,0,36,311,
  	1,0,0,0,38,316,1,0,0,0,40,324,1,0,0,0,42,331,1,0,0,0,44,334,1,0,0,0,46,
  	339,1,0,0,0,48,348,1,0,0,0,50,365,1,0,0,0,52,367,1,0,0,0,54,374,1,0,0,
  	0,56,380,1,0,0,0,58,394,1,0,0,0,60,398,1,0,0,0,62,406,1,0,0,0,64,410,
  	1,0,0,0,66,430,1,0,0,0,68,434,1,0,0,0,70,438,1,0,0,0,72,441,1,0,0,0,74,
  	445,1,0,0,0,76,448,1,0,0,0,78,456,1,0,0,0,80,464,1,0,0,0,82,472,1,0,0,
  	0,84,483,1,0,0,0,86,488,1,0,0,0,88,497,1,0,0,0,90,499,1,0,0,0,92,507,
  	1,0,0,0,94,515,1,0,0,0,96,524,1,0,0,0,98,528,1,0,0,0,100,551,1,0,0,0,
  	102,553,1,0,0,0,104,561,1,0,0,0,106,563,1,0,0,0,108,569,1,0,0,0,110,573,
  	1,0,0,0,112,585,1,0,0,0,114,596,1,0,0,0,116,603,1,0,0,0,118,608,1,0,0,
  	0,120,624,1,0,0,0,122,626,1,0,0,0,124,631,1,0,0,0,126,633,1,0,0,0,128,
  	658,1,0,0,0,130,660,1,0,0,0,132,680,1,0,0,0,134,682,1,0,0,0,136,697,1,
  	0,0,0,138,709,1,0,0,0,140,715,1,0,0,0,142,723,1,0,0,0,144,731,1,0,0,0,
  	146,741,1,0,0,0,148,745,1,0,0,0,150,750,1,0,0,0,152,762,1,0,0,0,154,768,
  	1,0,0,0,156,776,1,0,0,0,158,789,1,0,0,0,160,795,1,0,0,0,162,800,1,0,0,
  	0,164,808,1,0,0,0,166,827,1,0,0,0,168,839,1,0,0,0,170,841,1,0,0,0,172,
  	853,1,0,0,0,174,855,1,0,0,0,176,857,1,0,0,0,178,859,1,0,0,0,180,861,1,
  	0,0,0,182,867,1,0,0,0,184,880,1,0,0,0,186,886,1,0,0,0,188,888,1,0,0,0,
  	190,890,1,0,0,0,192,194,3,2,1,0,193,195,5,9,0,0,194,193,1,0,0,0,194,195,
  	1,0,0,0,195,196,1,0,0,0,196,197,5,0,0,1,197,1,1,0,0,0,198,200,5,50,0,
  	0,199,198,1,0,0,0,199,200,1,0,0,0,200,201,1,0,0,0,201,204,3,4,2,0,202,
  	204,3,16,8,0,203,199,1,0,0,0,203,202,1,0,0,0,204,3,1,0,0,0,205,209,3,
  	6,3,0,206,208,3,138,69,0,207,206,1,0,0,0,208,211,1,0,0,0,209,207,1,0,
  	0,0,209,210,1,0,0,0,210,5,1,0,0,0,211,209,1,0,0,0,212,214,3,8,4,0,213,
  	212,1,0,0,0,214,217,1,0,0,0,215,213,1,0,0,0,215,216,1,0,0,0,216,219,1,
  	0,0,0,217,215,1,0,0,0,218,220,3,14,7,0,219,218,1,0,0,0,219,220,1,0,0,
  	0,220,7,1,0,0,0,221,225,3,10,5,0,222,225,3,12,6,0,223,225,3,52,26,0,224,
  	221,1,0,0,0,224,222,1,0,0,0,224,223,1,0,0,0,225,9,1,0,0,0,226,230,3,36,
  	18,0,227,230,3,38,19,0,228,230,3,20,10,0,229,226,1,0,0,0,229,227,1,0,
  	0,0,229,228,1,0,0,0,230,11,1,0,0,0,231,238,3,70,35,0,232,238,3,60,30,
  	0,233,238,3,44,22,0,234,238,3,64,32,0,235,238,3,46,23,0,236,238,3,48,
  	24,0,237,231,1,0,0,0,237,232,1,0,0,0,237,233,1,0,0,0,237,234,1,0,0,0,
  	237,235,1,0,0,0,237,236,1,0,0,0,238,13,1,0,0,0,239,240,3,18,9,0,240,15,
  	1,0,0,0,241,242,5,29,0,0,242,244,3,142,71,0,243,245,3,54,27,0,244,243,
  	1,0,0,0,244,245,1,0,0,0,245,251,1,0,0,0,246,249,5,30,0,0,247,250,5,23,
  	0,0,248,250,3,56,28,0,249,247,1,0,0,0,249,248,1,0,0,0,250,252,1,0,0,0,
  	251,246,1,0,0,0,251,252,1,0,0,0,252,17,1,0,0,0,253,254,5,58,0,0,254,255,
  	3,26,13,0,255,19,1,0,0,0,256,257,5,62,0,0,257,259,3,26,13,0,258,260,3,
  	74,37,0,259,258,1,0,0,0,259,260,1,0,0,0,260,21,1,0,0,0,261,262,5,60,0,
  	0,262,263,3,78,39,0,263,23,1,0,0,0,264,265,5,51,0,0,265,266,3,78,39,0,
  	266,25,1,0,0,0,267,269,5,68,0,0,268,267,1,0,0,0,268,269,1,0,0,0,269,270,
  	1,0,0,0,270,272,3,28,14,0,271,273,3,34,17,0,272,271,1,0,0,0,272,273,1,
  	0,0,0,273,275,1,0,0,0,274,276,3,22,11,0,275,274,1,0,0,0,275,276,1,0,0,
  	0,276,278,1,0,0,0,277,279,3,24,12,0,278,277,1,0,0,0,278,279,1,0,0,0,279,
  	27,1,0,0,0,280,283,5,23,0,0,281,283,3,30,15,0,282,280,1,0,0,0,282,281,
  	1,0,0,0,283,288,1,0,0,0,284,285,5,11,0,0,285,287,3,30,15,0,286,284,1,
  	0,0,0,287,290,1,0,0,0,288,286,1,0,0,0,288,289,1,0,0,0,289,29,1,0,0,0,
  	290,288,1,0,0,0,291,294,3,78,39,0,292,293,5,66,0,0,293,295,3,188,94,0,
  	294,292,1,0,0,0,294,295,1,0,0,0,295,31,1,0,0,0,296,298,3,78,39,0,297,
  	299,7,0,0,0,298,297,1,0,0,0,298,299,1,0,0,0,299,33,1,0,0,0,300,301,5,
  	56,0,0,301,302,5,43,0,0,302,307,3,32,16,0,303,304,5,11,0,0,304,306,3,
  	32,16,0,305,303,1,0,0,0,306,309,1,0,0,0,307,305,1,0,0,0,307,308,1,0,0,
  	0,308,35,1,0,0,0,309,307,1,0,0,0,310,312,5,55,0,0,311,310,1,0,0,0,311,
  	312,1,0,0,0,312,313,1,0,0,0,313,314,5,52,0,0,314,315,3,72,36,0,315,37,
  	1,0,0,0,316,317,5,64,0,0,317,318,3,78,39,0,318,319,5,66,0,0,319,320,3,
  	188,94,0,320,39,1,0,0,0,321,325,3,36,18,0,322,325,3,38,19,0,323,325,3,
  	52,26,0,324,321,1,0,0,0,324,322,1,0,0,0,324,323,1,0,0,0,325,41,1,0,0,
  	0,326,332,3,70,35,0,327,332,3,60,30,0,328,332,3,44,22,0,329,332,3,64,
  	32,0,330,332,3,46,23,0,331,326,1,0,0,0,331,327,1,0,0,0,331,328,1,0,0,
  	0,331,329,1,0,0,0,331,330,1,0,0,0,332,43,1,0,0,0,333,335,5,48,0,0,334,
  	333,1,0,0,0,334,335,1,0,0,0,335,336,1,0,0,0,336,337,5,45,0,0,337,338,
  	3,162,81,0,338,45,1,0,0,0,339,340,5,57,0,0,340,345,3,50,25,0,341,342,
  	5,11,0,0,342,344,3,50,25,0,343,341,1,0,0,0,344,347,1,0,0,0,345,343,1,
  	0,0,0,345,346,1,0,0,0,346,47,1,0,0,0,347,345,1,0,0,0,348,349,5,94,0,0,
  	349,350,5,12,0,0,350,351,3,188,94,0,351,352,5,70,0,0,352,353,3,78,39,
  	0,353,355,5,27,0,0,354,356,3,12,6,0,355,354,1,0,0,0,356,357,1,0,0,0,357,
  	355,1,0,0,0,357,358,1,0,0,0,358,359,1,0,0,0,359,360,5,13,0,0,360,49,1,
  	0,0,0,361,362,3,188,94,0,362,363,3,68,34,0,363,366,1,0,0,0,364,366,3,
  	110,55,0,365,361,1,0,0,0,365,364,1,0,0,0,366,51,1,0,0,0,367,368,5,29,
  	0,0,368,369,3,142,71,0,369,372,3,54,27,0,370,371,5,30,0,0,371,373,3,56,
  	28,0,372,370,1,0,0,0,372,373,1,0,0,0,373,53,1,0,0,0,374,376,5,12,0,0,
  	375,377,3,162,81,0,376,375,1,0,0,0,376,377,1,0,0,0,377,378,1,0,0,0,378,
  	379,5,13,0,0,379,55,1,0,0,0,380,385,3,58,29,0,381,382,5,11,0,0,382,384,
  	3,58,29,0,383,381,1,0,0,0,384,387,1,0,0,0,385,383,1,0,0,0,385,386,1,0,
  	0,0,386,389,1,0,0,0,387,385,1,0,0,0,388,390,3,74,37,0,389,388,1,0,0,0,
  	389,390,1,0,0,0,390,57,1,0,0,0,391,392,3,188,94,0,392,393,5,66,0,0,393,
  	395,1,0,0,0,394,391,1,0,0,0,394,395,1,0,0,0,395,396,1,0,0,0,396,397,3,
  	188,94,0,397,59,1,0,0,0,398,399,5,53,0,0,399,403,3,114,57,0,400,402,3,
  	62,31,0,401,400,1,0,0,0,402,405,1,0,0,0,403,401,1,0,0,0,403,404,1,0,0,
  	0,404,61,1,0,0,0,405,403,1,0,0,0,406,407,5,54,0,0,407,408,7,1,0,0,408,
  	409,3,64,32,0,409,63,1,0,0,0,410,411,5,59,0,0,411,416,3,66,33,0,412,413,
  	5,11,0,0,413,415,3,66,33,0,414,412,1,0,0,0,415,418,1,0,0,0,416,414,1,
  	0,0,0,416,417,1,0,0,0,417,65,1,0,0,0,418,416,1,0,0,0,419,420,3,110,55,
  	0,420,421,5,1,0,0,421,422,3,78,39,0,422,431,1,0,0,0,423,424,3,188,94,
  	0,424,425,7,2,0,0,425,426,3,78,39,0,426,431,1,0,0,0,427,428,3,188,94,
  	0,428,429,3,68,34,0,429,431,1,0,0,0,430,419,1,0,0,0,430,423,1,0,0,0,430,
  	427,1,0,0,0,431,67,1,0,0,0,432,433,5,25,0,0,433,435,3,186,93,0,434,432,
  	1,0,0,0,435,436,1,0,0,0,436,434,1,0,0,0,436,437,1,0,0,0,437,69,1,0,0,
  	0,438,439,5,44,0,0,439,440,3,76,38,0,440,71,1,0,0,0,441,443,3,76,38,0,
  	442,444,3,74,37,0,443,442,1,0,0,0,443,444,1,0,0,0,444,73,1,0,0,0,445,
  	446,5,61,0,0,446,447,3,78,39,0,447,75,1,0,0,0,448,453,3,114,57,0,449,
  	450,5,11,0,0,450,452,3,114,57,0,451,449,1,0,0,0,452,455,1,0,0,0,453,451,
  	1,0,0,0,453,454,1,0,0,0,454,77,1,0,0,0,455,453,1,0,0,0,456,461,3,80,40,
  	0,457,458,5,73,0,0,458,460,3,80,40,0,459,457,1,0,0,0,460,463,1,0,0,0,
  	461,459,1,0,0,0,461,462,1,0,0,0,462,79,1,0,0,0,463,461,1,0,0,0,464,469,
  	3,82,41,0,465,466,5,75,0,0,466,468,3,82,41,0,467,465,1,0,0,0,468,471,
  	1,0,0,0,469,467,1,0,0,0,469,470,1,0,0,0,470,81,1,0,0,0,471,469,1,0,0,
  	0,472,477,3,84,42,0,473,474,5,65,0,0,474,476,3,84,42,0,475,473,1,0,0,
  	0,476,479,1,0,0,0,477,475,1,0,0,0,477,478,1,0,0,0,478,83,1,0,0,0,479,
  	477,1,0,0,0,480,482,5,72,0,0,481,480,1,0,0,0,482,485,1,0,0,0,483,481,
  	1,0,0,0,483,484,1,0,0,0,484,486,1,0,0,0,485,483,1,0,0,0,486,487,3,86,
  	43,0,487,85,1,0,0,0,488,494,3,90,45,0,489,490,3,88,44,0,490,491,3,90,
  	45,0,491,493,1,0,0,0,492,489,1,0,0,0,493,496,1,0,0,0,494,492,1,0,0,0,
  	494,495,1,0,0,0,495,87,1,0,0,0,496,494,1,0,0,0,497,498,7,3,0,0,498,89,
  	1,0,0,0,499,504,3,92,46,0,500,501,7,4,0,0,501,503,3,92,46,0,502,500,1,
  	0,0,0,503,506,1,0,0,0,504,502,1,0,0,0,504,505,1,0,0,0,505,91,1,0,0,0,
  	506,504,1,0,0,0,507,512,3,94,47,0,508,509,7,5,0,0,509,511,3,94,47,0,510,
  	508,1,0,0,0,511,514,1,0,0,0,512,510,1,0,0,0,512,513,1,0,0,0,513,93,1,
  	0,0,0,514,512,1,0,0,0,515,520,3,96,48,0,516,517,5,22,0,0,517,519,3,96,
  	48,0,518,516,1,0,0,0,519,522,1,0,0,0,520,518,1,0,0,0,520,521,1,0,0,0,
  	521,95,1,0,0,0,522,520,1,0,0,0,523,525,7,4,0,0,524,523,1,0,0,0,524,525,
  	1,0,0,0,525,526,1,0,0,0,526,527,3,98,49,0,527,97,1,0,0,0,528,534,3,108,
  	54,0,529,533,3,102,51,0,530,533,3,100,50,0,531,533,3,106,53,0,532,529,
  	1,0,0,0,532,530,1,0,0,0,532,531,1,0,0,0,533,536,1,0,0,0,534,532,1,0,0,
  	0,534,535,1,0,0,0,535,99,1,0,0,0,536,534,1,0,0,0,537,538,5,70,0,0,538,
  	552,3,108,54,0,539,548,5,16,0,0,540,542,3,78,39,0,541,540,1,0,0,0,541,
  	542,1,0,0,0,542,543,1,0,0,0,543,545,5,8,0,0,544,546,3,78,39,0,545,544,
  	1,0,0,0,545,546,1,0,0,0,546,549,1,0,0,0,547,549,3,78,39,0,548,541,1,0,
  	0,0,548,547,1,0,0,0,549,550,1,0,0,0,550,552,5,17,0,0,551,537,1,0,0,0,
  	551,539,1,0,0,0,552,101,1,0,0,0,553,554,3,104,52,0,554,555,3,108,54,0,
  	555,103,1,0,0,0,556,557,5,74,0,0,557,562,5,62,0,0,558,559,5,69,0,0,559,
  	562,5,62,0,0,560,562,5,67,0,0,561,556,1,0,0,0,561,558,1,0,0,0,561,560,
  	1,0,0,0,562,105,1,0,0,0,563,565,5,71,0,0,564,566,5,72,0,0,565,564,1,0,
  	0,0,565,566,1,0,0,0,566,567,1,0,0,0,567,568,5,78,0,0,568,107,1,0,0,0,
  	569,571,3,110,55,0,570,572,3,68,34,0,571,570,1,0,0,0,571,572,1,0,0,0,
  	572,109,1,0,0,0,573,578,3,128,64,0,574,575,5,10,0,0,575,577,3,186,93,
  	0,576,574,1,0,0,0,577,580,1,0,0,0,578,576,1,0,0,0,578,579,1,0,0,0,579,
  	583,1,0,0,0,580,578,1,0,0,0,581,582,5,26,0,0,582,584,3,112,56,0,583,581,
  	1,0,0,0,583,584,1,0,0,0,584,111,1,0,0,0,585,590,3,186,93,0,586,587,5,
  	10,0,0,587,589,3,186,93,0,588,586,1,0,0,0,589,592,1,0,0,0,590,588,1,0,
  	0,0,590,591,1,0,0,0,591,113,1,0,0,0,592,590,1,0,0,0,593,594,3,188,94,
  	0,594,595,5,1,0,0,595,597,1,0,0,0,596,593,1,0,0,0,596,597,1,0,0,0,597,
  	601,1,0,0,0,598,602,3,116,58,0,599,602,3,118,59,0,600,602,3,120,60,0,
  	601,598,1,0,0,0,601,599,1,0,0,0,601,600,1,0,0,0,602,115,1,0,0,0,603,604,
  	5,38,0,0,604,605,5,12,0,0,605,606,3,120,60,0,606,607,5,13,0,0,607,117,
  	1,0,0,0,608,609,5,39,0,0,609,610,5,12,0,0,610,611,3,120,60,0,611,612,
  	5,13,0,0,612,119,1,0,0,0,613,617,3,126,63,0,614,616,3,122,61,0,615,614,
  	1,0,0,0,616,619,1,0,0,0,617,615,1,0,0,0,617,618,1,0,0,0,618,625,1,0,0,
  	0,619,617,1,0,0,0,620,621,5,12,0,0,621,622,3,120,60,0,622,623,5,13,0,
  	0,623,625,1,0,0,0,624,613,1,0,0,0,624,620,1,0,0,0,625,121,1,0,0,0,626,
  	627,3,132,66,0,627,628,3,126,63,0,628,123,1,0,0,0,629,632,3,182,91,0,
  	630,632,3,166,83,0,631,629,1,0,0,0,631,630,1,0,0,0,632,125,1,0,0,0,633,
  	635,5,12,0,0,634,636,3,188,94,0,635,634,1,0,0,0,635,636,1,0,0,0,636,638,
  	1,0,0,0,637,639,3,68,34,0,638,637,1,0,0,0,638,639,1,0,0,0,639,641,1,0,
  	0,0,640,642,3,124,62,0,641,640,1,0,0,0,641,642,1,0,0,0,642,643,1,0,0,
  	0,643,644,5,13,0,0,644,127,1,0,0,0,645,659,3,168,84,0,646,659,3,166,83,
  	0,647,659,3,164,82,0,648,659,3,160,80,0,649,659,3,154,77,0,650,659,3,
  	156,78,0,651,659,3,150,75,0,652,659,3,148,74,0,653,659,3,152,76,0,654,
  	659,3,146,73,0,655,659,3,144,72,0,656,659,3,188,94,0,657,659,3,140,70,
  	0,658,645,1,0,0,0,658,646,1,0,0,0,658,647,1,0,0,0,658,648,1,0,0,0,658,
  	649,1,0,0,0,658,650,1,0,0,0,658,651,1,0,0,0,658,652,1,0,0,0,658,653,1,
  	0,0,0,658,654,1,0,0,0,658,655,1,0,0,0,658,656,1,0,0,0,658,657,1,0,0,0,
  	659,129,1,0,0,0,660,661,3,188,94,0,661,662,5,1,0,0,662,131,1,0,0,0,663,
  	664,5,6,0,0,664,666,5,18,0,0,665,667,3,134,67,0,666,665,1,0,0,0,666,667,
  	1,0,0,0,667,668,1,0,0,0,668,670,5,18,0,0,669,671,5,5,0,0,670,669,1,0,
  	0,0,670,671,1,0,0,0,671,681,1,0,0,0,672,674,5,18,0,0,673,675,3,134,67,
  	0,674,673,1,0,0,0,674,675,1,0,0,0,675,676,1,0,0,0,676,678,5,18,0,0,677,
  	679,5,5,0,0,678,677,1,0,0,0,678,679,1,0,0,0,679,681,1,0,0,0,680,663,1,
  	0,0,0,680,672,1,0,0,0,681,133,1,0,0,0,682,684,5,16,0,0,683,685,3,188,
  	94,0,684,683,1,0,0,0,684,685,1,0,0,0,685,687,1,0,0,0,686,688,3,136,68,
  	0,687,686,1,0,0,0,687,688,1,0,0,0,688,690,1,0,0,0,689,691,3,170,85,0,
  	690,689,1,0,0,0,690,691,1,0,0,0,691,693,1,0,0,0,692,694,3,124,62,0,693,
  	692,1,0,0,0,693,694,1,0,0,0,694,695,1,0,0,0,695,696,5,17,0,0,696,135,
  	1,0,0,0,697,698,5,25,0,0,698,706,3,186,93,0,699,701,5,27,0,0,700,702,
  	5,25,0,0,701,700,1,0,0,0,701,702,1,0,0,0,702,703,1,0,0,0,703,705,3,186,
  	93,0,704,699,1,0,0,0,705,708,1,0,0,0,706,704,1,0,0,0,706,707,1,0,0,0,
  	707,137,1,0,0,0,708,706,1,0,0,0,709,711,5,63,0,0,710,712,5,40,0,0,711,
  	710,1,0,0,0,711,712,1,0,0,0,712,713,1,0,0,0,713,714,3,6,3,0,714,139,1,
  	0,0,0,715,716,5,49,0,0,716,719,5,14,0,0,717,720,3,4,2,0,718,720,3,72,
  	36,0,719,717,1,0,0,0,719,718,1,0,0,0,720,721,1,0,0,0,721,722,5,15,0,0,
  	722,141,1,0,0,0,723,728,3,188,94,0,724,725,5,10,0,0,725,727,3,188,94,
  	0,726,724,1,0,0,0,727,730,1,0,0,0,728,726,1,0,0,0,728,729,1,0,0,0,729,
  	143,1,0,0,0,730,728,1,0,0,0,731,732,3,142,71,0,732,734,5,12,0,0,733,735,
  	5,68,0,0,734,733,1,0,0,0,734,735,1,0,0,0,735,737,1,0,0,0,736,738,3,162,
  	81,0,737,736,1,0,0,0,737,738,1,0,0,0,738,739,1,0,0,0,739,740,5,13,0,0,
  	740,145,1,0,0,0,741,742,5,12,0,0,742,743,3,78,39,0,743,744,5,13,0,0,744,
  	147,1,0,0,0,745,746,7,6,0,0,746,747,5,12,0,0,747,748,3,158,79,0,748,749,
  	5,13,0,0,749,149,1,0,0,0,750,752,5,16,0,0,751,753,3,130,65,0,752,751,
  	1,0,0,0,752,753,1,0,0,0,753,754,1,0,0,0,754,756,3,152,76,0,755,757,3,
  	74,37,0,756,755,1,0,0,0,756,757,1,0,0,0,757,758,1,0,0,0,758,759,5,27,
  	0,0,759,760,3,78,39,0,760,761,5,17,0,0,761,151,1,0,0,0,762,764,3,126,
  	63,0,763,765,3,122,61,0,764,763,1,0,0,0,765,766,1,0,0,0,766,764,1,0,0,
  	0,766,767,1,0,0,0,767,153,1,0,0,0,768,769,5,16,0,0,769,772,3,158,79,0,
  	770,771,5,27,0,0,771,773,3,78,39,0,772,770,1,0,0,0,772,773,1,0,0,0,773,
  	774,1,0,0,0,774,775,5,17,0,0,775,155,1,0,0,0,776,777,5,37,0,0,777,778,
  	5,12,0,0,778,779,3,188,94,0,779,780,5,1,0,0,780,781,3,78,39,0,781,782,
  	5,11,0,0,782,783,3,188,94,0,783,784,5,70,0,0,784,785,3,78,39,0,785,786,
  	5,27,0,0,786,787,3,78,39,0,787,788,5,13,0,0,788,157,1,0,0,0,789,790,3,
  	188,94,0,790,791,5,70,0,0,791,793,3,78,39,0,792,794,3,74,37,0,793,792,
  	1,0,0,0,793,794,1,0,0,0,794,159,1,0,0,0,795,796,5,33,0,0,796,797,5,12,
  	0,0,797,798,5,23,0,0,798,799,5,13,0,0,799,161,1,0,0,0,800,805,3,78,39,
  	0,801,802,5,11,0,0,802,804,3,78,39,0,803,801,1,0,0,0,804,807,1,0,0,0,
  	805,803,1,0,0,0,805,806,1,0,0,0,806,163,1,0,0,0,807,805,1,0,0,0,808,810,
  	5,84,0,0,809,811,3,78,39,0,810,809,1,0,0,0,810,811,1,0,0,0,811,817,1,
  	0,0,0,812,813,5,85,0,0,813,814,3,78,39,0,814,815,5,86,0,0,815,816,3,78,
  	39,0,816,818,1,0,0,0,817,812,1,0,0,0,818,819,1,0,0,0,819,817,1,0,0,0,
  	819,820,1,0,0,0,820,823,1,0,0,0,821,822,5,87,0,0,822,824,3,78,39,0,823,
  	821,1,0,0,0,823,824,1,0,0,0,824,825,1,0,0,0,825,826,5,88,0,0,826,165,
  	1,0,0,0,827,830,5,28,0,0,828,831,3,188,94,0,829,831,3,174,87,0,830,828,
  	1,0,0,0,830,829,1,0,0,0,831,167,1,0,0,0,832,840,3,172,86,0,833,840,3,
  	174,87,0,834,840,5,78,0,0,835,840,3,176,88,0,836,840,3,178,89,0,837,840,
  	3,180,90,0,838,840,3,182,91,0,839,832,1,0,0,0,839,833,1,0,0,0,839,834,
  	1,0,0,0,839,835,1,0,0,0,839,836,1,0,0,0,839,837,1,0,0,0,839,838,1,0,0,
  	0,840,169,1,0,0,0,841,844,5,23,0,0,842,845,3,174,87,0,843,845,5,95,0,
  	0,844,842,1,0,0,0,844,843,1,0,0,0,844,845,1,0,0,0,845,851,1,0,0,0,846,
  	849,5,8,0,0,847,850,3,174,87,0,848,850,5,95,0,0,849,847,1,0,0,0,849,848,
  	1,0,0,0,849,850,1,0,0,0,850,852,1,0,0,0,851,846,1,0,0,0,851,852,1,0,0,
  	0,852,171,1,0,0,0,853,854,7,7,0,0,854,173,1,0,0,0,855,856,5,99,0,0,856,
  	175,1,0,0,0,857,858,5,98,0,0,858,177,1,0,0,0,859,860,5,97,0,0,860,179,
  	1,0,0,0,861,863,5,16,0,0,862,864,3,162,81,0,863,862,1,0,0,0,863,864,1,
  	0,0,0,864,865,1,0,0,0,865,866,5,17,0,0,866,181,1,0,0,0,867,876,5,14,0,
  	0,868,873,3,184,92,0,869,870,5,11,0,0,870,872,3,184,92,0,871,869,1,0,
  	0,0,872,875,1,0,0,0,873,871,1,0,0,0,873,874,1,0,0,0,874,877,1,0,0,0,875,
  	873,1,0,0,0,876,868,1,0,0,0,876,877,1,0,0,0,877,878,1,0,0,0,878,879,5,
  	15,0,0,879,183,1,0,0,0,880,881,3,186,93,0,881,882,5,25,0,0,882,883,3,
  	78,39,0,883,185,1,0,0,0,884,887,3,188,94,0,885,887,3,190,95,0,886,884,
  	1,0,0,0,886,885,1,0,0,0,887,187,1,0,0,0,888,889,7,8,0,0,889,189,1,0,0,
  	0,890,891,7,9,0,0,891,191,1,0,0,0,104,194,199,203,209,215,219,224,229,
  	237,244,249,251,259,268,272,275,278,282,288,294,298,307,311,324,331,334,
  	345,357,365,372,376,385,389,394,403,416,430,436,443,453,461,469,477,483,
  	494,504,512,520,524,532,534,541,545,548,551,561,565,571,578,583,590,596,
  	601,617,624,631,635,638,641,658,666,670,674,678,680,684,687,690,693,701,
  	706,711,719,728,734,737,752,756,766,772,793,805,810,819,823,830,839,844,
  	849,851,863,873,876,886
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
    setState(192);
    query();
    setState(194);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SEMI) {
      setState(193);
      match(CypherParser::SEMI);
    }
    setState(196);
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
    setState(203);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 2, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(199);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::EXPLAIN) {
        setState(198);
        match(CypherParser::EXPLAIN);
      }
      setState(201);
      regularQuery();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(202);
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
    setState(205);
    singleQuery();
    setState(209);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::UNION) {
      setState(206);
      unionSt();
      setState(211);
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
    setState(215);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 5382135806779457536) != 0) || _la == CypherParser::UNWIND

    || _la == CypherParser::FOREACH) {
      setState(212);
      clause();
      setState(217);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(219);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RETURN) {
      setState(218);
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
    setState(224);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL:
      case CypherParser::WITH:
      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 1);
        setState(221);
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
        setState(222);
        updatingClause();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(223);
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
    setState(229);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(226);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(227);
        unwindSt();
        break;
      }

      case CypherParser::WITH: {
        enterOuterAlt(_localctx, 3);
        setState(228);
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
    setState(237);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(231);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(232);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(233);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(234);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(235);
        removeSt();
        break;
      }

      case CypherParser::FOREACH: {
        enterOuterAlt(_localctx, 6);
        setState(236);
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
    setState(239);
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
    setState(241);
    match(CypherParser::CALL);
    setState(242);
    invocationName();
    setState(244);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LPAREN) {
      setState(243);
      parenExpressionChain();
    }
    setState(251);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(246);
      match(CypherParser::YIELD);
      setState(249);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::MULT: {
          setState(247);
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
          setState(248);
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
    setState(253);
    match(CypherParser::RETURN);
    setState(254);
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
    setState(256);
    match(CypherParser::WITH);
    setState(257);
    projectionBody();
    setState(259);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(258);
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
    setState(261);
    match(CypherParser::SKIP_W);
    setState(262);
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
    setState(264);
    match(CypherParser::LIMIT);
    setState(265);
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
    setState(268);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(267);
      match(CypherParser::DISTINCT);
    }
    setState(270);
    projectionItems();
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ORDER) {
      setState(271);
      orderSt();
    }
    setState(275);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SKIP_W) {
      setState(274);
      skipSt();
    }
    setState(278);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LIMIT) {
      setState(277);
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
    setState(282);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MULT: {
        setState(280);
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
        setState(281);
        projectionItem();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(288);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(284);
      match(CypherParser::COMMA);
      setState(285);
      projectionItem();
      setState(290);
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
    setState(291);
    expression();
    setState(294);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::AS) {
      setState(292);
      match(CypherParser::AS);
      setState(293);
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
    setState(296);
    expression();
    setState(298);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 217703302299648) != 0)) {
      setState(297);
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
    setState(300);
    match(CypherParser::ORDER);
    setState(301);
    match(CypherParser::BY);
    setState(302);
    orderItem();
    setState(307);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(303);
      match(CypherParser::COMMA);
      setState(304);
      orderItem();
      setState(309);
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
    setState(311);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::OPTIONAL) {
      setState(310);
      match(CypherParser::OPTIONAL);
    }
    setState(313);
    match(CypherParser::MATCH);
    setState(314);
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
    setState(316);
    match(CypherParser::UNWIND);
    setState(317);
    expression();
    setState(318);
    match(CypherParser::AS);
    setState(319);
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
    setState(324);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(321);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(322);
        unwindSt();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(323);
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
    setState(331);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(326);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(327);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(328);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(329);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(330);
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
    setState(334);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DETACH) {
      setState(333);
      match(CypherParser::DETACH);
    }
    setState(336);
    match(CypherParser::DELETE);
    setState(337);
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
    setState(339);
    match(CypherParser::REMOVE);
    setState(340);
    removeItem();
    setState(345);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(341);
      match(CypherParser::COMMA);
      setState(342);
      removeItem();
      setState(347);
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
    setState(348);
    match(CypherParser::FOREACH);
    setState(349);
    match(CypherParser::LPAREN);
    setState(350);
    symbol();
    setState(351);
    match(CypherParser::IN);
    setState(352);
    expression();
    setState(353);
    match(CypherParser::STICK);
    setState(355); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(354);
      updatingClause();
      setState(357); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (((((_la - 44) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 44)) & 1125899906884115) != 0));
    setState(359);
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
    setState(365);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(361);
      symbol();
      setState(362);
      nodeLabels();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(364);
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
    setState(367);
    match(CypherParser::CALL);
    setState(368);
    invocationName();
    setState(369);
    parenExpressionChain();
    setState(372);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(370);
      match(CypherParser::YIELD);
      setState(371);
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
    setState(374);
    match(CypherParser::LPAREN);
    setState(376);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 72) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 72)) & 264245361) != 0)) {
      setState(375);
      expressionChain();
    }
    setState(378);
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
    setState(380);
    yieldItem();
    setState(385);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(381);
      match(CypherParser::COMMA);
      setState(382);
      yieldItem();
      setState(387);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(389);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(388);
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
    setState(394);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx)) {
    case 1: {
      setState(391);
      symbol();
      setState(392);
      match(CypherParser::AS);
      break;
    }

    default:
      break;
    }
    setState(396);
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
    setState(398);
    match(CypherParser::MERGE);
    setState(399);
    patternPart();
    setState(403);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::ON) {
      setState(400);
      mergeAction();
      setState(405);
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
    setState(406);
    match(CypherParser::ON);
    setState(407);
    _la = _input->LA(1);
    if (!(_la == CypherParser::CREATE

    || _la == CypherParser::MATCH)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(408);
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
    setState(410);
    match(CypherParser::SET);
    setState(411);
    setItem();
    setState(416);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(412);
      match(CypherParser::COMMA);
      setState(413);
      setItem();
      setState(418);
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
    setState(430);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(419);
      propertyExpression();
      setState(420);
      match(CypherParser::ASSIGN);
      setState(421);
      expression();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(423);
      symbol();
      setState(424);
      _la = _input->LA(1);
      if (!(_la == CypherParser::ASSIGN

      || _la == CypherParser::ADD_ASSIGN)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(425);
      expression();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(427);
      symbol();
      setState(428);
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
    setState(434); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(432);
      match(CypherParser::COLON);
      setState(433);
      name();
      setState(436); 
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
    setState(438);
    match(CypherParser::CREATE);
    setState(439);
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
    setState(441);
    pattern();
    setState(443);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(442);
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
    setState(445);
    match(CypherParser::WHERE);
    setState(446);
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
    setState(448);
    patternPart();
    setState(453);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(449);
      match(CypherParser::COMMA);
      setState(450);
      patternPart();
      setState(455);
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
    setState(456);
    xorExpression();
    setState(461);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::OR) {
      setState(457);
      match(CypherParser::OR);
      setState(458);
      xorExpression();
      setState(463);
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
    setState(464);
    andExpression();
    setState(469);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::XOR) {
      setState(465);
      match(CypherParser::XOR);
      setState(466);
      andExpression();
      setState(471);
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
    setState(472);
    notExpression();
    setState(477);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::AND) {
      setState(473);
      match(CypherParser::AND);
      setState(474);
      notExpression();
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
    setState(483);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::NOT) {
      setState(480);
      match(CypherParser::NOT);
      setState(485);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(486);
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
    setState(488);
    addSubExpression();
    setState(494);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 250) != 0)) {
      setState(489);
      comparisonSigns();
      setState(490);
      addSubExpression();
      setState(496);
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
    setState(497);
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
    setState(499);
    multDivExpression();
    setState(504);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(500);
      _la = _input->LA(1);
      if (!(_la == CypherParser::SUB

      || _la == CypherParser::PLUS)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(501);
      multDivExpression();
      setState(506);
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
    setState(507);
    powerExpression();
    setState(512);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 11534336) != 0)) {
      setState(508);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 11534336) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(509);
      powerExpression();
      setState(514);
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
    setState(515);
    unaryAddSubExpression();
    setState(520);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::CARET) {
      setState(516);
      match(CypherParser::CARET);
      setState(517);
      unaryAddSubExpression();
      setState(522);
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
    setState(524);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(523);
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
    setState(526);
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
    setState(528);
    propertyOrLabelExpression();
    setState(534);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 16) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 16)) & 353532570748583937) != 0)) {
      setState(532);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::CONTAINS:
        case CypherParser::ENDS:
        case CypherParser::STARTS: {
          setState(529);
          stringExpression();
          break;
        }

        case CypherParser::LBRACK:
        case CypherParser::IN: {
          setState(530);
          listExpression();
          break;
        }

        case CypherParser::IS: {
          setState(531);
          nullExpression();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(536);
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
  enterRule(_localctx, 100, CypherParser::RuleListExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(551);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::IN: {
        enterOuterAlt(_localctx, 1);
        setState(537);
        match(CypherParser::IN);
        setState(538);
        propertyOrLabelExpression();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 2);
        setState(539);
        match(CypherParser::LBRACK);
        setState(548);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx)) {
        case 1: {
          setState(541);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 72) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 72)) & 264245361) != 0)) {
            setState(540);
            expression();
          }
          setState(543);
          match(CypherParser::RANGE);
          setState(545);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 72) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 72)) & 264245361) != 0)) {
            setState(544);
            expression();
          }
          break;
        }

        case 2: {
          setState(547);
          expression();
          break;
        }

        default:
          break;
        }
        setState(550);
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
    setState(553);
    stringExpPrefix();
    setState(554);
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
    setState(561);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::STARTS: {
        enterOuterAlt(_localctx, 1);
        setState(556);
        match(CypherParser::STARTS);
        setState(557);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::ENDS: {
        enterOuterAlt(_localctx, 2);
        setState(558);
        match(CypherParser::ENDS);
        setState(559);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::CONTAINS: {
        enterOuterAlt(_localctx, 3);
        setState(560);
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
    setState(563);
    match(CypherParser::IS);
    setState(565);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::NOT) {
      setState(564);
      match(CypherParser::NOT);
    }
    setState(567);
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
    setState(569);
    propertyExpression();
    setState(571);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(570);
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
    setState(573);
    atom();
    setState(578);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(574);
      match(CypherParser::DOT);
      setState(575);
      name();
      setState(580);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(583);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLONCOLON) {
      setState(581);
      match(CypherParser::COLONCOLON);
      setState(582);
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
  enterRule(_localctx, 112, CypherParser::RuleLabelCast);
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
    setState(585);
    name();
    setState(590);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(586);
      match(CypherParser::DOT);
      setState(587);
      name();
      setState(592);
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
  enterRule(_localctx, 114, CypherParser::RulePatternPart);
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
    setState(596);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 94) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 94)) & 7) != 0)) {
      setState(593);
      symbol();
      setState(594);
      match(CypherParser::ASSIGN);
    }
    setState(601);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::SHORTESTPATH: {
        setState(598);
        shortestPathPattern();
        break;
      }

      case CypherParser::ALLSHORTESTPATHS: {
        setState(599);
        allShortestPathsPattern();
        break;
      }

      case CypherParser::LPAREN: {
        setState(600);
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
  enterRule(_localctx, 116, CypherParser::RuleShortestPathPattern);

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
    match(CypherParser::SHORTESTPATH);
    setState(604);
    match(CypherParser::LPAREN);
    setState(605);
    patternElem();
    setState(606);
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
  enterRule(_localctx, 118, CypherParser::RuleAllShortestPathsPattern);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(608);
    match(CypherParser::ALLSHORTESTPATHS);
    setState(609);
    match(CypherParser::LPAREN);
    setState(610);
    patternElem();
    setState(611);
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
  enterRule(_localctx, 120, CypherParser::RulePatternElem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(624);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 64, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(613);
      nodePattern();
      setState(617);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::LT

      || _la == CypherParser::SUB) {
        setState(614);
        patternElemChain();
        setState(619);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(620);
      match(CypherParser::LPAREN);
      setState(621);
      patternElem();
      setState(622);
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
  enterRule(_localctx, 122, CypherParser::RulePatternElemChain);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(626);
    relationshipPattern();
    setState(627);
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
  enterRule(_localctx, 124, CypherParser::RuleProperties);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(631);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 1);
        setState(629);
        mapLit();
        break;
      }

      case CypherParser::DOLLAR: {
        enterOuterAlt(_localctx, 2);
        setState(630);
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
  enterRule(_localctx, 126, CypherParser::RuleNodePattern);
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
    setState(633);
    match(CypherParser::LPAREN);
    setState(635);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 94) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 94)) & 7) != 0)) {
      setState(634);
      symbol();
    }
    setState(638);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(637);
      nodeLabels();
    }
    setState(641);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(640);
      properties();
    }
    setState(643);
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
  enterRule(_localctx, 128, CypherParser::RuleAtom);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(658);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(645);
      literal();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(646);
      parameter();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(647);
      caseExpression();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(648);
      countAll();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(649);
      listComprehension();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(650);
      reduceExpression();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(651);
      patternComprehension();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(652);
      filterWith();
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(653);
      relationshipsChainPattern();
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(654);
      parenthesizedExpression();
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(655);
      functionInvocation();
      break;
    }

    case 12: {
      enterOuterAlt(_localctx, 12);
      setState(656);
      symbol();
      break;
    }

    case 13: {
      enterOuterAlt(_localctx, 13);
      setState(657);
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
  enterRule(_localctx, 130, CypherParser::RuleLhs);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(660);
    symbol();
    setState(661);
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
  enterRule(_localctx, 132, CypherParser::RuleRelationshipPattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(680);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LT: {
        enterOuterAlt(_localctx, 1);
        setState(663);
        match(CypherParser::LT);
        setState(664);
        match(CypherParser::SUB);
        setState(666);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(665);
          relationDetail();
        }
        setState(668);
        match(CypherParser::SUB);
        setState(670);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(669);
          match(CypherParser::GT);
        }
        break;
      }

      case CypherParser::SUB: {
        enterOuterAlt(_localctx, 2);
        setState(672);
        match(CypherParser::SUB);
        setState(674);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(673);
          relationDetail();
        }
        setState(676);
        match(CypherParser::SUB);
        setState(678);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(677);
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
  enterRule(_localctx, 134, CypherParser::RuleRelationDetail);
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
    setState(682);
    match(CypherParser::LBRACK);
    setState(684);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 94) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 94)) & 7) != 0)) {
      setState(683);
      symbol();
    }
    setState(687);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(686);
      relationshipTypes();
    }
    setState(690);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::MULT) {
      setState(689);
      rangeLit();
    }
    setState(693);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(692);
      properties();
    }
    setState(695);
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
  enterRule(_localctx, 136, CypherParser::RuleRelationshipTypes);
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
    setState(697);
    match(CypherParser::COLON);
    setState(698);
    name();
    setState(706);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::STICK) {
      setState(699);
      match(CypherParser::STICK);
      setState(701);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::COLON) {
        setState(700);
        match(CypherParser::COLON);
      }
      setState(703);
      name();
      setState(708);
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
  enterRule(_localctx, 138, CypherParser::RuleUnionSt);
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
    setState(709);
    match(CypherParser::UNION);
    setState(711);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ALL) {
      setState(710);
      match(CypherParser::ALL);
    }
    setState(713);
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
  enterRule(_localctx, 140, CypherParser::RuleSubqueryExist);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(715);
    match(CypherParser::EXISTS);
    setState(716);
    match(CypherParser::LBRACE);
    setState(719);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 82, _ctx)) {
    case 1: {
      setState(717);
      regularQuery();
      break;
    }

    case 2: {
      setState(718);
      patternWhere();
      break;
    }

    default:
      break;
    }
    setState(721);
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
  enterRule(_localctx, 142, CypherParser::RuleInvocationName);
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
    setState(723);
    symbol();
    setState(728);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(724);
      match(CypherParser::DOT);
      setState(725);
      symbol();
      setState(730);
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
  enterRule(_localctx, 144, CypherParser::RuleFunctionInvocation);
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
    setState(731);
    invocationName();
    setState(732);
    match(CypherParser::LPAREN);
    setState(734);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(733);
      match(CypherParser::DISTINCT);
    }
    setState(737);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 72) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 72)) & 264245361) != 0)) {
      setState(736);
      expressionChain();
    }
    setState(739);
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
  enterRule(_localctx, 146, CypherParser::RuleParenthesizedExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(741);
    match(CypherParser::LPAREN);
    setState(742);
    expression();
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
  enterRule(_localctx, 148, CypherParser::RuleFilterWith);
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
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1219770712064) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(746);
    match(CypherParser::LPAREN);
    setState(747);
    filterExpression();
    setState(748);
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
  enterRule(_localctx, 150, CypherParser::RulePatternComprehension);
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
    setState(750);
    match(CypherParser::LBRACK);
    setState(752);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 94) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 94)) & 7) != 0)) {
      setState(751);
      lhs();
    }
    setState(754);
    relationshipsChainPattern();
    setState(756);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(755);
      where();
    }
    setState(758);
    match(CypherParser::STICK);
    setState(759);
    expression();
    setState(760);
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
  enterRule(_localctx, 152, CypherParser::RuleRelationshipsChainPattern);

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
    setState(762);
    nodePattern();
    setState(764); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(763);
              patternElemChain();
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(766); 
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
  enterRule(_localctx, 154, CypherParser::RuleListComprehension);
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
    setState(768);
    match(CypherParser::LBRACK);
    setState(769);
    filterExpression();
    setState(772);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::STICK) {
      setState(770);
      match(CypherParser::STICK);
      setState(771);
      expression();
    }
    setState(774);
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
  enterRule(_localctx, 156, CypherParser::RuleReduceExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(776);
    match(CypherParser::REDUCE);
    setState(777);
    match(CypherParser::LPAREN);
    setState(778);
    symbol();
    setState(779);
    match(CypherParser::ASSIGN);
    setState(780);
    expression();
    setState(781);
    match(CypherParser::COMMA);
    setState(782);
    symbol();
    setState(783);
    match(CypherParser::IN);
    setState(784);
    expression();
    setState(785);
    match(CypherParser::STICK);
    setState(786);
    expression();
    setState(787);
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
  enterRule(_localctx, 158, CypherParser::RuleFilterExpression);
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
    setState(789);
    symbol();
    setState(790);
    match(CypherParser::IN);
    setState(791);
    expression();
    setState(793);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(792);
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
  enterRule(_localctx, 160, CypherParser::RuleCountAll);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(795);
    match(CypherParser::COUNT);
    setState(796);
    match(CypherParser::LPAREN);
    setState(797);
    match(CypherParser::MULT);
    setState(798);
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
  enterRule(_localctx, 162, CypherParser::RuleExpressionChain);
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
    setState(800);
    expression();
    setState(805);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(801);
      match(CypherParser::COMMA);
      setState(802);
      expression();
      setState(807);
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
  enterRule(_localctx, 164, CypherParser::RuleCaseExpression);
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
    setState(808);
    match(CypherParser::CASE);
    setState(810);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 72) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 72)) & 264245361) != 0)) {
      setState(809);
      expression();
    }
    setState(817); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(812);
      match(CypherParser::WHEN);
      setState(813);
      expression();
      setState(814);
      match(CypherParser::THEN);
      setState(815);
      expression();
      setState(819); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == CypherParser::WHEN);
    setState(823);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ELSE) {
      setState(821);
      match(CypherParser::ELSE);
      setState(822);
      expression();
    }
    setState(825);
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
  enterRule(_localctx, 166, CypherParser::RuleParameter);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(827);
    match(CypherParser::DOLLAR);
    setState(830);
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
        setState(828);
        symbol();
        break;
      }

      case CypherParser::DIGIT: {
        setState(829);
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
  enterRule(_localctx, 168, CypherParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(839);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FALSE:
      case CypherParser::TRUE: {
        enterOuterAlt(_localctx, 1);
        setState(832);
        boolLit();
        break;
      }

      case CypherParser::DIGIT: {
        enterOuterAlt(_localctx, 2);
        setState(833);
        numLit();
        break;
      }

      case CypherParser::NULL_W: {
        enterOuterAlt(_localctx, 3);
        setState(834);
        match(CypherParser::NULL_W);
        break;
      }

      case CypherParser::STRING_LITERAL: {
        enterOuterAlt(_localctx, 4);
        setState(835);
        stringLit();
        break;
      }

      case CypherParser::CHAR_LITERAL: {
        enterOuterAlt(_localctx, 5);
        setState(836);
        charLit();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 6);
        setState(837);
        listLit();
        break;
      }

      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 7);
        setState(838);
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
  enterRule(_localctx, 170, CypherParser::RuleRangeLit);
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
    setState(841);
    match(CypherParser::MULT);
    setState(844);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::DIGIT: {
        setState(842);
        numLit();
        break;
      }

      case CypherParser::ID: {
        setState(843);
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
    setState(851);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RANGE) {
      setState(846);
      match(CypherParser::RANGE);
      setState(849);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::DIGIT: {
          setState(847);
          numLit();
          break;
        }

        case CypherParser::ID: {
          setState(848);
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
  enterRule(_localctx, 172, CypherParser::RuleBoolLit);
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
    setState(853);
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
  enterRule(_localctx, 174, CypherParser::RuleNumLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(855);
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
  enterRule(_localctx, 176, CypherParser::RuleStringLit);

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
  enterRule(_localctx, 178, CypherParser::RuleCharLit);

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
  enterRule(_localctx, 180, CypherParser::RuleListLit);
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
    setState(861);
    match(CypherParser::LBRACK);
    setState(863);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 564322464780288) != 0) || ((((_la - 72) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 72)) & 264245361) != 0)) {
      setState(862);
      expressionChain();
    }
    setState(865);
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
  enterRule(_localctx, 182, CypherParser::RuleMapLit);
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
    setState(867);
    match(CypherParser::LBRACE);
    setState(876);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & -1126864127000576) != 0) || ((((_la - 64) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 64)) & 8589934591) != 0)) {
      setState(868);
      mapPair();
      setState(873);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::COMMA) {
        setState(869);
        match(CypherParser::COMMA);
        setState(870);
        mapPair();
        setState(875);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(878);
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
  enterRule(_localctx, 184, CypherParser::RuleMapPair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(880);
    name();
    setState(881);
    match(CypherParser::COLON);
    setState(882);
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
  enterRule(_localctx, 186, CypherParser::RuleName);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(886);
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
        setState(884);
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
        setState(885);
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
  enterRule(_localctx, 188, CypherParser::RuleSymbol);
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
    setState(888);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 135291469824) != 0) || ((((_la - 94) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 94)) & 7) != 0))) {
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
  enterRule(_localctx, 190, CypherParser::RuleReservedWord);
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
    setState(890);
    _la = _input->LA(1);
    if (!(((((_la - 40) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 40)) & 18014398509480959) != 0))) {
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
