
// Generated from grammar/CypherParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "CypherParserVisitor.h"


/**
 * This class provides an empty implementation of CypherParserVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  CypherParserBaseVisitor : public CypherParserVisitor {
public:

  virtual std::any visitScript(CypherParser::ScriptContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQuery(CypherParser::QueryContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRegularQuery(CypherParser::RegularQueryContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSingleQuery(CypherParser::SingleQueryContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStandaloneCall(CypherParser::StandaloneCallContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReturnSt(CypherParser::ReturnStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWithSt(CypherParser::WithStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSkipSt(CypherParser::SkipStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLimitSt(CypherParser::LimitStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitProjectionBody(CypherParser::ProjectionBodyContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitProjectionItems(CypherParser::ProjectionItemsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitProjectionItem(CypherParser::ProjectionItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOrderItem(CypherParser::OrderItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOrderSt(CypherParser::OrderStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSinglePartQ(CypherParser::SinglePartQContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultiPartQ(CypherParser::MultiPartQContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMatchSt(CypherParser::MatchStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnwindSt(CypherParser::UnwindStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReadingStatement(CypherParser::ReadingStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUpdatingStatement(CypherParser::UpdatingStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeleteSt(CypherParser::DeleteStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRemoveSt(CypherParser::RemoveStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRemoveItem(CypherParser::RemoveItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQueryCallSt(CypherParser::QueryCallStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParenExpressionChain(CypherParser::ParenExpressionChainContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitYieldItems(CypherParser::YieldItemsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitYieldItem(CypherParser::YieldItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMergeSt(CypherParser::MergeStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMergeAction(CypherParser::MergeActionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSetSt(CypherParser::SetStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSetItem(CypherParser::SetItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNodeLabels(CypherParser::NodeLabelsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCreateSt(CypherParser::CreateStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPatternWhere(CypherParser::PatternWhereContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWhere(CypherParser::WhereContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPattern(CypherParser::PatternContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpression(CypherParser::ExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitXorExpression(CypherParser::XorExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAndExpression(CypherParser::AndExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNotExpression(CypherParser::NotExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitComparisonExpression(CypherParser::ComparisonExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitComparisonSigns(CypherParser::ComparisonSignsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddSubExpression(CypherParser::AddSubExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultDivExpression(CypherParser::MultDivExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPowerExpression(CypherParser::PowerExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryAddSubExpression(CypherParser::UnaryAddSubExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAtomicExpression(CypherParser::AtomicExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListExpression(CypherParser::ListExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStringExpression(CypherParser::StringExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStringExpPrefix(CypherParser::StringExpPrefixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNullExpression(CypherParser::NullExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPropertyOrLabelExpression(CypherParser::PropertyOrLabelExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPropertyExpression(CypherParser::PropertyExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPatternPart(CypherParser::PatternPartContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPatternElem(CypherParser::PatternElemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPatternElemChain(CypherParser::PatternElemChainContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitProperties(CypherParser::PropertiesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNodePattern(CypherParser::NodePatternContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAtom(CypherParser::AtomContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLhs(CypherParser::LhsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelationshipPattern(CypherParser::RelationshipPatternContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelationDetail(CypherParser::RelationDetailContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelationshipTypes(CypherParser::RelationshipTypesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnionSt(CypherParser::UnionStContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSubqueryExist(CypherParser::SubqueryExistContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInvocationName(CypherParser::InvocationNameContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFunctionInvocation(CypherParser::FunctionInvocationContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParenthesizedExpression(CypherParser::ParenthesizedExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFilterWith(CypherParser::FilterWithContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPatternComprehension(CypherParser::PatternComprehensionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelationshipsChainPattern(CypherParser::RelationshipsChainPatternContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListComprehension(CypherParser::ListComprehensionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFilterExpression(CypherParser::FilterExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCountAll(CypherParser::CountAllContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpressionChain(CypherParser::ExpressionChainContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCaseExpression(CypherParser::CaseExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParameter(CypherParser::ParameterContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLiteral(CypherParser::LiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRangeLit(CypherParser::RangeLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBoolLit(CypherParser::BoolLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNumLit(CypherParser::NumLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStringLit(CypherParser::StringLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCharLit(CypherParser::CharLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListLit(CypherParser::ListLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapLit(CypherParser::MapLitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapPair(CypherParser::MapPairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitName(CypherParser::NameContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSymbol(CypherParser::SymbolContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReservedWord(CypherParser::ReservedWordContext *ctx) override {
    return visitChildren(ctx);
  }


};

