
// TODO: ASTTypeSpecs will have a TypeTypes typeType; that can be used
// to determine quickly if it is a primitive. fits in 4bits btw
typedef enum TypeTypes {
    TYUnresolved = 0, // unknown type
                      // nonprimitives: means they should have their own
                      // methods to print,
                      // serialise, identify, reflect, etc.
    TYObject, // resolved to an ASTType
              // primitives that can be printed or represented with no fuss
    TYErrorType, // use this to poison an expr which has a type error
                 // and the error has been reported, to avoid
                 // reporting the same error for all parents, as the
                 // error-checking routine unwinds.
    TYSize, // this is actually uintptr_t, since actual ptrs are
    // TYObjects. maybe rename it
    TYString, // need to distinguish String and char*?
    TYBool,
    // above this, ie. > 4 or >= TYInt8, all may have units |kg.m/s etc.
    TYInt8,
    TYUInt8, //
    TYInt16,
    TYUInt16,
    TYInt32,
    TYUInt32,
    TYInt64, // for loop indices start out as size_t by default
    TYUInt64,
    //  TYReal16,
    TYReal32,
    TYReal64, // Numbers start out with Real64 by default
              // conplex, duals, intervals,etc.??
} TypeTypes;

static bool TypeType_isnum(TypeTypes tyty) { return tyty >= TYInt8; }

static const char* TypeType_name(TypeTypes tyty)
{
    switch (tyty) {
    case TYUnresolved:
        return NULL;
    case TYErrorType:
        return "<invalid>";
    case TYString:
        return "String";
    case TYBool:
        return "Logical";
    case TYObject:
        return "";
    case TYSize:
    case TYInt8:
    case TYUInt8:
    case TYInt16:
    case TYUInt16:
    case TYInt32:
    case TYUInt32:
    case TYInt64:
    case TYUInt64:
    case TYReal32:
    case TYReal64:
        return "Scalar";
    }
}

// these are DEFAULTS
static const char* TypeType_format(TypeTypes tyty, bool quoted)
{
    switch (tyty) {
    case TYUnresolved:
        return NULL;
    case TYErrorType:
        return NULL;
    case TYObject:
        return "%p";
    case TYSize: // this is actually uintptr_t, since actual ptrs are
                 // TYObjects. maybe rename it
        return "%lu";
    case TYString:
        return quoted ? "\\\"%s\\\"" : "%s";
    case TYBool:
        return "%d";
        // above this, ie. > 4 or >= TYInt8, all may have units |kg.m/s etc.
        // so you can test tyty >= TYInt8 to see if *units must be
        // processed.
    case TYInt8:
        return "%d";
    case TYUInt8:
        return "%u";
    case TYInt16:
        return "%d";
    case TYUInt16:
        return "%u";
    case TYInt32:
        return "%d";
    case TYUInt32:
        return "%u";
    case TYInt64: // for loop indices start out as size_t by default
        return "%d";
    case TYUInt64:
        return "%u";
    case TYReal32:
        return "%g";
    case TYReal64: // Numbers start out with Real64 by default
        return "%g";
    }
}

// needed to compute stack usage
static unsigned int TypeType_size(TypeTypes tyty)
{
    switch (tyty) {
    case TYUnresolved:
    case TYErrorType:
        return 0;
    case TYObject:
        return sizeof(void*);
    case TYSize:
        return sizeof(size_t);
    case TYString:
        return sizeof(char*); // what about length
    case TYBool:
        return sizeof(int);
    case TYInt8:
        return 1;
    case TYUInt8:
        return 1;
    case TYInt16:
        return 2;
    case TYUInt16:
        return 2;
    case TYInt32:
        return 4;
    case TYUInt32:
        return 4;
    case TYInt64:
        return 8;
    case TYUInt64:
        return 8;
    case TYReal32:
        return 4;
    case TYReal64:
        return 8;
    }
}

// If we get entirely rid of type annotation, the process for determining
// type as it is now will be very convoluted. First you pass strings around
// and compare them ("String", "Scalar", etc.) and then set TypeTypes
// according to that why not set the right TypeTypes directly in analysis?
// but this is needed as long as there is ANY type annotation somewhere.
// (e.g. func args)
static TypeTypes TypeType_byName(const char* spec)
{
    if (not spec) return TYUnresolved;
    if (not strcasecmp(spec, "Scalar"))
        return TYReal64; // this is default, analysis might change it to
                         // more specific
    if (not strcasecmp(spec, "String")) return TYString;
    if (not strcasecmp(spec, "Logical")) return TYBool;
    // note that Vector, Matrix, etc. are actually types, so they should
    // resolve to TYObject.
    return TYUnresolved;
}

// says what exactly a collection should generate to, since in check
// source code, collections' actual kind is abstracted away. 4 bits.
typedef enum CollectionTypes {
    CTYNone = 0, // scalar value
    CTYArray,
    CTYList,
    CTYDList,
    CTYDictS,
    CTYDictU,
    CTYOrderedDictS, // String keys
    CTYSortedDictS, // UInt/Int/Ptr keys
    CTYOrderedDictU,
    CTYSortedDictU,
    CTYSet,
    CTYOrderedSet,
    CTYSortedSet,
    CTYTensor, // will need to store dims. includes vector/matrix/tensor
    CTYDataFrame,
    CTYStackArray, // computed size from init. can get later using countof()
    CTYStackArray8, // these are NOT in BYTES, but sizeof(whatever), so
                    // careful with double/int arrays
    CTYStackArray16,
    CTYStackArray32,
    CTYStackArray64,
    CTYStackArray128,
    CTYStackArray256,
    CTYStackArray512,
    CTYStackArray1024,
    CTYStackArray2048,
    CTYStackArray4096, // really? you need any larger?
} CollectionTypes;

static const char* CollectionType_nativeName(CollectionTypes coty)
{
    switch (coty) {
    case CTYNone:
        return NULL;
    case CTYArray:
        return "_A";
    case CTYList:
        return "_l";
    case CTYDList:
        return "_L";
    case CTYDictS:
        return "_d";
    case CTYDictU:
        return "_D";
    case CTYOrderedDictS:
        return "_o";
    case CTYSortedDictS:
        return "_s";
    case CTYOrderedDictU:
        return "_O";
    case CTYSortedDictU:
        return "_S";
    case CTYSet:
        return "_Z";
    case CTYOrderedSet:
        return "_Y";
    case CTYSortedSet:
        return "_X";
    case CTYTensor:
        return "_T";
    case CTYDataFrame:
        return "_F";
    case CTYStackArray:
        return "[]";
    case CTYStackArray8:
        return "[8]";
    case CTYStackArray16:
        return "[16]";
    case CTYStackArray32:
        return "[32]";
    case CTYStackArray64:
        return "[64]";
    case CTYStackArray128:
        return "[128]";
    case CTYStackArray256:
        return "[256]";
    case CTYStackArray512:
        return "[512]";
    case CTYStackArray1024:
        return "[1024]";
    case CTYStackArray2048:
        return "[2048]";
    case CTYStackArray4096:
        return "[4096]";
    }
}