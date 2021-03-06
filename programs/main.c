
// #include <assert.h>
// #include <ctype.h>
// #include <limits.h>
// #include <stdio.h>
// #include <stdint.h>
// #include <stdlib.h>
// #include <string.h>
// #include <math.h>

#include "cycle.h"
#include "jet_base.h"
#include "jet_clock.h"

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

typedef struct {
    double start, end;
} Interval;
// TODO: replace this with the generic RealRange

typedef struct ASTTypeSpec {
    union {
        struct ASTType* type;
        char* name;
        Interval* intv;
        // ^ e.g. var x in [1:250]
        // this does not make x integer. it only provides a constraint, you need
        // other clues to decide if x should really be integer.
        // e.g. var x in [1:1:256] -> now its an integer
        ASTUnits* units;
        // not keeping units on vars; move them to exprs instead. that way | is
        // simply a check/multiply op. in tthe AST | is a tkDimensionedExpr, or
        // simply every expr has a ASTUnits* member.
    };
    struct { // todo: this must be named TypeInfo & reused in ASTExpr not copy
             // pasted
        uint16_t dims; // more than 65535 dims will not be handled at compile
                       // time (size check, shape check etc) but at runtime. if
                       // collectionType is tensor but dims is 0, it means too
                       // many dims or ct-unknown dims, in any case it is then
                       // the generic ArrayND.
        CollectionTypes collectionType : 6;
        bool hasRange : 1, hasUnits : 1;
        TypeTypes typeType : 7;
        bool nullable : 1;
    };
    uint16_t line;
    uint8_t col;
} ASTTypeSpec;

typedef struct ASTVar {
    char* name;
    ASTTypeSpec* typeSpec;
    union {
        struct ASTExpr* init;
        // when you move to having return var, there wont be a returnSpec on
        // funcs anymore, only on vars. then you can get rid of asttypespec and
        // move its stuff here. When there is an init, you take typeinfo from
        // the expr, else directly here. check lower dword of the init ptr to be
        // null to know that init is null. playing with fire, but its safe play
        // imho. well its all for later.
        struct {
            unsigned _init_lower32;
            struct {
                uint16_t dims;
                CollectionTypes collectionType : 6;
                bool hasRange : 1, hasUnits : 1;
                TypeTypes typeType : 7;
                bool nullable : 1;
            };
        };
    };
    // List(ASTVar*) deps; // TODO: keep deps of each var so you can tell when a
    // dependency of an async var is changed before the async is awaited. First
    // you will have to figure out how to analyze array members and type members
    // (e.g. init an instance for each compound type and array for each array
    // and then set the member of that as dep...)
    // struct ASTExpr* lastUsed; //
    // last expr in owning scope that refers to this var. Note that you should
    // dive to search for the last expr, it may be within an inner scope. The
    // drop call should go at the end of such subscope, NOT within the subscope
    // itself after the actual expr. (so set the if/for/while as the lastref,
    // not the actual lastref) WHY NOT JUST SAVE THE LINE NUMBER OF THE LAST
    // USE?
    uint16_t line; //
    //, lineLastUsed;
    // ^ YOu canot use the last used line no to decide drops etc. because code
    // motion can rearrange statements and leave the line numbers stale.
    struct {
        bool used : 1, //
            changed : 1, //
            isLet : 1, //
            isVar : 1, //
            isArg : 1, // a function arg, not a local var
            stackAlloc : 1, // this var is of a ref type, but it will be
                            // stack allocated. it will be passed around by
                            // reference as usual.
            isTarget : 1, // x = f(x,y)
            visited : 1, // for generating checks, used to avoid printing this
                         // var more than once.
            escapes : 1, // does it escape the owning SCOPE?
            canInplace : 1,
            isPromise : 1, // is it an async var (transparently a Promise<T>)?
            hasRefs : 1, // there are other vars/lets that reference this var or
                         // overlap with its storage. e.g. simply pointers that
                         // refer to this var, or slices or filters taken if
                         // this is an array/dataframe etc, or StringRefs, etc.
                         // useful for understanding aliasing patterns
                         // I think you rather need refCount which can be kept
                         // at compile time because inplacing decisions etc.
                         // need surety of var having no other refs outside the
                         // scope
            obtainedBySerialization : 1, // this is a JSON/XML/YAML obj obtained
                                         // by serializing something. If it is
                                         // passed to functions, warn and
                                         // recommend passing the object instead
                                         // and calling JSON/XML/YAML in that
                                         // func. This is so that calls like
                                         // print(YAML(xyz)) can be optim to
                                         // print_YAML(xyz) (i.e. not generating
                                         // an actual YAML tree just for print)
            usedAsIndex : 1, // if ys, should be converted to  a pointer rather
                             // than offset, and arr[b] should be converted to
                             // *b to avoid a + op.
                             // someone has to pay the price somewhere: if you
                             // e.g. print the loop variable user expects to see
                             // the offset, so there you have to -.
                             // generated loops can be done w/ ptrs
                             // e.g. a[:,:] = random()
            reassigned : 1, // this var was reassigned after init. If it is a
                            // non-primitive type, it generally implies this
                            // should be generated as a pointer.
            resized : 1, // this collection var was resized after init (due to
                         // resize(), push() etc.) and means that it cannot be
                         // generated as a fixed size array (static if size is
                         // known at compile time).
            returned : 1; // is a return variable ie. b in
        // function asd(x as Anc) returns (b as Whatever)
        // all args (in/out) are in the same list in func -> args.
    };
    uint8_t col;
} ASTVar;

// when does something escape a scope?
// -- if it is assigned to a variable outside the scope
//    -- for func toplevels, one such var is the return var: anything
//    assigned to it escapes
// -- if it is passed to a func as an arg and the arg `escapes`
//    analyseExpr sets `escapes` on each arg as it traverses the func

typedef struct ASTExpr {
    struct {
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
        uint16_t line;

        // blow this bool to bits to store more flags
        uint8_t dims : 5, // hack for now so you can set upto 32 dims. figure it
                          // out later how to have a common typeinfo struct
                          // between astexpr & astvar
            promote : 1, // should this expr be promoted, e.g.
                         // count(arr[arr<34]) or sum{arr[3:9]}. does not
                         // propagate.
            canEval : 1, // the value is known (computable) at compile time,
                         // either by jetc or by backend cc.
            didEval : 1; //
        uint8_t prec : 6, // operator precedence for this expr
            unary : 1, // for an operator, is it unary (negation, not,
                       // return, check, array literal, ...)
            rassoc : 1; // is this a right-associative operator e.g.
                        // exponentiation
        uint8_t col;
        TokenKind kind : 8;
    };
    union {
        struct ASTExpr* left;
        List(ASTVar*) * vars; // for tkString
        struct ASTType* elementType; // for tkListLiteral, tkDictLiteral only!!
    };
    union {
        // ASTEvalInfo eval;
        struct {
            uint32_t hash, slen;
        }; // str len for strings, idents, unresolved funcs/vars etc.
        // why do you need the hash in the expr? just add to the dict using the
        // computed hash! TO COMPUTE HASH OF EXPR TREES
    };
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
    char* name;
    ASTTypeSpec* super;
    ASTScope* body;
    uint16_t line;
    uint8_t col;
    bool analysed : 1, needJSON : 1, needXML : 1, needYAML : 1, visited : 1,
        isValueType : 1; // all vars of this type will be stack
                         // allocated and passed around by value.
} ASTType;

typedef struct ASTEnum {
    char* name;
    ASTScope* body;
    uint16_t line;
    uint8_t col;
    bool analysed : 1, visited : 1;
} ASTEnum;

typedef struct ASTFunc {
    char* name;
    ASTScope* body;
    List(ASTVar) * args;
    ASTTypeSpec* returnSpec;
    char* selector;
    struct {
        uint16_t line;
        struct {
            uint16_t throws : 1,
                isRecursive : 1, // usesNet : 1,usesIO : 1, usesGUI : 1,
                usesSerialisation : 1, isExported : 1, usesReflection : 1,
                nodispatch : 1, isStmt : 1, isDeclare : 1,
                isCalledFromWithinLoop : 1, elemental : 1, isDefCtor : 1,
                intrinsic : 1, // intrinsic: print, describe, json, etc. not to
                               // be output by linter
                analysed : 1, // semantic pass has been done, don't repeat
                isCalledAsync : 1, // is this func called async at least once?
                returnsNewObjectSometimes : 1,
                returnsNewObjectAlways : 1; // what this func returns is an
                                            // object that was obtained by a
                                            // constructor. Useful for checking
                                            // cycles in types.
            // Constructors ALWAYS return a new object. This means if you call a
            // constructor of a type from within the default constructor of
            // another type, and this chain has a cycle, you need to report
            // error. If this happens indirectly via intermediate funcs, check
            // the returnsNewObject flag of the func in question to see if it
            // internally calls the constructor. The function may have multiple
            // return paths and not all of them may call the constructor; in
            // this case set returnsNewObjectAlways accordingly.
        };
        uint8_t argCount, nameLen;
    };
} ASTFunc;

typedef struct ASTTest {
    char* name;
    ASTScope* body;
    char* selector;
    struct {
        uint16_t line;
        struct {
            uint16_t analysed : 1;
        } flags;
    };
} ASTTest;

typedef struct ASTModule {
    char* name;
    List(ASTFunc) * funcs;
    List(ASTTest) * tests;
    List(ASTExpr) * exprs;
    List(ASTType) * types;
    List(ASTVar) * globals;
    List(ASTImport) * imports;
    List(ASTEnum) * enums;
    char* moduleName;
} ASTModule;

// better keep a set or map instead and add as you encounter in code
// or best do nothing and let user write 'import formats' etc
// typedef struct {
//     int need_BitVector : 1, need_Colour : 1, need_Currency : 1,
//         need_DateTime : 1, need_DiskItem : 1, need_Duration : 1,
//         need_Number : 1, need_Range : 1, need_Rational : 1, need_Regex : 1,
//         need_Size : 1, need_String : 1, need_YesOrNo : 1, need_Array : 1,
//         need_ArrayND : 1, need_Dict : 1, need_Filter : 1, need_List : 1,
//         need_Selection : 1, need_Sequence : 1, need_SequenceND : 1,
//         need_Slice : 1, need_SliceND : 1, need_FML : 1, need_HTML : 1,
//         need_JSON : 1, need_XML : 1, need_YAML : 1, need_FTP : 1, need_HTTP :
//         1, need_IMAP : 1, need_POP3 : 1, need_SMTP : 1, need_SSH : 1,
//         need_Pool : 1;
// } FPNeedBuiltins;

#pragma mark - AST IMPORT IMPL.

#pragma mark - AST UNITS IMPL.

struct ASTTypeSpec;
struct ASTType;
struct ASTFunc;
struct ASTScope;
struct ASTExpr;
struct ASTVar;

#define List_ASTExpr jet_PtrList
#define List_ASTVar jet_PtrList
#define List_ASTModule jet_PtrList
#define List_ASTFunc jet_PtrList
#define List_ASTEnum jet_PtrList
#define List_ASTTest jet_PtrList
#define List_ASTType jet_PtrList
#define List_ASTImport jet_PtrList
#define List_ASTScope jet_PtrList

// these should be generated by the compiler in DEBUG mode
MKSTAT(ASTExpr)
MKSTAT(ASTFunc)
MKSTAT(ASTTest)
MKSTAT(ASTEnum)
MKSTAT(ASTTypeSpec)
MKSTAT(ASTType)
MKSTAT(ASTModule)
MKSTAT(ASTScope)
MKSTAT(ASTImport)
MKSTAT(ASTVar)
MKSTAT(Parser)
MKSTAT(List_ASTExpr)
MKSTAT(List_ASTFunc)
MKSTAT(List_ASTEnum)
MKSTAT(List_ASTTest)
MKSTAT(List_ASTType)
MKSTAT(List_ASTModule)
MKSTAT(List_ASTScope)
MKSTAT(List_ASTImport)
MKSTAT(List_ASTVar)
static uint32_t exprsAllocHistogram[128];

static ASTTypeSpec* ASTTypeSpec_new(TypeTypes tt, CollectionTypes ct) {
    ASTTypeSpec* ret = jet_new(ASTTypeSpec);
    ret->typeType = tt;
    ret->collectionType = ct;
    return ret;
}

static const char* ASTTypeSpec_name(ASTTypeSpec* self) {
    switch (self->typeType) {
    case TYUnresolved:
        return self->name;
    case TYObject:
        return self->type->name;
    default:
        return TypeType_name(self->typeType);
    }
    // what about collectiontype???
}

// The name of this type spec as it will appear in the generated C code.
static const char* ASTTypeSpec_cname(ASTTypeSpec* self) {
    switch (self->typeType) {
    case TYUnresolved:
        return self->name;
    case TYObject:
        return self->type->name;
    default:
        return TypeType_name(self->typeType);
    }
    // what about collectiontype???
}

static const char* getDefaultValueForType(ASTTypeSpec* type) {
    if (not type) return "";
    switch (type->typeType) {
    case TYUnresolved:
        unreachable(
            "unresolved: '%s' at %d:%d", type->name, type->line, type->col);
        return "ERROR_ERROR_ERROR";
    case TYString:
        return "\"\"";
    default:
        return "0";
    }
}

static ASTExpr* ASTExpr_fromToken(const Token* self) {
    ASTExpr* ret = jet_new(ASTExpr);
    ret->kind = self->kind;
    ret->line = self->line;
    ret->col = self->col;

    ret->prec = TokenKind_getPrecedence(ret->kind);
    if (ret->prec) {
        ret->rassoc = TokenKind_isRightAssociative(ret->kind);
        ret->unary = TokenKind_isUnary(ret->kind);
    }

    exprsAllocHistogram[ret->kind]++;

    switch (ret->kind) {
    case tkIdentifier:
    case tkString:
    case tkRawString:
    case tkRegexp:
    case tkNumber:
    case tkMultiDotNumber:
    case tkLineComment: // Comments go in the AST like regular stmts
        ret->string = self->pos;
        break;
    default:;
    }
    // the '!' will be trampled
    if (ret->kind == tkLineComment) ret->string++;
    // turn all 1.0234[DdE]+01 into 1.0234e+01.
    if (ret->kind == tkNumber) {
        str_tr_ip(ret->string, 'd', 'e', self->matchlen);
        str_tr_ip(ret->string, 'D', 'e', self->matchlen);
        str_tr_ip(ret->string, 'E', 'e', self->matchlen);
    }
    return ret;
}

static bool ASTExpr_throws(ASTExpr* self) { // NOOO REMOVE This func and set the
                                            // throws flag recursively like the
    // other flags (during e.g. the type resolution dive)
    if (not self) return false;
    switch (self->kind) {
    case tkNumber:
    case tkMultiDotNumber:
    case tkRawString:
    case tkRegexp:
    case tkIdentifier:
    case tkIdentifierResolved:
    case tkString:
    case tkLineComment:
        return false;
    case tkFunctionCall:
    case tkFunctionCallResolved:
        return true; // self->func->throws;
        // actually  only if the func really throws
    case tkSubscript:
    case tkSubscriptResolved:
        return ASTExpr_throws(self->left);
    case tkVarAssign:
        return self->var->used and ASTExpr_throws(self->var->init);
    case tkKeyword_for:
    case tkKeyword_if:
    case tkKeyword_while:
        return false; // actually the condition could throw.
    default:
        if (not self->prec) return false;
        return ASTExpr_throws(self->left) or ASTExpr_throws(self->right);
    }
}

static size_t ASTScope_calcSizeUsage(ASTScope* self) {
    size_t size = 0, sum = 0, subsize = 0, maxsubsize = 0;
    // all variables must be resolved before calling this
    jet_foreach(ASTExpr*, stmt, self->stmts) {
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
    jet_foreach(ASTVar*, var, self->locals) {
        size = TypeType_size(var->typeSpec->typeType);
        assert(size);
        if (var->used) sum += size;
    }
    // add the largest size among the sizes of the sub-scopes
    sum += maxsubsize;
    return sum;
}

static ASTVar* ASTScope_getVar(ASTScope* self, const char* name) {
    // stupid linear search, no dictionary yet
    jet_foreach(ASTVar*, local, self->locals) //
        if (not strcasecmp(name, local->name)) return local;
    if (self->parent) return ASTScope_getVar(self->parent, name);
    return NULL;
}

static ASTVar* ASTType_getVar(ASTType* self, const char* name) {
    // stupid linear search, no dictionary yet
    jet_foreach(ASTVar*, var, self->body->locals) //
        if (not strcasecmp(name, var->name)) return var;

    if (self->super and self->super->typeType == TYObject)
        return ASTType_getVar(self->super->type, name);
    return NULL;
}

#pragma mark - AST FUNC IMPL.

/// This creates a new ASTFunc marked as declare and having one
/// argument. The name of the function and the type of the argument can be
/// specified. This way you can create declared functions such as `print`,
/// `json`, etc. of each new type defined in source code.
static ASTFunc* ASTFunc_createDeclWithArg(
    char* name, char* retType, char* arg1Type) {
    ASTFunc* func = jet_new(ASTFunc);
    func->name = name;
    func->isDeclare = true;
    if (retType) {
        func->returnSpec = jet_new(ASTTypeSpec);
        func->returnSpec->name = retType;
    }
    if (arg1Type) {
        ASTVar* arg = jet_new(ASTVar);
        arg->name = "arg1";
        arg->typeSpec = jet_new(ASTTypeSpec);
        arg->typeSpec->name = arg1Type;
        jet_PtrList_append(&func->args, arg);
        func->argCount = 1;
    }
    return func;
}

static size_t ASTFunc_calcSizeUsage(ASTFunc* self) {
    size_t size = 0, sum = 0;
    jet_foreach(ASTVar*, arg, self->args) {
        // all variables must be resolved before calling this
        size = TypeType_size(arg->typeSpec->typeType);
        assert(size);
        // if (arg->used)
        sum += size;
    }
    if (self->body) sum += ASTScope_calcSizeUsage(self->body);
    return sum;
}

static size_t ASTType_calcSizeUsage(ASTType* self) {
    size_t size = 0, sum = 0;
    jet_foreach(ASTVar*, var, self->body->locals) {
        // all variables must be resolved before calling this
        size = TypeType_size(var->typeSpec->typeType);
        assert(size);
        sum += size;
    }
    return sum;
}

#pragma mark - AST EXPR IMPL.

static const char* ASTExpr_typeName(const ASTExpr* const self) {
    if (not self) return "";
    const char* ret = TypeType_name(self->typeType);
    if (!ret) return "<unknown>"; // unresolved
    if (*ret) return ret; // primitive type

    // all that is left is object
    switch (self->kind) {
    case tkFunctionCallResolved:
        return self->func->returnSpec->type->name;
    case tkIdentifierResolved:
    case tkSubscriptResolved:
        return self->var->typeSpec->type->name;
        //        }
        // TODO: tkOpColon should be handled separately in the semantic
        // pass, and should be assigned either TYObject or make a dedicated
        // TYRange
        //     case tkOpColon:
        //        return "Range";
        // TODO: what else???
    case tkPeriod:
        return ASTExpr_typeName(self->right);
    default:
        break;
    }
    return "<invalid>";
}

static void ASTExpr_catarglabels(ASTExpr* self) {
    switch (self->kind) {
    case tkOpComma:
        ASTExpr_catarglabels(self->left);
        ASTExpr_catarglabels(self->right);
        break;
    case tkOpAssign:
        printf("_%s", self->left->string);
        break;
    default:
        break;
    }
}

static int ASTExpr_strarglabels(ASTExpr* self, char* buf, int bufsize) {
    int ret = 0;
    switch (self->kind) {
    case tkOpComma:
        ret += ASTExpr_strarglabels(self->left, buf, bufsize);
        ret += ASTExpr_strarglabels(self->right, buf + ret, bufsize - ret);
        break;
    case tkOpAssign:
        ret += snprintf(buf, bufsize, "_%s", self->left->string);
        break;
    default:
        break;
    }
    return ret;
}

// TODO: see if this is still correct
static int ASTExpr_countCommaList(ASTExpr* expr) {
    int i = 0;
    if (expr)
        for (i = 1; expr->right and expr->kind == tkOpComma; i++)
            expr = expr->right;
    return i;
}

#pragma mark - AST MODULE IMPL.

static ASTType* ASTModule_getType(ASTModule* module, const char* name) {
    // the type may be "mm.XYZType" in which case you should look in
    // module mm instead. actually the caller should have bothered about
    // that.
    jet_foreach(ASTType*, type, module->types) //
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
static bool ASTModule_hasImportAlias(ASTModule* module, const char* alias) {
    jet_foreach(ASTImport*, imp, module->imports) //
        if (not strcmp(imp->importFile + imp->aliasOffset, alias)) return true;
    return false;
}

ASTFunc* ASTModule_getFunc(ASTModule* module, const char* selector) {
    jet_foreach(ASTFunc*, func, module->funcs) //
        if (not strcasecmp(func->selector, selector)) return func;
    //  no looking anywhere else. If the name is of the form
    // "mm.func" you should have bothered to look in mm instead.
    return NULL;
}

#include "lint.h"
#include "emit.h"

#pragma mark - PARSER

typedef enum ParserMode { PMLint, PMEmitC, PMGenTests } ParserMode;
typedef struct IssueMgr {
    uint16_t errCount, warnCount, errLimit;
    uint8_t lastError /*enum type*/, warnUnusedVar : 1, warnUnusedFunc : 1,
        warnUnusedType : 1, warnUnusedArg : 1, hasParseErrors : 1;
} IssueMgr;

typedef struct Parser {
    char* filename; // mod/submod/xyz/mycode.ch
    char* moduleName; // mod.submod.xyz.mycode
    char* mangledName; // mod_submod_xyz_mycode
    char* capsMangledName; // MOD_SUBMOD_XYZ_MYCODE
    char *data, *end;
    char* noext;

    Token token; // current
    IssueMgr issues;

    List(ASTModule*) * modules; // module node of the AST
                                // Stack(ASTScope*) scopes; // a stack
                                // that keeps track of scope nesting
    // uint32_t errCount, warnCount;
    // uint16_t errLimit;
    ParserMode mode : 8;
    bool generateCommentExprs; // set to false when compiling, set to
                               // true when linting

    // set these whenever use is detected (e.g. during resolveTypes or parsing
    // literals)
    struct {
        bool complex : 1, json : 1, yaml : 1, xml : 1, html : 1, http : 1,
            ftp : 1, imap : 1, pop3 : 1, smtp : 1, frpc : 1, fml : 1, fbin : 1,
            rational : 1, polynomial : 1, regex : 1, datetime : 1, colour : 1,
            range : 1, table : 1, ui : 1;
    } requires;
} Parser;

static const int sgr = sizeof(Parser);

#define STR(x) STR_(x)
#define STR_(x) #x

static const char* const jet_banner = //
    "________     _____  _\n"
    "______(_)______  /_ _|  The next-gen language of computing\n"
    "_____  /_  _ \\  __/ _|  %s %s %4d-%02d-%02d\n"
    "____  / /  __/ /_  __|\n"
    "___  /  \\___/\\__/ ___|  https://github.com/jetpilots/jet\n"
    "/___/ ______________________________________________________\n\n";

static void Parser_fini(Parser* parser) {
    free(parser->data);
    free(parser->noext);
    free(parser->moduleName);
    free(parser->mangledName);
    free(parser->capsMangledName);
}
#define FILE_SIZE_MAX 1 << 24

static Parser* Parser_fromFile(char* filename, bool skipws) {

    size_t flen = strlen(filename);

    // Error: the file might not end in .ch
    if (not str_endswith(filename, flen, ".jet", 4)) {
        eprintf("jet: file '%s' invalid: name must end in '.jet'.\n", filename);
        return NULL;
    }

    struct stat sb;

    // Error: the file might not exist
    if (stat(filename, &sb) != 0) {
        eprintf("jet: file '%s' not found.\n", filename);
        return NULL;
    } else if (S_ISDIR(sb.st_mode)) {
        // Error: the "file" might really be a folder
        eprintf("jet: '%s' is a folder; only files are accepted.\n", filename);
        return NULL;
    } else if (access(filename, R_OK) == -1) {
        // Error: the user might not have read permissions for the file
        eprintf("jet: no permission to read file '%s'.\n", filename);
        return NULL;
    }

    FILE* file = fopen(filename, "r");
    assert(file);

    Parser* ret = jet_new(Parser);

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
        ret->token.skipWhiteSpace = skipws;
        ret->token.mergeArrayDims = false;
        ret->token.kind = tkUnknown;
        ret->token.line = 1;
        ret->token.col = 1;
        ret->mode = PMEmitC; // parse args to set this
        ret->issues.errCount = 0;
        ret->issues.warnCount = 0;
        ret->issues.errLimit = 50000;
    } else {
        eputs("Source files larger than 16MB are not allowed.\n");
    }

    fclose(file);
    return ret;
}

#include "errors.h"
#include "stats.h"

#pragma mark - PARSING BASICS

static ASTExpr* exprFromCurrentToken(Parser* parser) {
    ASTExpr* expr = ASTExpr_fromToken(&parser->token);
    Token_advance(&parser->token);
    return expr;
}

static ASTExpr* next_token_node(
    Parser* parser, TokenKind expected, const bool ignore_error) {
    if (parser->token.kind == expected) {
        return exprFromCurrentToken(parser);
    } else {
        if (not ignore_error) Parser_errorExpectedToken(parser, expected);
        return NULL;
    }
}
// these should all be part of Token_ when converted back to C
// in the match case, self->token should be advanced on error
static ASTExpr* match(Parser* parser, TokenKind expected) {
    return next_token_node(parser, expected, false);
}

// this returns the match node or null
static ASTExpr* trymatch(Parser* parser, TokenKind expected) {
    return next_token_node(parser, expected, true);
}

// just yes or no, simple
static bool matches(Parser* parser, TokenKind expected) {
    return (parser->token.kind == expected);
}

static bool Parser_ignore(Parser* parser, TokenKind expected) {
    bool ret;
    if ((ret = matches(parser, expected))) Token_advance(&parser->token);
    return ret;
}

// this is same as match without return
static void discard(Parser* parser, TokenKind expected) {
    if (not Parser_ignore(parser, expected))
        Parser_errorExpectedToken(parser, expected);
}

static char* parseIdent(Parser* parser) {
    if (parser->token.kind != tkIdentifier)
        Parser_errorExpectedToken(parser, tkIdentifier);
    char* p = parser->token.pos;
    Token_advance(&parser->token);
    return p;
}

static void getSelector(ASTFunc* func) {
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

        jet_foreach(ASTVar*, arg, func->args->next) {
            wrote = snprintf(bufp, remain, "_%s", arg->name);
            selLen += wrote;
            bufp += wrote;
            remain -= wrote;
        }
        // TODO: why not use pstrndup here?
        func->selector = pstrndup(buf, selLen + 1);
        // func->selector = PoolB_alloc(strPool, selLen + 1);
        // memcpy(func->selector, buf, selLen + 1);
    } else {
        func->selector = func->name;
    }
}

#include "resolve.h"
#include "analyse.h"

// this is a global astexpr representing 0. it will be used when parsing e.g.
// the colon op with nothing on either side. : -> 0:0 means the same as 1:end
static ASTExpr expr_const_0[] = { { .kind = tkNumber, .string = "0" } };
static ASTExpr lparen[] = { { .kind = tkParenOpen } };
static ASTExpr rparen[] = { { .kind = tkParenClose } };

// static void initStaticExprs()
// {
//     expr_const_0.kind = tkNumber;
//     expr_const_0.string = "0";
//     lparen.kind = tkParenOpen;
//     rparen.kind = tkParenClose;
// }

#include "parse.h"

// TODO: this should be in ASTModule open/close
static void Parser_emit_open(Parser* parser) {
    printf("#ifndef HAVE_%s\n#define HAVE_%s\n\n", parser->capsMangledName,
        parser->capsMangledName);
    printf("#define THISMODULE %s\n", parser->mangledName);
    printf("#define THISFILE \"%s\"\n", parser->filename);
    printf("#define NUMLINES %d\n", parser->token.line);
    // if(genCoverage)
    // printf("static UInt64 _cov_[NUMLINES] = {};\n");
    // printf("static ticks _lprof_last_, _lprof_tmp_;\n");
    // printf("static ticks _lprof_[NUMLINES] = {};\n");
}

static void Parser_emit_close(Parser* parser) {
    printf("#undef THISMODULE\n");
    printf("#undef THISFILE\n");
    printf("#endif // HAVE_%s\n", parser->capsMangledName);
}

static void alloc_stat() { }

#include "ptr2off.h"

#pragma mark - main
int main(int argc, char* argv[]) {
    fprintf(stderr, jet_banner, "v0.2.2", "asd762345asd", 2020, 9, 21);
    if (argc == 1) {
        eputs("jet: no input files. What are you trying to do?\n");
        return 1;
    }
    bool printDiagnostics = (argc > 2 && *argv[2] == 'd') or false;

    // ticks t0 = getticks();
    jet_clock_Time t0 = jet_clock_getTime();
    List(ASTModule) * modules;
    Parser* parser;

    // initStaticExprs();

    parser = Parser_fromFile(argv[1], true);
    if (not parser) return 2;
    if (argc > 3 && *argv[3] == 'l') parser->mode = PMLint;
    if (argc > 2 && *argv[2] == 'l') parser->mode = PMLint;
    if (argc > 3 && *argv[3] == 't') parser->mode = PMGenTests;
    if (argc > 2 && *argv[2] == 't') parser->mode = PMGenTests;

    parser->generateCommentExprs = (parser->mode == PMLint);

    // If you ar not linting, even a single error is enough to stop and tell the
    // user to LINT THE DAMN FILE FIRST.
    if (parser->mode != PMLint) parser->issues.errLimit = 1;

    modules = parseModule(parser);

    if (parser->mode == PMLint) {
        if (parser->issues.hasParseErrors) {
            /* TODO: fallback to token-based linter (formatter)*/
        } else {
            jet_foreach(ASTModule*, mod, modules) ASTModule_lint(mod, 0);
        }
    } else if (not(parser->issues.errCount)) {
        switch (parser->mode) {
            // case PMLint: {
            //     jet_foreach(ASTModule*, mod, modules) ASTModule_lint(mod, 0);
            // } break;

        case PMEmitC: {
            // TODO: if (monolithic) printf("#define STATIC static\n");
            Parser_emit_open(parser);
            // ^ This is called before including the runtime, so that the
            // runtime can know THISFILE NUMLINES etc.
            printf("#include \"jet_runtime.h\"\n");
            jet_foreach(ASTModule*, mod, modules) ASTModule_emit(mod, 0);
            Parser_emit_close(parser);
        } break;

        case PMGenTests: {
            printf("#include \"jet_tester.h\"\n");
            // TODO : THISFILE must be defined since function callsites need
            // it, but the other stuff in Parser_emit_open isn't required.
            // Besides, THISFILE should be the actual module's file not the
            // test file

            jet_foreach(ASTModule*, mod, modules) ASTModule_genTests(mod, 0);
        } break;

        default:
            break;
        }
    }

    // if (printDiagnostics) printstats(parser, elapsed(getticks(), t0) /
    // 1e6);
    if (printDiagnostics)
        printstats(parser, jet_clock_clockSpanMicro(t0) / 1.0e3);

    if (parser->issues.errCount) {
        eprintf("\n\e[31;1;4m THERE ARE %2d ERRORS.                        "
                "      "
                "                         \e[0m\n How about fixing them? "
                "Reading "
                "the code would be a good start.\n",
            parser->issues.errCount);
    }

    if (parser->issues.warnCount)
        eprintf("\n\e[33m*** %d warnings\e[0m\n", parser->issues.warnCount);

    // returns the last error kind (from enum ParserErrors) so that test
    // scripts can check for specific errors raised.
    int ret = (parser->issues.errCount or _jet_InternalErrs);
    // eprintf("ret: %d\n", ret);
    return ret; // or parser->warnCount);
    // TODO: what about last warning?
}
