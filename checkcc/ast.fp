
type Import
    var importFile = ""
    var aliasOffset = 0
    var isPackage = no
    var hasAlias = no
end type

type Units
    var powers[7] = 0
    var factor = 0
    var factors[7] = 1
    var label = ""
end type

type TypeSpec
    type union
        var type as Type
        var name = ""
        var units as Units
    end type
    var dims = 0
    var col = 0
    var line = 0
    var typeType = Types.unresolved
    var collectionType = Collections.none
    var nullable = no
end type

type Var
    var typeSpec = TypeSpec()
    var init = Expr()
    var name = ""
    var line = 0
    var col = 0
    var used = no
    var changed = no
    var isLet = no
    var isVar = no
    var stackAlloc = no
    var isTarget = no
    var printed = no
    var escapes = no
    var canInplace = no
    var returned = no
end type

type Expr
    var line = 0
    var typeType = Types.unresolved
    var collectionType = Collections.none
    var nullable = no
    var impure = no
    var elemental = no
    var throws = no
    var promote = no
    var canEval = no
    var didEval = no
    var prec = 0
    var unary = no
    var rassoc = no
    var col = 0
    var kind = Kinds.unknown
    var left as Expr = nil
    var value as Value
    var func as Func = nil
    var var as Var = nil
    var body as Scope = nil
    var right as Expr = nil
end type

type Scope
    var stmts[] as Expr
    var locals[] as Var
    var parent as Scope = nil
end type

type Type
    var super as TypeSpec = nil
    var name = ""
    var body as Scope = nil
    var line = 0
    var col = 0
    var sempassDone = no
    var isValueType = no
end type

type Func
    var body as Scope = nil
    var args[] as Var
    var returnType as TypeSpec = nil
    var name = ""
    var selector = ""

        var line = 0

            uint16_t usesIO  throws  isRecursive  usesNet : 1
                usesGUI  usesSerialisation  isExported : 1
                usesReflection  nodispatch  isStmt  isDeclare : 1
                isCalledFromWithinLoop  elemental  isDefCtor : 1
                semPassDone : 1
        end type
        var argCount = 0
    end type
end type

type Test
    var body as Scope = nil
    var name = ""
    var selector = ""
    var line = 0
    var semPassDone = no
end type

type Module
    var funcs[] as Func
    var tests[] as Test
    var exprs[] as Expr
    var types[] as Type
    var globals[] as Var
    var imports[] as Import
    var name = ""
    var moduleName = ""
end type
