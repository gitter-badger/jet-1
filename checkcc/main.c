
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "cycle.h"
#include "chstd.h"

#define STEP 4

#include "types.h"
#include "tokenKind.h"
#include "token.h"

#define JOIN(x, y) x##y

#define NAME_CLASS(T) const char* JOIN(T, _typeName) = #T;

static const char* const spaces = //
    "                                                                     ";

#pragma mark - AST TYPE DEFINITIONS

typedef struct ASTImport {
    char* importFile;
    uint32_t aliasOffset;
    bool isPackage, hasAlias;
} ASTImport;

typedef struct ASTUnits {
    uint8_t powers[7], something;
    double factor, factors[7];
    char* label;
} ASTUnits;

typedef struct ASTTypeSpec {
    union {
        struct ASTType* type;
        char* name;
        ASTUnits* units;
    };
    uint32_t dims : 24, col : 8;
    uint16_t line;
    TypeTypes typeType : 8;
    CollectionTypes collectionType : 8;
} ASTTypeSpec;

typedef struct ASTVar {
    ASTTypeSpec* typeSpec;
    struct ASTExpr* init;
    char* name;
    uint16_t line;
    struct {
        bool used : 1, //
            changed : 1, //
            isLet : 1, //
            isVar : 1, //
            stackAlloc : 1, // this var is of a ref type, but it will be
                            // stack allocated. it will be passed around by
                            // reference as usual.
            isTarget : 1, // x = f(x,y)
            printed : 1, // for checks, used to avoid printing this var more
                         // than once.
            escapes : 1, // does it escape the owning SCOPE?
            canInplace : 1,
            returned : 1; // is a return variable ie. b in
        // function asd(x as Anc) returns (b as Whatever)
        // all args (in/out) are in the same list in func -> args.
    } flags;
    uint8_t col;
} ASTVar;

// when does something escape a scope?
// -- if it is assigned to a variable outside the scope
//    -- for func toplevels, one such var is the return var: anything
//    assigned to it escapes
// -- if it is passed to a func as an arg and the arg `escapes`
//    sempass sets `escapes` on each arg as it traverses the func

typedef struct ASTExpr {
    struct {
        uint16_t line;
        union {
            struct {
                uint16_t typeType : 8, // typeType of this expression -> must
                                       // match for ->left and ->right
                    collectionType : 4, // collectionType of this expr -> the
                                        // higher dim-type of left's and right's
                                        // collectionType.
                    nullable : 1, // is this expr nullable (applies only when
                                  // typeType is object.) generally will be set
                                  // on idents and func calls etc. since
                                  // arithmetic ops are not relevant to objects.
                                  // the OR expr may unset nullable: e.g.
                                  // `someNullabeFunc(..) or MyType()` is NOT
                                  // nullable.
                    impure : 1, // is this expr impure, has side effects?
                                // propagates: true if if either left or right
                                // is impure.
                    elemental : 1, // whether this expr is elemental.
                                   // propagates: true if either left or right
                                   // is elemental.
                    throws : 1; // whether this expr may throw an error.
                                // propagates: true if either left or right
                                // throws.
            };
            uint16_t allTypeInfo; // set this to set everything about the type
        };
        // blow this bool to bits to store more flags
        bool promote : 1, // should this expr be promoted, e.g.
                          // count(arr[arr<34]) or sum{arr[3:9]}. does not
                          // propagate.
            canEval : 1, didEval : 1;
        uint8_t prec : 6, // operator precedence for this expr
            unary : 1, // for an operator, is it unary (negation, not,
                       // return, check, array literal, ...)
            rassoc : 1; // is this a right-associative operator e.g.
                        // exponentiation
        uint8_t col;
        TokenKind kind : 8;
    };
    struct ASTExpr* left;
    union {
        char* string;
        double real;
        int64_t integer;
        uint64_t uinteger;
        // char* name; // for idents or unresolved call or subscript
        struct ASTFunc* func; // for functioncall
        struct ASTVar* var; // for array subscript, or a tkVarAssign
        struct ASTScope* body; // for if/for/while
        struct ASTExpr* right;
    };
    // TODO: the code motion routine should skip over exprs with
    // promote=false this is set for exprs with func calls or array
    // filtering etc...
} ASTExpr;

typedef struct ASTScope {
    List(ASTExpr) * stmts;
    List(ASTVar) * locals;
    struct ASTScope* parent;
    // still space left
} ASTScope;

typedef struct ASTType {
    ASTTypeSpec* super;
    char* name;
    // TODO: flags: hasUserInit : whether user has defined Type() ctor
    ASTScope* body;
    uint16_t line;
    uint8_t col;
    struct {
        bool sempassDone : 1,
            isValueType : 1; // all vars of this type will be stack
                             // allocated and passed around by value.

    } flags;
} ASTType;

typedef struct ASTFunc {
    ASTScope* body;
    List(ASTVar) * args;
    ASTTypeSpec* returnType;
    char* name;
    char* selector;
    struct {
        uint16_t line;
        struct {
            uint16_t usesIO : 1, throws : 1, isRecursive : 1, usesNet : 1,
                usesGUI : 1, usesSerialisation : 1, isExported : 1,
                usesReflection : 1, nodispatch : 1, isStmt : 1, isDeclare : 1,
                isCalledFromWithinLoop : 1, elemental : 1, isDefCtor : 1,
                semPassDone : 1;
        } flags;
        uint8_t argCount;
    };
} ASTFunc;

typedef struct ASTTest {
    ASTScope* body;
    char* name;
    char* selector;
    struct {
        uint16_t line;
        struct {
            uint16_t semPassDone : 1;
        } flags;
    };
} ASTTest;

typedef struct ASTModule {
    List(ASTFunc) * funcs;
    List(ASTTest) * tests;
    List(ASTExpr) * exprs;
    List(ASTType) * types;
    List(ASTVar) * globals;
    List(ASTImport) * imports;
    // List(ASTFunc) * tests;
    char* name;
    char* moduleName;
} ASTModule;

#pragma mark - AST IMPORT IMPL.

#pragma mark - AST UNITS IMPL.

struct ASTTypeSpec;
struct ASTType;
struct ASTFunc;
struct ASTScope;
struct ASTExpr;
struct ASTVar;

#define List_ASTExpr PtrList
#define List_ASTVar PtrList
#define List_ASTModule PtrList
#define List_ASTFunc PtrList
#define List_ASTTest PtrList
#define List_ASTType PtrList
#define List_ASTImport PtrList
#define List_ASTScope PtrList

// these should be generated by the compiler in DEBUG mode
MKSTAT(ASTExpr)
MKSTAT(ASTFunc)
MKSTAT(ASTTest)
MKSTAT(ASTTypeSpec)
MKSTAT(ASTType)
MKSTAT(ASTModule)
MKSTAT(ASTScope)
MKSTAT(ASTImport)
MKSTAT(ASTVar)
MKSTAT(Parser)
MKSTAT(List_ASTExpr)
MKSTAT(List_ASTFunc)
MKSTAT(List_ASTTest)
MKSTAT(List_ASTType)
MKSTAT(List_ASTModule)
MKSTAT(List_ASTScope)
MKSTAT(List_ASTImport)
MKSTAT(List_ASTVar)
static uint32_t exprsAllocHistogram[128];

static ASTTypeSpec* ASTTypeSpec_new(TypeTypes tt, CollectionTypes ct)
{
    ASTTypeSpec* ret = NEW(ASTTypeSpec);
    ret->typeType = tt;
    ret->collectionType = ct;
    return ret;
}

static const char* ASTTypeSpec_name(ASTTypeSpec* this)
{
    switch (this->typeType) {
    case TYUnresolved:
        return this->name;
    case TYObject:
        return this->type->name;
    default:
        return TypeType_name(this->typeType);
    }
    // what about collectiontype???
}

static const char* ASTTypeSpec_cname(ASTTypeSpec* this)
{
    switch (this->typeType) {
    case TYUnresolved:
        return this->name;
    case TYObject:
        return this->type->name;
    default:
        return TypeType_name(this->typeType);
    }
    // what about collectiontype???
}

static const char* getDefaultValueForType(ASTTypeSpec* type)
{
    if (not type) return "";
    switch (type->typeType) {
    case TYUnresolved:
        unreachable(
            "unresolved: '%s' at %d:%d", type->name, type->line, type->col);
        // assert(0);
        return "ERROR_ERROR_ERROR";
    case TYString:
        return "\"\"";
    default:
        return "0";
    }
}

static ASTExpr* ASTExpr_fromToken(const Token* this)
{
    ASTExpr* ret = NEW(ASTExpr);
    ret->kind = this->kind;
    ret->line = this->line;
    ret->col = this->col;

    ret->prec = TokenKind_getPrecedence(ret->kind);
    if (ret->prec) {
        ret->rassoc = TokenKind_isRightAssociative(ret->kind);
        ret->unary = TokenKind_isUnary(ret->kind);
    }

    exprsAllocHistogram[ret->kind]++;

    switch (ret->kind) {
    case tkIdentifier:
    case tkString:
    case tkRegex:
    case tkInline:
    case tkNumber:
    case tkMultiDotNumber:
    case tkLineComment: // Comments go in the AST like regular stmts
        ret->string = this->pos;
        break;
    default:;
    }
    // the '!' will be trampled
    if (ret->kind == tkLineComment) ret->string++;
    // turn all 1.0234[DdE]+01 into 1.0234e+01.
    if (ret->kind == tkNumber) {
        str_tr_ip(ret->string, 'd', 'e', this->matchlen);
        str_tr_ip(ret->string, 'D', 'e', this->matchlen);
        str_tr_ip(ret->string, 'E', 'e', this->matchlen);
    }
    return ret;
}

static bool ASTExpr_throws(ASTExpr* this)
{ // NOOO REMOVE This func and set the throws flag recursively like the
  // other flags (during e.g. the type resolution dive)
    if (not this) return false;
    switch (this->kind) {
    case tkNumber:
    case tkMultiDotNumber:
    case tkRegex:
    case tkInline:
    case tkIdentifier:
    case tkIdentifierResolved:
    case tkString:
    case tkLineComment:
        return false;
    case tkFunctionCall:
    case tkFunctionCallResolved:
        return true; // this->func->flags.throws;
        // actually  only if the func really throws
    case tkSubscript:
    case tkSubscriptResolved:
        return ASTExpr_throws(this->left);
    case tkVarAssign:
        return ASTExpr_throws(this->var->init);
    case tkKeyword_for:
    case tkKeyword_if:
    case tkKeyword_while:
        return false; // actually the condition could throw.
    default:
        if (not this->prec) return false;
        return ASTExpr_throws(this->left) or ASTExpr_throws(this->right);
    }
}

static size_t ASTScope_calcSizeUsage(ASTScope* this)
{
    size_t size = 0, sum = 0, subsize = 0, maxsubsize = 0;
    // all variables must be resolved before calling this
    foreach (ASTExpr*, stmt, this->stmts) {
        switch (stmt->kind) {
        case tkKeyword_if:
        case tkKeyword_else:
        case tkKeyword_for:
        case tkKeyword_while:
            subsize = ASTScope_calcSizeUsage(stmt->body);
            if (subsize > maxsubsize) maxsubsize = subsize;
            break;
        default:;
        }
    }
    // some vars are not assigned, esp. temporaries _1 _2 etc.
    foreach (ASTVar*, var, this->locals) {
        size = TypeType_size(var->typeSpec->typeType);
        assert(size);
        if (var->flags.used) sum += size;
    }
    // add the largest size among the sizes of the sub-scopes
    sum += maxsubsize;
    return sum;
}

static ASTVar* ASTScope_getVar(ASTScope* this, const char* name)
{
    // stupid linear search, no dictionary yet
    foreach (ASTVar*, local, this->locals)
        if (not strcasecmp(name, local->name)) return local;
    if (this->parent) return ASTScope_getVar(this->parent, name);
    return NULL;
}

static ASTVar* ASTType_getVar(ASTType* this, const char* name)
{
    // stupid linear search, no dictionary yet
    foreach (ASTVar*, var, this->body->locals)
        if (not strcasecmp(name, var->name)) return var;

    if (this->super and this->super->typeType == TYObject)
        return ASTType_getVar(this->super->type, name);
    return NULL;
}

#pragma mark - AST FUNC IMPL.

static ASTFunc* ASTFunc_createDeclWithArg(
    char* name, char* retType, char* arg1Type)
{
    ASTFunc* func = NEW(ASTFunc);
    func->name = name;
    func->flags.isDeclare = true;
    if (retType) {
        func->returnType = NEW(ASTTypeSpec);
        func->returnType->name = retType;
    }
    if (arg1Type) {
        ASTVar* arg = NEW(ASTVar);
        arg->name = "arg1";
        arg->typeSpec = NEW(ASTTypeSpec);
        arg->typeSpec->name = arg1Type;
        PtrList_append(&func->args, arg);
        func->argCount = 1;
    }
    return func;
}

static size_t ASTFunc_calcSizeUsage(ASTFunc* this)
{
    size_t size = 0, sum = 0;
    foreach (ASTVar*, arg, this->args) {
        // all variables must be resolved before calling this
        size = TypeType_size(arg->typeSpec->typeType);
        assert(size);
        // if (arg->flags.used)
        sum += size;
    }
    if (this->body) sum += ASTScope_calcSizeUsage(this->body);
    return sum;
}

static size_t ASTType_calcSizeUsage(ASTType* this)
{
    size_t size = 0, sum = 0;
    foreach (ASTVar*, var, this->body->locals) {
        // all variables must be resolved before calling this
        size = TypeType_size(var->typeSpec->typeType);
        assert(size);
        // if (arg->flags.used)
        sum += size;
    }
    // if (this->body) sum += ASTScope_calcSizeUsage(this->body);
    return sum;
}

#pragma mark - AST EXPR IMPL.

static const char* ASTExpr_typeName(ASTExpr* this)
{
    if (not this) return "";
    // TODO: here you should decide based onthe typeType not the token kind!
    // see as in ASTTypeSpec_name
    //    switch (this->typeType) {
    //    case TYUnresolved:
    //        return this->name;
    //    case TYObject:
    //        return this->type->name;
    //    default:
    //        return TypeType_name(this->typeType);
    //    }
    const char* ret = TypeType_name(this->typeType);
    if (!ret) return "<unknown>"; // unresolved
    if (*ret) return ret; // primitive type

    // all that is left is object
    switch (this->kind) {
    case tkFunctionCallResolved:
        return this->func->returnType->type->name;
    case tkIdentifierResolved:
    case tkSubscriptResolved:
        //        {
        //             const char* name =
        //             TypeType_name(this->var->typeSpec->typeType);
        //            if (!name) {
        //                unreachable("unresolved: %s %s",
        //                    TokenKind_repr(this->kind, false),
        //                    this->string);
        //                return "<unknown>";
        //            }
        //            if (!*name) name = ;
        return this->var->typeSpec->type->name;
        //        }
        // TODO: tkOpColon should be handled separately in the semantic
        // pass, and should be assigned either TYObject or make a dedicated
        // TYRange
        //     case tkOpColon:
        //        return "Range";
        // TODO: what else???
    case tkPeriod:
        return ASTExpr_typeName(this->right);
    default:
        break;
        // unreachable("unexpected: %s %s", TokenKind_repr(this->kind,
        // false),
        //     this->string);
    }
    return "<invalid>";
}

static void ASTExpr_catarglabels(ASTExpr* this)
{
    switch (this->kind) {
    case tkOpComma:
        ASTExpr_catarglabels(this->left);
        ASTExpr_catarglabels(this->right);
        break;
    case tkOpAssign:
        printf("_%s", this->left->string);
        break;
    default:
        break;
    }
}
static int ASTExpr_strarglabels(ASTExpr* this, char* buf, int bufsize)
{
    int ret = 0;
    switch (this->kind) {
    case tkOpComma:
        ret += ASTExpr_strarglabels(this->left, buf, bufsize);
        ret += ASTExpr_strarglabels(this->right, buf + ret, bufsize - ret);
        break;
    case tkOpAssign:
        ret += snprintf(buf, bufsize, "_%s", this->left->string);
        break;
    default:
        break;
    }
    return ret;
}
static int ASTExpr_countCommaList(ASTExpr* this)
{
    // whAT A MESS WITH BRANCHING
    if (not this) return 0;
    if (this->kind != tkOpComma) return 1;
    int i = 1;
    while (this->right) {
        this = this->right;
        i++;
        if (this->kind != tkOpComma) break;
    }
    return i;
}

#pragma mark - AST MODULE IMPL.

static ASTType* ASTModule_getType(ASTModule* this, const char* name)
{
    // the type may be "mm.XYZType" in which case you should look in
    // module mm instead. actually the caller should have bothered about
    // that.
    foreach (ASTType*, type, this->types)
        if (not strcasecmp(type->name, name)) return type;
    // type specs must be fully qualified, so there's no need to look in
    // other modules.
    return NULL;
}

// i like this pattern, getType, getFunc, getVar, etc.
// even the module should have getVar.
// you don't need the actual ASTImport object, so this one is just a
// bool. imports just turn into a #define for the alias and an #include
// for the actual file.
static bool ASTModule_hasImportAlias(ASTModule* this, const char* alias)
{
    foreach (ASTImport*, imp, this->imports)
        if (not strcmp(imp->importFile + imp->aliasOffset, alias)) return true;
    return false;
}

ASTFunc* ASTModule_getFunc(ASTModule* this, const char* selector)
{
    // figure out how to deal with overloads. or set a selector field in
    // each astfunc.
    foreach (ASTFunc*, func, this->funcs)
        if (not strcasecmp(func->selector, selector)) return func;
    // again no looking anywhere else. If the name is of the form
    // "mm.func" you should have bothered to look in mm instead.
    return NULL;
}

#include "gen.h"
#include "genc.h"
// #include "genlua.h"
// #include "gencpp.h"

#pragma mark - PARSER

typedef enum ParserMode {
    PMLint,
    PMGenC,
    PMGenTests
    // PMGenLua,
    // PMGenCpp,
    // PMGenJavaScript,
    // PMGenWebAsm,
    // PMGenPython
} ParserMode;

typedef struct Parser {
    char* filename; // mod/submod/xyz/mycode.ch
    char* moduleName; // mod.submod.xyz.mycode
    char* mangledName; // mod_submod_xyz_mycode
    char* capsMangledName; // MOD_SUBMOD_XYZ_MYCODE
    char *data, *end;
    char* noext;
    Token token; // current
    List(ASTModule*) * modules; // module node of the AST
                                // Stack(ASTScope*) scopes; // a stack
                                // that keeps track of scope nesting
    uint32_t errCount, warnCount;
    uint16_t errLimit;

    ParserMode mode : 8;
    bool generateCommentExprs : 1, // set to false when compiling, set to
                                   // true when linting
        warnUnusedVar : 1, warnUnusedFunc : 1, warnUnusedType : 1,
        warnUnusedArg : 1;

} Parser;

#define STR(x) STR_(x)
#define STR_(x) #x

static void Parser_fini(Parser* this)
{
    free(this->data);
    free(this->noext);
    free(this->moduleName);
    free(this->mangledName);
    free(this->capsMangledName);
}
#define FILE_SIZE_MAX 1 << 24

static Parser* Parser_fromFile(char* filename, bool skipws)
{

    size_t flen = strlen(filename);

    // Error: the file might not end in .ch
    if (not str_endswith(filename, flen, ".ch", 3)) {
        eprintf("F+: file '%s' invalid: name must end in '.ch'.\n", filename);
        return NULL;
    }

    struct stat sb;

    // Error: the file might not exist
    if (stat(filename, &sb) != 0) {
        eprintf("F+: file '%s' not found.\n", filename);
        return NULL;
    } else if (S_ISDIR(sb.st_mode)) {
        // Error: the "file" might really be a folder
        eprintf("F+: '%s' is a folder; only files are accepted.\n", filename);
        return NULL;
    } else if (access(filename, R_OK) == -1) {
        // Error: the user might not have read permissions for the file
        eprintf("F+: no permission to read file '%s'.\n", filename);
        return NULL;
    }

    FILE* file = fopen(filename, "r");
    assert(file);

    Parser* ret = NEW(Parser);

    ret->filename = filename;
    ret->noext = str_noext(filename);
    fseek(file, 0, SEEK_END);
    const size_t size = ftell(file) + 2;

    // 2 null chars, so we can always lookahead
    if (size < FILE_SIZE_MAX) {
        ret->data = (char*)malloc(size);
        fseek(file, 0, SEEK_SET);
        if (fread(ret->data, size - 2, 1, file) != 1) {
            eprintf("F+: the whole file '%s' could not be read.\n", filename);
            fclose(file);
            return NULL;
            // would leak if ret was malloc'd directly, but we have a pool
        }
        ret->data[size - 1] = 0;
        ret->data[size - 2] = 0;
        ret->moduleName = str_tr(ret->noext, '/', '.');
        ret->mangledName = str_tr(ret->noext, '/', '_');
        ret->capsMangledName = str_upper(ret->mangledName);
        ret->end = ret->data + size;
        ret->token.pos = ret->data;
        ret->token.flags.skipWhiteSpace = skipws;
        ret->token.flags.mergeArrayDims = false;
        ret->token.kind = tkUnknown;
        ret->token.line = 1;
        ret->token.col = 1;
        ret->mode = PMGenC; // parse args to set this
        ret->errCount = 0;
        ret->warnCount = 0;
        ret->errLimit = 20;
    } else {
        eputs("Source files larger than 16MB are not allowed.\n");
    }

    fclose(file);
    return ret;
}

#include "errors.h"
#include "stats.h"

#pragma mark - PARSING BASICS

static ASTExpr* exprFromCurrentToken(Parser* this)
{
    ASTExpr* expr = ASTExpr_fromToken(&this->token);
    Token_advance(&this->token);
    return expr;
}

static ASTExpr* next_token_node(
    Parser* this, TokenKind expected, const bool ignore_error)
{
    if (this->token.kind == expected) {
        return exprFromCurrentToken(this);
    } else {
        if (not ignore_error) Parser_errorExpectedToken(this, expected);
        return NULL;
    }
}
// these should all be part of Token_ when converted back to C
// in the match case, this->token should be advanced on error
static ASTExpr* match(Parser* this, TokenKind expected)
{
    return next_token_node(this, expected, false);
}

// this returns the match node or null
static ASTExpr* trymatch(Parser* this, TokenKind expected)
{
    return next_token_node(this, expected, true);
}

// just yes or no, simple
static bool matches(Parser* this, TokenKind expected)
{
    return (this->token.kind == expected);
}

static bool Parser_ignore(Parser* this, TokenKind expected)
{
    bool ret;
    if ((ret = matches(this, expected))) Token_advance(&this->token);
    return ret;
}

// this is same as match without return
static void discard(Parser* this, TokenKind expected)
{
    if (not Parser_ignore(this, expected))
        Parser_errorExpectedToken(this, expected);
}

static char* parseIdent(Parser* this)
{
    if (this->token.kind != tkIdentifier)
        Parser_errorExpectedToken(this, tkIdentifier);
    char* p = this->token.pos;
    Token_advance(&this->token);
    return p;
}

static void getSelector(ASTFunc* func)
{
    if (func->argCount) {
        size_t selLen = 0;
        int remain = 128, wrote = 0;
        char buf[128];
        buf[127] = 0;
        char* bufp = buf;
        ASTVar* arg1 = (ASTVar*)func->args->item;
        wrote = snprintf(bufp, remain, "%s_", ASTTypeSpec_name(arg1->typeSpec));
        selLen += wrote;
        bufp += wrote;
        remain -= wrote;

        wrote = snprintf(bufp, remain, "%s", func->name);
        selLen += wrote;
        bufp += wrote;
        remain -= wrote;

        foreach (ASTVar*, arg, func->args->next) {
            wrote = snprintf(bufp, remain, "_%s", arg->name);
            selLen += wrote;
            bufp += wrote;
            remain -= wrote;
        }
        func->selector = PoolB_alloc(&strPool, selLen + 1);
        memcpy(func->selector, buf, selLen + 1);
    } else
        func->selector = func->name;
    // printf("// got func %s: %s\n", func->name, func->selector);
}

#include "typecheck.h"
#include "resolve.h"
#include "sempass.h"

#include "parse.h"

// TODO: this should be in ASTModule open/close
static void Parser_genc_open(Parser* this)
{
    printf("#ifndef HAVE_%s\n#define HAVE_%s\n\n", this->capsMangledName,
        this->capsMangledName);
    printf("#define THISMODULE %s\n", this->mangledName);
    printf("#define THISFILE \"%s\"\n", this->filename);
}

static void Parser_genc_close(Parser* this)
{
    printf("#undef THISMODULE\n");
    printf("#undef THISFILE\n");
    printf("#endif // HAVE_%s\n", this->capsMangledName);
}

static void alloc_stat() {}

#pragma mark - main
int main(int argc, char* argv[])
{
    if (argc == 1) {
        eputs("F+: no input files.\n");
        return 1;
    }
    bool printDiagnostics = (argc > 2 && *argv[2] == 'd') or false;

    ticks t0 = getticks();

    List(ASTModule) * modules;
    Parser* parser;

    parser = Parser_fromFile(argv[1], true);
    if (not parser) return 2;
    if (argc > 3 && *argv[3] == 'l') parser->mode = PMLint;
    if (argc > 2 && *argv[2] == 'l') parser->mode = PMLint;
    if (argc > 3 && *argv[3] == 't') parser->mode = PMGenTests;
    if (argc > 2 && *argv[2] == 't') parser->mode = PMGenTests;

    modules = parseModule(parser);

    if (not(parser->errCount)) {
        switch (parser->mode) {
        case PMLint: {
            foreach (ASTModule*, mod, modules)
                ASTModule_gen(mod, 0);
        } break;

            // case PMGenLua: {
            //     foreach (ASTModule*, mod, modules)
            //         ASTModule_genlua(mod, 0);
            // } break;

        case PMGenC: {
            printf("#include \"checkstd.h\"\n");
            Parser_genc_open(parser);
            foreach (ASTModule*, mod, modules)
                ASTModule_genc(mod, 0);
            Parser_genc_close(parser);
        } break;

        case PMGenTests: {
            printf("#include \"checktest.h\"\n");
            // TODO : THISFILE must be defined since function callsites need it,
            // but the other stuff in Parser_genc_open isn't required. Besides,
            // THISFILE should be the actual module's file not the test file
            // Parser_genc_open(parser);
            foreach (ASTModule*, mod, modules)
                ASTModule_genTests(mod, 0);
            // Parser_genc_close(parser);
        } break;

        // case PMGenCpp: {
        //     printf("#include \"checkstd.hpp\"\n");
        //     Parser_genc_open(parser);
        //     foreach (ASTModule*, mod, modules)
        //         ASTModule_gencpp(mod, 0);
        //     Parser_genc_close(parser);
        // } break;
        default:
            break;
        }
    }

    if (printDiagnostics) printstats(parser, elapsed(getticks(), t0) / 1e6);

    if (parser->errCount)
        eprintf("\n\e[31m*** %d errors\e[0m\n", parser->errCount);
    if (parser->warnCount)
        eprintf("\n\e[33m*** %d warnings\e[0m\n", parser->warnCount);

    return (parser->errCount); // or parser->warnCount);
}
