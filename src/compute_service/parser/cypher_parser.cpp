#include "compute_service/parser/cypher_parser.hpp"

// ANTLR 生成的头文件（全局命名空间）
#include "CypherLexer.h"
#include "CypherParser.h"

#include <algorithm>
#include <antlr4-runtime.h>

namespace eugraph {
namespace cypher {

using AP = ::CypherParser;

// ==================== Error Listener ====================

class ParseErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer*, antlr4::Token*, size_t line, size_t charPositionInLine,
                     const std::string& msg, std::exception_ptr) override {
        has_error_ = true;
        error_.message = msg;
        error_.line = static_cast<int>(line);
        error_.column = static_cast<int>(charPositionInLine);
    }
    bool hasError() const {
        return has_error_;
    }
    ParseError getError() const {
        return error_;
    }

private:
    bool has_error_ = false;
    ParseError error_;
};

// ==================== AST Builder ====================
// 直接遍历 ANTLR Parse Tree，不继承 BaseVisitor（避免 std::any 与 move-only 类型冲突）

class AstBuilder {
public:
    // 顶层入口
    std::variant<Statement, ParseError> build(AP::ScriptContext* ctx) {
        return buildQuery(ctx->query());
    }

private:
    // === Query ===

    Statement buildQuery(AP::QueryContext* ctx) {
        if (ctx->regularQuery())
            return Statement(buildRegularQuery(ctx->regularQuery()));
        return Statement(buildStandaloneCall(ctx->standaloneCall()));
    }

    std::unique_ptr<RegularQuery> buildRegularQuery(AP::RegularQueryContext* ctx) {
        auto q = std::make_unique<RegularQuery>();
        q->first = buildSingleQuery(ctx->singleQuery());
        for (auto* uc : ctx->unionSt()) {
            UnionClause ucl;
            ucl.all = (uc->ALL() != nullptr);
            q->unions.push_back({ucl, buildSingleQuery(uc->singleQuery())});
        }
        return q;
    }

    std::unique_ptr<StandaloneCall> buildStandaloneCall(AP::StandaloneCallContext* ctx) {
        auto c = std::make_unique<StandaloneCall>();
        c->procedure_name = buildInvocationName(ctx->invocationName());
        if (ctx->parenExpressionChain())
            c->args = buildExprChainFromParen(ctx->parenExpressionChain());
        if (ctx->YIELD() && ctx->yieldItems())
            c->yield_items = buildYieldItems(ctx->yieldItems());
        return c;
    }

    // === SingleQuery ===

    SingleQuery buildSingleQuery(AP::SingleQueryContext* ctx) {
        if (ctx->singlePartQ())
            return buildSinglePartQ(ctx->singlePartQ());
        return buildMultiPartQ(ctx->multiPartQ());
    }

    SingleQuery buildSinglePartQ(AP::SinglePartQContext* ctx) {
        SingleQuery sq;
        for (auto* rs : ctx->readingStatement())
            sq.clauses.push_back(buildReadingStatement(rs));
        for (auto* us : ctx->updatingStatement())
            sq.clauses.push_back(buildUpdatingStatement(us));
        if (ctx->returnSt())
            sq.clauses.push_back(Clause(std::move(buildReturnClause(ctx->returnSt()))));

        // Reorder: reading -> updating -> return
        std::vector<Clause> reading, updating, returning;
        for (auto& c : sq.clauses) {
            if (std::holds_alternative<std::unique_ptr<ReturnClause>>(c) ||
                std::holds_alternative<std::unique_ptr<WithClause>>(c))
                returning.push_back(std::move(c));
            else if (std::holds_alternative<std::unique_ptr<CreateClause>>(c) ||
                     std::holds_alternative<std::unique_ptr<MergeClause>>(c) ||
                     std::holds_alternative<std::unique_ptr<DeleteClause>>(c) ||
                     std::holds_alternative<std::unique_ptr<SetClause>>(c) ||
                     std::holds_alternative<std::unique_ptr<RemoveClause>>(c))
                updating.push_back(std::move(c));
            else
                reading.push_back(std::move(c));
        }
        sq.clauses.clear();
        for (auto& c : reading)
            sq.clauses.push_back(std::move(c));
        for (auto& c : updating)
            sq.clauses.push_back(std::move(c));
        for (auto& c : returning)
            sq.clauses.push_back(std::move(c));
        return sq;
    }

    SingleQuery buildMultiPartQ(AP::MultiPartQContext* ctx) {
        SingleQuery sq;
        for (auto* rs : ctx->readingStatement())
            sq.clauses.push_back(buildReadingStatement(rs));
        for (auto* us : ctx->updatingStatement())
            sq.clauses.push_back(buildUpdatingStatement(us));
        for (auto* ws : ctx->withSt())
            sq.clauses.push_back(Clause(buildWithClause(ws)));
        auto tail = buildSinglePartQ(ctx->singlePartQ());
        for (auto& c : tail.clauses)
            sq.clauses.push_back(std::move(c));
        return sq;
    }

    // === Clause Dispatch ===

    Clause buildReadingStatement(AP::ReadingStatementContext* ctx) {
        if (ctx->matchSt())
            return Clause(buildMatchClause(ctx->matchSt()));
        if (ctx->unwindSt())
            return Clause(buildUnwindClause(ctx->unwindSt()));
        return Clause(buildCallClause(ctx->queryCallSt()));
    }

    Clause buildUpdatingStatement(AP::UpdatingStatementContext* ctx) {
        if (ctx->createSt())
            return Clause(buildCreateClause(ctx->createSt()));
        if (ctx->mergeSt())
            return Clause(buildMergeClause(ctx->mergeSt()));
        if (ctx->deleteSt())
            return Clause(buildDeleteClause(ctx->deleteSt()));
        if (ctx->setSt())
            return Clause(buildSetClauseFromSetSt(ctx->setSt()));
        return Clause(buildRemoveClause(ctx->removeSt()));
    }

    // === Match ===

    std::unique_ptr<MatchClause> buildMatchClause(AP::MatchStContext* ctx) {
        auto m = std::make_unique<MatchClause>();
        m->optional = (ctx->OPTIONAL() != nullptr);
        m->patterns = buildPattern(ctx->patternWhere()->pattern());
        if (ctx->patternWhere()->where())
            m->where_pred = buildExpression(ctx->patternWhere()->where()->expression());
        return m;
    }

    std::unique_ptr<UnwindClause> buildUnwindClause(AP::UnwindStContext* ctx) {
        auto u = std::make_unique<UnwindClause>();
        u->list_expr = buildExpression(ctx->expression());
        u->variable = ctx->symbol()->getText();
        return u;
    }

    std::unique_ptr<CallClause> buildCallClause(AP::QueryCallStContext* ctx) {
        auto c = std::make_unique<CallClause>();
        c->procedure_name = buildInvocationName(ctx->invocationName());
        if (ctx->parenExpressionChain())
            c->args = buildExprChainFromParen(ctx->parenExpressionChain());
        if (ctx->yieldItems())
            c->yield_items = buildYieldItems(ctx->yieldItems());
        return c;
    }

    // === Create / Delete / Set / Remove / Merge ===

    std::unique_ptr<CreateClause> buildCreateClause(AP::CreateStContext* ctx) {
        auto c = std::make_unique<CreateClause>();
        c->patterns = buildPattern(ctx->pattern());
        return c;
    }

    std::unique_ptr<MergeClause> buildMergeClause(AP::MergeStContext* ctx) {
        auto m = std::make_unique<MergeClause>();
        m->pattern = buildPatternPart(ctx->patternPart());
        for (auto* action : ctx->mergeAction()) {
            auto items = buildSetItems(action->setSt());
            if (action->MATCH())
                m->on_match = std::move(items);
            else
                m->on_create = std::move(items);
        }
        return m;
    }

    std::unique_ptr<DeleteClause> buildDeleteClause(AP::DeleteStContext* ctx) {
        auto d = std::make_unique<DeleteClause>();
        d->detach = (ctx->DETACH() != nullptr);
        d->expressions = buildExprChain(ctx->expressionChain());
        return d;
    }

    std::unique_ptr<SetClause> buildSetClauseFromSetSt(AP::SetStContext* ctx) {
        auto s = std::make_unique<SetClause>();
        s->items = buildSetItems(ctx);
        return s;
    }

    std::unique_ptr<RemoveClause> buildRemoveClause(AP::RemoveStContext* ctx) {
        auto r = std::make_unique<RemoveClause>();
        for (auto* item : ctx->removeItem()) {
            RemoveItem ri;
            if (item->symbol() && item->nodeLabels()) {
                ri.kind = RemoveItem::Kind::LABEL;
                ri.target = makeVariable(item->symbol()->getText());
                for (auto* n : item->nodeLabels()->name())
                    ri.name = n->getText();
            } else {
                ri.kind = RemoveItem::Kind::PROPERTY;
                ri.target = buildPropertyExpression(item->propertyExpression());
                if (auto* prop = std::get_if<std::unique_ptr<PropertyAccess>>(&ri.target)) {
                    ri.name = (*prop)->property;
                    auto obj = std::move((*prop)->object);
                    ri.target = std::move(obj);
                }
            }
            r->items.push_back(std::move(ri));
        }
        return r;
    }

    // === Return / With ===

    std::unique_ptr<ReturnClause> buildReturnClause(AP::ReturnStContext* ctx) {
        return buildProjectionBody(ctx->projectionBody());
    }

    std::unique_ptr<WithClause> buildWithClause(AP::WithStContext* ctx) {
        auto w = buildProjectionBodyAsWith(ctx->projectionBody());
        if (ctx->where())
            w->where_pred = buildExpression(ctx->where()->expression());
        return w;
    }

    // ==================== Expression ====================

    Expression buildExpression(AP::ExpressionContext* ctx) {
        auto xors = ctx->xorExpression();
        if (xors.size() == 1)
            return buildXorExpr(xors[0]);
        Expression r = buildXorExpr(xors[0]);
        for (size_t i = 1; i < xors.size(); i++)
            r = makeBinaryOp(BinaryOperator::OR, std::move(r), buildXorExpr(xors[i]));
        return r;
    }

    Expression buildXorExpr(AP::XorExpressionContext* ctx) {
        auto ands = ctx->andExpression();
        if (ands.size() == 1)
            return buildAndExpr(ands[0]);
        Expression r = buildAndExpr(ands[0]);
        for (size_t i = 1; i < ands.size(); i++)
            r = makeBinaryOp(BinaryOperator::XOR, std::move(r), buildAndExpr(ands[i]));
        return r;
    }

    Expression buildAndExpr(AP::AndExpressionContext* ctx) {
        auto nots = ctx->notExpression();
        if (nots.size() == 1)
            return buildNotExpr(nots[0]);
        Expression r = buildNotExpr(nots[0]);
        for (size_t i = 1; i < nots.size(); i++)
            r = makeBinaryOp(BinaryOperator::AND, std::move(r), buildNotExpr(nots[i]));
        return r;
    }

    Expression buildNotExpr(AP::NotExpressionContext* ctx) {
        auto e = buildComparisonExpr(ctx->comparisonExpression());
        if (ctx->NOT())
            return makeUnaryOp(UnaryOperator::NOT, std::move(e));
        return e;
    }

    Expression buildComparisonExpr(AP::ComparisonExpressionContext* ctx) {
        auto adds = ctx->addSubExpression();
        auto signs = ctx->comparisonSigns();
        if (adds.size() == 1)
            return buildAddSubExpr(adds[0]);
        Expression r = buildAddSubExpr(adds[0]);
        for (size_t i = 0; i < signs.size(); i++)
            r = makeBinaryOp(parseCompSign(signs[i]), std::move(r), buildAddSubExpr(adds[i + 1]));
        return r;
    }

    BinaryOperator parseCompSign(AP::ComparisonSignsContext* ctx) {
        if (ctx->ASSIGN())
            return BinaryOperator::EQ;
        if (ctx->LE())
            return BinaryOperator::LTE;
        if (ctx->GE())
            return BinaryOperator::GTE;
        if (ctx->GT())
            return BinaryOperator::GT;
        if (ctx->LT())
            return BinaryOperator::LT;
        if (ctx->NOT_EQUAL())
            return BinaryOperator::NEQ;
        return BinaryOperator::EQ;
    }

    Expression buildAddSubExpr(AP::AddSubExpressionContext* ctx) {
        auto muls = ctx->multDivExpression();
        if (muls.size() == 1)
            return buildMulDivExpr(muls[0]);
        Expression r = buildMulDivExpr(muls[0]);
        for (size_t i = 1; i < muls.size(); i++)
            r = makeBinaryOp(ctx->PLUS(i - 1) ? BinaryOperator::ADD : BinaryOperator::SUB, std::move(r),
                             buildMulDivExpr(muls[i]));
        return r;
    }

    Expression buildMulDivExpr(AP::MultDivExpressionContext* ctx) {
        auto pows = ctx->powerExpression();
        if (pows.size() == 1)
            return buildPowerExpr(pows[0]);
        Expression r = buildPowerExpr(pows[0]);
        for (size_t i = 1; i < pows.size(); i++) {
            BinaryOperator op = ctx->MULT(i - 1)  ? BinaryOperator::MUL
                                : ctx->DIV(i - 1) ? BinaryOperator::DIV
                                                  : BinaryOperator::MOD;
            r = makeBinaryOp(op, std::move(r), buildPowerExpr(pows[i]));
        }
        return r;
    }

    Expression buildPowerExpr(AP::PowerExpressionContext* ctx) {
        auto us = ctx->unaryAddSubExpression();
        if (us.size() == 1)
            return buildUnaryExpr(us[0]);
        Expression r = buildUnaryExpr(us[0]);
        for (size_t i = 1; i < us.size(); i++)
            r = makeBinaryOp(BinaryOperator::POW, std::move(r), buildUnaryExpr(us[i]));
        return r;
    }

    Expression buildUnaryExpr(AP::UnaryAddSubExpressionContext* ctx) {
        auto e = buildAtomicExpr(ctx->atomicExpression());
        if (ctx->SUB())
            return makeUnaryOp(UnaryOperator::NEGATE, std::move(e));
        return e;
    }

    Expression buildAtomicExpr(AP::AtomicExpressionContext* ctx) {
        Expression r = buildPropOrLabelExpr(ctx->propertyOrLabelExpression());

        for (auto* se : ctx->stringExpression()) {
            auto right = buildPropOrLabelExpr(se->propertyOrLabelExpression());
            auto* p = se->stringExpPrefix();
            BinaryOperator op = p->STARTS() ? BinaryOperator::STARTS_WITH
                                : p->ENDS() ? BinaryOperator::ENDS_WITH
                                            : BinaryOperator::CONTAINS;
            r = makeBinaryOp(op, std::move(r), std::move(right));
        }

        for (auto* le : ctx->listExpression()) {
            if (le->IN()) {
                auto right = buildPropOrLabelExpr(le->propertyOrLabelExpression());
                r = makeBinaryOp(BinaryOperator::IN, std::move(r), std::move(right));
            } else if (le->RBRACK()) {
                if (le->RANGE()) {
                    auto slice = std::make_unique<SliceExpr>();
                    slice->list = std::move(r);
                    auto exprs = le->expression();
                    if (exprs.size() >= 1)
                        slice->from = buildExpression(exprs[0]);
                    if (exprs.size() >= 2)
                        slice->to = buildExpression(exprs.back());
                    r = Expression(std::move(slice));
                } else {
                    auto sub = std::make_unique<SubscriptExpr>();
                    sub->list = std::move(r);
                    if (!le->expression().empty())
                        sub->index = buildExpression(le->expression(0));
                    r = Expression(std::move(sub));
                }
            }
        }

        for (auto* ne : ctx->nullExpression()) {
            r = makeUnaryOp(ne->NOT() ? UnaryOperator::IS_NOT_NULL : UnaryOperator::IS_NULL, std::move(r));
        }
        return r;
    }

    Expression buildPropOrLabelExpr(AP::PropertyOrLabelExpressionContext* ctx) {
        return buildPropertyExpression(ctx->propertyExpression());
    }

    Expression buildPropertyExpression(AP::PropertyExpressionContext* ctx) {
        auto* atom = ctx->atom();
        auto names = ctx->name();
        Expression r = buildAtomNode(atom);
        for (auto* n : names)
            r = makePropertyAccess(std::move(r), n->getText());
        return r;
    }

    Expression buildAtomNode(AP::AtomContext* ctx) {
        if (ctx->literal())
            return buildLiteral(ctx->literal());
        if (ctx->parameter())
            return buildParameter(ctx->parameter());
        if (ctx->caseExpression())
            return buildCaseExpr(ctx->caseExpression());
        if (ctx->countAll())
            return makeFunctionCall("count", {}, false);
        if (ctx->listComprehension())
            return buildListComprehension(ctx->listComprehension());
        if (ctx->patternComprehension())
            return buildPatternComprehension(ctx->patternComprehension());
        if (ctx->filterWith())
            return buildFilterWith(ctx->filterWith());
        if (ctx->parenthesizedExpression())
            return buildExpression(ctx->parenthesizedExpression()->expression());
        if (ctx->functionInvocation())
            return buildFuncInvocation(ctx->functionInvocation());
        if (ctx->subqueryExist())
            return buildExistsExpr(ctx->subqueryExist());

        // ANTLR's adaptive prediction may route numeric literals (emitted as ID tokens)
        // to the symbol alternative instead of the literal alternative.
        if (ctx->symbol()) {
            auto text = ctx->symbol()->getText();
            return parseNumericOrVariable(text);
        }
        return makeVariable(ctx->getText());
    }

    // When the lexer emits a pure number as an ID token, convert it to a Literal.
    Expression parseNumericOrVariable(const std::string& text) {
        if (text.empty())
            return makeVariable(text);
        // Check if text looks like a number (starts with digit, '.', or '-')
        char c = text[0];
        if ((c >= '0' && c <= '9') || c == '.' || (c == '-' && text.size() > 1 && text[1] >= '0' && text[1] <= '9')) {
            try {
                if (text.find('.') != std::string::npos || text.find('e') != std::string::npos ||
                    text.find('E') != std::string::npos)
                    return makeLiteral(std::stod(text));
                return makeLiteral(static_cast<int64_t>(std::stoll(text)));
            } catch (...) {
                // Not actually a number, fall through
            }
        }
        return makeVariable(text);
    }

    Expression buildLiteral(AP::LiteralContext* ctx) {
        if (ctx->boolLit()) {
            auto text = ctx->boolLit()->getText();
            // Grammar is case-insensitive, so getText() may be "true"/"True"/"TRUE" etc.
            std::transform(text.begin(), text.end(), text.begin(), ::toupper);
            return makeLiteral(text == "TRUE");
        }
        if (ctx->NULL_W())
            return makeLiteral(NullValue{});
        if (ctx->stringLit()) {
            auto t = ctx->stringLit()->getText();
            return makeLiteral(t.substr(1, t.size() - 2));
        }
        if (ctx->charLit()) {
            auto t = ctx->charLit()->getText();
            return makeLiteral(t.substr(1, t.size() - 2));
        }
        if (ctx->numLit()) {
            auto t = ctx->numLit()->getText();
            if (t.find('.') != std::string::npos || t.find('e') != std::string::npos ||
                t.find('E') != std::string::npos)
                return makeLiteral(std::stod(t));
            return makeLiteral(static_cast<int64_t>(std::stoll(t)));
        }
        if (ctx->listLit()) {
            auto list = std::make_unique<ListExpr>();
            if (ctx->listLit()->expressionChain())
                list->elements = buildExprChain(ctx->listLit()->expressionChain());
            return Expression(std::move(list));
        }
        if (ctx->mapLit()) {
            auto map = std::make_unique<MapExpr>();
            for (auto* pair : ctx->mapLit()->mapPair())
                map->entries.push_back({pair->name()->getText(), buildExpression(pair->expression())});
            return Expression(std::move(map));
        }
        return makeLiteral(NullValue{});
    }

    Expression buildParameter(AP::ParameterContext* ctx) {
        auto p = std::make_unique<Parameter>();
        if (ctx->symbol())
            p->name = ctx->symbol()->getText();
        else if (ctx->numLit())
            p->name = ctx->numLit()->getText();
        return Expression(std::move(p));
    }

    Expression buildCaseExpr(AP::CaseExpressionContext* ctx) {
        auto c = std::make_unique<CaseExpr>();
        auto whenCount = ctx->WHEN().size();
        auto totalExprs = ctx->expression().size();
        auto expected = whenCount * 2 + (ctx->ELSE() ? 1 : 0);
        if (totalExprs > expected)
            c->subject = buildExpression(ctx->expression(0));
        for (size_t i = 0; i < whenCount; i++) {
            size_t base = (c->subject ? 1 : 0) + i * 2;
            c->when_thens.push_back(
                {buildExpression(ctx->expression(base)), buildExpression(ctx->expression(base + 1))});
        }
        if (ctx->ELSE()) {
            size_t idx = (c->subject ? 1 : 0) + whenCount * 2;
            c->else_expr = buildExpression(ctx->expression(idx));
        }
        return Expression(std::move(c));
    }

    Expression buildListComprehension(AP::ListComprehensionContext* ctx) {
        auto lc = std::make_unique<ListComprehension>();
        auto fe = ctx->filterExpression();
        lc->variable = fe->symbol()->getText();
        lc->list_expr = buildExpression(fe->expression());
        if (fe->where())
            lc->where_pred = buildExpression(fe->where()->expression());
        if (ctx->expression())
            lc->projection = buildExpression(ctx->expression());
        return Expression(std::move(lc));
    }

    Expression buildPatternComprehension(AP::PatternComprehensionContext* ctx) {
        auto pc = std::make_unique<PatternComprehension>();
        if (ctx->lhs())
            pc->variable = ctx->lhs()->symbol()->getText();
        if (auto* chain = ctx->relationshipsChainPattern()) {
            PatternPart pp;
            pp.element = buildPatternElemFromChain(chain);
            pc->patterns.push_back(std::move(pp));
        }
        if (ctx->where())
            pc->where_pred = buildExpression(ctx->where()->expression());
        if (ctx->expression())
            pc->projection = buildExpression(ctx->expression());
        return Expression(std::move(pc));
    }

    Expression buildFilterWith(AP::FilterWithContext* ctx) {
        auto fe = ctx->filterExpression();
        std::string var = fe->symbol()->getText();
        Expression listExpr = buildExpression(fe->expression());
        std::optional<Expression> wp;
        if (fe->where())
            wp = buildExpression(fe->where()->expression());

        if (ctx->ALL()) {
            auto e = std::make_unique<AllExpr>();
            e->variable = var;
            e->list_expr = std::move(listExpr);
            e->where_pred = std::move(wp);
            return Expression(std::move(e));
        }
        if (ctx->ANY()) {
            auto e = std::make_unique<AnyExpr>();
            e->variable = var;
            e->list_expr = std::move(listExpr);
            e->where_pred = std::move(wp);
            return Expression(std::move(e));
        }
        if (ctx->NONE()) {
            auto e = std::make_unique<NoneExpr>();
            e->variable = var;
            e->list_expr = std::move(listExpr);
            e->where_pred = std::move(wp);
            return Expression(std::move(e));
        }
        auto e = std::make_unique<SingleExpr>();
        e->variable = var;
        e->list_expr = std::move(listExpr);
        e->where_pred = std::move(wp);
        return Expression(std::move(e));
    }

    Expression buildFuncInvocation(AP::FunctionInvocationContext* ctx) {
        auto fn = std::make_unique<FunctionCall>();
        fn->name = buildInvocationName(ctx->invocationName());
        fn->distinct = (ctx->DISTINCT() != nullptr);
        if (ctx->expressionChain())
            fn->args = buildExprChain(ctx->expressionChain());
        return Expression(std::move(fn));
    }

    Expression buildExistsExpr(AP::SubqueryExistContext* ctx) {
        auto ex = std::make_unique<ExistsExpr>();
        if (ctx->patternWhere()) {
            ex->patterns = buildPattern(ctx->patternWhere()->pattern());
            if (ctx->patternWhere()->where())
                ex->where_pred = buildExpression(ctx->patternWhere()->where()->expression());
        }
        return Expression(std::move(ex));
    }

    // ==================== Pattern ====================

    std::vector<PatternPart> buildPattern(AP::PatternContext* ctx) {
        std::vector<PatternPart> parts;
        for (auto* pp : ctx->patternPart())
            parts.push_back(buildPatternPart(pp));
        return parts;
    }

    PatternPart buildPatternPart(AP::PatternPartContext* ctx) {
        PatternPart pp;
        if (ctx->symbol() && ctx->ASSIGN())
            pp.variable = ctx->symbol()->getText();
        pp.element = buildPatternElem(ctx->patternElem());
        return pp;
    }

    PatternElement buildPatternElem(AP::PatternElemContext* ctx) {
        if (ctx->LPAREN())
            return buildPatternElem(ctx->patternElem());
        PatternElement pe;
        pe.node = buildNodePattern(ctx->nodePattern());
        for (auto* chain : ctx->patternElemChain())
            pe.chain.push_back({buildRelPattern(chain->relationshipPattern()), buildNodePattern(chain->nodePattern())});
        return pe;
    }

    PatternElement buildPatternElemFromChain(AP::RelationshipsChainPatternContext* ctx) {
        PatternElement pe;
        pe.node = buildNodePattern(ctx->nodePattern());
        for (auto* chain : ctx->patternElemChain())
            pe.chain.push_back({buildRelPattern(chain->relationshipPattern()), buildNodePattern(chain->nodePattern())});
        return pe;
    }

    NodePattern buildNodePattern(AP::NodePatternContext* ctx) {
        NodePattern np;
        if (ctx->symbol())
            np.variable = ctx->symbol()->getText();
        if (ctx->nodeLabels())
            for (auto* n : ctx->nodeLabels()->name())
                np.labels.push_back(n->getText());
        if (ctx->properties())
            np.properties = buildProperties(ctx->properties());
        return np;
    }

    RelationshipPattern buildRelPattern(AP::RelationshipPatternContext* ctx) {
        RelationshipPattern rp;
        if (ctx->LT())
            rp.direction = RelationshipDirection::RIGHT_TO_LEFT;
        else if (ctx->GT())
            rp.direction = RelationshipDirection::LEFT_TO_RIGHT;
        else
            rp.direction = RelationshipDirection::UNDIRECTED;
        if (auto* d = ctx->relationDetail()) {
            if (d->symbol())
                rp.variable = d->symbol()->getText();
            if (d->relationshipTypes())
                for (auto* n : d->relationshipTypes()->name())
                    rp.rel_types.push_back(n->getText());
            if (d->properties())
                rp.properties = buildProperties(d->properties());
        }
        return rp;
    }

    std::optional<PropertiesMap> buildProperties(AP::PropertiesContext* ctx) {
        if (!ctx->mapLit())
            return std::nullopt;
        PropertiesMap pm;
        for (auto* pair : ctx->mapLit()->mapPair())
            pm.entries.push_back({pair->name()->getText(), buildExpression(pair->expression())});
        return pm;
    }

    // ==================== Projection ====================

    std::unique_ptr<ReturnClause> buildProjectionBody(AP::ProjectionBodyContext* ctx) {
        auto ret = std::make_unique<ReturnClause>();
        ret->distinct = (ctx->DISTINCT() != nullptr);
        auto* items = ctx->projectionItems();
        if (items->MULT()) {
            ret->return_all = true;
        } else {
            for (auto* item : items->projectionItem()) {
                ReturnItem ri;
                ri.expr = buildExpression(item->expression());
                if (item->AS() && item->symbol())
                    ri.alias = item->symbol()->getText();
                ret->items.push_back(std::move(ri));
            }
        }
        if (ctx->orderSt())
            ret->order_by = buildOrderBy(ctx->orderSt());
        if (ctx->skipSt())
            ret->skip = buildExpression(ctx->skipSt()->expression());
        if (ctx->limitSt())
            ret->limit = buildExpression(ctx->limitSt()->expression());
        return ret;
    }

    std::unique_ptr<WithClause> buildProjectionBodyAsWith(AP::ProjectionBodyContext* ctx) {
        auto w = std::make_unique<WithClause>();
        w->distinct = (ctx->DISTINCT() != nullptr);
        auto* items = ctx->projectionItems();
        if (items->MULT()) {
            w->return_all = true;
        } else {
            for (auto* item : items->projectionItem()) {
                ReturnItem ri;
                ri.expr = buildExpression(item->expression());
                if (item->AS() && item->symbol())
                    ri.alias = item->symbol()->getText();
                w->items.push_back(std::move(ri));
            }
        }
        if (ctx->orderSt())
            w->order_by = buildOrderBy(ctx->orderSt());
        if (ctx->skipSt())
            w->skip = buildExpression(ctx->skipSt()->expression());
        if (ctx->limitSt())
            w->limit = buildExpression(ctx->limitSt()->expression());
        return w;
    }

    OrderBy buildOrderBy(AP::OrderStContext* ctx) {
        OrderBy ob;
        for (auto* item : ctx->orderItem()) {
            OrderBy::SortItem si;
            si.expr = buildExpression(item->expression());
            if (item->DESC() || item->DESCENDING())
                si.direction = OrderBy::Direction::DESC;
            ob.items.push_back(std::move(si));
        }
        return ob;
    }

    // ==================== Set Items ====================

    std::vector<SetItem> buildSetItems(AP::SetStContext* ctx) {
        std::vector<SetItem> items;
        for (auto* si : ctx->setItem()) {
            SetItem item;
            if (si->propertyExpression() && si->expression()) {
                item.kind = SetItemKind::SET_PROPERTY;
                item.target = buildPropertyExpression(si->propertyExpression());
                item.value = buildExpression(si->expression());
            } else if (si->symbol() && si->nodeLabels()) {
                item.kind = SetItemKind::SET_LABELS;
                item.target = makeVariable(si->symbol()->getText());
                for (auto* n : si->nodeLabels()->name())
                    item.label = n->getText();
            } else if (si->symbol() && (si->ASSIGN() || si->ADD_ASSIGN())) {
                item.kind = SetItemKind::SET_PROPERTIES;
                item.target = makeVariable(si->symbol()->getText());
                item.value = buildExpression(si->expression());
            }
            items.push_back(std::move(item));
        }
        return items;
    }

    // ==================== Helpers ====================

    std::vector<Expression> buildExprChain(AP::ExpressionChainContext* ctx) {
        std::vector<Expression> r;
        if (ctx)
            for (auto* e : ctx->expression())
                r.push_back(buildExpression(e));
        return r;
    }

    std::vector<Expression> buildExprChainFromParen(AP::ParenExpressionChainContext* ctx) {
        std::vector<Expression> r;
        if (ctx && ctx->expressionChain())
            for (auto* e : ctx->expressionChain()->expression())
                r.push_back(buildExpression(e));
        return r;
    }

    std::vector<ReturnItem> buildYieldItems(AP::YieldItemsContext* ctx) {
        std::vector<ReturnItem> items;
        for (auto* yi : ctx->yieldItem()) {
            ReturnItem ri;
            auto syms = yi->symbol();
            if (syms.size() == 2) {
                ri.expr = makeVariable(syms[0]->getText());
                ri.alias = syms[1]->getText();
            } else if (syms.size() == 1) {
                ri.expr = makeVariable(syms[0]->getText());
            }
            items.push_back(std::move(ri));
        }
        return items;
    }

    std::string buildInvocationName(AP::InvocationNameContext* ctx) {
        std::string r;
        auto syms = ctx->symbol();
        for (size_t i = 0; i < syms.size(); i++) {
            if (i)
                r += ".";
            r += syms[i]->getText();
        }
        return r;
    }
};

// ==================== CypherQueryParser ====================

struct CypherQueryParser::Impl {};

CypherQueryParser::CypherQueryParser() : impl_(std::make_unique<Impl>()) {}
CypherQueryParser::~CypherQueryParser() = default;

std::variant<Statement, ParseError> CypherQueryParser::parse(const std::string& cypher_text) {
    antlr4::ANTLRInputStream input(cypher_text);

    CypherLexer lexer(&input);
    ParseErrorListener error_listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();

    if (error_listener.hasError())
        return error_listener.getError();

    ::CypherParser antlrParser(&tokens);
    antlrParser.removeErrorListeners();
    antlrParser.addErrorListener(&error_listener);

    auto* tree = antlrParser.script();

    if (error_listener.hasError())
        return error_listener.getError();

    AstBuilder builder;
    return builder.build(tree);
}

} // namespace cypher
} // namespace eugraph
