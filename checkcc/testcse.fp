# declare type Strings
declare function print(boom as String, wha as Number)

declare function sum(size as Number) result Number

type TestType
    var m = 43
    var g = 33
    # always 0 <= g <= 1
end type

function main(args[] as String) result Number
    # check len(args[]) > 1

    print("Hello")
    print("Hello", wha=5)
    var m = 0
    m = 3 + 5

    var x = 1 + 3 + 5
    # Each ASTVar has a nchanged int that increments upon each change found while walking the scope. While hashing idents that resolve to a var, include this number in the hashing. So if there are common subexpressions containing a var that has changed between them, the hashes will be different and they will not be considered "common" in the sense of being fit for extraction and caching to a new var.
    # e.g. if d[] would change between the many usages of sum(d[...]).
    # Recall that changes are implied by =, +=, -=, etc. as well as passing a var into a func that modifies arg1.
    # Really ambitious would be to detect selective changes to members of aggregate types or elements of collections. For this you could add a List(ASTVar*) to each ASTVar, which is a copy of var->type->body->locals. Then you can attribute and analyse the individual type members for a given var of aggregate type. For arrays, if the size can be known, create an array of ASTVar*s holding analysis info for each individual element.
    var d[] = [1, 2, 3, 8]
    var sd[] = [1, 2, 3, 4]
    var z = 3.0 + 5
    var b = sum(d[2:300000])
    var l = m + x + z + sum(d[2:300000])
    var cgg = 6 + sum(d[2:300000])
    describe(m)

    # check 4 < out <= 6
end function
