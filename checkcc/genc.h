#define genLineNumbers 0
#define genCoverage 1
#define genLineProfile 1

///////////////////////////////////////////////////////////////////////////
static void ASTImport_genc(ASTImport* import, int level)
{
    str_tr_ip(import->importFile, '.', '_', 0);
    printf("\n#include \"%s.h\"\n", import->importFile);
    if (import->hasAlias)
        printf("#define %s %s\n", import->importFile + import->aliasOffset,
            import->importFile);
    str_tr_ip(import->importFile, '_', '.', 0);
}

///////////////////////////////////////////////////////////////////////////
static void ASTImport_undefc(ASTImport* import)
{
    if (import->hasAlias)
        printf("#undef %s\n", import->importFile + import->aliasOffset);
}

///////////////////////////////////////////////////////////////////////////
static void ASTTypeSpec_genc(ASTTypeSpec* typeSpec, int level, bool isconst)
{
    if (isconst) printf("const ");
    // TODO: actually this depends on the collectionType. In general
    // Array is the default, but in other cases it may be SArray, Array64,
    // whatever
    if (typeSpec->dims) {
        if (typeSpec->dims > 1)
            // TODO: this should be TensorND, without type params?
            // well actually there isn't a TensorND, since its not always
            // double thats in a tensor but can be Complex, Range,
            // Reciprocal, Rational, whatever
            // -- sure, but double (and float) should be enough since
            // the other types are rarely needed in a tensor form
            printf("SArray%dD(", typeSpec->dims);
        else
            printf("SArray(");
    }

    switch (typeSpec->typeType) {
    case TYObject:
        // objects are always T* const, if meant to be r/o they are
        // const T* const. Later we may have a byval flag to embed structs
        // or pass around by value.
        // leaving it as is for now
        printf("%s", typeSpec->type->name);
        break;
    case TYUnresolved:
        unreachable("unresolved: '%s' at %d:%d", typeSpec->name, typeSpec->line,
            typeSpec->col);
        printf("%s", typeSpec->name);
        break;
    default:
        printf("%s", TypeType_name(typeSpec->typeType));
        break;
    }

    //     if (isconst ) printf(" const"); // only if a ptr type
    if (typeSpec->dims /*or typeSpec->typeType == TYObject*/) printf("%s", ")");
    //        if (status == TSDimensionedNumber) {
    //            genc(units, level);
    //        }
}

static void ASTExpr_genc(
    ASTExpr* self, int level, bool spacing, bool inFuncArgs, bool escStrings);

///////////////////////////////////////////////////////////////////////////
static void ASTVar_genc(ASTVar* var, int level, bool isconst)
{
    // for C the variables go at the top of the block, without init
    printf("%.*s", level, spaces);
    if (var->typeSpec) ASTTypeSpec_genc(var->typeSpec, level + STEP, isconst);
    printf(" %s", var->name);
}

///////////////////////////////////////////////////////////////////////////
// Functions like Array_any_filter, Array_count_filter etc.
// are macros and don't return a value but may set one. For these
// and other such funcs, the call must be moved to before the
// containing statement, and in place of the original call you
// should place a temporary holding the value that would have been
// "returned".
static bool mustPromote(const char* name)
{
    // TODO: at some point these should go into a dict or trie or MPH
    // whatever
    if (not strcmp(name, "Array_any_filter")) return true;
    if (not strcmp(name, "Array_all_filter")) return true;
    if (not strcmp(name, "Array_count_filter")) return true;
    if (not strcmp(name, "Array_write_filter")) return true;
    if (not strcmp(name, "Strs_print_filter")) return true;
    return false;
}

///////////////////////////////////////////////////////////////////////////
static void ASTExpr_unmarkVisited(ASTExpr* expr)
{
    switch (expr->kind) {
    case tkIdentifierResolved:
    case tkVarAssign:
        expr->var->visited = false;
        break;
    case tkFunctionCallResolved:
    case tkFunctionCall: // shouldnt happen
    case tkSubscriptResolved:
    case tkSubscript:
    case tkKeyword_if:
    case tkKeyword_for:
    case tkKeyword_else:
    case tkKeyword_while:
        ASTExpr_unmarkVisited(expr->left);
        break;
    default:
        if (expr->prec) {
            if (not expr->unary) ASTExpr_unmarkVisited(expr->left);
            ASTExpr_unmarkVisited(expr->right);
        }
    }
}

///////////////////////////////////////////////////////////////////////////
// given an expr, generate code to print all the resolved vars in it (only
// scalars). for example in f(x + 4) + m + y[5:6], the following should be
// generated
// printf("x = %?\n", x);
// printf("m = %?\n", m);
// checks will print the vars involved in the check expr, if the check
// fails. This routine will be used there.
static void ASTExpr_genPrintVars(ASTExpr* expr, int level)
{
    assert(expr);
    // what about func args?
    switch (expr->kind) {
    case tkIdentifierResolved:
    case tkVarAssign:
        if (expr->var->visited) break;
        printf("%.*sprintf(\"    %s = %s\\n\", %s);\n", level, spaces,
            expr->var->name, TypeType_format(expr->typeType, true),
            expr->var->name);
        expr->var->visited = true;
        break;

    case tkPeriod:
        //        {
        //            ASTExpr* e = expr->right;
        //            while (e->kind==tkPeriod) e=e->right;
        ////            if (e->var->visited) break;
        //            printf("%.*sprintf(\"    %s = %s\\n\", %s);\n", level,
        //            spaces,
        //                   expr->var->name, TypeType_format(e->typeType,
        //                   true), expr->var->name);
        //        }
        break;

    case tkFunctionCallResolved:
    case tkFunctionCall: // shouldnt happen
    case tkSubscriptResolved:
    case tkSubscript:
    case tkKeyword_if:
    case tkKeyword_else:
    case tkKeyword_for:
    case tkKeyword_while:
        ASTExpr_genPrintVars(expr->left, level);
        break;

    default:
        if (expr->prec) {
            if (not expr->unary) ASTExpr_genPrintVars(expr->left, level);
            ASTExpr_genPrintVars(expr->right, level);
        }
    }
}

///////////////////////////////////////////////////////////////////////////
// Promotion scan & promotion happens AFTER resolving functions!
static ASTExpr* ASTExpr_promotionCandidate(ASTExpr* expr)
{
    assert(expr);
    ASTExpr* ret;

    // what about func args?
    switch (expr->kind) {
    case tkFunctionCallResolved:
        // promote innermost first, so check args
        if (expr->left and (ret = ASTExpr_promotionCandidate(expr->left)))
            return ret;
        else if (mustPromote(expr->func->selector))
            return expr;
        break;

    case tkSubscriptResolved:
        // TODO: here see if the subscript itself needs to be promoted up
        return ASTExpr_promotionCandidate(expr->left);

    case tkSubscript:
        return ASTExpr_promotionCandidate(expr->left);

    case tkKeyword_if:
    case tkKeyword_for:
    case tkKeyword_else:
    case tkKeyword_elif:
    case tkKeyword_while:
        if (expr->left) return ASTExpr_promotionCandidate(expr->left);
        // body will be handled by parent scope

    case tkVarAssign:
        if ((ret = ASTExpr_promotionCandidate(expr->var->init))) return ret;
        break;

    case tkFunctionCall: // unresolved
        // assert(0);
        unreachable("unresolved call %s\n", expr->string);
        if ((ret = ASTExpr_promotionCandidate(expr->left))) return ret;
        break;

    default:
        if (expr->prec) {
            if ((ret = ASTExpr_promotionCandidate(expr->right))) return ret;
            if (not expr->unary)
                if ((ret = ASTExpr_promotionCandidate(expr->left))) return ret;
        }
    }
    return NULL;
}

static char* newTmpVarName(int num, char c)
{
    char buf[8];
    int l = snprintf(buf, 8, "_%c%d", c, num);
    return pstrndup(buf, l);
}

///////////////////////////////////////////////////////////////////////////
static bool isCtrlExpr(ASTExpr* expr)
{
    return expr->kind == tkKeyword_if //
        or expr->kind == tkKeyword_for //
        or expr->kind == tkKeyword_while //
        or expr->kind == tkKeyword_else;
}

static bool isLiteralExpr(ASTExpr* expr) { return false; }
static bool isComparatorExpr(ASTExpr* expr) { return false; }

///////////////////////////////////////////////////////////////////////////
static void ASTScope_lowerElementalOps(ASTScope* scope)
{
    fp_foreach(ASTExpr*, stmt, scope->stmts)
    {

        if (isCtrlExpr(stmt) and stmt->body)
            ASTScope_lowerElementalOps(stmt->body);

        if (not stmt->elemental) continue;

        // wrap it in an empty block (or use if true)
        ASTExpr* ifblk = fp_new(ASTExpr);
        ifblk->kind = tkKeyword_if;
        ifblk->left = fp_new(ASTExpr);
        ifblk->left->kind = tkNumber;
        ifblk->string = "1";

        // look top-down for subscripts. if you encounter a node with
        // elemental=false, don't process it further even if it may have
        // ranges inside. e.g.
        // vec[7:9] = arr2[6:8] + sin(arr2[-6:-1:-4]) + test[[8,6,5]]
        // + 3 + count(vec[vec < 5]) + M ** x[-8:-1:-4]
        // the matmul above is not
        // elemental, but the range inside it is.
        // the Array_count_filter will be promoted and isnt elemental
        // (unless you plan to set elemental op on boolean subscripts.)
        // Even so, count is a reduce op and will unset elemental.
        // --
        // as you find each subscript, add 2 local vars to the ifblk body
        // so then you might have for the above example :
        // T* vec_p1 = vec->start + 7;
        // // ^ this func could be membptr(a,i) -> i<0 ? a->end-i :
        // a->start+i #define vec_1 *vec_p1 // these could be ASTVars with
        // an isCMacro flag T2* arr2_p1 = membptr(arr2, 6); #define arr2_1
        // *arr2_p1 T3* arr2_p2 = membptr(arr2, -6); #define arr2_2 *arr2_p2
        // ...
        // // now add vars for each slice end and delta
        // const T* const vec_e1 = vec->start + 9; // use membptr
        // const T2* const arr2_e1 = arr2->start + 8; // membptr
        // const T3* const arr2_e2 = membptr(arr2, -4);
        // what about test[[8,6,5]]?
        // const T* const vec_d1 = 1;
        // const T2* const arr2_d1 = 1;
        // const T3* const arr2_d2 = -1;
        // ...
        // // the ends (and starts) could be used for BC.
        // ...
        // // now add a check / separate checks for count match and bounds
        // check_span1deq(vec_e1,vec_p1,arr2_e1,arr2_p1,col1,col2,"vec[7:9]","arr2[6:8]",__FILE__,__LINE__);
        // check_span1deq(arr2_e1,arr2_p1,arr2_e2,arr2_p2,col1,col2,"arr2[6:8]","arr2[-6:-4]",__FILE__,__LINE__);
        // check_inbounds1d(vec, vec_p1, vec_e1,col1,
        // "vec[7:9]",__FILE__,__LINE__) check_inbounds1d(arr2, arr2_p1,
        // arr2_e1,col1, "arr2[6:8]",__FILE__,__LINE__) now change the
        // subscripts in the stmt to unresolved idents, and change the ident
        // by appending _1, _2 etc. based on their position. so when they
        // are generated they will refer to the current item of that array.
        // then wrap the stmt in a for expr 'forblk'. put the for expr in
        // ifblk. the active scope is now the for's body. generate the stmt.
        // it should come out in Number form if all went well.. add
        // increments for each ptr. vec_p1 += vec_d1; arr2_p1 += arr2_d1;
        // arr2_p2 += arr2_d2;
        // ...
        // all done, at the end put the ifblk at the spot of stmt in the
        // original scope. stmt is already inside ifblk inside forblk.
    }
}

///////////////////////////////////////////////////////////////////////////
static void ASTScope_promoteCandidates(ASTScope* scope)
{
    int tmpCount = 0;
    ASTExpr* pc = NULL;
    List(ASTExpr)* prev = NULL;
    fp_foreachn(ASTExpr*, stmt, stmts, scope->stmts)
    {
        // TODO:
        // if (not stmt->promote) {prev=stmts;continue;}

        if (isCtrlExpr(stmt) and stmt->body)
            ASTScope_promoteCandidates(stmt->body);

    startloop:

        if (not(pc = ASTExpr_promotionCandidate(stmt))) { // most likely
            prev = stmts;
            continue;
        }
        if (pc == stmt) {
            // possible, less likely: stmt already at toplevel.
            // TODO: in this case, you still have to add the extra arg.
            prev = stmts;
            continue;
        }

        ASTExpr* pcClone = fp_new(ASTExpr);
        *pcClone = *pc;

        // 1. add a temp var to the scope
        ASTVar* tmpvar = fp_new(ASTVar);
        tmpvar->name = newTmpVarName(++tmpCount, 'p');
        tmpvar->typeSpec = fp_new(ASTTypeSpec);
        //        tmpvar->typeSpec->typeType = TYReal64; // FIXME
        // TODO: setup tmpvar->typeSpec
        fp_PtrList_append(&scope->locals, tmpvar);

        // 2. change the original to an ident
        pc->kind = tkIdentifierResolved;
        pc->prec = 0;
        pc->var = tmpvar;

        // 3. insert the tmp var as an additional argument into the call

        if (not pcClone->left)
            pcClone->left = pc;
        else if (pcClone->left->kind != tkOpComma) {
            // single arg
            ASTExpr* com = fp_new(ASTExpr);
            // TODO: really should have an astexpr ctor
            com->prec = TokenKind_getPrecedence(tkOpComma);
            com->kind = tkOpComma;
            com->left = pcClone->left;
            com->right = pc;
            pcClone->left = com;
        } else {
            ASTExpr* argn = pcClone->left;
            while (argn->kind == tkOpComma and argn->right->kind == tkOpComma)
                argn = argn->right;
            ASTExpr* com = fp_new(ASTExpr);
            // TODO: really should have an astexpr ctor
            com->prec = TokenKind_getPrecedence(tkOpComma);
            com->kind = tkOpComma;
            com->left = argn->right;
            com->right = pc;
            argn->right = com;
        }

        // 4. insert the promoted expr BEFORE the current stmt
        //        PtrList_append(prev ? &prev : &self->stmts, pcClone);
        //        PtrList* tmp = prev->next;
        // THIS SHOULD BE in PtrList as insertAfter method
        if (not prev) {
            scope->stmts = fp_PtrList_with(pcClone);
            scope->stmts->next = stmts;
            prev = scope->stmts;
        } else {
            prev->next = fp_PtrList_with(pcClone);
            prev->next->next = stmts;
            prev = prev->next;
        } // List(ASTExpr)* insertionPos = prev ? prev->next : self->stmts;
          //  insertionPos
        //  = insertionPos;
        goto startloop; // it will continue there if no more promotions are
                        // needed

        prev = stmts;
    }
}

///////////////////////////////////////////////////////////////////////////
static void ASTScope_genc(ASTScope* scope, int level)
{
    fp_foreach(ASTVar*, local, scope->locals) if (local->used)
    {
        ASTVar_genc(local, level, false);
        puts(";");
    } // these will be declared at top and defined within the expr list
    fp_foreach(ASTExpr*, stmt, scope->stmts)
    {
        if (stmt->kind == tkLineComment) continue;

        if (genLineNumbers) printf("#line %d\n", stmt->line);
        if (genCoverage) printf("_cov_[%d]++;\n", stmt->line - 1);
        if (genLineProfile) {
            printf("_lprof_tmp_ = getticks();\n");
            printf("_lprof_[%d] += (_lprof_tmp_-_lprof_last_)/100;\n",
                stmt->line - 1);
            printf("_lprof_last_ = _lprof_tmp_;\n");
        }
        ASTExpr_genc(stmt, level, true, false, false);
        if (not isCtrlExpr(stmt) and stmt->kind != tkKeyword_return)
            puts(";");
        else
            puts("");
        // convert this into a flag which is set in the resolution pass
        if (ASTExpr_throws(stmt))
            puts("    if (_err_ == ERROR_TRACE) goto backtrace;");
    }
}

///////////////////////////////////////////////////////////////////////////
static void ASTType_genJson(ASTType* type)
{
    printf("static void %s_json_(const %s self, int nspc) {\n", type->name,
        type->name);

    printf("    printf(\"{\\n\");\n");
    // printf("    printf(\"\\\"_type_\\\": \\\"%s\\\"\");\n", type->name);
    // if (type->body->locals) printf("    printf(\",\\n\");\n");

    // TODO: move this part into its own func so that subclasses can ask the
    // superclass to add in their fields inline
    fp_foreachn(ASTVar*, var, vars, type->body->locals)
    {
        if (not var) continue;
        printf("    printf(\"%%.*s\\\"%s\\\": \", nspc+4, _spaces_);\n",
            var->name);
        const char* valueType = ASTExpr_typeName(var->init);
        printf("    %s_json_(self->%s, nspc+4);\n    printf(\"", valueType,
            var->name);
        if (vars->next) printf(",");
        printf("\\n\");\n");
    }
    printf("    printf(\"%%.*s}\", nspc, _spaces_);\n");
    printf("}\nMAKE_json_wrap_(%s)\n//MAKE_json_file(%s)\n", type->name,
        type->name);
    // printf("#define %s_json(x) { printf(\"\\\"%%s\\\": \",#x); "
    //        "%s_json_wrap_(x); }\n\n",
    //     type->name, type->name);
}

///////////////////////////////////////////////////////////////////////////
static void ASTType_genJsonReader(ASTType* type) {}

static const char* functionEntryStuff_UNESCAPED
    = "#ifndef NOSTACKCHECK\n"
      "    STACKDEPTH_UP\n"
      "    // printf(\"%8lu %8lu\\n\",_scUsage_, _scSize_);\n"
      "    if (_scUsage_ >= _scSize_) {\n"
      "#ifdef DEBUG\n"
      "        _scPrintAbove_ = _scDepth_ - _btLimit_;\n"
      "        printf(\"\\e[31mfatal: stack overflow at call depth %lu.\\n    "
      "in %s\\e[0m\\n\", _scDepth_, sig_);\n"
      "        printf(\"\\e[90mBacktrace (innermost first):\\n\");\n"
      "        if (_scDepth_ > 2*_btLimit_)\n"
      "            printf(\"    limited to %d outer and %d inner "
      "entries.\\n\", _btLimit_, _btLimit_);\n"
      "        printf(\"[%lu] \\e[36m%s\\n\", _scDepth_, callsite_);\n"
      "#else\n"
      "        printf(\"\\e[31mfatal: stack  overflow at call depth "
      "%lu.\\e[0m\\n\",_scDepth_);\n"
      "#endif\n"
      "        DONE\n    }\n"
      "#endif\n";
static const char* functionExitStuff_UNESCAPED
    = "    return DEFAULT_VALUE;\n assert(0);\n"
      "error:\n"
      "#ifdef DEBUG\n"
      "    eprintf(\"error: %s\\n\",_err_);\n"
      "#endif\n"
      "backtrace:\n"
      "#ifdef DEBUG\n"

      "    if (_scDepth_ <= _btLimit_ || _scDepth_ > _scPrintAbove_)\n"
      "        printf(\"\\e[90m[%lu] \\e[36m%s\\n\", _scDepth_, callsite_);\n"
      "    else if (_scDepth_ == _scPrintAbove_)\n"
      "        printf(\"\\e[90m... truncated ...\\e[0m\\n\");\n"
      "#endif\n"
      "done:\n"
      "#ifndef NOSTACKCHECK\n"
      "    STACKDEPTH_DOWN\n"
      "#endif\n"
      "    return DEFAULT_VALUE;";

///////////////////////////////////////////////////////////////////////////
static void ASTFunc_printStackUsageDef(size_t stackUsage)
{
    printf("#ifdef DEBUG\n"
           "#define MYSTACKUSAGE (%lu + 6*sizeof(void*) + sizeof(char*))\n"
           "#else\n"
           "#define MYSTACKUSAGE (%lu + 6*sizeof(void*))\n"
           "#endif\n",
        stackUsage, stackUsage);
}

///////////////////////////////////////////////////////////////////////////
static void ASTType_genc(ASTType* type, int level)
{
    if (not type->body or not type->analysed) return;
    const char* const name = type->name;
    printf("#define FIELDS_%s \\\n", name);
    fp_foreach(ASTVar*, var, type->body->locals)
    {
        if (not var) continue;
        ASTVar_genc(var, level + STEP, false);
        printf("; \\\n");
    }
    printf("\n\nstruct %s {\n", name);

    if (type->super) {
        printf("    FIELDS_");
        ASTTypeSpec_genc(type->super, level, false);
        printf("\n");
    }

    printf("    FIELDS_%s\n};\n\n", name);
    printf("static const char* %s_name_ = \"%s\";\n", name, name);
    printf("static %s %s_alloc_() {\n    return _Pool_alloc_(&gPool_, "
           "sizeof(struct %s));\n}\n",
        name, name, name);
    printf("static %s %s_init_(%s self) {\n", name, name, name);

    fp_foreach(ASTVar*, var, type->body->locals)
        printf("#define %s self->%s\n", var->name, var->name);

    fp_foreach(ASTExpr*, stmt, type->body->stmts)
    {
        if (not stmt or stmt->kind != tkVarAssign or not stmt->var->init)
            continue;
        printf("%.*s%s = ", level + STEP, spaces, stmt->var->name);
        ASTExpr_genc(stmt->var->init, 0, true, false, false);
        puts(";");
        if (ASTExpr_throws(stmt->var->init))
            puts("    if (_err_ == ERROR_TRACE) return NULL;");
    }
    fp_foreach(ASTVar*, var, type->body->locals)
        printf("#undef %s \n", var->name);

    printf("    return self;\n}\n");
    printf(
        "%s %s_new_(\n#ifdef DEBUG\n    const char* callsite_\n#endif\n) {\n",
        name, name);
    printf("#define DEFAULT_VALUE NULL\n#ifdef DEBUG\n    static const char* "
           "sig_ = \"%s()\";\n#endif\n",
        name);
    ASTFunc_printStackUsageDef(48);
    puts(functionEntryStuff_UNESCAPED);
    printf("    %s ret = %s_alloc_(); %s_init_(ret); if (_err_==ERROR_TRACE) "
           "goto backtrace;"
           "_err_ = NULL; \n#ifndef NOSTACKCHECK\n    "
           "STACKDEPTH_DOWN\n#endif\n     return ret;",
        name, name, name);
    puts(functionExitStuff_UNESCAPED);
    puts("#undef DEFAULT_VALUE\n#undef MYSTACKUSAGE\n}");
    printf("#define %s_print_(p) %s_print__(p, STR(p))\n", name, name);
    printf("void %s_print__(%s self, const char* name) {\n    printf(\"<%s "
           "'%%s' at %%p\\n>\",name,self);\n}\n",
        name, name, name);
    puts("");

    ASTType_genJson(type);
    ASTType_genJsonReader(type);
}

///////////////////////////////////////////////////////////////////////////
static void ASTType_genh(ASTType* type, int level)
{
    if (not type->body or not type->analysed) return;
    const char* const name = type->name;
    printf("typedef struct %s* %s;\nstruct %s;\n", name, name, name);
    printf("static %s %s_alloc_(); \n", name, name);
    printf("static %s %s_init_(%s self);\n", name, name, name);
    printf(
        "%s %s_new_(\n#ifdef DEBUG\n    const char* callsite_\n#endif\n); \n",
        name, name);
    printf("\nDECL_json_wrap_(%s)\n//DECL_json_file(%s)\n", name, name);
    printf("#define %s_json(x) { printf(\"\\\"%%s\\\": \",#x); "
           "%s_json_wrap_(x); }\n\n",
        name, name);
    printf("static void %s_json_(const %s self, int nspc);\n", name, name);
}

///////////////////////////////////////////////////////////////////////////
static void ASTFunc_genc(ASTFunc* func, int level)
{
    if (not func->body or not func->analysed) return; // declares, default ctors

    // actual stack usage is higher due to stack protection, frame bookkeeping
    // ...
    size_t stackUsage = ASTFunc_calcSizeUsage(func);
    ASTFunc_printStackUsageDef(stackUsage);

    printf(
        "#define DEFAULT_VALUE %s\n", getDefaultValueForType(func->returnSpec));
    if (not func->isExported) printf("static ");
    if (func->returnSpec) {
        ASTTypeSpec_genc(func->returnSpec, level, false);
    } else {
        printf("void");
    }
    printf(" %s(", func->selector);
    fp_foreachn(ASTVar*, arg, args, func->args)
    {
        ASTVar_genc(arg, level, true);
        printf(args->next ? ", " : "");
    }

    printf("\n#ifdef DEBUG\n"
           "    %c const char* callsite_ "
           "\n#endif\n",
        ((func->args and func->args->item ? ',' : ' ')));

    // TODO: if (flags.throws) printf("const char** _err_");
    puts(") {");
    printf("#ifdef DEBUG\n"
           "    static const char* sig_ = \"");
    printf("%s%s(", func->isStmt ? "" : "function ", func->name);

    fp_foreachn(ASTVar*, arg, args, func->args)
    {
        ASTVar_gen(arg, level);
        printf(args->next ? ", " : ")");
    }
    if (func->returnSpec) {
        printf(" returns ");
        ASTTypeSpec_gen(func->returnSpec, level);
    }
    puts("\";\n#endif");

    puts(functionEntryStuff_UNESCAPED);

    ASTScope_genc(func->body, level + STEP);

    puts(functionExitStuff_UNESCAPED);
    puts("}\n#undef DEFAULT_VALUE");
    puts("#undef MYSTACKUSAGE");
}

///////////////////////////////////////////////////////////////////////////
static void ASTFunc_genh(ASTFunc* func, int level)
{
    if (not func->body or not func->analysed) return;
    if (not func->isExported) printf("static ");
    if (func->returnSpec) {
        ASTTypeSpec_genc(func->returnSpec, level, false);
    } else {
        printf("void");
    }
    printf(" %s(", func->selector);
    fp_foreachn(ASTVar*, arg, args, func->args)
    {
        ASTVar_genc(arg, level, true);
        printf(args->next ? ", " : "");
    }
    printf("\n#ifdef DEBUG\n    %c const char* callsite_ "
           "\n#endif\n",
        ((func->args and func->args->item) ? ',' : ' '));
    puts(");\n");
}

////////////////////////////////////////////////////

static void ASTTest_genc(ASTTest* test)
{
    // TODO: should tests not return BOOL?
    if (not test->body) return;
    printf("\nstatic void test_%s() {\n", test->name);
    ASTScope_genc(test->body, STEP);
    puts("}");
}

///////////////////////////////////////////////////////////////////////////
static void ASTExpr_genc(
    ASTExpr* expr, int level, bool spacing, bool inFuncArgs, bool escStrings)
{
    // generally an expr is not split over several lines (but maybe in
    // rare cases). so level is not passed on to recursive calls.

    printf("%.*s", level, spaces);
    switch (expr->kind) {
    case tkNumber:
    case tkMultiDotNumber:
    case tkIdentifier:
        printf("%s", expr->string);
        break;

    case tkString:
        // TODO: parse vars inside, escape stuff, etc.
        printf(escStrings ? "\\%s\\\"" : "%s\"", expr->string);
        break;

    case tkIdentifierResolved:
        printf("%s", expr->var->name);
        break;

    case tkRegex:
        // 'raw strings' or 'regexes'
        printf("\"%s\"", expr->string + 1);
        break;

    case tkInline:
        // inline C code?
        printf("%s", expr->string + 1);
        break;

    case tkLineComment:
        // TODO: skip  comments in generated code
        printf("// %s", expr->string);
        break;

    case tkFunctionCall:
        unreachable("unresolved call to %s\n", expr->string);
        assert(0);
        break;

    case tkFunctionCallResolved: {
        char* tmp = expr->func->selector;

        ASTExpr* arg1 = expr->left;
        const char* tmpc = "";
        if (arg1) {
            if (arg1->kind == tkOpComma) arg1 = arg1->left;
            tmpc = CollectionType_nativeName(arg1->collectionType);
        }
        printf("%s%s", tmpc, tmp);
        if (*tmp >= 'A' and *tmp <= 'Z' and not strchr(tmp, '_'))
            printf("_new_");
        printf("(");

        if (expr->left) ASTExpr_genc(expr->left, 0, false, true, escStrings);

        if (not expr->func->isDeclare) {
            printf("\n#ifdef DEBUG\n"
                   "      %c \"./\" THISFILE \":%d:%d:\\e[0m ",
                expr->left ? ',' : ' ', expr->line, expr->col);
            ASTExpr_gen(expr, 0, false, true);
            printf("\"\n"
                   "#endif\n        ");
        }
        printf(")");
    } break;

    case tkSubscript:
        assert(0);
        break;

    case tkSubscriptResolved: {
        char* name = expr->var->name;
        ASTExpr* index = expr->left;
        assert(index);
        switch (index->kind) {
        case tkNumber:
            // indexing with a single number
            // can be a -ve number
            printf("Array_get_%s(%s, %s)",
                ASTTypeSpec_cname(expr->var->typeSpec), name, index->string);
            break;

        case tkString:
        case tkRegex:
            // indexing with single string or regex
            printf("Dict_get_CString_%s(%s, %s)",
                ASTTypeSpec_cname(expr->var->typeSpec), name, index->string);
            break;

        case tkOpComma:
            // higher dimensions. validation etc. has been done by this
            // stage.

            // this is for cases like arr[2, 3, 4].
            printf("Tensor%dD_get_%s(%s, {", expr->var->typeSpec->dims,
                ASTTypeSpec_cname(expr->var->typeSpec), name);
            ASTExpr_genc(index, 0, false, inFuncArgs, escStrings);
            printf("})");

            // TODO: cases like arr[2:3, 4:5, 1:end]
            // basically the idea is to generate getijk/getIJK/getIJk etc.
            // where a caps means range and lowercase means single number.
            // so arr[2:3, 4:5, 1:end] should generate `getIJK`,
            // arr[2:3, 4, 2:end] should generate `getIjK` and so on.
            // Those are then macros in the "runtime" that have for loops
            // for the ranges and nothing special for the single indices.
            // but they should be put into a tmpvar to avoid repeated eval.

            break;

        case tkOpColon:
            // a single range.
            printf("Array_getSlice_%s(%s, ",
                ASTTypeSpec_name(expr->var->typeSpec), name);
            ASTExpr_genc(index, 0, false, inFuncArgs, escStrings);
            printf(")");
            break;
            // what about mixed cases, e.g. arr[2:3, 5, 3:end]
            // make this portion a recursive function then, or promote
            // all indexes to ranges first and then let opcomma handle it

        case tkOpEQ:
        case tkOpLE:
        case tkOpGE:
        case tkOpGT:
        case tkOpLT:
        case tkOpNE:
        case tkKeyword_and:
        case tkKeyword_or:
        case tkKeyword_not:
            // indexing by a Boolean expression (filter)
            // by default this implies a copy, but certain funcs e.g. print
            // min max sum count etc. can be done in-place without a copy
            // since they are not mutating the array. That requires either
            // the user to call print(arr, filter = arr < 5) instead of
            // print(arr[arr < 5]), or the compiler to transform the second
            // into the first transparently.
            // Probably the tkFunctionCall should check if its argument is
            // a tkSubscript with a Boolean index, and then tip the user
            // to call the optimised function instead (or just generate it).
            // For now, and in the absence of more context, this is a copy.
            // Array_copy_filter is implemented as a C macro for loop, as
            // are most other filtering-enabled functions on arrays.
            // TODO: be careful with the "template" style call here xx()()
            // TODO: actually I think arr[arr < 5] etc. should just be
            // promoted
            //    and then the generation will follow the modified AST.
            //    Don't handle this as a special case at the code generation
            //    stage.
            printf("Array_copy_filter_%s(%s, ",
                ASTTypeSpec_name(expr->var->typeSpec), name);
            ASTExpr_genc(index, 0, false, inFuncArgs, escStrings);
            printf(")");
            break;

        default:
            assert(0);
            break;
        }
        break;
    }

    case tkOpAssign:
    case tkPlusEq:
    case tkMinusEq:
    case tkTimesEq:
    case tkSlashEq:
    case tkPowerEq:
    case tkOpModEq:

        switch (expr->left->kind) {
        case tkSubscriptResolved:
            switch (expr->left->left->kind) {
            case tkNumber:
            case tkString:
            case tkRegex:
                // TODO: astexpr_typename should return Array_Scalar or
                // Tensor2D_Scalar or Dict_String_Scalar etc.
                printf("%s_set(%s, %s,%s, ", ASTExpr_typeName(expr->left),
                    expr->left->var->name, expr->left->left->string,
                    TokenKind_repr(expr->kind, spacing));
                ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
                printf(")");
                break;

            case tkOpColon:
                printf("%s_setSlice(%s, ", ASTExpr_typeName(expr->left),
                    expr->left->var->name);
                ASTExpr_genc(
                    expr->left->left, 0, spacing, inFuncArgs, escStrings);
                printf(",%s, ", TokenKind_repr(expr->kind, spacing));
                ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
                printf(")");
                break;

            case tkOpEQ:
            case tkOpGE:
            case tkOpNE:
            case tkOpGT:
            case tkOpLE:
            case tkOpLT:
            case tkKeyword_and:
            case tkKeyword_or:
            case tkKeyword_not:
                printf("%s_setFiltered(%s, ", ASTExpr_typeName(expr->left),
                    expr->left->var->name);
                ASTExpr_genc(
                    expr->left->left, 0, spacing, inFuncArgs, escStrings);
                printf(",%s, ", TokenKind_repr(expr->kind, spacing));
                ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
                printf(")");
                break;

            case tkOpComma:
                // figure out the type of each element
                // there should be a RangeND just like TensorND and SliceND
                // then you can just pass that to _setSlice
                break;
            case tkIdentifierResolved:
                // lookup the var type. note that it need not be Number,
                // string, range etc. it could be an arbitrary object in
                // case you are indexing a Dict with keys of that type.
                break;
            case tkSubscriptResolved:
                // arr[arr2[4]] etc.
                break;
            case tkFunctionCallResolved:
                // arr[func(x)]
                break;
            default:
                unreachable("%s\n", TokenKind_str[expr->left->kind]);
                assert(0);
            }
            break;
        case tkIdentifierResolved:
        case tkPeriod:
            ASTExpr_genc(expr->left, 0, spacing, inFuncArgs, escStrings);
            printf("%s", TokenKind_repr(expr->kind, spacing));
            ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
            break;
        case tkIdentifier:
            assert(inFuncArgs);
            ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
            // function call arg label, do not generate ->left
            break;
        default:
            // error: not a valid lvalue
            // TODO: you should at some point e,g, during resolution check
            // for assignments to invalid lvalues and raise an error
            assert(0);
        }
        // if (not inFuncArgs) {
        //     ASTExpr_genc(self->left, 0, spacing, inFuncArgs,
        //     escStrings); printf("%s", TokenKind_repr(tkOpAssign,
        //     spacing));
        // }
        // ASTExpr_genc(self->right, 0, spacing, inFuncArgs, escStrings);
        // check various types of lhs  here, eg arr[9:87] = 0,
        // map["uuyt"]="hello" etc.
        break;

    case tkArrayOpen:
        // TODO: send parent ASTExpr* as an arg to this function. Then
        // here do various things based on whether parent is a =,
        // funcCall, etc.
        printf("mkarr(((%s[]) {", "double"); // FIXME
        // TODO: MKARR should be different based on the CollectionType
        // of the var or arg in question, eg stack cArray, heap
        // allocated Array, etc.
        ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
        printf("})");
        printf(", %d)", ASTExpr_countCommaList(expr->right));
        break;

    case tkOpColon: // convert 3:4:5 to range(...)
                    // must do bounds check first!
        printf(
            "%s(", expr->left->kind != tkOpColon ? "range_to" : "range_to_by");
        if (expr->left->kind == tkOpColon) {
            expr->left->kind = tkOpComma;
            ASTExpr_genc(expr->left, 0, false, inFuncArgs, escStrings);
            expr->left->kind = tkOpColon;
        } else
            ASTExpr_genc(expr->left, 0, false, inFuncArgs, escStrings);
        printf(", ");
        ASTExpr_genc(expr->right, 0, false, inFuncArgs, escStrings);
        printf(")");
        break;

    case tkVarAssign: // basically a tkOpAssign corresponding to a local
                      // var
        // var x as XYZ = abc... -> becomes an ASTVar and an
        // ASTExpr (to keep location). Send it to ASTVar::gen.
        if (expr->var->init != NULL and expr->var->used) {
            printf("%s = ", expr->var->name);
            ASTExpr_genc(expr->var->init, 0, true, inFuncArgs, escStrings);
        }
        break;

    case tkKeyword_else:
        puts("else {");
        if (expr->body) ASTScope_genc(expr->body, level + STEP);
        printf("%.*s}", level, spaces);
        break;

    case tkKeyword_elif:
        puts("else if (");
        ASTExpr_genc(expr->left, 0, spacing, inFuncArgs, escStrings);
        puts(") {");
        if (expr->body) ASTScope_genc(expr->body, level + STEP);
        printf("%.*s}", level, spaces);
        break;

    case tkKeyword_for:
    case tkKeyword_if:
        //    case tkKeyword_elif:
        //    case tkKeyword_else:
    case tkKeyword_while:
        if (expr->kind == tkKeyword_for)
            printf("FOR(");
        else
            printf("%s (", TokenKind_repr(expr->kind, true));
        if (expr->kind == tkKeyword_for) expr->left->kind = tkOpComma;
        if (expr->left)
            ASTExpr_genc(expr->left, 0, spacing, inFuncArgs, escStrings);
        if (expr->kind == tkKeyword_for) expr->left->kind = tkOpAssign;
        puts(") {");
        if (expr->body) ASTScope_genc(expr->body, level + STEP);
        printf("%.*s}", level, spaces);
        break;

    case tkPower:
        printf("pow(");
        ASTExpr_genc(expr->left, 0, false, inFuncArgs, escStrings);
        printf(",");
        ASTExpr_genc(expr->right, 0, false, inFuncArgs, escStrings);
        printf(")");
        break;

    case tkKeyword_return:
        printf("{_err_ = NULL; \n#ifndef NOSTACKCHECK\n    "
               "STACKDEPTH_DOWN\n#endif\nreturn ");
        ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
        printf(";}\n");
        break;
    case tkKeyword_check: {
        // TODO: need llhs and lrhs in case all 3 in 3way are exprs
        // e.g. check a+b < c+d < e+f
        ASTExpr* checkExpr = expr->right; // now use checkExpr below
        ASTExpr* lhsExpr = checkExpr->left;
        ASTExpr* rhsExpr = checkExpr->right;
        printf("{\n");
        if (not checkExpr->unary) {
            printf("%.*s%s _lhs = ", level, spaces, ASTExpr_typeName(lhsExpr));
            ASTExpr_genc(lhsExpr, 0, spacing, false, false);
            printf(";\n");
        }
        printf("%.*s%s _rhs = ", level, spaces, ASTExpr_typeName(rhsExpr));
        ASTExpr_genc(rhsExpr, 0, spacing, false, false);
        printf(";\n");
        printf("%.*sif (not(", level, spaces);
        ASTExpr_genc(checkExpr, 0, spacing, false, false);
        printf(")) {\n");
        printf("%.*sprintf(\"\\n\\n\e[31mruntime error:\e[0m check "
               "failed at "
               "\e[36m./%%s:%d:%d:\e[0m\\n    "
               "%%s\\n\\n\", THISFILE, \"",
            level + STEP, spaces, expr->line, expr->col + 6);
        ASTExpr_gen(checkExpr, 0, spacing, true);
        printf("\");\n");
        printf("#ifdef DEBUG\n%.*sprintf(\"\e[90mHere's some "
               "help:\e[0m\\n\");\n",
            level + STEP, spaces);

        ASTExpr_genPrintVars(checkExpr, level + STEP);
        // the `printed` flag on all vars of the expr will be set
        // (genPrintVars uses this to avoid printing the same var
        // twice). This should be unset after every toplevel call to
        // genPrintVars.
        if (not checkExpr->unary) {
            // dont print literals or arrays
            if (lhsExpr->collectionType == CTYNone //
                and lhsExpr->kind != tkString //
                and lhsExpr->kind != tkNumber //
                and lhsExpr->kind != tkRegex //
                and lhsExpr->kind != tkOpLE //
                and lhsExpr->kind != tkOpLT) {

                if (lhsExpr->kind != tkIdentifierResolved
                    or not lhsExpr->var->visited) {
                    printf(
                        "%.*s%s", level + STEP, spaces, "printf(\"    %s = ");
                    printf("%s", TypeType_format(lhsExpr->typeType, true));
                    printf("%s", "\\n\", \"");
                    ASTExpr_gen(lhsExpr, 0, spacing, true);
                    printf("%s", "\", _lhs);\n");
                }
                // checks can't have tkVarAssign inside them
                // if ()
                //     lhsExpr->var->visited = true;
            }
        }
        if (rhsExpr->collectionType == CTYNone //
            and rhsExpr->kind != tkString //
            and rhsExpr->kind != tkNumber //
            and rhsExpr->kind != tkRegex) {
            if (rhsExpr->kind != tkIdentifierResolved
                or not rhsExpr->var->visited) {
                printf("%.*s%s", level + STEP, spaces, "printf(\"    %s = ");
                printf("%s", TypeType_format(rhsExpr->typeType, true));
                printf("%s", "\\n\", \"");
                ASTExpr_gen(rhsExpr, 0, spacing, true);
                printf("%s", "\", _rhs);\n");
            }
        }

        ASTExpr_unmarkVisited(checkExpr);

        printf("%.*sprintf(\"\\n\");\n", level + STEP, spaces);
        printf("%s",
            /*"#ifdef DEBUG\n"*/
            "        printf(\"\\e[90mBacktrace (innermost "
            "first):\\n\");\n"
            "        if (_scDepth_ > 2*_btLimit_)\n        "
            "printf(\"    limited to %d outer and %d inner "
            "entries.\\n\", "
            "_btLimit_, _btLimit_);\n"
            "        BACKTRACE\n    \n"
            "#else\n"
            "        eputs(\"(run in debug mode to get more info)\\n\"); "
            "exit(1);\n"
            "#endif\n");
        printf("\n%.*s}\n", level, spaces);
        printf("%.*s}", level, spaces);
    } break;

    case tkPeriod:
        ASTExpr_genc(expr->left, 0, spacing, inFuncArgs, escStrings);
        printf("->"); // may be . if right is embedded and not a reference
        ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
        break;

    case tkOpEQ:
    case tkOpNE:
    case tkOpGE:
    case tkOpLE:
    case tkOpGT:
    case tkOpLT:
        if ((expr->kind == tkOpLE or expr->kind == tkOpLT)
            and (expr->left->kind == tkOpLE or expr->left->kind == tkOpLT)) {
            printf("%s_cmp3way_%s_%s(", ASTExpr_typeName(expr->left->right),
                TokenKind_ascrepr(expr->kind, false),
                TokenKind_ascrepr(expr->left->kind, false));
            ASTExpr_genc(expr->left->left, 0, spacing, inFuncArgs, escStrings);
            printf(", ");
            ASTExpr_genc(expr->left->right, 0, spacing, inFuncArgs, escStrings);
            printf(", ");
            ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
            printf(")");
            break;
        } else if (expr->right->typeType == TYString) {
            printf("str_cmp(%s, ", tksrepr[expr->kind]);
            ASTExpr_genc(expr->left, 0, spacing, inFuncArgs, escStrings);
            printf(", ");
            ASTExpr_genc(expr->right, 0, spacing, inFuncArgs, escStrings);
            printf(")");
            break;
        }
        fallthrough default : if (not expr->prec) break;
        // not an operator, but this should be error if you reach here
        bool leftBr
            = expr->left and expr->left->prec and expr->left->prec < expr->prec;
        bool rightBr = expr->right and expr->right->prec
            and expr->right->kind != tkKeyword_return
            and expr->right->prec < expr->prec;
        // found in 'or return'

        char lpo = '(';
        char lpc = ')';
        if (leftBr) putc(lpo, stdout);
        if (expr->left)
            ASTExpr_genc(expr->left, 0,
                spacing and !leftBr and expr->kind != tkOpColon, inFuncArgs,
                escStrings);
        if (leftBr) putc(lpc, stdout);

        if (expr->kind == tkArrayOpen)
            putc('{', stdout);
        else
            printf("%s", TokenKind_repr(expr->kind, spacing));

        char rpo = '(';
        char rpc = ')';
        if (rightBr) putc(rpo, stdout);
        if (expr->right)
            ASTExpr_genc(expr->right, 0,
                spacing and not rightBr and expr->kind != tkOpColon, inFuncArgs,
                escStrings);
        if (rightBr) putc(rpc, stdout);

        if (expr->kind == tkArrayOpen) putc('}', stdout);
    }
}

// WARNING: DO NOT USE THESE STRINGS WITH PRINTF(...) USE PUTS(...).
static const char* coverageFunc[] = { //
    "static void fp_coverage_report() { /* unused */ }",
    "static void fp_coverage_report() {\n"
    "    int count=0,l=NUMLINES;\n"
    "    while(--l>0) count+=!!_cov_[l];\n"
    "    printf(\"coverage: %d/%d lines = %.2f%%\\n\","
    "        count, NUMLINES, count*100.0/NUMLINES);\n"
    "}"
};
// WARNING: DO NOT USE THESE STRINGS WITH PRINTF(...) USE PUTS(...).
static const char* lineProfileFunc[] = {
    //
    "static void fp_lineprofile_report() { /* unused */ }\n"
    "static void fp_lineprofile_begin() { /* unused */ }\n",
    "static void fp_lineprofile_report() {\n"
    // "    printf(\"profiler: %llu cycles\\n\","
    // "        _lprof_[NUMLINES-1]-_lprof_[0]);\n"
    "    FILE* fd = fopen(\".\" THISFILE \"r\", \"w\");\n"
    // "    for (int i=1; i<NUMLINES; i++)\n"
    // "        if (0== _lprof_[i]) _lprof_[i] = _lprof_[i-1];\n"
    // "    for (int i=NUMLINES-1; i>0; i--) _lprof_[i] -= _lprof_[i-1];\n"
    "    ticks sum=0; for (int i=0; i<NUMLINES; i++) sum += _lprof_[i];\n"
    "    for (int i=0; i<NUMLINES; i++) {\n"
    "        double pct = _lprof_[i] * 100.0 / sum;\n"
    "        if (pct>1.0)"
    "            fprintf(fd,\" %8.1f%% |\\n\", pct);\n"
    "        else if (pct == 0.0)"
    "            fprintf(fd,\"           |\\n\");\n"
    "        else"
    "            fprintf(fd,\"         ~ |\\n\");\n"
    "    }\n"
    "    fclose(fd);\n"
    "    system(\"paste -d ' ' .\" THISFILE \"r \" THISFILE \" > \" "
    "THISFILE "
    "\"r\" );"
    "}\n"
    "static void fp_lineprofile_begin() {_lprof_last_=getticks();}\n"
};
///////////////////////////////////////////////////////////////////////////
// TODO: why do you need to pass level here?
static void ASTModule_genc(ASTModule* module, int level)
{
    // puts("");
    fp_foreach(ASTImport*, import, module->imports)
        ASTImport_genc(import, level);

    puts("");

    fp_foreach(ASTType*, type, module->types) ASTType_genh(type, level);
    fp_foreach(ASTFunc*, func, module->funcs) ASTFunc_genh(func, level);

    fp_foreach(ASTType*, type, module->types) ASTType_genc(type, level);
    fp_foreach(ASTFunc*, func, module->funcs) ASTFunc_genc(func, level);
    fp_foreach(ASTImport*, import, module->imports) ASTImport_undefc(import);

    puts(coverageFunc[genCoverage]);
    puts(lineProfileFunc[genLineProfile]);
}

static void ASTModule_genTests(ASTModule* module, int level)
{
    ASTModule_genc(module, level);
    fp_foreach(ASTTest*, test, module->tests) ASTTest_genc(test);
    // generate a func that main will call
    printf("\nvoid tests_run_%s() {\n", module->name);
    fp_foreach(ASTTest*, test, module->tests)
        printf("    test_%s();\n", test->name);
    puts("}");
}
