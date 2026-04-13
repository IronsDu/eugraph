
// Generated from grammar/CypherParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "CypherParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by CypherParser.
 */
class  CypherParserVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by CypherParser.
   */
    virtual std::any visitScript(CypherParser::ScriptContext *context) = 0;

    virtual std::any visitQuery(CypherParser::QueryContext *context) = 0;

    virtual std::any visitRegularQuery(CypherParser::RegularQueryContext *context) = 0;

    virtual std::any visitSingleQuery(CypherParser::SingleQueryContext *context) = 0;

    virtual std::any visitStandaloneCall(CypherParser::StandaloneCallContext *context) = 0;

    virtual std::any visitReturnSt(CypherParser::ReturnStContext *context) = 0;

    virtual std::any visitWithSt(CypherParser::WithStContext *context) = 0;

    virtual std::any visitSkipSt(CypherParser::SkipStContext *context) = 0;

    virtual std::any visitLimitSt(CypherParser::LimitStContext *context) = 0;

    virtual std::any visitProjectionBody(CypherParser::ProjectionBodyContext *context) = 0;

    virtual std::any visitProjectionItems(CypherParser::ProjectionItemsContext *context) = 0;

    virtual std::any visitProjectionItem(CypherParser::ProjectionItemContext *context) = 0;

    virtual std::any visitOrderItem(CypherParser::OrderItemContext *context) = 0;

    virtual std::any visitOrderSt(CypherParser::OrderStContext *context) = 0;

    virtual std::any visitSinglePartQ(CypherParser::SinglePartQContext *context) = 0;

    virtual std::any visitMultiPartQ(CypherParser::MultiPartQContext *context) = 0;

    virtual std::any visitMatchSt(CypherParser::MatchStContext *context) = 0;

    virtual std::any visitUnwindSt(CypherParser::UnwindStContext *context) = 0;

    virtual std::any visitReadingStatement(CypherParser::ReadingStatementContext *context) = 0;

    virtual std::any visitUpdatingStatement(CypherParser::UpdatingStatementContext *context) = 0;

    virtual std::any visitDeleteSt(CypherParser::DeleteStContext *context) = 0;

    virtual std::any visitRemoveSt(CypherParser::RemoveStContext *context) = 0;

    virtual std::any visitRemoveItem(CypherParser::RemoveItemContext *context) = 0;

    virtual std::any visitQueryCallSt(CypherParser::QueryCallStContext *context) = 0;

    virtual std::any visitParenExpressionChain(CypherParser::ParenExpressionChainContext *context) = 0;

    virtual std::any visitYieldItems(CypherParser::YieldItemsContext *context) = 0;

    virtual std::any visitYieldItem(CypherParser::YieldItemContext *context) = 0;

    virtual std::any visitMergeSt(CypherParser::MergeStContext *context) = 0;

    virtual std::any visitMergeAction(CypherParser::MergeActionContext *context) = 0;

    virtual std::any visitSetSt(CypherParser::SetStContext *context) = 0;

    virtual std::any visitSetItem(CypherParser::SetItemContext *context) = 0;

    virtual std::any visitNodeLabels(CypherParser::NodeLabelsContext *context) = 0;

    virtual std::any visitCreateSt(CypherParser::CreateStContext *context) = 0;

    virtual std::any visitPatternWhere(CypherParser::PatternWhereContext *context) = 0;

    virtual std::any visitWhere(CypherParser::WhereContext *context) = 0;

    virtual std::any visitPattern(CypherParser::PatternContext *context) = 0;

    virtual std::any visitExpression(CypherParser::ExpressionContext *context) = 0;

    virtual std::any visitXorExpression(CypherParser::XorExpressionContext *context) = 0;

    virtual std::any visitAndExpression(CypherParser::AndExpressionContext *context) = 0;

    virtual std::any visitNotExpression(CypherParser::NotExpressionContext *context) = 0;

    virtual std::any visitComparisonExpression(CypherParser::ComparisonExpressionContext *context) = 0;

    virtual std::any visitComparisonSigns(CypherParser::ComparisonSignsContext *context) = 0;

    virtual std::any visitAddSubExpression(CypherParser::AddSubExpressionContext *context) = 0;

    virtual std::any visitMultDivExpression(CypherParser::MultDivExpressionContext *context) = 0;

    virtual std::any visitPowerExpression(CypherParser::PowerExpressionContext *context) = 0;

    virtual std::any visitUnaryAddSubExpression(CypherParser::UnaryAddSubExpressionContext *context) = 0;

    virtual std::any visitAtomicExpression(CypherParser::AtomicExpressionContext *context) = 0;

    virtual std::any visitListExpression(CypherParser::ListExpressionContext *context) = 0;

    virtual std::any visitStringExpression(CypherParser::StringExpressionContext *context) = 0;

    virtual std::any visitStringExpPrefix(CypherParser::StringExpPrefixContext *context) = 0;

    virtual std::any visitNullExpression(CypherParser::NullExpressionContext *context) = 0;

    virtual std::any visitPropertyOrLabelExpression(CypherParser::PropertyOrLabelExpressionContext *context) = 0;

    virtual std::any visitPropertyExpression(CypherParser::PropertyExpressionContext *context) = 0;

    virtual std::any visitPatternPart(CypherParser::PatternPartContext *context) = 0;

    virtual std::any visitPatternElem(CypherParser::PatternElemContext *context) = 0;

    virtual std::any visitPatternElemChain(CypherParser::PatternElemChainContext *context) = 0;

    virtual std::any visitProperties(CypherParser::PropertiesContext *context) = 0;

    virtual std::any visitNodePattern(CypherParser::NodePatternContext *context) = 0;

    virtual std::any visitAtom(CypherParser::AtomContext *context) = 0;

    virtual std::any visitLhs(CypherParser::LhsContext *context) = 0;

    virtual std::any visitRelationshipPattern(CypherParser::RelationshipPatternContext *context) = 0;

    virtual std::any visitRelationDetail(CypherParser::RelationDetailContext *context) = 0;

    virtual std::any visitRelationshipTypes(CypherParser::RelationshipTypesContext *context) = 0;

    virtual std::any visitUnionSt(CypherParser::UnionStContext *context) = 0;

    virtual std::any visitSubqueryExist(CypherParser::SubqueryExistContext *context) = 0;

    virtual std::any visitInvocationName(CypherParser::InvocationNameContext *context) = 0;

    virtual std::any visitFunctionInvocation(CypherParser::FunctionInvocationContext *context) = 0;

    virtual std::any visitParenthesizedExpression(CypherParser::ParenthesizedExpressionContext *context) = 0;

    virtual std::any visitFilterWith(CypherParser::FilterWithContext *context) = 0;

    virtual std::any visitPatternComprehension(CypherParser::PatternComprehensionContext *context) = 0;

    virtual std::any visitRelationshipsChainPattern(CypherParser::RelationshipsChainPatternContext *context) = 0;

    virtual std::any visitListComprehension(CypherParser::ListComprehensionContext *context) = 0;

    virtual std::any visitFilterExpression(CypherParser::FilterExpressionContext *context) = 0;

    virtual std::any visitCountAll(CypherParser::CountAllContext *context) = 0;

    virtual std::any visitExpressionChain(CypherParser::ExpressionChainContext *context) = 0;

    virtual std::any visitCaseExpression(CypherParser::CaseExpressionContext *context) = 0;

    virtual std::any visitParameter(CypherParser::ParameterContext *context) = 0;

    virtual std::any visitLiteral(CypherParser::LiteralContext *context) = 0;

    virtual std::any visitRangeLit(CypherParser::RangeLitContext *context) = 0;

    virtual std::any visitBoolLit(CypherParser::BoolLitContext *context) = 0;

    virtual std::any visitNumLit(CypherParser::NumLitContext *context) = 0;

    virtual std::any visitStringLit(CypherParser::StringLitContext *context) = 0;

    virtual std::any visitCharLit(CypherParser::CharLitContext *context) = 0;

    virtual std::any visitListLit(CypherParser::ListLitContext *context) = 0;

    virtual std::any visitMapLit(CypherParser::MapLitContext *context) = 0;

    virtual std::any visitMapPair(CypherParser::MapPairContext *context) = 0;

    virtual std::any visitName(CypherParser::NameContext *context) = 0;

    virtual std::any visitSymbol(CypherParser::SymbolContext *context) = 0;

    virtual std::any visitReservedWord(CypherParser::ReservedWordContext *context) = 0;


};

