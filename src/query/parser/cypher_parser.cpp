#include "query/parser/cypher_parser.hpp"

// ANTLR 生成的头文件（全局命名空间）
#include "CypherLexer.h"
#include "CypherParser.h"

#include <algorithm>
#include <antlr4-runtime.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>

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

// ==================== String Escape Processing ====================

// Process escape sequences in string/char literals.
// Handles \uXXXX unicode escapes and standard escapes (\n, \t, \\, etc.).
// Throws std::invalid_argument("InvalidUnicodeLiteral") on malformed \u.
static std::string unescapeString(const std::string& raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            result += raw[i];
            continue;
        }
        if (raw[i + 1] == 'u') {
            // Unicode escape: \u followed by optional extra 'u' chars (per
            // ANTLR grammar u'+') then exactly 4 hex digits.
            size_t j = i + 2;
            while (j < raw.size() && raw[j] == 'u')
                ++j;
            if (j + 4 > raw.size())
                throw std::invalid_argument("InvalidUnicodeLiteral");
            uint32_t cp = 0;
            for (int k = 0; k < 4; ++k) {
                char d = raw[j + k];
                uint32_t val = 0;
                if (d >= '0' && d <= '9')
                    val = d - '0';
                else if (d >= 'a' && d <= 'f')
                    val = d - 'a' + 10;
                else if (d >= 'A' && d <= 'F')
                    val = d - 'A' + 10;
                else
                    throw std::invalid_argument("InvalidUnicodeLiteral");
                cp = (cp << 4) | val;
            }
            if (cp > 0x10FFFF)
                throw std::invalid_argument("InvalidUnicodeLiteral");
            // Encode codepoint to UTF-8
            if (cp <= 0x7F) {
                result += static_cast<char>(cp);
            } else if (cp <= 0x7FF) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp <= 0xFFFF) {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (cp >> 18));
                result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
            i = j + 3;
        } else {
            // Standard escape sequences
            switch (raw[i + 1]) {
            case 'n':
                result += '\n';
                break;
            case 't':
                result += '\t';
                break;
            case 'r':
                result += '\r';
                break;
            case 'b':
                result += '\b';
                break;
            case 'f':
                result += '\f';
                break;
            case '\\':
                result += '\\';
                break;
            case '\'':
                result += '\'';
                break;
            case '"':
                result += '"';
                break;
            default:
                // Unknown escape: pass through as-is
                result += raw[i];
                break;
            }
            ++i; // skip the escaped character
        }
    }
    return result;
}

// ==================== Atomic Predicate Hoisting ====================
// The grammar places IN, IS NULL, IS NOT NULL, STARTS WITH, ENDS WITH, CONTAINS
// at atomicExpression level (below arithmetic). But Cypher spec says they bind
// looser than arithmetic and tighter than comparison. We restructure the AST
// after parsing to fix the precedence.

static bool isAtomicPredicateExpr(const Expression& expr) {
    if (auto* binOp = std::get_if<std::unique_ptr<BinaryOp>>(&expr)) {
        if (*binOp) {
            auto op = (*binOp)->op;
            return op == BinaryOperator::IN || op == BinaryOperator::STARTS_WITH || op == BinaryOperator::ENDS_WITH ||
                   op == BinaryOperator::CONTAINS;
        }
    }
    if (auto* unOp = std::get_if<std::unique_ptr<UnaryOp>>(&expr)) {
        if (*unOp) {
            auto op = (*unOp)->op;
            return op == UnaryOperator::IS_NULL || op == UnaryOperator::IS_NOT_NULL;
        }
    }
    return false;
}

// Hoists atomic predicates above arithmetic operators (ADD/SUB/MUL/DIV/MOD/POW).
// E.g. ADD(a, IN(b, c)) → IN(ADD(a, b), c)
static Expression hoistAtomicPredicates(Expression expr) {
    // ParenExpr: recurse into inner, re-wrap to preserve parentheses boundary.
    if (auto* paren = std::get_if<std::unique_ptr<ParenExpr>>(&expr)) {
        if (*paren) {
            (*paren)->inner = hoistAtomicPredicates(std::move((*paren)->inner));
        }
        return expr;
    }

    auto* binOp = std::get_if<std::unique_ptr<BinaryOp>>(&expr);
    if (!binOp || !*binOp)
        return expr;

    auto op = (*binOp)->op;
    bool isArith = (op == BinaryOperator::ADD || op == BinaryOperator::SUB || op == BinaryOperator::MUL ||
                    op == BinaryOperator::DIV || op == BinaryOperator::MOD || op == BinaryOperator::POW);
    if (!isArith) {
        (*binOp)->left = hoistAtomicPredicates(std::move((*binOp)->left));
        (*binOp)->right = hoistAtomicPredicates(std::move((*binOp)->right));
        return expr;
    }

    (*binOp)->left = hoistAtomicPredicates(std::move((*binOp)->left));
    (*binOp)->right = hoistAtomicPredicates(std::move((*binOp)->right));

    bool leftIsPred = isAtomicPredicateExpr((*binOp)->left);
    bool rightIsPred = isAtomicPredicateExpr((*binOp)->right);

    if (!leftIsPred && !rightIsPred)
        return expr;

    if (rightIsPred) {
        // OP(a, PRED(b, c)) → PRED(OP(a, b), c)
        auto* rbin = std::get_if<std::unique_ptr<BinaryOp>>(&(*binOp)->right);
        auto* run = std::get_if<std::unique_ptr<UnaryOp>>(&(*binOp)->right);
        if (rbin && *rbin) {
            auto ro = std::move(*rbin);
            ro->left = makeBinaryOp(op, std::move((*binOp)->left), std::move(ro->left));
            return hoistAtomicPredicates(Expression(std::move(ro)));
        }
        if (run && *run) {
            auto ruo = std::move(*run);
            ruo->operand = makeBinaryOp(op, std::move((*binOp)->left), std::move(ruo->operand));
            return hoistAtomicPredicates(Expression(std::move(ruo)));
        }
    }

    if (leftIsPred) {
        // OP(PRED(a, b), c) → PRED(a, OP(b, c))
        auto* lbin = std::get_if<std::unique_ptr<BinaryOp>>(&(*binOp)->left);
        auto* lun = std::get_if<std::unique_ptr<UnaryOp>>(&(*binOp)->left);
        if (lbin && *lbin) {
            auto lo = std::move(*lbin);
            lo->right = makeBinaryOp(op, std::move(lo->right), std::move((*binOp)->right));
            return hoistAtomicPredicates(Expression(std::move(lo)));
        }
        if (lun && *lun) {
            auto luo = std::move(*lun);
            luo->operand = makeBinaryOp(op, std::move(luo->operand), std::move((*binOp)->right));
            return hoistAtomicPredicates(Expression(std::move(luo)));
        }
    }

    return expr;
}

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
        bool is_explain = (ctx->EXPLAIN() != nullptr);
        if (ctx->regularQuery()) {
            auto rq = buildRegularQuery(ctx->regularQuery());
            if (is_explain) {
                auto es = std::make_unique<ExplainStatement>();
                es->query = std::move(rq);
                return Statement(std::move(es));
            }
            return Statement(std::move(rq));
        }
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
    // Mirrors openCypher BNF:
    //   <linear statement> ::= <primitive statement>... [ <primitive result statement> ]

    SingleQuery buildSingleQuery(AP::SingleQueryContext* ctx) {
        SingleQuery sq;
        for (auto* c : ctx->clause()) {
            if (auto* rc = c->readingClause()) {
                if (rc->matchSt())
                    sq.clauses.push_back(buildReadingStatementMatch(rc->matchSt()));
                else if (rc->unwindSt())
                    sq.clauses.push_back(buildReadingStatementUnwind(rc->unwindSt()));
                else if (rc->withSt())
                    sq.clauses.push_back(Clause(buildWithClause(rc->withSt())));
            } else if (auto* uc = c->updatingClause()) {
                sq.clauses.push_back(buildUpdatingStatement(uc));
            } else if (auto* cc = c->queryCallSt()) {
                sq.clauses.push_back(Clause(buildCallClause(cc)));
            }
        }
        if (auto* rs = ctx->primitiveResultStatement())
            sq.clauses.push_back(Clause(std::move(buildReturnClause(rs->returnSt()))));
        if (sq.clauses.empty())
            throw std::invalid_argument("UnexpectedSyntax");
        return sq;
    }

    Clause buildReadingStatementMatch(AP::MatchStContext* ctx) {
        return Clause(buildMatchClause(ctx));
    }
    Clause buildReadingStatementUnwind(AP::UnwindStContext* ctx) {
        return Clause(buildUnwindClause(ctx));
    }
    Clause buildReadingStatementCall(AP::QueryCallStContext* ctx) {
        return Clause(buildCallClause(ctx));
    }

    // === Clause Dispatch ===

    Clause buildReadingStatement(AP::ReadingStatementContext* ctx) {
        if (ctx->matchSt())
            return Clause(buildMatchClause(ctx->matchSt()));
        if (ctx->unwindSt())
            return Clause(buildUnwindClause(ctx->unwindSt()));
        return Clause(buildCallClause(ctx->queryCallSt()));
    }

    Clause buildUpdatingStatement(AP::UpdatingClauseContext* ctx) {
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
                // Extract prop_name from expression tree without destroying it
                if (auto* prop = std::get_if<std::unique_ptr<PropertyAccess>>(&ri.target)) {
                    ri.name = (*prop)->property;
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
        auto nots = ctx->NOT();
        // Wrap with NOT for each NOT token (NOT NOT x → NOT(NOT(x)))
        for (size_t i = nots.size(); i > 0; --i)
            e = makeUnaryOp(UnaryOperator::NOT, std::move(e));
        return e;
    }

    Expression buildComparisonExpr(AP::ComparisonExpressionContext* ctx) {
        auto adds = ctx->addSubExpression();
        auto signs = ctx->comparisonSigns();
        if (adds.size() == 1)
            return buildAddSubExpr(adds[0]);

        // Chained comparison (a < b < c): expands to (a < b) AND (b < c).
        // Each middle operand is shared between two adjacent pairwise comparisons.
        // E.g. for "1 < n.num <= 3":
        //   (1 < n.num) AND (n.num <= 3)
        std::vector<Expression> parts;
        for (size_t i = 0; i < signs.size(); i++)
            parts.push_back(
                makeBinaryOp(parseCompSign(signs[i]), buildAddSubExpr(adds[i]), buildAddSubExpr(adds[i + 1])));

        Expression r = std::move(parts[0]);
        for (size_t i = 1; i < parts.size(); i++)
            r = makeBinaryOp(BinaryOperator::AND, std::move(r), std::move(parts[i]));
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
            return hoistAtomicPredicates(buildMulDivExpr(muls[0]));
        Expression r = buildMulDivExpr(muls[0]);
        for (size_t i = 1; i < muls.size(); i++) {
            // Children layout: multDivExpr, operator, multDivExpr, operator, ...
            auto* opNode = static_cast<antlr4::tree::TerminalNode*>(ctx->children[2 * i - 1]);
            BinaryOperator op =
                (opNode->getSymbol()->getType() == AP::PLUS) ? BinaryOperator::ADD : BinaryOperator::SUB;
            r = makeBinaryOp(op, std::move(r), buildMulDivExpr(muls[i]));
        }
        return hoistAtomicPredicates(std::move(r));
    }

    Expression buildMulDivExpr(AP::MultDivExpressionContext* ctx) {
        auto pows = ctx->powerExpression();
        if (pows.size() == 1)
            return hoistAtomicPredicates(buildPowerExpr(pows[0]));
        Expression r = buildPowerExpr(pows[0]);
        for (size_t i = 1; i < pows.size(); i++) {
            // Children layout: powerExpression, operator, powerExpression, operator, ...
            auto& opNode = static_cast<antlr4::tree::TerminalNode&>(*ctx->children[2 * i - 1]);
            BinaryOperator op = BinaryOperator::MOD; // fallback
            switch (opNode.getSymbol()->getType()) {
            case AP::MULT:
                op = BinaryOperator::MUL;
                break;
            case AP::DIV:
                op = BinaryOperator::DIV;
                break;
            }
            r = makeBinaryOp(op, std::move(r), buildPowerExpr(pows[i]));
        }
        return hoistAtomicPredicates(std::move(r));
    }

    Expression buildPowerExpr(AP::PowerExpressionContext* ctx) {
        auto us = ctx->unaryAddSubExpression();
        if (us.size() == 1)
            return hoistAtomicPredicates(buildUnaryExpr(us[0]));
        Expression r = buildUnaryExpr(us[0]);
        for (size_t i = 1; i < us.size(); i++)
            r = makeBinaryOp(BinaryOperator::POW, std::move(r), buildUnaryExpr(us[i]));
        return hoistAtomicPredicates(std::move(r));
    }

    Expression buildUnaryExpr(AP::UnaryAddSubExpressionContext* ctx) {
        auto e = buildAtomicExpr(ctx->atomicExpression());
        if (ctx->SUB())
            return makeUnaryOp(UnaryOperator::NEGATE, std::move(e));
        return e;
    }

    // Apply a single listExpression context (subscript or slice) to `base`.
    // Walks children to distinguish implicit-start (`[..N]`) from implicit-end
    // (`[N..]`) slices: any expression appearing before RANGE is `from`; any
    // expression after RANGE is `to`.
    Expression applyListPostfix(AP::ListExpressionContext* le, Expression base) {
        if (le->RANGE()) {
            auto slice = std::make_unique<SliceExpr>();
            slice->list = std::move(base);
            // Walk children in source order to determine which expression is
            // `from` (before RANGE) vs `to` (after RANGE).
            bool seen_range = false;
            for (auto* child : le->children) {
                if (child == le->RANGE()) {
                    seen_range = true;
                    continue;
                }
                auto* expr_ctx = dynamic_cast<AP::ExpressionContext*>(child);
                if (!expr_ctx)
                    continue;
                Expression built = buildExpression(expr_ctx);
                if (!seen_range)
                    slice->from = std::move(built);
                else
                    slice->to = std::move(built);
            }
            return Expression(std::move(slice));
        }
        auto sub = std::make_unique<SubscriptExpr>();
        sub->list = std::move(base);
        if (!le->expression().empty())
            sub->index = buildExpression(le->expression(0));
        return Expression(std::move(sub));
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

        // Cypher precedence: list subscript `[...]` binds tighter than `IN`.
        // The grammar attaches both as sibling listExpressions to the same
        // atomicExpression, so for `a IN b[i]` we must attach `[i]` to `b`
        // (the IN's RHS) rather than to the IN result. We do this by folding
        // trailing subscripts into the IN's RHS before building the IN.
        auto list_exprs = ctx->listExpression();
        for (size_t i = 0; i < list_exprs.size(); ++i) {
            auto* le = list_exprs[i];
            if (le->IN()) {
                Expression right = buildPropOrLabelExpr(le->propertyOrLabelExpression());
                // Consume any immediately-following subscripts/slices so they
                // bind to the IN's RHS (correct Cypher precedence).
                while (i + 1 < list_exprs.size() && !list_exprs[i + 1]->IN()) {
                    right = applyListPostfix(list_exprs[i + 1], std::move(right));
                    ++i;
                }
                r = makeBinaryOp(BinaryOperator::IN, std::move(r), std::move(right));
            } else {
                r = applyListPostfix(le, std::move(r));
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
        Expression r = buildAtomNode(atom);
        // Process DOT name chains before ::
        auto names = ctx->name();
        for (auto* n : names) {
            std::string name_text = n->getText();
            // Strip backtick delimiters from ESC_LITERAL tokens
            if (name_text.size() >= 2 && name_text.front() == '`' && name_text.back() == '`')
                name_text = name_text.substr(1, name_text.size() - 2);
            r = makePropertyAccess(std::move(r), name_text);
        }
        // Process COLONCOLON labelCast if present
        if (ctx->COLONCOLON()) {
            auto* lc = ctx->labelCast();
            auto lc_names = lc->name();
            // First name after :: is the label
            r = makeLabelCast(std::move(r), lc_names[0]->getText());
            // Remaining names are property accesses
            for (size_t i = 1; i < lc_names.size(); i++) {
                std::string name_text = lc_names[i]->getText();
                if (name_text.size() >= 2 && name_text.front() == '`' && name_text.back() == '`')
                    name_text = name_text.substr(1, name_text.size() - 2);
                r = makePropertyAccess(std::move(r), name_text);
            }
        }
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
            return makeParenExpr(buildExpression(ctx->parenthesizedExpression()->expression()));
        if (ctx->functionInvocation())
            return buildFuncInvocation(ctx->functionInvocation());
        if (ctx->subqueryExist())
            return buildExistsExpr(ctx->subqueryExist());
        if (ctx->relationshipsChainPattern()) {
            // A bare pattern expression (e.g. `()--()` or `(a)-->(b)`) appearing
            // as an atom is only valid inside pattern comprehension or as a
            // pattern predicate. Using it as a value expression (e.g. inside a
            // function call like `size(...)`) is a syntax error.
            // Thrown as std::invalid_argument so that CypherQueryParser::parse()
            // catches it (its catch list excludes std::runtime_error) and
            // converts the message into a ParseError with SyntaxError prefix.
            throw std::invalid_argument("UnexpectedSyntax: pattern expression is not allowed in this context");
        }

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
            } catch (const std::out_of_range&) {
                throw; // Propagate overflow to parse() for proper error reporting
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
            return makeLiteral(unescapeString(t.substr(1, t.size() - 2)));
        }
        if (ctx->charLit()) {
            auto t = ctx->charLit()->getText();
            return makeLiteral(unescapeString(t.substr(1, t.size() - 2)));
        }
        if (ctx->numLit()) {
            auto t = ctx->numLit()->getText();
            try {
                if (t.find('.') != std::string::npos || t.find('e') != std::string::npos ||
                    t.find('E') != std::string::npos)
                    return makeLiteral(std::stod(t));
                return makeLiteral(static_cast<int64_t>(std::stoll(t)));
            } catch (const std::out_of_range&) {
                throw; // Propagate overflow to parse()
            }
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
            if (d->rangeLit()) {
                auto* rl = d->rangeLit();
                // Collect bound values from both numLit children and ID terminals,
                // ordered by token index. The ANTLR lexer may tokenize digits as ID.
                std::vector<std::pair<size_t, std::string>> bound_tokens;
                for (size_t i = 0; i < rl->numLit().size(); ++i)
                    bound_tokens.emplace_back(rl->numLit(i)->getStart()->getTokenIndex(), rl->numLit(i)->getText());
                for (size_t i = 0; i < rl->ID().size(); ++i)
                    bound_tokens.emplace_back(rl->ID(i)->getSymbol()->getTokenIndex(), rl->ID(i)->getText());
                std::sort(bound_tokens.begin(), bound_tokens.end());

                auto parseBound = [](const std::string& s) { return static_cast<int64_t>(std::stoll(s)); };
                bool has_range = rl->RANGE() != nullptr;
                if (has_range) {
                    // Patterns: *min..max, *..max, *min.., *..
                    // bound_tokens=[(i3,3),(i7,7)] => min=3,max=7
                    // bound_tokens=[(i3,3)] => *3.. or *..3 (check token vs RANGE)
                    // bound_tokens=[]      => *..
                    Expression min_expr, max_expr;
                    if (bound_tokens.size() == 2) {
                        min_expr = makeLiteral(parseBound(bound_tokens[0].second));
                        max_expr = makeLiteral(parseBound(bound_tokens[1].second));
                    } else if (bound_tokens.size() == 1) {
                        auto range_start = rl->RANGE()->getSymbol()->getTokenIndex();
                        if (bound_tokens[0].first < range_start) {
                            // bound before RANGE: *min..
                            min_expr = makeLiteral(parseBound(bound_tokens[0].second));
                            max_expr = makeLiteral(static_cast<int64_t>(-1));
                        } else {
                            // bound after RANGE: *..max
                            min_expr = makeLiteral(static_cast<int64_t>(1));
                            max_expr = makeLiteral(parseBound(bound_tokens[0].second));
                        }
                    } else {
                        // *.. (no bounds)
                        min_expr = makeLiteral(static_cast<int64_t>(1));
                        max_expr = makeLiteral(static_cast<int64_t>(-1));
                    }
                    rp.range = std::make_pair(std::move(min_expr), std::move(max_expr));
                } else if (!bound_tokens.empty()) {
                    // *n (exact hop count) => min=n, max=n
                    auto v = parseBound(bound_tokens[0].second);
                    rp.range = std::make_pair(makeLiteral(v), makeLiteral(v));
                } else {
                    // * (bare star) => unbounded, min=1, max=-1
                    rp.range =
                        std::make_pair(makeLiteral(static_cast<int64_t>(1)), makeLiteral(static_cast<int64_t>(-1)));
                }
            }
            if (d->properties())
                rp.properties = buildProperties(d->properties());
        }
        return rp;
    }

    std::optional<PropertiesMap> buildProperties(AP::PropertiesContext* ctx) {
        if (ctx->parameter() && !ctx->mapLit()) {
            PropertiesMap pm;
            pm.entries.push_back({"$param", buildParameter(ctx->parameter())});
            return pm;
        }
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
            if (si->symbol() && si->nodeLabels()) {
                item.kind = SetItemKind::SET_LABELS;
                item.target = makeVariable(si->symbol()->getText());
                for (auto* n : si->nodeLabels()->name())
                    item.label = n->getText();
            } else if (si->symbol() && (si->ASSIGN() || si->ADD_ASSIGN())) {
                item.kind = SetItemKind::SET_PROPERTIES;
                item.target = makeVariable(si->symbol()->getText());
                item.value = buildExpression(si->expression());
                item.is_add_assign = si->ADD_ASSIGN() != nullptr;
            } else if (si->propertyExpression() && si->expression()) {
                auto prop_expr = buildPropertyExpression(si->propertyExpression());
                // Distinguish SET n = map (Variable only) from SET n.prop = val (PropertyAccess)
                if (std::holds_alternative<std::unique_ptr<cypher::Variable>>(prop_expr)) {
                    item.kind = SetItemKind::SET_PROPERTIES;
                    item.target = std::move(prop_expr);
                    item.value = buildExpression(si->expression());
                } else {
                    item.kind = SetItemKind::SET_PROPERTY;
                    item.target = std::move(prop_expr);
                    item.value = buildExpression(si->expression());
                }
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

// Preprocess: replace hex/oct integer literals with decimal equivalents
// so the ANTLR lexer can tokenize them as plain DIGIT tokens.
// Matches: 0x[0-9a-fA-F_]+ and 0o[0-7_]+ optionally preceded by '-'
static std::string preprocessIntegerLiterals(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        char c = input[i];
        // Skip string/char literals: '...' and "..."
        if (c == '\'' || c == '"') {
            char quote = c;
            result += c;
            ++i;
            while (i < input.size() && input[i] != quote) {
                if (input[i] == '\\' && i + 1 < input.size()) {
                    if (input[i + 1] == 'u') {
                        // Validate \uXXXX in preprocessor so malformed
                        // escapes (e.g. \uH) are caught before ANTLR
                        // tokenization fails and produces garbled parse
                        // results.
                        size_t u = i + 2;
                        while (u < input.size() && input[u] == 'u')
                            ++u;
                        bool valid = (u + 4 <= input.size());
                        if (valid) {
                            for (int k = 0; k < 4; ++k) {
                                char d = input[u + k];
                                if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))) {
                                    valid = false;
                                    break;
                                }
                            }
                        }
                        if (!valid)
                            throw std::invalid_argument("InvalidUnicodeLiteral");
                    }
                    result += input[i++];
                    result += input[i++];
                } else {
                    result += input[i++];
                }
            }
            if (i < input.size()) {
                result += input[i];
                ++i;
            }
            continue;
        }
        // Check for hex/oct prefix: optional '-' then '0' then 'x'/'X' or 'o'/'O'
        bool negative = (c == '-' && i + 1 < input.size() && input[i + 1] == '0');
        size_t start = negative ? i + 1 : i;
        if (start + 1 < input.size() && input[start] == '0' &&
            (input[start + 1] == 'x' || input[start + 1] == 'X' || input[start + 1] == 'o' ||
             input[start + 1] == 'O')) {
            // Collect hex/oct digits
            bool is_hex = (input[start + 1] == 'x' || input[start + 1] == 'X');
            size_t j = start + 2; // skip 0x or 0o
            while (j < input.size()) {
                char d = input[j];
                if (is_hex && isxdigit(d)) {
                    ++j;
                    continue;
                }
                if (!is_hex && d >= '0' && d <= '7') {
                    ++j;
                    continue;
                }
                if (d == '_') {
                    ++j;
                    continue;
                }
                break;
            }
            // If no digits after 0x/0o prefix (e.g. "0x") or digits
            // followed by an alphabetic character (e.g. 0x1A2b3j...),
            // throw here so ANTLR's error recovery doesn't silently
            // produce a valid parse from the split tokens.
            bool no_digits = (j == start + 2);
            if (no_digits)
                throw std::invalid_argument("InvalidNumberLiteral");
            bool malformed = (j < input.size() && isalnum(static_cast<unsigned char>(input[j])));
            if (malformed)
                throw std::invalid_argument("InvalidNumberLiteral");
            if (j > start + 2) {
                // Parse and convert to decimal string
                std::string digits = input.substr(start + 2, j - (start + 2));
                digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
                errno = 0;
                int64_t val = std::strtoll(digits.c_str(), nullptr, is_hex ? 16 : 8);
                bool overflow = (errno == ERANGE);
                // Handle INT64_MIN edge case: -0x8000000000000000 = -2^63 = INT64_MIN.
                // strtoll overflows on the positive value 2^63, but the negative
                // is representable. Check via unsigned strtoull.
                if (overflow && negative) {
                    uint64_t uval = std::strtoull(digits.c_str(), nullptr, is_hex ? 16 : 8);
                    if (uval == static_cast<uint64_t>(INT64_MAX) + 1) {
                        overflow = false;
                        val = INT64_MIN;
                    }
                }
                if (!overflow && negative && val == INT64_MIN) {
                    // Check if unsigned value exceeds INT64_MAX+1
                    uint64_t uval = std::strtoull(digits.c_str(), nullptr, is_hex ? 16 : 8);
                    if (uval > static_cast<uint64_t>(INT64_MAX) + 1)
                        overflow = true;
                }
                if (overflow) {
                    // Emit a decimal guaranteed to overflow std::stoll
                    // so the parser's overflow handler produces a clean
                    // SyntaxError: IntegerOverflow message.
                    if (negative)
                        result += '-';
                    result += "99999999999999999999"; // 20 digits, > INT64_MAX
                } else if (negative) {
                    result += '-' + std::to_string(std::abs(val));
                } else {
                    result += std::to_string(val);
                }
                i = j;
                continue;
            }
        }

        // Check for invalid suffix after a decimal digit.
        // ANTLR tokenizes digit sequences greedily; a following
        // alphabetic character (other than exponent / float suffix)
        // or symbol produces split tokens that the parser may
        // silently accept through error recovery.
        if (isdigit(static_cast<unsigned char>(c))) {
            // If the digit is preceded by an alphanumeric character,
            // it's part of an identifier (e.g. map key "a1B2c3e67")
            // and should not be checked for number-literal validity.
            if (i > 0 && isalnum(static_cast<unsigned char>(input[i - 1]))) {
                result += c;
                ++i;
                continue;
            }
            size_t j = i;
            while (j < input.size() && (isdigit(static_cast<unsigned char>(input[j])) || input[j] == '_'))
                ++j;
            if (j < input.size()) {
                char nc = input[j];
                if (isalpha(static_cast<unsigned char>(nc)) && nc != 'e' && nc != 'E' && nc != 'f' && nc != 'F' &&
                    nc != 'd' && nc != 'D') {
                    // In map context (after '{'), a digit-letter sequence is
                    // an invalid map key, not an invalid number literal.
                    if (i > 0 && input[i - 1] == '{')
                        throw std::invalid_argument("UnexpectedSyntax");
                    throw std::invalid_argument("InvalidNumberLiteral");
                }
                if (nc == '#')
                    throw std::invalid_argument("UnexpectedSyntax");
            }
        }

        result += c;
        ++i;
    }
    return result;
}

std::variant<Statement, ParseError> CypherQueryParser::parse(const std::string& cypher_text) {
    // U+2014 (EM DASH) is used as a minus sign in some contexts but is
    // invalid in Cypher: SyntaxError: InvalidUnicodeCharacter
    if (cypher_text.find("\xE2\x80\x94") != std::string::npos) {
        ParseError err;
        err.message = "SyntaxError: InvalidUnicodeCharacter";
        return err;
    }
    std::string preprocessed;
    try {
        preprocessed = preprocessIntegerLiterals(cypher_text);
    } catch (const std::invalid_argument& e) {
        ParseError err;
        std::string what_str = e.what();
        if (what_str.find("InvalidUnicodeLiteral") != std::string::npos) {
            err.message = "SyntaxError: InvalidUnicodeLiteral";
        } else if (what_str.find("InvalidNumberLiteral") != std::string::npos) {
            err.message = "SyntaxError: InvalidNumberLiteral";
        } else if (what_str.find("UnexpectedSyntax") != std::string::npos) {
            err.message = "SyntaxError: UnexpectedSyntax";
        } else {
            err.message = std::string("Invalid number: ") + what_str;
        }
        return err;
    }
    antlr4::ANTLRInputStream input(preprocessed);

    CypherLexer lexer(&input);
    ParseErrorListener error_listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();

    if (error_listener.hasError()) {
        ParseError err = error_listener.getError();
        err.message = "SyntaxError: UnexpectedSyntax";
        return err;
    }

    ::CypherParser antlrParser(&tokens);
    antlrParser.removeErrorListeners();
    antlrParser.addErrorListener(&error_listener);

    auto* tree = antlrParser.script();

    if (error_listener.hasError()) {
        ParseError err = error_listener.getError();
        err.message = "SyntaxError: UnexpectedSyntax";
        return err;
    }

    AstBuilder builder;
    try {
        return builder.build(tree);
    } catch (const std::out_of_range& e) {
        ParseError err;
        // Use "SyntaxError" prefix so TCK error type classifier recognizes it.
        // "FloatingPointOverflow" matches the TCK-expected error detail.
        // e.what() from std::stod/stoll typically contains "stod" or "stoll" —
        // check for a dot/exponent to distinguish float vs integer overflow.
        std::string what_str = e.what();
        bool is_float = (what_str.find("stod") != std::string::npos || what_str.find("stof") != std::string::npos);
        err.message = std::string("SyntaxError: ") + (is_float ? "FloatingPointOverflow" : "IntegerOverflow");
        return err;
    } catch (const std::invalid_argument& e) {
        ParseError err;
        std::string what_str = e.what();
        if (what_str.find("InvalidUnicodeLiteral") != std::string::npos) {
            err.message = "SyntaxError: InvalidUnicodeLiteral";
        } else if (what_str.find("InvalidNumberLiteral") != std::string::npos) {
            err.message = "SyntaxError: InvalidNumberLiteral";
        } else if (what_str.find("UnexpectedSyntax") != std::string::npos) {
            err.message = "SyntaxError: UnexpectedSyntax";
        } else {
            err.message = std::string("Invalid number: ") + what_str;
        }
        return err;
    }
}

} // namespace cypher
} // namespace eugraph
