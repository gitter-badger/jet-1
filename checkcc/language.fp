-- Comments start with two hyphens.

-- Values
"the quick brown fox", 'what happens in vegas' -- string
`\.?[a-z][a-zA-Z0-9]*` -- regular expression w/ compile-time error checking
6.6032e-27, 1, 0, 42 -- (real) number
3.5+41.2i, 1i -- (complex) number
350x200 -- size (for 2D use)
yes, no -- logical. true/false are read but linted to yes/no.

-- Builtin types
... as String
... as Regex
... as Real
... as Complex
... as Integer
... as Size
... as Logical

-- Variables
-- only one way: with keyword 'var'. names cannot start with caps.
var x = '5'
var x as String
var pi as Real
var pi = 3.14159
var magFieldStrength as Complex
var magFieldStrength = 0i

-- Collections
[3, 4, 5] -- arrays are homogenous
[3, 4, 5i] -- arrays can be promoted, but linter will make it [3i, 4i, 5i]
[1, 2, 3; 4, 5, 6; 7, 8, 9] -- 2D array literals use ';' to separate dims
{'name' = 35, 'size' = 33} -- maps are homogenous (in both keys and values)
var mx[] as Real = [3, 4, 5] -- write var mx = ... and linter will annotate
var mx[] as Real -- implies = []. arrays are never nil nor nilable, only empty
var mp[String] as Real = {'w' = 4} -- again just write var mp = {...} and annotation comes from linter
var mp[String] as Real -- implies = {}. maps are never nil nor nilable.

-- Functions
-- func names cannot start with caps.
-- second and higher arguments must be named at call sites (linter can do it)
function myfunc(parm as String, parm2 as Integer) result (ans as String)
    ans = 'empty'
end function -- basic function definition

myfunc(parm as String, parm2 as Integer) := 'empty' -- statement function

var x = myfunc('test', parm2=45) -- function call

---- funcs cannot mutate params, except first param if declared with 'var'
function pop(!array[] as Real) result (ans as Real)
    var tmp = array[-1]
    array.count -= 1
    ans = tmp
end function

var array = [3, 4, 5, 6]
var last = pop(!array) -- callsite must be !-annotated (linter can do it)

-- Types
-- type names start with caps.

type MyType
    var m = 0
    var mj = 42 -- just write var mj = 42 and linter will fix
    var name = ''
end type
---- default constructor is defined in the type body. CANNOT REDEFINE IT LATER.
---- default construction must always be possible and defined.

-- Objects

var m = MyType() -- new instance
var m as MyType -- implies new ('dummy') instance, not nil by default
---- this kind of apparently 'unintialized' var will be linted to the above line
---- so nil objects of any type cannot be 'created' at all.
m.m = 3 -- will not crash! m has an instance
m = MyType() -- will overwrite dummy instance

---- can define custom constructors, just like a function
function MyType(mi as Real, gi as Real) result (ans as MyType)
    ans = MyType()
    ans.m = mi
    ans.mj = gi
end function
var x = MyType(mi=3, gi=5)

---- objects are never nil (but may be nilable).
---- variables cannot be assigned nil, only functions can return nil.
---- nil can be passed to function arguments if it is provably safe
---- (by looking at what the function does with the argument).

-- Expressions
var pi = 22/7 -- literals are always reals, so 22/7 -> 3.14159...

-- Physical units
-- can be attached to numeric literals, vars and exprs (real, complex, or int)
var speed = 40|km/hr
var dens = num(input())|kg/m3
var test = speed + dens -- compile time error, units inconsistent for '*' op
var test2 = speed * dens -- result is |km.kg/hr.m3, whatever that is
var dspeed = 25|m/s
var test3 = speed + dspeed -- automatic unit conversion to that of the first
var test4|m/s = speed + dspeed -- explicit unit conversion to that of the second
var test5|ft/min = speed + dspeed -- explicit unit conversion to something else

---- automatic conversion where possible when passing to funcs etc.
function flux(!rate[:,:]|kg/s as Complex, speed|m/s as Integer) result (ans|kg.m3/s as Real)
end function

-- Syntax specials
var a = f(x,
          some=y,
          other=z) -- ',' can skip over succeeding whitespace and newlines

-- Configuration