
// Generated from grammar/CypherParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  CypherParser : public antlr4::Parser {
public:
  enum {
    ASSIGN = 1, ADD_ASSIGN = 2, LE = 3, GE = 4, GT = 5, LT = 6, NOT_EQUAL = 7, 
    RANGE = 8, SEMI = 9, DOT = 10, COMMA = 11, LPAREN = 12, RPAREN = 13, 
    LBRACE = 14, RBRACE = 15, LBRACK = 16, RBRACK = 17, SUB = 18, PLUS = 19, 
    DIV = 20, MOD = 21, CARET = 22, MULT = 23, ESC = 24, COLON = 25, STICK = 26, 
    DOLLAR = 27, CALL = 28, YIELD = 29, FILTER = 30, EXTRACT = 31, COUNT = 32, 
    ANY = 33, NONE = 34, SINGLE = 35, ALL = 36, ASC = 37, ASCENDING = 38, 
    BY = 39, CREATE = 40, DELETE = 41, DESC = 42, DESCENDING = 43, DETACH = 44, 
    EXISTS = 45, LIMIT = 46, MATCH = 47, MERGE = 48, ON = 49, OPTIONAL = 50, 
    ORDER = 51, REMOVE = 52, RETURN = 53, SET = 54, SKIP_W = 55, WHERE = 56, 
    WITH = 57, UNION = 58, UNWIND = 59, AND = 60, AS = 61, CONTAINS = 62, 
    DISTINCT = 63, ENDS = 64, IN = 65, IS = 66, NOT = 67, OR = 68, STARTS = 69, 
    XOR = 70, FALSE = 71, TRUE = 72, NULL_W = 73, CONSTRAINT = 74, DO = 75, 
    FOR = 76, REQUIRE = 77, UNIQUE = 78, CASE = 79, WHEN = 80, THEN = 81, 
    ELSE = 82, END = 83, MANDATORY = 84, SCALAR = 85, OF = 86, ADD = 87, 
    DROP = 88, ID = 89, ESC_LITERAL = 90, CHAR_LITERAL = 91, STRING_LITERAL = 92, 
    DIGIT = 93, FLOAT = 94, WS = 95, COMMENT = 96, LINE_COMMENT = 97, ERRCHAR = 98, 
    Letter = 99
  };

  enum {
    RuleScript = 0, RuleQuery = 1, RuleRegularQuery = 2, RuleSingleQuery = 3, 
    RuleStandaloneCall = 4, RuleReturnSt = 5, RuleWithSt = 6, RuleSkipSt = 7, 
    RuleLimitSt = 8, RuleProjectionBody = 9, RuleProjectionItems = 10, RuleProjectionItem = 11, 
    RuleOrderItem = 12, RuleOrderSt = 13, RuleSinglePartQ = 14, RuleMultiPartQ = 15, 
    RuleMatchSt = 16, RuleUnwindSt = 17, RuleReadingStatement = 18, RuleUpdatingStatement = 19, 
    RuleDeleteSt = 20, RuleRemoveSt = 21, RuleRemoveItem = 22, RuleQueryCallSt = 23, 
    RuleParenExpressionChain = 24, RuleYieldItems = 25, RuleYieldItem = 26, 
    RuleMergeSt = 27, RuleMergeAction = 28, RuleSetSt = 29, RuleSetItem = 30, 
    RuleNodeLabels = 31, RuleCreateSt = 32, RulePatternWhere = 33, RuleWhere = 34, 
    RulePattern = 35, RuleExpression = 36, RuleXorExpression = 37, RuleAndExpression = 38, 
    RuleNotExpression = 39, RuleComparisonExpression = 40, RuleComparisonSigns = 41, 
    RuleAddSubExpression = 42, RuleMultDivExpression = 43, RulePowerExpression = 44, 
    RuleUnaryAddSubExpression = 45, RuleAtomicExpression = 46, RuleListExpression = 47, 
    RuleStringExpression = 48, RuleStringExpPrefix = 49, RuleNullExpression = 50, 
    RulePropertyOrLabelExpression = 51, RulePropertyExpression = 52, RulePatternPart = 53, 
    RulePatternElem = 54, RulePatternElemChain = 55, RuleProperties = 56, 
    RuleNodePattern = 57, RuleAtom = 58, RuleLhs = 59, RuleRelationshipPattern = 60, 
    RuleRelationDetail = 61, RuleRelationshipTypes = 62, RuleUnionSt = 63, 
    RuleSubqueryExist = 64, RuleInvocationName = 65, RuleFunctionInvocation = 66, 
    RuleParenthesizedExpression = 67, RuleFilterWith = 68, RulePatternComprehension = 69, 
    RuleRelationshipsChainPattern = 70, RuleListComprehension = 71, RuleFilterExpression = 72, 
    RuleCountAll = 73, RuleExpressionChain = 74, RuleCaseExpression = 75, 
    RuleParameter = 76, RuleLiteral = 77, RuleRangeLit = 78, RuleBoolLit = 79, 
    RuleNumLit = 80, RuleStringLit = 81, RuleCharLit = 82, RuleListLit = 83, 
    RuleMapLit = 84, RuleMapPair = 85, RuleName = 86, RuleSymbol = 87, RuleReservedWord = 88
  };

  explicit CypherParser(antlr4::TokenStream *input);

  CypherParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~CypherParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class ScriptContext;
  class QueryContext;
  class RegularQueryContext;
  class SingleQueryContext;
  class StandaloneCallContext;
  class ReturnStContext;
  class WithStContext;
  class SkipStContext;
  class LimitStContext;
  class ProjectionBodyContext;
  class ProjectionItemsContext;
  class ProjectionItemContext;
  class OrderItemContext;
  class OrderStContext;
  class SinglePartQContext;
  class MultiPartQContext;
  class MatchStContext;
  class UnwindStContext;
  class ReadingStatementContext;
  class UpdatingStatementContext;
  class DeleteStContext;
  class RemoveStContext;
  class RemoveItemContext;
  class QueryCallStContext;
  class ParenExpressionChainContext;
  class YieldItemsContext;
  class YieldItemContext;
  class MergeStContext;
  class MergeActionContext;
  class SetStContext;
  class SetItemContext;
  class NodeLabelsContext;
  class CreateStContext;
  class PatternWhereContext;
  class WhereContext;
  class PatternContext;
  class ExpressionContext;
  class XorExpressionContext;
  class AndExpressionContext;
  class NotExpressionContext;
  class ComparisonExpressionContext;
  class ComparisonSignsContext;
  class AddSubExpressionContext;
  class MultDivExpressionContext;
  class PowerExpressionContext;
  class UnaryAddSubExpressionContext;
  class AtomicExpressionContext;
  class ListExpressionContext;
  class StringExpressionContext;
  class StringExpPrefixContext;
  class NullExpressionContext;
  class PropertyOrLabelExpressionContext;
  class PropertyExpressionContext;
  class PatternPartContext;
  class PatternElemContext;
  class PatternElemChainContext;
  class PropertiesContext;
  class NodePatternContext;
  class AtomContext;
  class LhsContext;
  class RelationshipPatternContext;
  class RelationDetailContext;
  class RelationshipTypesContext;
  class UnionStContext;
  class SubqueryExistContext;
  class InvocationNameContext;
  class FunctionInvocationContext;
  class ParenthesizedExpressionContext;
  class FilterWithContext;
  class PatternComprehensionContext;
  class RelationshipsChainPatternContext;
  class ListComprehensionContext;
  class FilterExpressionContext;
  class CountAllContext;
  class ExpressionChainContext;
  class CaseExpressionContext;
  class ParameterContext;
  class LiteralContext;
  class RangeLitContext;
  class BoolLitContext;
  class NumLitContext;
  class StringLitContext;
  class CharLitContext;
  class ListLitContext;
  class MapLitContext;
  class MapPairContext;
  class NameContext;
  class SymbolContext;
  class ReservedWordContext; 

  class  ScriptContext : public antlr4::ParserRuleContext {
  public:
    ScriptContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    QueryContext *query();
    antlr4::tree::TerminalNode *EOF();
    antlr4::tree::TerminalNode *SEMI();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ScriptContext* script();

  class  QueryContext : public antlr4::ParserRuleContext {
  public:
    QueryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RegularQueryContext *regularQuery();
    StandaloneCallContext *standaloneCall();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  QueryContext* query();

  class  RegularQueryContext : public antlr4::ParserRuleContext {
  public:
    RegularQueryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SingleQueryContext *singleQuery();
    std::vector<UnionStContext *> unionSt();
    UnionStContext* unionSt(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RegularQueryContext* regularQuery();

  class  SingleQueryContext : public antlr4::ParserRuleContext {
  public:
    SingleQueryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SinglePartQContext *singlePartQ();
    MultiPartQContext *multiPartQ();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SingleQueryContext* singleQuery();

  class  StandaloneCallContext : public antlr4::ParserRuleContext {
  public:
    StandaloneCallContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CALL();
    InvocationNameContext *invocationName();
    ParenExpressionChainContext *parenExpressionChain();
    antlr4::tree::TerminalNode *YIELD();
    antlr4::tree::TerminalNode *MULT();
    YieldItemsContext *yieldItems();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StandaloneCallContext* standaloneCall();

  class  ReturnStContext : public antlr4::ParserRuleContext {
  public:
    ReturnStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    ProjectionBodyContext *projectionBody();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ReturnStContext* returnSt();

  class  WithStContext : public antlr4::ParserRuleContext {
  public:
    WithStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WITH();
    ProjectionBodyContext *projectionBody();
    WhereContext *where();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithStContext* withSt();

  class  SkipStContext : public antlr4::ParserRuleContext {
  public:
    SkipStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *SKIP_W();
    ExpressionContext *expression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SkipStContext* skipSt();

  class  LimitStContext : public antlr4::ParserRuleContext {
  public:
    LimitStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LIMIT();
    ExpressionContext *expression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LimitStContext* limitSt();

  class  ProjectionBodyContext : public antlr4::ParserRuleContext {
  public:
    ProjectionBodyContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ProjectionItemsContext *projectionItems();
    antlr4::tree::TerminalNode *DISTINCT();
    OrderStContext *orderSt();
    SkipStContext *skipSt();
    LimitStContext *limitSt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProjectionBodyContext* projectionBody();

  class  ProjectionItemsContext : public antlr4::ParserRuleContext {
  public:
    ProjectionItemsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MULT();
    std::vector<ProjectionItemContext *> projectionItem();
    ProjectionItemContext* projectionItem(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProjectionItemsContext* projectionItems();

  class  ProjectionItemContext : public antlr4::ParserRuleContext {
  public:
    ProjectionItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *AS();
    SymbolContext *symbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProjectionItemContext* projectionItem();

  class  OrderItemContext : public antlr4::ParserRuleContext {
  public:
    OrderItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *ASCENDING();
    antlr4::tree::TerminalNode *ASC();
    antlr4::tree::TerminalNode *DESCENDING();
    antlr4::tree::TerminalNode *DESC();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  OrderItemContext* orderItem();

  class  OrderStContext : public antlr4::ParserRuleContext {
  public:
    OrderStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ORDER();
    antlr4::tree::TerminalNode *BY();
    std::vector<OrderItemContext *> orderItem();
    OrderItemContext* orderItem(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  OrderStContext* orderSt();

  class  SinglePartQContext : public antlr4::ParserRuleContext {
  public:
    SinglePartQContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ReturnStContext *returnSt();
    std::vector<ReadingStatementContext *> readingStatement();
    ReadingStatementContext* readingStatement(size_t i);
    std::vector<UpdatingStatementContext *> updatingStatement();
    UpdatingStatementContext* updatingStatement(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SinglePartQContext* singlePartQ();

  class  MultiPartQContext : public antlr4::ParserRuleContext {
  public:
    MultiPartQContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SinglePartQContext *singlePartQ();
    std::vector<ReadingStatementContext *> readingStatement();
    ReadingStatementContext* readingStatement(size_t i);
    std::vector<WithStContext *> withSt();
    WithStContext* withSt(size_t i);
    std::vector<UpdatingStatementContext *> updatingStatement();
    UpdatingStatementContext* updatingStatement(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MultiPartQContext* multiPartQ();

  class  MatchStContext : public antlr4::ParserRuleContext {
  public:
    MatchStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MATCH();
    PatternWhereContext *patternWhere();
    antlr4::tree::TerminalNode *OPTIONAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MatchStContext* matchSt();

  class  UnwindStContext : public antlr4::ParserRuleContext {
  public:
    UnwindStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *UNWIND();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *AS();
    SymbolContext *symbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnwindStContext* unwindSt();

  class  ReadingStatementContext : public antlr4::ParserRuleContext {
  public:
    ReadingStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    MatchStContext *matchSt();
    UnwindStContext *unwindSt();
    QueryCallStContext *queryCallSt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ReadingStatementContext* readingStatement();

  class  UpdatingStatementContext : public antlr4::ParserRuleContext {
  public:
    UpdatingStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CreateStContext *createSt();
    MergeStContext *mergeSt();
    DeleteStContext *deleteSt();
    SetStContext *setSt();
    RemoveStContext *removeSt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UpdatingStatementContext* updatingStatement();

  class  DeleteStContext : public antlr4::ParserRuleContext {
  public:
    DeleteStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *DELETE();
    ExpressionChainContext *expressionChain();
    antlr4::tree::TerminalNode *DETACH();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DeleteStContext* deleteSt();

  class  RemoveStContext : public antlr4::ParserRuleContext {
  public:
    RemoveStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *REMOVE();
    std::vector<RemoveItemContext *> removeItem();
    RemoveItemContext* removeItem(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RemoveStContext* removeSt();

  class  RemoveItemContext : public antlr4::ParserRuleContext {
  public:
    RemoveItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    NodeLabelsContext *nodeLabels();
    PropertyExpressionContext *propertyExpression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RemoveItemContext* removeItem();

  class  QueryCallStContext : public antlr4::ParserRuleContext {
  public:
    QueryCallStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CALL();
    InvocationNameContext *invocationName();
    ParenExpressionChainContext *parenExpressionChain();
    antlr4::tree::TerminalNode *YIELD();
    YieldItemsContext *yieldItems();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  QueryCallStContext* queryCallSt();

  class  ParenExpressionChainContext : public antlr4::ParserRuleContext {
  public:
    ParenExpressionChainContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    ExpressionChainContext *expressionChain();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParenExpressionChainContext* parenExpressionChain();

  class  YieldItemsContext : public antlr4::ParserRuleContext {
  public:
    YieldItemsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<YieldItemContext *> yieldItem();
    YieldItemContext* yieldItem(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);
    WhereContext *where();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  YieldItemsContext* yieldItems();

  class  YieldItemContext : public antlr4::ParserRuleContext {
  public:
    YieldItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);
    antlr4::tree::TerminalNode *AS();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  YieldItemContext* yieldItem();

  class  MergeStContext : public antlr4::ParserRuleContext {
  public:
    MergeStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MERGE();
    PatternPartContext *patternPart();
    std::vector<MergeActionContext *> mergeAction();
    MergeActionContext* mergeAction(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MergeStContext* mergeSt();

  class  MergeActionContext : public antlr4::ParserRuleContext {
  public:
    MergeActionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ON();
    SetStContext *setSt();
    antlr4::tree::TerminalNode *MATCH();
    antlr4::tree::TerminalNode *CREATE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MergeActionContext* mergeAction();

  class  SetStContext : public antlr4::ParserRuleContext {
  public:
    SetStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *SET();
    std::vector<SetItemContext *> setItem();
    SetItemContext* setItem(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SetStContext* setSt();

  class  SetItemContext : public antlr4::ParserRuleContext {
  public:
    SetItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PropertyExpressionContext *propertyExpression();
    antlr4::tree::TerminalNode *ASSIGN();
    ExpressionContext *expression();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ADD_ASSIGN();
    NodeLabelsContext *nodeLabels();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SetItemContext* setItem();

  class  NodeLabelsContext : public antlr4::ParserRuleContext {
  public:
    NodeLabelsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> COLON();
    antlr4::tree::TerminalNode* COLON(size_t i);
    std::vector<NameContext *> name();
    NameContext* name(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NodeLabelsContext* nodeLabels();

  class  CreateStContext : public antlr4::ParserRuleContext {
  public:
    CreateStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CREATE();
    PatternContext *pattern();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CreateStContext* createSt();

  class  PatternWhereContext : public antlr4::ParserRuleContext {
  public:
    PatternWhereContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PatternContext *pattern();
    WhereContext *where();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternWhereContext* patternWhere();

  class  WhereContext : public antlr4::ParserRuleContext {
  public:
    WhereContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WHERE();
    ExpressionContext *expression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WhereContext* where();

  class  PatternContext : public antlr4::ParserRuleContext {
  public:
    PatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<PatternPartContext *> patternPart();
    PatternPartContext* patternPart(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternContext* pattern();

  class  ExpressionContext : public antlr4::ParserRuleContext {
  public:
    ExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<XorExpressionContext *> xorExpression();
    XorExpressionContext* xorExpression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> OR();
    antlr4::tree::TerminalNode* OR(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExpressionContext* expression();

  class  XorExpressionContext : public antlr4::ParserRuleContext {
  public:
    XorExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AndExpressionContext *> andExpression();
    AndExpressionContext* andExpression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> XOR();
    antlr4::tree::TerminalNode* XOR(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  XorExpressionContext* xorExpression();

  class  AndExpressionContext : public antlr4::ParserRuleContext {
  public:
    AndExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<NotExpressionContext *> notExpression();
    NotExpressionContext* notExpression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> AND();
    antlr4::tree::TerminalNode* AND(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AndExpressionContext* andExpression();

  class  NotExpressionContext : public antlr4::ParserRuleContext {
  public:
    NotExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ComparisonExpressionContext *comparisonExpression();
    antlr4::tree::TerminalNode *NOT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NotExpressionContext* notExpression();

  class  ComparisonExpressionContext : public antlr4::ParserRuleContext {
  public:
    ComparisonExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AddSubExpressionContext *> addSubExpression();
    AddSubExpressionContext* addSubExpression(size_t i);
    std::vector<ComparisonSignsContext *> comparisonSigns();
    ComparisonSignsContext* comparisonSigns(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ComparisonExpressionContext* comparisonExpression();

  class  ComparisonSignsContext : public antlr4::ParserRuleContext {
  public:
    ComparisonSignsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ASSIGN();
    antlr4::tree::TerminalNode *LE();
    antlr4::tree::TerminalNode *GE();
    antlr4::tree::TerminalNode *GT();
    antlr4::tree::TerminalNode *LT();
    antlr4::tree::TerminalNode *NOT_EQUAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ComparisonSignsContext* comparisonSigns();

  class  AddSubExpressionContext : public antlr4::ParserRuleContext {
  public:
    AddSubExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<MultDivExpressionContext *> multDivExpression();
    MultDivExpressionContext* multDivExpression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> PLUS();
    antlr4::tree::TerminalNode* PLUS(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SUB();
    antlr4::tree::TerminalNode* SUB(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AddSubExpressionContext* addSubExpression();

  class  MultDivExpressionContext : public antlr4::ParserRuleContext {
  public:
    MultDivExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<PowerExpressionContext *> powerExpression();
    PowerExpressionContext* powerExpression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> MULT();
    antlr4::tree::TerminalNode* MULT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> DIV();
    antlr4::tree::TerminalNode* DIV(size_t i);
    std::vector<antlr4::tree::TerminalNode *> MOD();
    antlr4::tree::TerminalNode* MOD(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MultDivExpressionContext* multDivExpression();

  class  PowerExpressionContext : public antlr4::ParserRuleContext {
  public:
    PowerExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<UnaryAddSubExpressionContext *> unaryAddSubExpression();
    UnaryAddSubExpressionContext* unaryAddSubExpression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> CARET();
    antlr4::tree::TerminalNode* CARET(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PowerExpressionContext* powerExpression();

  class  UnaryAddSubExpressionContext : public antlr4::ParserRuleContext {
  public:
    UnaryAddSubExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AtomicExpressionContext *atomicExpression();
    antlr4::tree::TerminalNode *PLUS();
    antlr4::tree::TerminalNode *SUB();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryAddSubExpressionContext* unaryAddSubExpression();

  class  AtomicExpressionContext : public antlr4::ParserRuleContext {
  public:
    AtomicExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PropertyOrLabelExpressionContext *propertyOrLabelExpression();
    std::vector<StringExpressionContext *> stringExpression();
    StringExpressionContext* stringExpression(size_t i);
    std::vector<ListExpressionContext *> listExpression();
    ListExpressionContext* listExpression(size_t i);
    std::vector<NullExpressionContext *> nullExpression();
    NullExpressionContext* nullExpression(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AtomicExpressionContext* atomicExpression();

  class  ListExpressionContext : public antlr4::ParserRuleContext {
  public:
    ListExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IN();
    PropertyOrLabelExpressionContext *propertyOrLabelExpression();
    antlr4::tree::TerminalNode *LBRACK();
    antlr4::tree::TerminalNode *RBRACK();
    antlr4::tree::TerminalNode *RANGE();
    std::vector<ExpressionContext *> expression();
    ExpressionContext* expression(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListExpressionContext* listExpression();

  class  StringExpressionContext : public antlr4::ParserRuleContext {
  public:
    StringExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StringExpPrefixContext *stringExpPrefix();
    PropertyOrLabelExpressionContext *propertyOrLabelExpression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StringExpressionContext* stringExpression();

  class  StringExpPrefixContext : public antlr4::ParserRuleContext {
  public:
    StringExpPrefixContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *STARTS();
    antlr4::tree::TerminalNode *WITH();
    antlr4::tree::TerminalNode *ENDS();
    antlr4::tree::TerminalNode *CONTAINS();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StringExpPrefixContext* stringExpPrefix();

  class  NullExpressionContext : public antlr4::ParserRuleContext {
  public:
    NullExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IS();
    antlr4::tree::TerminalNode *NULL_W();
    antlr4::tree::TerminalNode *NOT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NullExpressionContext* nullExpression();

  class  PropertyOrLabelExpressionContext : public antlr4::ParserRuleContext {
  public:
    PropertyOrLabelExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PropertyExpressionContext *propertyExpression();
    NodeLabelsContext *nodeLabels();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PropertyOrLabelExpressionContext* propertyOrLabelExpression();

  class  PropertyExpressionContext : public antlr4::ParserRuleContext {
  public:
    PropertyExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AtomContext *atom();
    std::vector<antlr4::tree::TerminalNode *> DOT();
    antlr4::tree::TerminalNode* DOT(size_t i);
    std::vector<NameContext *> name();
    NameContext* name(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PropertyExpressionContext* propertyExpression();

  class  PatternPartContext : public antlr4::ParserRuleContext {
  public:
    PatternPartContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PatternElemContext *patternElem();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ASSIGN();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternPartContext* patternPart();

  class  PatternElemContext : public antlr4::ParserRuleContext {
  public:
    PatternElemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    NodePatternContext *nodePattern();
    std::vector<PatternElemChainContext *> patternElemChain();
    PatternElemChainContext* patternElemChain(size_t i);
    antlr4::tree::TerminalNode *LPAREN();
    PatternElemContext *patternElem();
    antlr4::tree::TerminalNode *RPAREN();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternElemContext* patternElem();

  class  PatternElemChainContext : public antlr4::ParserRuleContext {
  public:
    PatternElemChainContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RelationshipPatternContext *relationshipPattern();
    NodePatternContext *nodePattern();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternElemChainContext* patternElemChain();

  class  PropertiesContext : public antlr4::ParserRuleContext {
  public:
    PropertiesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    MapLitContext *mapLit();
    ParameterContext *parameter();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PropertiesContext* properties();

  class  NodePatternContext : public antlr4::ParserRuleContext {
  public:
    NodePatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    SymbolContext *symbol();
    NodeLabelsContext *nodeLabels();
    PropertiesContext *properties();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NodePatternContext* nodePattern();

  class  AtomContext : public antlr4::ParserRuleContext {
  public:
    AtomContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LiteralContext *literal();
    ParameterContext *parameter();
    CaseExpressionContext *caseExpression();
    CountAllContext *countAll();
    ListComprehensionContext *listComprehension();
    PatternComprehensionContext *patternComprehension();
    FilterWithContext *filterWith();
    RelationshipsChainPatternContext *relationshipsChainPattern();
    ParenthesizedExpressionContext *parenthesizedExpression();
    FunctionInvocationContext *functionInvocation();
    SymbolContext *symbol();
    SubqueryExistContext *subqueryExist();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AtomContext* atom();

  class  LhsContext : public antlr4::ParserRuleContext {
  public:
    LhsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ASSIGN();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LhsContext* lhs();

  class  RelationshipPatternContext : public antlr4::ParserRuleContext {
  public:
    RelationshipPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LT();
    std::vector<antlr4::tree::TerminalNode *> SUB();
    antlr4::tree::TerminalNode* SUB(size_t i);
    RelationDetailContext *relationDetail();
    antlr4::tree::TerminalNode *GT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelationshipPatternContext* relationshipPattern();

  class  RelationDetailContext : public antlr4::ParserRuleContext {
  public:
    RelationDetailContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LBRACK();
    antlr4::tree::TerminalNode *RBRACK();
    SymbolContext *symbol();
    RelationshipTypesContext *relationshipTypes();
    RangeLitContext *rangeLit();
    PropertiesContext *properties();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelationDetailContext* relationDetail();

  class  RelationshipTypesContext : public antlr4::ParserRuleContext {
  public:
    RelationshipTypesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> COLON();
    antlr4::tree::TerminalNode* COLON(size_t i);
    std::vector<NameContext *> name();
    NameContext* name(size_t i);
    std::vector<antlr4::tree::TerminalNode *> STICK();
    antlr4::tree::TerminalNode* STICK(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelationshipTypesContext* relationshipTypes();

  class  UnionStContext : public antlr4::ParserRuleContext {
  public:
    UnionStContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *UNION();
    SingleQueryContext *singleQuery();
    antlr4::tree::TerminalNode *ALL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnionStContext* unionSt();

  class  SubqueryExistContext : public antlr4::ParserRuleContext {
  public:
    SubqueryExistContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EXISTS();
    antlr4::tree::TerminalNode *LBRACE();
    antlr4::tree::TerminalNode *RBRACE();
    RegularQueryContext *regularQuery();
    PatternWhereContext *patternWhere();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SubqueryExistContext* subqueryExist();

  class  InvocationNameContext : public antlr4::ParserRuleContext {
  public:
    InvocationNameContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);
    std::vector<antlr4::tree::TerminalNode *> DOT();
    antlr4::tree::TerminalNode* DOT(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  InvocationNameContext* invocationName();

  class  FunctionInvocationContext : public antlr4::ParserRuleContext {
  public:
    FunctionInvocationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    InvocationNameContext *invocationName();
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    antlr4::tree::TerminalNode *DISTINCT();
    ExpressionChainContext *expressionChain();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FunctionInvocationContext* functionInvocation();

  class  ParenthesizedExpressionContext : public antlr4::ParserRuleContext {
  public:
    ParenthesizedExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *RPAREN();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParenthesizedExpressionContext* parenthesizedExpression();

  class  FilterWithContext : public antlr4::ParserRuleContext {
  public:
    FilterWithContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    FilterExpressionContext *filterExpression();
    antlr4::tree::TerminalNode *RPAREN();
    antlr4::tree::TerminalNode *ALL();
    antlr4::tree::TerminalNode *ANY();
    antlr4::tree::TerminalNode *NONE();
    antlr4::tree::TerminalNode *SINGLE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FilterWithContext* filterWith();

  class  PatternComprehensionContext : public antlr4::ParserRuleContext {
  public:
    PatternComprehensionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LBRACK();
    RelationshipsChainPatternContext *relationshipsChainPattern();
    antlr4::tree::TerminalNode *STICK();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *RBRACK();
    LhsContext *lhs();
    WhereContext *where();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternComprehensionContext* patternComprehension();

  class  RelationshipsChainPatternContext : public antlr4::ParserRuleContext {
  public:
    RelationshipsChainPatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    NodePatternContext *nodePattern();
    std::vector<PatternElemChainContext *> patternElemChain();
    PatternElemChainContext* patternElemChain(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelationshipsChainPatternContext* relationshipsChainPattern();

  class  ListComprehensionContext : public antlr4::ParserRuleContext {
  public:
    ListComprehensionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LBRACK();
    FilterExpressionContext *filterExpression();
    antlr4::tree::TerminalNode *RBRACK();
    antlr4::tree::TerminalNode *STICK();
    ExpressionContext *expression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListComprehensionContext* listComprehension();

  class  FilterExpressionContext : public antlr4::ParserRuleContext {
  public:
    FilterExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *IN();
    ExpressionContext *expression();
    WhereContext *where();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FilterExpressionContext* filterExpression();

  class  CountAllContext : public antlr4::ParserRuleContext {
  public:
    CountAllContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *COUNT();
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *MULT();
    antlr4::tree::TerminalNode *RPAREN();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CountAllContext* countAll();

  class  ExpressionChainContext : public antlr4::ParserRuleContext {
  public:
    ExpressionChainContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExpressionContext *> expression();
    ExpressionContext* expression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExpressionChainContext* expressionChain();

  class  CaseExpressionContext : public antlr4::ParserRuleContext {
  public:
    CaseExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CASE();
    antlr4::tree::TerminalNode *END();
    std::vector<ExpressionContext *> expression();
    ExpressionContext* expression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> WHEN();
    antlr4::tree::TerminalNode* WHEN(size_t i);
    std::vector<antlr4::tree::TerminalNode *> THEN();
    antlr4::tree::TerminalNode* THEN(size_t i);
    antlr4::tree::TerminalNode *ELSE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CaseExpressionContext* caseExpression();

  class  ParameterContext : public antlr4::ParserRuleContext {
  public:
    ParameterContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *DOLLAR();
    SymbolContext *symbol();
    NumLitContext *numLit();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParameterContext* parameter();

  class  LiteralContext : public antlr4::ParserRuleContext {
  public:
    LiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    BoolLitContext *boolLit();
    NumLitContext *numLit();
    antlr4::tree::TerminalNode *NULL_W();
    StringLitContext *stringLit();
    CharLitContext *charLit();
    ListLitContext *listLit();
    MapLitContext *mapLit();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LiteralContext* literal();

  class  RangeLitContext : public antlr4::ParserRuleContext {
  public:
    RangeLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MULT();
    std::vector<NumLitContext *> numLit();
    NumLitContext* numLit(size_t i);
    antlr4::tree::TerminalNode *RANGE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RangeLitContext* rangeLit();

  class  BoolLitContext : public antlr4::ParserRuleContext {
  public:
    BoolLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *FALSE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BoolLitContext* boolLit();

  class  NumLitContext : public antlr4::ParserRuleContext {
  public:
    NumLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *DIGIT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NumLitContext* numLit();

  class  StringLitContext : public antlr4::ParserRuleContext {
  public:
    StringLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *STRING_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StringLitContext* stringLit();

  class  CharLitContext : public antlr4::ParserRuleContext {
  public:
    CharLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CHAR_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CharLitContext* charLit();

  class  ListLitContext : public antlr4::ParserRuleContext {
  public:
    ListLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LBRACK();
    antlr4::tree::TerminalNode *RBRACK();
    ExpressionChainContext *expressionChain();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListLitContext* listLit();

  class  MapLitContext : public antlr4::ParserRuleContext {
  public:
    MapLitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LBRACE();
    antlr4::tree::TerminalNode *RBRACE();
    std::vector<MapPairContext *> mapPair();
    MapPairContext* mapPair(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MapLitContext* mapLit();

  class  MapPairContext : public antlr4::ParserRuleContext {
  public:
    MapPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    NameContext *name();
    antlr4::tree::TerminalNode *COLON();
    ExpressionContext *expression();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MapPairContext* mapPair();

  class  NameContext : public antlr4::ParserRuleContext {
  public:
    NameContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    ReservedWordContext *reservedWord();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NameContext* name();

  class  SymbolContext : public antlr4::ParserRuleContext {
  public:
    SymbolContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ESC_LITERAL();
    antlr4::tree::TerminalNode *ID();
    antlr4::tree::TerminalNode *COUNT();
    antlr4::tree::TerminalNode *FILTER();
    antlr4::tree::TerminalNode *EXTRACT();
    antlr4::tree::TerminalNode *ANY();
    antlr4::tree::TerminalNode *NONE();
    antlr4::tree::TerminalNode *SINGLE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SymbolContext* symbol();

  class  ReservedWordContext : public antlr4::ParserRuleContext {
  public:
    ReservedWordContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ALL();
    antlr4::tree::TerminalNode *ASC();
    antlr4::tree::TerminalNode *ASCENDING();
    antlr4::tree::TerminalNode *BY();
    antlr4::tree::TerminalNode *CREATE();
    antlr4::tree::TerminalNode *DELETE();
    antlr4::tree::TerminalNode *DESC();
    antlr4::tree::TerminalNode *DESCENDING();
    antlr4::tree::TerminalNode *DETACH();
    antlr4::tree::TerminalNode *EXISTS();
    antlr4::tree::TerminalNode *LIMIT();
    antlr4::tree::TerminalNode *MATCH();
    antlr4::tree::TerminalNode *MERGE();
    antlr4::tree::TerminalNode *ON();
    antlr4::tree::TerminalNode *OPTIONAL();
    antlr4::tree::TerminalNode *ORDER();
    antlr4::tree::TerminalNode *REMOVE();
    antlr4::tree::TerminalNode *RETURN();
    antlr4::tree::TerminalNode *SET();
    antlr4::tree::TerminalNode *SKIP_W();
    antlr4::tree::TerminalNode *WHERE();
    antlr4::tree::TerminalNode *WITH();
    antlr4::tree::TerminalNode *UNION();
    antlr4::tree::TerminalNode *UNWIND();
    antlr4::tree::TerminalNode *AND();
    antlr4::tree::TerminalNode *AS();
    antlr4::tree::TerminalNode *CONTAINS();
    antlr4::tree::TerminalNode *DISTINCT();
    antlr4::tree::TerminalNode *ENDS();
    antlr4::tree::TerminalNode *IN();
    antlr4::tree::TerminalNode *IS();
    antlr4::tree::TerminalNode *NOT();
    antlr4::tree::TerminalNode *OR();
    antlr4::tree::TerminalNode *STARTS();
    antlr4::tree::TerminalNode *XOR();
    antlr4::tree::TerminalNode *FALSE();
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *NULL_W();
    antlr4::tree::TerminalNode *CONSTRAINT();
    antlr4::tree::TerminalNode *DO();
    antlr4::tree::TerminalNode *FOR();
    antlr4::tree::TerminalNode *REQUIRE();
    antlr4::tree::TerminalNode *UNIQUE();
    antlr4::tree::TerminalNode *CASE();
    antlr4::tree::TerminalNode *WHEN();
    antlr4::tree::TerminalNode *THEN();
    antlr4::tree::TerminalNode *ELSE();
    antlr4::tree::TerminalNode *END();
    antlr4::tree::TerminalNode *MANDATORY();
    antlr4::tree::TerminalNode *SCALAR();
    antlr4::tree::TerminalNode *OF();
    antlr4::tree::TerminalNode *ADD();
    antlr4::tree::TerminalNode *DROP();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ReservedWordContext* reservedWord();


  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

