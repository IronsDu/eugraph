
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
      "'OF'", "'ADD'", "'DROP'", "'FOREACH'"
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
      "OF", "ADD", "DROP", "FOREACH", "ID", "ESC_LITERAL", "CHAR_LITERAL", 
      "STRING_LITERAL", "DIGIT", "FLOAT", "WS", "COMMENT", "LINE_COMMENT", 
      "ERRCHAR", "Letter"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,102,860,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
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
  	91,2,92,7,92,1,0,1,0,3,0,189,8,0,1,0,1,0,1,1,3,1,194,8,1,1,1,1,1,3,1,
  	198,8,1,1,2,1,2,5,2,202,8,2,10,2,12,2,205,9,2,1,3,5,3,208,8,3,10,3,12,
  	3,211,9,3,1,3,3,3,214,8,3,1,4,1,4,1,4,3,4,219,8,4,1,5,1,5,1,5,3,5,224,
  	8,5,1,6,1,6,1,6,1,6,1,6,1,6,3,6,232,8,6,1,7,1,7,1,8,1,8,1,8,3,8,239,8,
  	8,1,8,1,8,1,8,3,8,244,8,8,3,8,246,8,8,1,9,1,9,1,9,1,10,1,10,1,10,3,10,
  	254,8,10,1,11,1,11,1,11,1,12,1,12,1,12,1,13,3,13,263,8,13,1,13,1,13,3,
  	13,267,8,13,1,13,3,13,270,8,13,1,13,3,13,273,8,13,1,14,1,14,3,14,277,
  	8,14,1,14,1,14,5,14,281,8,14,10,14,12,14,284,9,14,1,15,1,15,1,15,3,15,
  	289,8,15,1,16,1,16,3,16,293,8,16,1,17,1,17,1,17,1,17,1,17,5,17,300,8,
  	17,10,17,12,17,303,9,17,1,18,3,18,306,8,18,1,18,1,18,1,18,1,19,1,19,1,
  	19,1,19,1,19,1,20,1,20,1,20,3,20,319,8,20,1,21,1,21,1,21,1,21,1,21,3,
  	21,326,8,21,1,22,3,22,329,8,22,1,22,1,22,1,22,1,23,1,23,1,23,1,23,5,23,
  	338,8,23,10,23,12,23,341,9,23,1,24,1,24,1,24,1,24,1,24,1,24,1,24,4,24,
  	350,8,24,11,24,12,24,351,1,24,1,24,1,25,1,25,1,25,1,25,3,25,360,8,25,
  	1,26,1,26,1,26,1,26,1,26,3,26,367,8,26,1,27,1,27,3,27,371,8,27,1,27,1,
  	27,1,28,1,28,1,28,5,28,378,8,28,10,28,12,28,381,9,28,1,28,3,28,384,8,
  	28,1,29,1,29,1,29,3,29,389,8,29,1,29,1,29,1,30,1,30,1,30,5,30,396,8,30,
  	10,30,12,30,399,9,30,1,31,1,31,1,31,1,31,1,32,1,32,1,32,1,32,5,32,409,
  	8,32,10,32,12,32,412,9,32,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,
  	1,33,1,33,3,33,425,8,33,1,34,1,34,4,34,429,8,34,11,34,12,34,430,1,35,
  	1,35,1,35,1,36,1,36,3,36,438,8,36,1,37,1,37,1,37,1,38,1,38,1,38,5,38,
  	446,8,38,10,38,12,38,449,9,38,1,39,1,39,1,39,5,39,454,8,39,10,39,12,39,
  	457,9,39,1,40,1,40,1,40,5,40,462,8,40,10,40,12,40,465,9,40,1,41,1,41,
  	1,41,5,41,470,8,41,10,41,12,41,473,9,41,1,42,5,42,476,8,42,10,42,12,42,
  	479,9,42,1,42,1,42,1,43,1,43,1,43,1,43,5,43,487,8,43,10,43,12,43,490,
  	9,43,1,44,1,44,1,45,1,45,1,45,5,45,497,8,45,10,45,12,45,500,9,45,1,46,
  	1,46,1,46,5,46,505,8,46,10,46,12,46,508,9,46,1,47,1,47,1,47,5,47,513,
  	8,47,10,47,12,47,516,9,47,1,48,3,48,519,8,48,1,48,1,48,1,49,1,49,1,49,
  	1,49,5,49,527,8,49,10,49,12,49,530,9,49,1,50,1,50,1,50,1,50,3,50,536,
  	8,50,1,50,1,50,3,50,540,8,50,1,50,3,50,543,8,50,1,50,3,50,546,8,50,1,
  	51,1,51,1,51,1,52,1,52,1,52,1,52,1,52,3,52,556,8,52,1,53,1,53,3,53,560,
  	8,53,1,53,1,53,1,54,1,54,3,54,566,8,54,1,55,1,55,1,55,5,55,571,8,55,10,
  	55,12,55,574,9,55,1,55,1,55,3,55,578,8,55,1,56,1,56,1,56,5,56,583,8,56,
  	10,56,12,56,586,9,56,1,57,1,57,1,57,3,57,591,8,57,1,57,1,57,1,58,1,58,
  	5,58,597,8,58,10,58,12,58,600,9,58,1,58,1,58,1,58,1,58,3,58,606,8,58,
  	1,59,1,59,1,59,1,60,1,60,3,60,613,8,60,1,61,1,61,3,61,617,8,61,1,61,3,
  	61,620,8,61,1,61,3,61,623,8,61,1,61,1,61,1,62,1,62,1,62,1,62,1,62,1,62,
  	1,62,1,62,1,62,1,62,1,62,1,62,3,62,639,8,62,1,63,1,63,1,63,1,64,1,64,
  	1,64,3,64,647,8,64,1,64,1,64,3,64,651,8,64,1,64,1,64,3,64,655,8,64,1,
  	64,1,64,3,64,659,8,64,3,64,661,8,64,1,65,1,65,3,65,665,8,65,1,65,3,65,
  	668,8,65,1,65,3,65,671,8,65,1,65,3,65,674,8,65,1,65,1,65,1,66,1,66,1,
  	66,1,66,3,66,682,8,66,1,66,5,66,685,8,66,10,66,12,66,688,9,66,1,67,1,
  	67,3,67,692,8,67,1,67,1,67,1,68,1,68,1,68,1,68,3,68,700,8,68,1,68,1,68,
  	1,69,1,69,1,69,5,69,707,8,69,10,69,12,69,710,9,69,1,70,1,70,1,70,3,70,
  	715,8,70,1,70,3,70,718,8,70,1,70,1,70,1,71,1,71,1,71,1,71,1,72,1,72,1,
  	72,1,72,1,72,1,73,1,73,3,73,733,8,73,1,73,1,73,3,73,737,8,73,1,73,1,73,
  	1,73,1,73,1,74,1,74,4,74,745,8,74,11,74,12,74,746,1,75,1,75,1,75,1,75,
  	3,75,753,8,75,1,75,1,75,1,76,1,76,1,76,1,76,3,76,761,8,76,1,77,1,77,1,
  	77,1,77,1,77,1,78,1,78,1,78,5,78,771,8,78,10,78,12,78,774,9,78,1,79,1,
  	79,3,79,778,8,79,1,79,1,79,1,79,1,79,1,79,4,79,785,8,79,11,79,12,79,786,
  	1,79,1,79,3,79,791,8,79,1,79,1,79,1,80,1,80,1,80,3,80,798,8,80,1,81,1,
  	81,1,81,1,81,1,81,1,81,1,81,3,81,807,8,81,1,82,1,82,1,82,3,82,812,8,82,
  	1,82,1,82,1,82,3,82,817,8,82,3,82,819,8,82,1,83,1,83,1,84,1,84,1,85,1,
  	85,1,86,1,86,1,87,1,87,3,87,831,8,87,1,87,1,87,1,88,1,88,1,88,1,88,5,
  	88,839,8,88,10,88,12,88,842,9,88,3,88,844,8,88,1,88,1,88,1,89,1,89,1,
  	89,1,89,1,90,1,90,3,90,854,8,90,1,91,1,91,1,92,1,92,1,92,0,0,93,0,2,4,
  	6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,
  	54,56,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,
  	100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,
  	136,138,140,142,144,146,148,150,152,154,156,158,160,162,164,166,168,170,
  	172,174,176,178,180,182,184,0,10,2,0,38,39,43,44,2,0,41,41,49,49,1,0,
  	1,2,2,0,1,1,3,7,1,0,18,19,2,0,20,21,23,23,1,0,34,37,1,0,73,74,2,0,31,
  	36,91,93,2,0,37,46,48,90,899,0,186,1,0,0,0,2,197,1,0,0,0,4,199,1,0,0,
  	0,6,209,1,0,0,0,8,218,1,0,0,0,10,223,1,0,0,0,12,231,1,0,0,0,14,233,1,
  	0,0,0,16,235,1,0,0,0,18,247,1,0,0,0,20,250,1,0,0,0,22,255,1,0,0,0,24,
  	258,1,0,0,0,26,262,1,0,0,0,28,276,1,0,0,0,30,285,1,0,0,0,32,290,1,0,0,
  	0,34,294,1,0,0,0,36,305,1,0,0,0,38,310,1,0,0,0,40,318,1,0,0,0,42,325,
  	1,0,0,0,44,328,1,0,0,0,46,333,1,0,0,0,48,342,1,0,0,0,50,359,1,0,0,0,52,
  	361,1,0,0,0,54,368,1,0,0,0,56,374,1,0,0,0,58,388,1,0,0,0,60,392,1,0,0,
  	0,62,400,1,0,0,0,64,404,1,0,0,0,66,424,1,0,0,0,68,428,1,0,0,0,70,432,
  	1,0,0,0,72,435,1,0,0,0,74,439,1,0,0,0,76,442,1,0,0,0,78,450,1,0,0,0,80,
  	458,1,0,0,0,82,466,1,0,0,0,84,477,1,0,0,0,86,482,1,0,0,0,88,491,1,0,0,
  	0,90,493,1,0,0,0,92,501,1,0,0,0,94,509,1,0,0,0,96,518,1,0,0,0,98,522,
  	1,0,0,0,100,545,1,0,0,0,102,547,1,0,0,0,104,555,1,0,0,0,106,557,1,0,0,
  	0,108,563,1,0,0,0,110,567,1,0,0,0,112,579,1,0,0,0,114,590,1,0,0,0,116,
  	605,1,0,0,0,118,607,1,0,0,0,120,612,1,0,0,0,122,614,1,0,0,0,124,638,1,
  	0,0,0,126,640,1,0,0,0,128,660,1,0,0,0,130,662,1,0,0,0,132,677,1,0,0,0,
  	134,689,1,0,0,0,136,695,1,0,0,0,138,703,1,0,0,0,140,711,1,0,0,0,142,721,
  	1,0,0,0,144,725,1,0,0,0,146,730,1,0,0,0,148,742,1,0,0,0,150,748,1,0,0,
  	0,152,756,1,0,0,0,154,762,1,0,0,0,156,767,1,0,0,0,158,775,1,0,0,0,160,
  	794,1,0,0,0,162,806,1,0,0,0,164,808,1,0,0,0,166,820,1,0,0,0,168,822,1,
  	0,0,0,170,824,1,0,0,0,172,826,1,0,0,0,174,828,1,0,0,0,176,834,1,0,0,0,
  	178,847,1,0,0,0,180,853,1,0,0,0,182,855,1,0,0,0,184,857,1,0,0,0,186,188,
  	3,2,1,0,187,189,5,9,0,0,188,187,1,0,0,0,188,189,1,0,0,0,189,190,1,0,0,
  	0,190,191,5,0,0,1,191,1,1,0,0,0,192,194,5,47,0,0,193,192,1,0,0,0,193,
  	194,1,0,0,0,194,195,1,0,0,0,195,198,3,4,2,0,196,198,3,16,8,0,197,193,
  	1,0,0,0,197,196,1,0,0,0,198,3,1,0,0,0,199,203,3,6,3,0,200,202,3,134,67,
  	0,201,200,1,0,0,0,202,205,1,0,0,0,203,201,1,0,0,0,203,204,1,0,0,0,204,
  	5,1,0,0,0,205,203,1,0,0,0,206,208,3,8,4,0,207,206,1,0,0,0,208,211,1,0,
  	0,0,209,207,1,0,0,0,209,210,1,0,0,0,210,213,1,0,0,0,211,209,1,0,0,0,212,
  	214,3,14,7,0,213,212,1,0,0,0,213,214,1,0,0,0,214,7,1,0,0,0,215,219,3,
  	10,5,0,216,219,3,12,6,0,217,219,3,52,26,0,218,215,1,0,0,0,218,216,1,0,
  	0,0,218,217,1,0,0,0,219,9,1,0,0,0,220,224,3,36,18,0,221,224,3,38,19,0,
  	222,224,3,20,10,0,223,220,1,0,0,0,223,221,1,0,0,0,223,222,1,0,0,0,224,
  	11,1,0,0,0,225,232,3,70,35,0,226,232,3,60,30,0,227,232,3,44,22,0,228,
  	232,3,64,32,0,229,232,3,46,23,0,230,232,3,48,24,0,231,225,1,0,0,0,231,
  	226,1,0,0,0,231,227,1,0,0,0,231,228,1,0,0,0,231,229,1,0,0,0,231,230,1,
  	0,0,0,232,13,1,0,0,0,233,234,3,18,9,0,234,15,1,0,0,0,235,236,5,29,0,0,
  	236,238,3,138,69,0,237,239,3,54,27,0,238,237,1,0,0,0,238,239,1,0,0,0,
  	239,245,1,0,0,0,240,243,5,30,0,0,241,244,5,23,0,0,242,244,3,56,28,0,243,
  	241,1,0,0,0,243,242,1,0,0,0,244,246,1,0,0,0,245,240,1,0,0,0,245,246,1,
  	0,0,0,246,17,1,0,0,0,247,248,5,55,0,0,248,249,3,26,13,0,249,19,1,0,0,
  	0,250,251,5,59,0,0,251,253,3,26,13,0,252,254,3,74,37,0,253,252,1,0,0,
  	0,253,254,1,0,0,0,254,21,1,0,0,0,255,256,5,57,0,0,256,257,3,78,39,0,257,
  	23,1,0,0,0,258,259,5,48,0,0,259,260,3,78,39,0,260,25,1,0,0,0,261,263,
  	5,65,0,0,262,261,1,0,0,0,262,263,1,0,0,0,263,264,1,0,0,0,264,266,3,28,
  	14,0,265,267,3,34,17,0,266,265,1,0,0,0,266,267,1,0,0,0,267,269,1,0,0,
  	0,268,270,3,22,11,0,269,268,1,0,0,0,269,270,1,0,0,0,270,272,1,0,0,0,271,
  	273,3,24,12,0,272,271,1,0,0,0,272,273,1,0,0,0,273,27,1,0,0,0,274,277,
  	5,23,0,0,275,277,3,30,15,0,276,274,1,0,0,0,276,275,1,0,0,0,277,282,1,
  	0,0,0,278,279,5,11,0,0,279,281,3,30,15,0,280,278,1,0,0,0,281,284,1,0,
  	0,0,282,280,1,0,0,0,282,283,1,0,0,0,283,29,1,0,0,0,284,282,1,0,0,0,285,
  	288,3,78,39,0,286,287,5,63,0,0,287,289,3,182,91,0,288,286,1,0,0,0,288,
  	289,1,0,0,0,289,31,1,0,0,0,290,292,3,78,39,0,291,293,7,0,0,0,292,291,
  	1,0,0,0,292,293,1,0,0,0,293,33,1,0,0,0,294,295,5,53,0,0,295,296,5,40,
  	0,0,296,301,3,32,16,0,297,298,5,11,0,0,298,300,3,32,16,0,299,297,1,0,
  	0,0,300,303,1,0,0,0,301,299,1,0,0,0,301,302,1,0,0,0,302,35,1,0,0,0,303,
  	301,1,0,0,0,304,306,5,52,0,0,305,304,1,0,0,0,305,306,1,0,0,0,306,307,
  	1,0,0,0,307,308,5,49,0,0,308,309,3,72,36,0,309,37,1,0,0,0,310,311,5,61,
  	0,0,311,312,3,78,39,0,312,313,5,63,0,0,313,314,3,182,91,0,314,39,1,0,
  	0,0,315,319,3,36,18,0,316,319,3,38,19,0,317,319,3,52,26,0,318,315,1,0,
  	0,0,318,316,1,0,0,0,318,317,1,0,0,0,319,41,1,0,0,0,320,326,3,70,35,0,
  	321,326,3,60,30,0,322,326,3,44,22,0,323,326,3,64,32,0,324,326,3,46,23,
  	0,325,320,1,0,0,0,325,321,1,0,0,0,325,322,1,0,0,0,325,323,1,0,0,0,325,
  	324,1,0,0,0,326,43,1,0,0,0,327,329,5,45,0,0,328,327,1,0,0,0,328,329,1,
  	0,0,0,329,330,1,0,0,0,330,331,5,42,0,0,331,332,3,156,78,0,332,45,1,0,
  	0,0,333,334,5,54,0,0,334,339,3,50,25,0,335,336,5,11,0,0,336,338,3,50,
  	25,0,337,335,1,0,0,0,338,341,1,0,0,0,339,337,1,0,0,0,339,340,1,0,0,0,
  	340,47,1,0,0,0,341,339,1,0,0,0,342,343,5,91,0,0,343,344,5,12,0,0,344,
  	345,3,182,91,0,345,346,5,67,0,0,346,347,3,78,39,0,347,349,5,27,0,0,348,
  	350,3,12,6,0,349,348,1,0,0,0,350,351,1,0,0,0,351,349,1,0,0,0,351,352,
  	1,0,0,0,352,353,1,0,0,0,353,354,5,13,0,0,354,49,1,0,0,0,355,356,3,182,
  	91,0,356,357,3,68,34,0,357,360,1,0,0,0,358,360,3,110,55,0,359,355,1,0,
  	0,0,359,358,1,0,0,0,360,51,1,0,0,0,361,362,5,29,0,0,362,363,3,138,69,
  	0,363,366,3,54,27,0,364,365,5,30,0,0,365,367,3,56,28,0,366,364,1,0,0,
  	0,366,367,1,0,0,0,367,53,1,0,0,0,368,370,5,12,0,0,369,371,3,156,78,0,
  	370,369,1,0,0,0,370,371,1,0,0,0,371,372,1,0,0,0,372,373,5,13,0,0,373,
  	55,1,0,0,0,374,379,3,58,29,0,375,376,5,11,0,0,376,378,3,58,29,0,377,375,
  	1,0,0,0,378,381,1,0,0,0,379,377,1,0,0,0,379,380,1,0,0,0,380,383,1,0,0,
  	0,381,379,1,0,0,0,382,384,3,74,37,0,383,382,1,0,0,0,383,384,1,0,0,0,384,
  	57,1,0,0,0,385,386,3,182,91,0,386,387,5,63,0,0,387,389,1,0,0,0,388,385,
  	1,0,0,0,388,389,1,0,0,0,389,390,1,0,0,0,390,391,3,182,91,0,391,59,1,0,
  	0,0,392,393,5,50,0,0,393,397,3,114,57,0,394,396,3,62,31,0,395,394,1,0,
  	0,0,396,399,1,0,0,0,397,395,1,0,0,0,397,398,1,0,0,0,398,61,1,0,0,0,399,
  	397,1,0,0,0,400,401,5,51,0,0,401,402,7,1,0,0,402,403,3,64,32,0,403,63,
  	1,0,0,0,404,405,5,56,0,0,405,410,3,66,33,0,406,407,5,11,0,0,407,409,3,
  	66,33,0,408,406,1,0,0,0,409,412,1,0,0,0,410,408,1,0,0,0,410,411,1,0,0,
  	0,411,65,1,0,0,0,412,410,1,0,0,0,413,414,3,110,55,0,414,415,5,1,0,0,415,
  	416,3,78,39,0,416,425,1,0,0,0,417,418,3,182,91,0,418,419,7,2,0,0,419,
  	420,3,78,39,0,420,425,1,0,0,0,421,422,3,182,91,0,422,423,3,68,34,0,423,
  	425,1,0,0,0,424,413,1,0,0,0,424,417,1,0,0,0,424,421,1,0,0,0,425,67,1,
  	0,0,0,426,427,5,25,0,0,427,429,3,180,90,0,428,426,1,0,0,0,429,430,1,0,
  	0,0,430,428,1,0,0,0,430,431,1,0,0,0,431,69,1,0,0,0,432,433,5,41,0,0,433,
  	434,3,76,38,0,434,71,1,0,0,0,435,437,3,76,38,0,436,438,3,74,37,0,437,
  	436,1,0,0,0,437,438,1,0,0,0,438,73,1,0,0,0,439,440,5,58,0,0,440,441,3,
  	78,39,0,441,75,1,0,0,0,442,447,3,114,57,0,443,444,5,11,0,0,444,446,3,
  	114,57,0,445,443,1,0,0,0,446,449,1,0,0,0,447,445,1,0,0,0,447,448,1,0,
  	0,0,448,77,1,0,0,0,449,447,1,0,0,0,450,455,3,80,40,0,451,452,5,70,0,0,
  	452,454,3,80,40,0,453,451,1,0,0,0,454,457,1,0,0,0,455,453,1,0,0,0,455,
  	456,1,0,0,0,456,79,1,0,0,0,457,455,1,0,0,0,458,463,3,82,41,0,459,460,
  	5,72,0,0,460,462,3,82,41,0,461,459,1,0,0,0,462,465,1,0,0,0,463,461,1,
  	0,0,0,463,464,1,0,0,0,464,81,1,0,0,0,465,463,1,0,0,0,466,471,3,84,42,
  	0,467,468,5,62,0,0,468,470,3,84,42,0,469,467,1,0,0,0,470,473,1,0,0,0,
  	471,469,1,0,0,0,471,472,1,0,0,0,472,83,1,0,0,0,473,471,1,0,0,0,474,476,
  	5,69,0,0,475,474,1,0,0,0,476,479,1,0,0,0,477,475,1,0,0,0,477,478,1,0,
  	0,0,478,480,1,0,0,0,479,477,1,0,0,0,480,481,3,86,43,0,481,85,1,0,0,0,
  	482,488,3,90,45,0,483,484,3,88,44,0,484,485,3,90,45,0,485,487,1,0,0,0,
  	486,483,1,0,0,0,487,490,1,0,0,0,488,486,1,0,0,0,488,489,1,0,0,0,489,87,
  	1,0,0,0,490,488,1,0,0,0,491,492,7,3,0,0,492,89,1,0,0,0,493,498,3,92,46,
  	0,494,495,7,4,0,0,495,497,3,92,46,0,496,494,1,0,0,0,497,500,1,0,0,0,498,
  	496,1,0,0,0,498,499,1,0,0,0,499,91,1,0,0,0,500,498,1,0,0,0,501,506,3,
  	94,47,0,502,503,7,5,0,0,503,505,3,94,47,0,504,502,1,0,0,0,505,508,1,0,
  	0,0,506,504,1,0,0,0,506,507,1,0,0,0,507,93,1,0,0,0,508,506,1,0,0,0,509,
  	514,3,96,48,0,510,511,5,22,0,0,511,513,3,96,48,0,512,510,1,0,0,0,513,
  	516,1,0,0,0,514,512,1,0,0,0,514,515,1,0,0,0,515,95,1,0,0,0,516,514,1,
  	0,0,0,517,519,7,4,0,0,518,517,1,0,0,0,518,519,1,0,0,0,519,520,1,0,0,0,
  	520,521,3,98,49,0,521,97,1,0,0,0,522,528,3,108,54,0,523,527,3,102,51,
  	0,524,527,3,100,50,0,525,527,3,106,53,0,526,523,1,0,0,0,526,524,1,0,0,
  	0,526,525,1,0,0,0,527,530,1,0,0,0,528,526,1,0,0,0,528,529,1,0,0,0,529,
  	99,1,0,0,0,530,528,1,0,0,0,531,532,5,67,0,0,532,546,3,108,54,0,533,542,
  	5,16,0,0,534,536,3,78,39,0,535,534,1,0,0,0,535,536,1,0,0,0,536,537,1,
  	0,0,0,537,539,5,8,0,0,538,540,3,78,39,0,539,538,1,0,0,0,539,540,1,0,0,
  	0,540,543,1,0,0,0,541,543,3,78,39,0,542,535,1,0,0,0,542,541,1,0,0,0,543,
  	544,1,0,0,0,544,546,5,17,0,0,545,531,1,0,0,0,545,533,1,0,0,0,546,101,
  	1,0,0,0,547,548,3,104,52,0,548,549,3,108,54,0,549,103,1,0,0,0,550,551,
  	5,71,0,0,551,556,5,59,0,0,552,553,5,66,0,0,553,556,5,59,0,0,554,556,5,
  	64,0,0,555,550,1,0,0,0,555,552,1,0,0,0,555,554,1,0,0,0,556,105,1,0,0,
  	0,557,559,5,68,0,0,558,560,5,69,0,0,559,558,1,0,0,0,559,560,1,0,0,0,560,
  	561,1,0,0,0,561,562,5,75,0,0,562,107,1,0,0,0,563,565,3,110,55,0,564,566,
  	3,68,34,0,565,564,1,0,0,0,565,566,1,0,0,0,566,109,1,0,0,0,567,572,3,124,
  	62,0,568,569,5,10,0,0,569,571,3,180,90,0,570,568,1,0,0,0,571,574,1,0,
  	0,0,572,570,1,0,0,0,572,573,1,0,0,0,573,577,1,0,0,0,574,572,1,0,0,0,575,
  	576,5,26,0,0,576,578,3,112,56,0,577,575,1,0,0,0,577,578,1,0,0,0,578,111,
  	1,0,0,0,579,584,3,180,90,0,580,581,5,10,0,0,581,583,3,180,90,0,582,580,
  	1,0,0,0,583,586,1,0,0,0,584,582,1,0,0,0,584,585,1,0,0,0,585,113,1,0,0,
  	0,586,584,1,0,0,0,587,588,3,182,91,0,588,589,5,1,0,0,589,591,1,0,0,0,
  	590,587,1,0,0,0,590,591,1,0,0,0,591,592,1,0,0,0,592,593,3,116,58,0,593,
  	115,1,0,0,0,594,598,3,122,61,0,595,597,3,118,59,0,596,595,1,0,0,0,597,
  	600,1,0,0,0,598,596,1,0,0,0,598,599,1,0,0,0,599,606,1,0,0,0,600,598,1,
  	0,0,0,601,602,5,12,0,0,602,603,3,116,58,0,603,604,5,13,0,0,604,606,1,
  	0,0,0,605,594,1,0,0,0,605,601,1,0,0,0,606,117,1,0,0,0,607,608,3,128,64,
  	0,608,609,3,122,61,0,609,119,1,0,0,0,610,613,3,176,88,0,611,613,3,160,
  	80,0,612,610,1,0,0,0,612,611,1,0,0,0,613,121,1,0,0,0,614,616,5,12,0,0,
  	615,617,3,182,91,0,616,615,1,0,0,0,616,617,1,0,0,0,617,619,1,0,0,0,618,
  	620,3,68,34,0,619,618,1,0,0,0,619,620,1,0,0,0,620,622,1,0,0,0,621,623,
  	3,120,60,0,622,621,1,0,0,0,622,623,1,0,0,0,623,624,1,0,0,0,624,625,5,
  	13,0,0,625,123,1,0,0,0,626,639,3,162,81,0,627,639,3,160,80,0,628,639,
  	3,158,79,0,629,639,3,154,77,0,630,639,3,150,75,0,631,639,3,146,73,0,632,
  	639,3,144,72,0,633,639,3,148,74,0,634,639,3,142,71,0,635,639,3,140,70,
  	0,636,639,3,182,91,0,637,639,3,136,68,0,638,626,1,0,0,0,638,627,1,0,0,
  	0,638,628,1,0,0,0,638,629,1,0,0,0,638,630,1,0,0,0,638,631,1,0,0,0,638,
  	632,1,0,0,0,638,633,1,0,0,0,638,634,1,0,0,0,638,635,1,0,0,0,638,636,1,
  	0,0,0,638,637,1,0,0,0,639,125,1,0,0,0,640,641,3,182,91,0,641,642,5,1,
  	0,0,642,127,1,0,0,0,643,644,5,6,0,0,644,646,5,18,0,0,645,647,3,130,65,
  	0,646,645,1,0,0,0,646,647,1,0,0,0,647,648,1,0,0,0,648,650,5,18,0,0,649,
  	651,5,5,0,0,650,649,1,0,0,0,650,651,1,0,0,0,651,661,1,0,0,0,652,654,5,
  	18,0,0,653,655,3,130,65,0,654,653,1,0,0,0,654,655,1,0,0,0,655,656,1,0,
  	0,0,656,658,5,18,0,0,657,659,5,5,0,0,658,657,1,0,0,0,658,659,1,0,0,0,
  	659,661,1,0,0,0,660,643,1,0,0,0,660,652,1,0,0,0,661,129,1,0,0,0,662,664,
  	5,16,0,0,663,665,3,182,91,0,664,663,1,0,0,0,664,665,1,0,0,0,665,667,1,
  	0,0,0,666,668,3,132,66,0,667,666,1,0,0,0,667,668,1,0,0,0,668,670,1,0,
  	0,0,669,671,3,164,82,0,670,669,1,0,0,0,670,671,1,0,0,0,671,673,1,0,0,
  	0,672,674,3,120,60,0,673,672,1,0,0,0,673,674,1,0,0,0,674,675,1,0,0,0,
  	675,676,5,17,0,0,676,131,1,0,0,0,677,678,5,25,0,0,678,686,3,180,90,0,
  	679,681,5,27,0,0,680,682,5,25,0,0,681,680,1,0,0,0,681,682,1,0,0,0,682,
  	683,1,0,0,0,683,685,3,180,90,0,684,679,1,0,0,0,685,688,1,0,0,0,686,684,
  	1,0,0,0,686,687,1,0,0,0,687,133,1,0,0,0,688,686,1,0,0,0,689,691,5,60,
  	0,0,690,692,5,37,0,0,691,690,1,0,0,0,691,692,1,0,0,0,692,693,1,0,0,0,
  	693,694,3,6,3,0,694,135,1,0,0,0,695,696,5,46,0,0,696,699,5,14,0,0,697,
  	700,3,4,2,0,698,700,3,72,36,0,699,697,1,0,0,0,699,698,1,0,0,0,700,701,
  	1,0,0,0,701,702,5,15,0,0,702,137,1,0,0,0,703,708,3,182,91,0,704,705,5,
  	10,0,0,705,707,3,182,91,0,706,704,1,0,0,0,707,710,1,0,0,0,708,706,1,0,
  	0,0,708,709,1,0,0,0,709,139,1,0,0,0,710,708,1,0,0,0,711,712,3,138,69,
  	0,712,714,5,12,0,0,713,715,5,65,0,0,714,713,1,0,0,0,714,715,1,0,0,0,715,
  	717,1,0,0,0,716,718,3,156,78,0,717,716,1,0,0,0,717,718,1,0,0,0,718,719,
  	1,0,0,0,719,720,5,13,0,0,720,141,1,0,0,0,721,722,5,12,0,0,722,723,3,78,
  	39,0,723,724,5,13,0,0,724,143,1,0,0,0,725,726,7,6,0,0,726,727,5,12,0,
  	0,727,728,3,152,76,0,728,729,5,13,0,0,729,145,1,0,0,0,730,732,5,16,0,
  	0,731,733,3,126,63,0,732,731,1,0,0,0,732,733,1,0,0,0,733,734,1,0,0,0,
  	734,736,3,148,74,0,735,737,3,74,37,0,736,735,1,0,0,0,736,737,1,0,0,0,
  	737,738,1,0,0,0,738,739,5,27,0,0,739,740,3,78,39,0,740,741,5,17,0,0,741,
  	147,1,0,0,0,742,744,3,122,61,0,743,745,3,118,59,0,744,743,1,0,0,0,745,
  	746,1,0,0,0,746,744,1,0,0,0,746,747,1,0,0,0,747,149,1,0,0,0,748,749,5,
  	16,0,0,749,752,3,152,76,0,750,751,5,27,0,0,751,753,3,78,39,0,752,750,
  	1,0,0,0,752,753,1,0,0,0,753,754,1,0,0,0,754,755,5,17,0,0,755,151,1,0,
  	0,0,756,757,3,182,91,0,757,758,5,67,0,0,758,760,3,78,39,0,759,761,3,74,
  	37,0,760,759,1,0,0,0,760,761,1,0,0,0,761,153,1,0,0,0,762,763,5,33,0,0,
  	763,764,5,12,0,0,764,765,5,23,0,0,765,766,5,13,0,0,766,155,1,0,0,0,767,
  	772,3,78,39,0,768,769,5,11,0,0,769,771,3,78,39,0,770,768,1,0,0,0,771,
  	774,1,0,0,0,772,770,1,0,0,0,772,773,1,0,0,0,773,157,1,0,0,0,774,772,1,
  	0,0,0,775,777,5,81,0,0,776,778,3,78,39,0,777,776,1,0,0,0,777,778,1,0,
  	0,0,778,784,1,0,0,0,779,780,5,82,0,0,780,781,3,78,39,0,781,782,5,83,0,
  	0,782,783,3,78,39,0,783,785,1,0,0,0,784,779,1,0,0,0,785,786,1,0,0,0,786,
  	784,1,0,0,0,786,787,1,0,0,0,787,790,1,0,0,0,788,789,5,84,0,0,789,791,
  	3,78,39,0,790,788,1,0,0,0,790,791,1,0,0,0,791,792,1,0,0,0,792,793,5,85,
  	0,0,793,159,1,0,0,0,794,797,5,28,0,0,795,798,3,182,91,0,796,798,3,168,
  	84,0,797,795,1,0,0,0,797,796,1,0,0,0,798,161,1,0,0,0,799,807,3,166,83,
  	0,800,807,3,168,84,0,801,807,5,75,0,0,802,807,3,170,85,0,803,807,3,172,
  	86,0,804,807,3,174,87,0,805,807,3,176,88,0,806,799,1,0,0,0,806,800,1,
  	0,0,0,806,801,1,0,0,0,806,802,1,0,0,0,806,803,1,0,0,0,806,804,1,0,0,0,
  	806,805,1,0,0,0,807,163,1,0,0,0,808,811,5,23,0,0,809,812,3,168,84,0,810,
  	812,5,92,0,0,811,809,1,0,0,0,811,810,1,0,0,0,811,812,1,0,0,0,812,818,
  	1,0,0,0,813,816,5,8,0,0,814,817,3,168,84,0,815,817,5,92,0,0,816,814,1,
  	0,0,0,816,815,1,0,0,0,816,817,1,0,0,0,817,819,1,0,0,0,818,813,1,0,0,0,
  	818,819,1,0,0,0,819,165,1,0,0,0,820,821,7,7,0,0,821,167,1,0,0,0,822,823,
  	5,96,0,0,823,169,1,0,0,0,824,825,5,95,0,0,825,171,1,0,0,0,826,827,5,94,
  	0,0,827,173,1,0,0,0,828,830,5,16,0,0,829,831,3,156,78,0,830,829,1,0,0,
  	0,830,831,1,0,0,0,831,832,1,0,0,0,832,833,5,17,0,0,833,175,1,0,0,0,834,
  	843,5,14,0,0,835,840,3,178,89,0,836,837,5,11,0,0,837,839,3,178,89,0,838,
  	836,1,0,0,0,839,842,1,0,0,0,840,838,1,0,0,0,840,841,1,0,0,0,841,844,1,
  	0,0,0,842,840,1,0,0,0,843,835,1,0,0,0,843,844,1,0,0,0,844,845,1,0,0,0,
  	845,846,5,15,0,0,846,177,1,0,0,0,847,848,3,180,90,0,848,849,5,25,0,0,
  	849,850,3,78,39,0,850,179,1,0,0,0,851,854,3,182,91,0,852,854,3,184,92,
  	0,853,851,1,0,0,0,853,852,1,0,0,0,854,181,1,0,0,0,855,856,7,8,0,0,856,
  	183,1,0,0,0,857,858,7,9,0,0,858,185,1,0,0,0,103,188,193,197,203,209,213,
  	218,223,231,238,243,245,253,262,266,269,272,276,282,288,292,301,305,318,
  	325,328,339,351,359,366,370,379,383,388,397,410,424,430,437,447,455,463,
  	471,477,488,498,506,514,518,526,528,535,539,542,545,555,559,565,572,577,
  	584,590,598,605,612,616,619,622,638,646,650,654,658,660,664,667,670,673,
  	681,686,691,699,708,714,717,732,736,746,752,760,772,777,786,790,797,806,
  	811,816,818,830,840,843,853
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
    setState(186);
    query();
    setState(188);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SEMI) {
      setState(187);
      match(CypherParser::SEMI);
    }
    setState(190);
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
    setState(197);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 2, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(193);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::EXPLAIN) {
        setState(192);
        match(CypherParser::EXPLAIN);
      }
      setState(195);
      regularQuery();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(196);
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
    setState(199);
    singleQuery();
    setState(203);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::UNION) {
      setState(200);
      unionSt();
      setState(205);
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
    setState(209);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 29) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 29)) & 4611686023975481345) != 0)) {
      setState(206);
      clause();
      setState(211);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(213);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RETURN) {
      setState(212);
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
    setState(218);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL:
      case CypherParser::WITH:
      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 1);
        setState(215);
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
        setState(216);
        updatingClause();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(217);
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
    setState(223);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(220);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(221);
        unwindSt();
        break;
      }

      case CypherParser::WITH: {
        enterOuterAlt(_localctx, 3);
        setState(222);
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
    setState(231);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(225);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(226);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(227);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(228);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(229);
        removeSt();
        break;
      }

      case CypherParser::FOREACH: {
        enterOuterAlt(_localctx, 6);
        setState(230);
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
    setState(233);
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
    setState(235);
    match(CypherParser::CALL);
    setState(236);
    invocationName();
    setState(238);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LPAREN) {
      setState(237);
      parenExpressionChain();
    }
    setState(245);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(240);
      match(CypherParser::YIELD);
      setState(243);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::MULT: {
          setState(241);
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
          setState(242);
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
    setState(247);
    match(CypherParser::RETURN);
    setState(248);
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
    setState(250);
    match(CypherParser::WITH);
    setState(251);
    projectionBody();
    setState(253);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(252);
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
    setState(255);
    match(CypherParser::SKIP_W);
    setState(256);
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
    setState(258);
    match(CypherParser::LIMIT);
    setState(259);
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
    setState(262);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(261);
      match(CypherParser::DISTINCT);
    }
    setState(264);
    projectionItems();
    setState(266);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ORDER) {
      setState(265);
      orderSt();
    }
    setState(269);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SKIP_W) {
      setState(268);
      skipSt();
    }
    setState(272);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LIMIT) {
      setState(271);
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
    setState(276);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MULT: {
        setState(274);
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
      case CypherParser::FOREACH:
      case CypherParser::ID:
      case CypherParser::ESC_LITERAL:
      case CypherParser::CHAR_LITERAL:
      case CypherParser::STRING_LITERAL:
      case CypherParser::DIGIT: {
        setState(275);
        projectionItem();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(282);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(278);
      match(CypherParser::COMMA);
      setState(279);
      projectionItem();
      setState(284);
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
    setState(285);
    expression();
    setState(288);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::AS) {
      setState(286);
      match(CypherParser::AS);
      setState(287);
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
    setState(290);
    expression();
    setState(292);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 27212912787456) != 0)) {
      setState(291);
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
    setState(294);
    match(CypherParser::ORDER);
    setState(295);
    match(CypherParser::BY);
    setState(296);
    orderItem();
    setState(301);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(297);
      match(CypherParser::COMMA);
      setState(298);
      orderItem();
      setState(303);
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
    setState(305);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::OPTIONAL) {
      setState(304);
      match(CypherParser::OPTIONAL);
    }
    setState(307);
    match(CypherParser::MATCH);
    setState(308);
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
    setState(310);
    match(CypherParser::UNWIND);
    setState(311);
    expression();
    setState(312);
    match(CypherParser::AS);
    setState(313);
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
    setState(318);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::MATCH:
      case CypherParser::OPTIONAL: {
        enterOuterAlt(_localctx, 1);
        setState(315);
        matchSt();
        break;
      }

      case CypherParser::UNWIND: {
        enterOuterAlt(_localctx, 2);
        setState(316);
        unwindSt();
        break;
      }

      case CypherParser::CALL: {
        enterOuterAlt(_localctx, 3);
        setState(317);
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
    setState(325);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::CREATE: {
        enterOuterAlt(_localctx, 1);
        setState(320);
        createSt();
        break;
      }

      case CypherParser::MERGE: {
        enterOuterAlt(_localctx, 2);
        setState(321);
        mergeSt();
        break;
      }

      case CypherParser::DELETE:
      case CypherParser::DETACH: {
        enterOuterAlt(_localctx, 3);
        setState(322);
        deleteSt();
        break;
      }

      case CypherParser::SET: {
        enterOuterAlt(_localctx, 4);
        setState(323);
        setSt();
        break;
      }

      case CypherParser::REMOVE: {
        enterOuterAlt(_localctx, 5);
        setState(324);
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
    setState(328);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DETACH) {
      setState(327);
      match(CypherParser::DETACH);
    }
    setState(330);
    match(CypherParser::DELETE);
    setState(331);
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
    setState(333);
    match(CypherParser::REMOVE);
    setState(334);
    removeItem();
    setState(339);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(335);
      match(CypherParser::COMMA);
      setState(336);
      removeItem();
      setState(341);
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
    setState(342);
    match(CypherParser::FOREACH);
    setState(343);
    match(CypherParser::LPAREN);
    setState(344);
    symbol();
    setState(345);
    match(CypherParser::IN);
    setState(346);
    expression();
    setState(347);
    match(CypherParser::STICK);
    setState(349); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(348);
      updatingClause();
      setState(351); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (((((_la - 41) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 41)) & 1125899906884115) != 0));
    setState(353);
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
    setState(359);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(355);
      symbol();
      setState(356);
      nodeLabels();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(358);
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
    setState(361);
    match(CypherParser::CALL);
    setState(362);
    invocationName();
    setState(363);
    parenExpressionChain();
    setState(366);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::YIELD) {
      setState(364);
      match(CypherParser::YIELD);
      setState(365);
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
    setState(368);
    match(CypherParser::LPAREN);
    setState(370);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 264245361) != 0)) {
      setState(369);
      expressionChain();
    }
    setState(372);
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
    setState(374);
    yieldItem();
    setState(379);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(375);
      match(CypherParser::COMMA);
      setState(376);
      yieldItem();
      setState(381);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(383);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(382);
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
    setState(388);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx)) {
    case 1: {
      setState(385);
      symbol();
      setState(386);
      match(CypherParser::AS);
      break;
    }

    default:
      break;
    }
    setState(390);
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
    setState(392);
    match(CypherParser::MERGE);
    setState(393);
    patternPart();
    setState(397);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::ON) {
      setState(394);
      mergeAction();
      setState(399);
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
    setState(400);
    match(CypherParser::ON);
    setState(401);
    _la = _input->LA(1);
    if (!(_la == CypherParser::CREATE

    || _la == CypherParser::MATCH)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(402);
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
    setState(404);
    match(CypherParser::SET);
    setState(405);
    setItem();
    setState(410);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(406);
      match(CypherParser::COMMA);
      setState(407);
      setItem();
      setState(412);
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
    setState(424);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(413);
      propertyExpression();
      setState(414);
      match(CypherParser::ASSIGN);
      setState(415);
      expression();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(417);
      symbol();
      setState(418);
      _la = _input->LA(1);
      if (!(_la == CypherParser::ASSIGN

      || _la == CypherParser::ADD_ASSIGN)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(419);
      expression();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(421);
      symbol();
      setState(422);
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
    setState(428); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(426);
      match(CypherParser::COLON);
      setState(427);
      name();
      setState(430); 
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
    setState(432);
    match(CypherParser::CREATE);
    setState(433);
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
    setState(435);
    pattern();
    setState(437);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(436);
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
    setState(439);
    match(CypherParser::WHERE);
    setState(440);
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
    setState(442);
    patternPart();
    setState(447);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(443);
      match(CypherParser::COMMA);
      setState(444);
      patternPart();
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
    setState(450);
    xorExpression();
    setState(455);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::OR) {
      setState(451);
      match(CypherParser::OR);
      setState(452);
      xorExpression();
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
    setState(458);
    andExpression();
    setState(463);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::XOR) {
      setState(459);
      match(CypherParser::XOR);
      setState(460);
      andExpression();
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
    setState(466);
    notExpression();
    setState(471);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::AND) {
      setState(467);
      match(CypherParser::AND);
      setState(468);
      notExpression();
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
    setState(477);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::NOT) {
      setState(474);
      match(CypherParser::NOT);
      setState(479);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(480);
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
    setState(482);
    addSubExpression();
    setState(488);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 250) != 0)) {
      setState(483);
      comparisonSigns();
      setState(484);
      addSubExpression();
      setState(490);
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
    setState(491);
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
    setState(493);
    multDivExpression();
    setState(498);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(494);
      _la = _input->LA(1);
      if (!(_la == CypherParser::SUB

      || _la == CypherParser::PLUS)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(495);
      multDivExpression();
      setState(500);
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
    setState(501);
    powerExpression();
    setState(506);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 11534336) != 0)) {
      setState(502);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 11534336) != 0))) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(503);
      powerExpression();
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
    setState(509);
    unaryAddSubExpression();
    setState(514);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::CARET) {
      setState(510);
      match(CypherParser::CARET);
      setState(511);
      unaryAddSubExpression();
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
    setState(518);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::SUB

    || _la == CypherParser::PLUS) {
      setState(517);
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
    setState(520);
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
    setState(522);
    propertyOrLabelExpression();
    setState(528);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (((((_la - 16) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 16)) & 44191571343572993) != 0)) {
      setState(526);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::CONTAINS:
        case CypherParser::ENDS:
        case CypherParser::STARTS: {
          setState(523);
          stringExpression();
          break;
        }

        case CypherParser::LBRACK:
        case CypherParser::IN: {
          setState(524);
          listExpression();
          break;
        }

        case CypherParser::IS: {
          setState(525);
          nullExpression();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(530);
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
    setState(545);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::IN: {
        enterOuterAlt(_localctx, 1);
        setState(531);
        match(CypherParser::IN);
        setState(532);
        propertyOrLabelExpression();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 2);
        setState(533);
        match(CypherParser::LBRACK);
        setState(542);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 53, _ctx)) {
        case 1: {
          setState(535);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 69)) & 264245361) != 0)) {
            setState(534);
            expression();
          }
          setState(537);
          match(CypherParser::RANGE);
          setState(539);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
            ((1ULL << (_la - 69)) & 264245361) != 0)) {
            setState(538);
            expression();
          }
          break;
        }

        case 2: {
          setState(541);
          expression();
          break;
        }

        default:
          break;
        }
        setState(544);
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
    setState(547);
    stringExpPrefix();
    setState(548);
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
    setState(555);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::STARTS: {
        enterOuterAlt(_localctx, 1);
        setState(550);
        match(CypherParser::STARTS);
        setState(551);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::ENDS: {
        enterOuterAlt(_localctx, 2);
        setState(552);
        match(CypherParser::ENDS);
        setState(553);
        match(CypherParser::WITH);
        break;
      }

      case CypherParser::CONTAINS: {
        enterOuterAlt(_localctx, 3);
        setState(554);
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
    setState(557);
    match(CypherParser::IS);
    setState(559);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::NOT) {
      setState(558);
      match(CypherParser::NOT);
    }
    setState(561);
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
    setState(563);
    propertyExpression();
    setState(565);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(564);
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
    setState(567);
    atom();
    setState(572);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(568);
      match(CypherParser::DOT);
      setState(569);
      name();
      setState(574);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(577);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLONCOLON) {
      setState(575);
      match(CypherParser::COLONCOLON);
      setState(576);
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
    setState(579);
    name();
    setState(584);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(580);
      match(CypherParser::DOT);
      setState(581);
      name();
      setState(586);
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
    setState(590);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 8070450532247928895) != 0)) {
      setState(587);
      symbol();
      setState(588);
      match(CypherParser::ASSIGN);
    }
    setState(592);
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
  enterRule(_localctx, 116, CypherParser::RulePatternElem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(605);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 63, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(594);
      nodePattern();
      setState(598);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::LT

      || _la == CypherParser::SUB) {
        setState(595);
        patternElemChain();
        setState(600);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(601);
      match(CypherParser::LPAREN);
      setState(602);
      patternElem();
      setState(603);
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
  enterRule(_localctx, 118, CypherParser::RulePatternElemChain);

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
    relationshipPattern();
    setState(608);
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
  enterRule(_localctx, 120, CypherParser::RuleProperties);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(612);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 1);
        setState(610);
        mapLit();
        break;
      }

      case CypherParser::DOLLAR: {
        enterOuterAlt(_localctx, 2);
        setState(611);
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
  enterRule(_localctx, 122, CypherParser::RuleNodePattern);
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
    setState(614);
    match(CypherParser::LPAREN);
    setState(616);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 8070450532247928895) != 0)) {
      setState(615);
      symbol();
    }
    setState(619);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(618);
      nodeLabels();
    }
    setState(622);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(621);
      properties();
    }
    setState(624);
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
  enterRule(_localctx, 124, CypherParser::RuleAtom);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(638);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 68, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(626);
      literal();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(627);
      parameter();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(628);
      caseExpression();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(629);
      countAll();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(630);
      listComprehension();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(631);
      patternComprehension();
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(632);
      filterWith();
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(633);
      relationshipsChainPattern();
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(634);
      parenthesizedExpression();
      break;
    }

    case 10: {
      enterOuterAlt(_localctx, 10);
      setState(635);
      functionInvocation();
      break;
    }

    case 11: {
      enterOuterAlt(_localctx, 11);
      setState(636);
      symbol();
      break;
    }

    case 12: {
      enterOuterAlt(_localctx, 12);
      setState(637);
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
  enterRule(_localctx, 126, CypherParser::RuleLhs);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(640);
    symbol();
    setState(641);
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
  enterRule(_localctx, 128, CypherParser::RuleRelationshipPattern);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(660);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::LT: {
        enterOuterAlt(_localctx, 1);
        setState(643);
        match(CypherParser::LT);
        setState(644);
        match(CypherParser::SUB);
        setState(646);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(645);
          relationDetail();
        }
        setState(648);
        match(CypherParser::SUB);
        setState(650);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(649);
          match(CypherParser::GT);
        }
        break;
      }

      case CypherParser::SUB: {
        enterOuterAlt(_localctx, 2);
        setState(652);
        match(CypherParser::SUB);
        setState(654);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::LBRACK) {
          setState(653);
          relationDetail();
        }
        setState(656);
        match(CypherParser::SUB);
        setState(658);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CypherParser::GT) {
          setState(657);
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
  enterRule(_localctx, 130, CypherParser::RuleRelationDetail);
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
    setState(662);
    match(CypherParser::LBRACK);
    setState(664);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 8070450532247928895) != 0)) {
      setState(663);
      symbol();
    }
    setState(667);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::COLON) {
      setState(666);
      relationshipTypes();
    }
    setState(670);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::MULT) {
      setState(669);
      rangeLit();
    }
    setState(673);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::LBRACE

    || _la == CypherParser::DOLLAR) {
      setState(672);
      properties();
    }
    setState(675);
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
  enterRule(_localctx, 132, CypherParser::RuleRelationshipTypes);
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
    setState(677);
    match(CypherParser::COLON);
    setState(678);
    name();
    setState(686);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::STICK) {
      setState(679);
      match(CypherParser::STICK);
      setState(681);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CypherParser::COLON) {
        setState(680);
        match(CypherParser::COLON);
      }
      setState(683);
      name();
      setState(688);
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
  enterRule(_localctx, 134, CypherParser::RuleUnionSt);
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
    setState(689);
    match(CypherParser::UNION);
    setState(691);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ALL) {
      setState(690);
      match(CypherParser::ALL);
    }
    setState(693);
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
  enterRule(_localctx, 136, CypherParser::RuleSubqueryExist);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(695);
    match(CypherParser::EXISTS);
    setState(696);
    match(CypherParser::LBRACE);
    setState(699);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 81, _ctx)) {
    case 1: {
      setState(697);
      regularQuery();
      break;
    }

    case 2: {
      setState(698);
      patternWhere();
      break;
    }

    default:
      break;
    }
    setState(701);
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
  enterRule(_localctx, 138, CypherParser::RuleInvocationName);
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
    setState(703);
    symbol();
    setState(708);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::DOT) {
      setState(704);
      match(CypherParser::DOT);
      setState(705);
      symbol();
      setState(710);
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
  enterRule(_localctx, 140, CypherParser::RuleFunctionInvocation);
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
    setState(711);
    invocationName();
    setState(712);
    match(CypherParser::LPAREN);
    setState(714);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::DISTINCT) {
      setState(713);
      match(CypherParser::DISTINCT);
    }
    setState(717);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 264245361) != 0)) {
      setState(716);
      expressionChain();
    }
    setState(719);
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
  enterRule(_localctx, 142, CypherParser::RuleParenthesizedExpression);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(721);
    match(CypherParser::LPAREN);
    setState(722);
    expression();
    setState(723);
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
  enterRule(_localctx, 144, CypherParser::RuleFilterWith);
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
    setState(725);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 257698037760) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
    setState(726);
    match(CypherParser::LPAREN);
    setState(727);
    filterExpression();
    setState(728);
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
  enterRule(_localctx, 146, CypherParser::RulePatternComprehension);
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
    setState(730);
    match(CypherParser::LBRACK);
    setState(732);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 8070450532247928895) != 0)) {
      setState(731);
      lhs();
    }
    setState(734);
    relationshipsChainPattern();
    setState(736);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(735);
      where();
    }
    setState(738);
    match(CypherParser::STICK);
    setState(739);
    expression();
    setState(740);
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
  enterRule(_localctx, 148, CypherParser::RuleRelationshipsChainPattern);

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
    setState(742);
    nodePattern();
    setState(744); 
    _errHandler->sync(this);
    alt = 1;
    do {
      switch (alt) {
        case 1: {
              setState(743);
              patternElemChain();
              break;
            }

      default:
        throw NoViableAltException(this);
      }
      setState(746); 
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 87, _ctx);
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
  enterRule(_localctx, 150, CypherParser::RuleListComprehension);
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
    setState(748);
    match(CypherParser::LBRACK);
    setState(749);
    filterExpression();
    setState(752);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::STICK) {
      setState(750);
      match(CypherParser::STICK);
      setState(751);
      expression();
    }
    setState(754);
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
  enterRule(_localctx, 152, CypherParser::RuleFilterExpression);
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
    symbol();
    setState(757);
    match(CypherParser::IN);
    setState(758);
    expression();
    setState(760);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::WHERE) {
      setState(759);
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
  enterRule(_localctx, 154, CypherParser::RuleCountAll);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(762);
    match(CypherParser::COUNT);
    setState(763);
    match(CypherParser::LPAREN);
    setState(764);
    match(CypherParser::MULT);
    setState(765);
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
  enterRule(_localctx, 156, CypherParser::RuleExpressionChain);
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
    setState(767);
    expression();
    setState(772);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CypherParser::COMMA) {
      setState(768);
      match(CypherParser::COMMA);
      setState(769);
      expression();
      setState(774);
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
  enterRule(_localctx, 158, CypherParser::RuleCaseExpression);
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
    setState(775);
    match(CypherParser::CASE);
    setState(777);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 264245361) != 0)) {
      setState(776);
      expression();
    }
    setState(784); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(779);
      match(CypherParser::WHEN);
      setState(780);
      expression();
      setState(781);
      match(CypherParser::THEN);
      setState(782);
      expression();
      setState(786); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == CypherParser::WHEN);
    setState(790);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::ELSE) {
      setState(788);
      match(CypherParser::ELSE);
      setState(789);
      expression();
    }
    setState(792);
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
  enterRule(_localctx, 160, CypherParser::RuleParameter);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(794);
    match(CypherParser::DOLLAR);
    setState(797);
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
        setState(795);
        symbol();
        break;
      }

      case CypherParser::DIGIT: {
        setState(796);
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
  enterRule(_localctx, 162, CypherParser::RuleLiteral);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(806);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::FALSE:
      case CypherParser::TRUE: {
        enterOuterAlt(_localctx, 1);
        setState(799);
        boolLit();
        break;
      }

      case CypherParser::DIGIT: {
        enterOuterAlt(_localctx, 2);
        setState(800);
        numLit();
        break;
      }

      case CypherParser::NULL_W: {
        enterOuterAlt(_localctx, 3);
        setState(801);
        match(CypherParser::NULL_W);
        break;
      }

      case CypherParser::STRING_LITERAL: {
        enterOuterAlt(_localctx, 4);
        setState(802);
        stringLit();
        break;
      }

      case CypherParser::CHAR_LITERAL: {
        enterOuterAlt(_localctx, 5);
        setState(803);
        charLit();
        break;
      }

      case CypherParser::LBRACK: {
        enterOuterAlt(_localctx, 6);
        setState(804);
        listLit();
        break;
      }

      case CypherParser::LBRACE: {
        enterOuterAlt(_localctx, 7);
        setState(805);
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
  enterRule(_localctx, 164, CypherParser::RuleRangeLit);
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
    match(CypherParser::MULT);
    setState(811);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CypherParser::DIGIT: {
        setState(809);
        numLit();
        break;
      }

      case CypherParser::ID: {
        setState(810);
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
    setState(818);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CypherParser::RANGE) {
      setState(813);
      match(CypherParser::RANGE);
      setState(816);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case CypherParser::DIGIT: {
          setState(814);
          numLit();
          break;
        }

        case CypherParser::ID: {
          setState(815);
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
  enterRule(_localctx, 166, CypherParser::RuleBoolLit);
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
    setState(820);
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
  enterRule(_localctx, 168, CypherParser::RuleNumLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(822);
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
  enterRule(_localctx, 170, CypherParser::RuleStringLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(824);
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
  enterRule(_localctx, 172, CypherParser::RuleCharLit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(826);
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
  enterRule(_localctx, 174, CypherParser::RuleListLit);
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
    setState(828);
    match(CypherParser::LBRACK);
    setState(830);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 70641743908864) != 0) || ((((_la - 69) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 69)) & 264245361) != 0)) {
      setState(829);
      expressionChain();
    }
    setState(832);
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
  enterRule(_localctx, 176, CypherParser::RuleMapLit);
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
    setState(834);
    match(CypherParser::LBRACE);
    setState(843);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 9223372036854710271) != 0)) {
      setState(835);
      mapPair();
      setState(840);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CypherParser::COMMA) {
        setState(836);
        match(CypherParser::COMMA);
        setState(837);
        mapPair();
        setState(842);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(845);
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
  enterRule(_localctx, 178, CypherParser::RuleMapPair);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(847);
    name();
    setState(848);
    match(CypherParser::COLON);
    setState(849);
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
  enterRule(_localctx, 180, CypherParser::RuleName);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(853);
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
        setState(851);
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
        setState(852);
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
  enterRule(_localctx, 182, CypherParser::RuleSymbol);
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
    setState(855);
    _la = _input->LA(1);
    if (!(((((_la - 31) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 31)) & 8070450532247928895) != 0))) {
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
  enterRule(_localctx, 184, CypherParser::RuleReservedWord);
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
