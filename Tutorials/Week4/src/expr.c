#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "expr.h"
#include "lex.h"

#define TEMP_VAR_NAME_FMT "t%d"

#define EmitIR printf

/////////////////////////////////////////////////////////////////////

// the number of temporary variables
static int tmpNo;

/////////////////////////////////////////////////////////////////////

// Temporary variables are named as "t0", "t1", "t2", ....
// They are generated by arithmetic operations like a+b, a-b, a*b, a/b.
static int NewTemp(void) { return tmpNo++; }

/*
    Create an Ast node for an expression.
 */
AstExprNodePtr CreateAstExprNode(TokenKind tk, Value *pVal, AstExprNodePtr left,
                                 AstExprNodePtr right) {
    AstExprNodePtr pNode = (AstExprNodePtr)malloc(sizeof(struct astExprNode));
    assert(pNode != NULL && pVal != NULL);

    memset(pNode, 0, sizeof(*pNode));
    pNode->op = tk;
    pNode->value = *pVal;
    pNode->kids[0] = left;
    pNode->kids[1] = right;
    return pNode;
}

/*
    PrimaryExpression:
        NUM
        (Expression)
 */
static AstExprNodePtr PrimaryExpression(void) {
    AstExprNodePtr expr = NULL;
    if (curToken.kind == TK_NUM) {
        expr = CreateAstExprNode(curToken.kind, &curToken.value, NULL, NULL);
        NEXT_TOKEN;
    } else if (curToken.kind == TK_LPAREN) {
        NEXT_TOKEN;
        expr = Expression();
        Expect(TK_RPAREN);
    } else {
        Error("number or '(' expected");
    }
    return expr;
}

/*
    Here, mop stands for a multiplicative operator.
  
    mop:
                *
                /
  
    MultiplicativeExpression:
            PrimaryExpression
            PrimaryExpression  mop PrimaryExpression  ...   mop PrimaryExpression
 */
static AstExprNodePtr MultiplicativeExpression(void) {
    AstExprNodePtr left;
    left = PrimaryExpression();
    while (curToken.kind == TK_MUL || curToken.kind == TK_DIV) {
        Value value;
        AstExprNodePtr expr;
        memset(&value, 0, sizeof(value));
        snprintf(value.name, MAX_ID_LEN, TEMP_VAR_NAME_FMT, NewTemp());
        // create a tree node for * or /
        expr = CreateAstExprNode(curToken.kind, &value, NULL, NULL);
        // skip '*' or '/'
        NEXT_TOKEN;
        expr->kids[0] = left;
        expr->kids[1] = PrimaryExpression();
        left = expr;
    }
    return left;
}

/*
    Here, aop stands for an additive operator.
  
    aop:
                +
                -
  
    AdditiveExpression:
          MultiplicativeExpression
          MultiplicativeExpression  aop MultiplicativeExpression  ...   aop  MultiplicativeExpression
 */
static AstExprNodePtr AdditiveExpression(void) {
    AstExprNodePtr left;
    /*
        Take "9000  +  ( 6  *  4 )" as an example.

        9000  +  ( 6  *  4 )
        ^
        ^
        Current Token

        Let's call MultiplicativeExpression() to parse the first
        MultiplicativeExpression in an AdditiveExpression.
        MultiplicativeExpression() will return a sub-tree for "9000"
     */
    left = MultiplicativeExpression();
    /*
        Now, the curToken points to '+' (i.e, TK_ADD)
  
        9000  +  ( 6  *  4 )
              ^
              ^
              Current Token
  
        Parse "aop MultiplicativeExpression" if they are in the input stream
     */
    while (curToken.kind == TK_SUB || curToken.kind == TK_ADD) {
        Value value;
        AstExprNodePtr expr;
        memset(&value, 0, sizeof(value));
        // A temporary variable "t0" is assigned the value resulting from the
        // expression "t0 = 9000 + t1". "t1 = 6 * 4" will be created in
        // MultiplicativeExpression() later.
        snprintf(value.name, MAX_ID_LEN, TEMP_VAR_NAME_FMT, NewTemp());
        // create a tree node for '+' or '-'
        expr = CreateAstExprNode(curToken.kind, &value, NULL, NULL);
        // skip '+' or '-'
        NEXT_TOKEN;
        /*
            Now, the curToken points to '('
  
            9000  +  ( 6  *  4 )
                     ^
                     ^
                     Current Token
  
            Call MultiplicativeExpression() to parse the right operand "(6 * 4)"
            Again, it will return a sub-tree for "(6 * 4)"
        */
        expr->kids[0] = left;
        expr->kids[1] = MultiplicativeExpression();
        /*
            If the current token is '+' or '-', The while-loop will iterate once
            more. Otherwise, the while-loop will stop, meaning our parser has
            recognized an additive expression in the input stream.
  
            In this simple example, now, the current token is TK_EOF (end of
            file).
  
            9000  +  ( 6  *  4 )
                                  ^
                                  ^
                                  Current Token
  
         */
        left = expr;
    }
    return left;
}

void ReleaseAstExpr(AstExprNodePtr root) {
    if (root) {
        ReleaseAstExpr(root->kids[0]);
        ReleaseAstExpr(root->kids[1]);
        free(root);
    }
}

/*
    Expression:
        AdditiveExpression
  */
AstExprNodePtr Expression(void) { return AdditiveExpression(); }

static int isArithmeticOperator(TokenKind tk) {
    return tk == TK_ADD || tk == TK_SUB || tk == TK_MUL || tk == TK_DIV;
}

// In fact, it is a simple interpreter
long EvalExpression(AstExprNodePtr root) {
    assert(root);
    if (root->op == TK_NUM) { // 9000, 6, 4
        return root->value.numVal;
    } else if (isArithmeticOperator(root->op)) { // +, -, *, /
        //
        assert(root->kids[0]);
        assert(root->kids[1]);
        long leftOperand = EvalExpression(root->kids[0]);
        long rightOperand = EvalExpression(root->kids[1]);
        // Postorder traversal
        long result = 0;
        switch (root->op) {
        case TK_ADD:
            result = leftOperand + rightOperand;
            EmitIR("%s = %s + %s\n", root->value.name,
                   root->kids[0]->value.name, root->kids[1]->value.name);
            break;
        case TK_SUB:
            result = leftOperand - rightOperand;
            EmitIR("%s = %s - %s\n", root->value.name,
                   root->kids[0]->value.name, root->kids[1]->value.name);
            break;
        case TK_MUL:
            result = leftOperand * rightOperand;
            EmitIR("%s = %s * %s\n", root->value.name,
                   root->kids[0]->value.name, root->kids[1]->value.name);
            break;
        case TK_DIV:
            result = leftOperand / rightOperand;
            EmitIR("%s = %s / %s\n", root->value.name,
                   root->kids[0]->value.name, root->kids[1]->value.name);
            break;
        default:
            break;
        }
        return result;
    } else {
        Error("Unknown operator/operand");
        return 0;
    }
}


