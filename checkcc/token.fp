
let TokenKindTable[256] = {
    tkNullChar, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkNewline, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown,
    tkSpaces, tkExclamation,
    tkStringBoundary, tkHash, tkDollar,
    tkOpMod, tkAmpersand, tkRegexBoundary,
    tkParenOpen, tkParenClose, tkTimes,
    tkPlus,  tkOpComma, tkMinus,
    tkPeriod, tkSlash, tkDigit,
    tkDigit, tkDigit, tkDigit,
    tkDigit, tkDigit, tkDigit,
    tkDigit, tkDigit, tkDigit,
    tkOpColon, tkOpSemiColon, tkOpLT,
    tkOpAssign, tkOpGT, tkQuestion,
    tkAt,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet,
    tkArrayOpen, tkBackslash, tkArrayClose,
    tkPower, tkUnderscore,
    tkInlineBoundary,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet, tkAlphabet,
    tkAlphabet, tkAlphabet,
    tkBraceOpen, tkPipe, tkBraceClose,
    tkTilde,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown,
    tkUnknown, tkUnknown, tkUnknown
end


#define Token_matchesKeyword(tok)                                              \
    # if (sizeof(#tok) - 1 == l and not strncmp(#tok, s, l)) return true

static bool doesKeywordMatch(const char* s, const int l)
{

    Token_matchesKeyword(and)
    Token_matchesKeyword(cheater)
    Token_matchesKeyword(for)
    Token_matchesKeyword(do)
    Token_matchesKeyword(while)
    Token_matchesKeyword(if)
    Token_matchesKeyword(then)
    Token_matchesKeyword(end)
    Token_matchesKeyword(function)
    Token_matchesKeyword(declare)
    Token_matchesKeyword(test)
    Token_matchesKeyword(not)
    Token_matchesKeyword(and)
    Token_matchesKeyword(or)
    Token_matchesKeyword(in)
    Token_matchesKeyword(else)
    Token_matchesKeyword(type)
    #    matchesen_compareKeyword(check)
    Token_matchesKeyword(extends)
    Token_matchesKeyword(var)
    Token_matchesKeyword(let)
    Token_matchesKeyword(import)
    Token_matchesKeyword(return)
    Token_matchesKeyword(result)
    Token_matchesKeyword(as)
    return false
end

type Token
    var pos = ""
    var matchlen = 0
    var skipWhiteSpace = no
    var mergeArrayDims = no
    var noKeywordDetect = no
    var strictSpacing = no
    var line = 0
    always line < 50000
    var col = 0
    always col < 250
    var kind = TokenKinds.none
end type


# Peek at the char after the current (complete) token
function peekCharAfter(token as Token) result (s as String)
    var i = token.matchlen
    if token.skipWhiteSpace
        do j = 1len(s)
            # we definitely need to allow auto converting bool to 0 or 1
            # if token.pos[i] == " " then i += 1
            i += (token.pos[j] == " ") # no branching
        end do
    end if

    # var j = [8, 7, 6, 5, 4, 3]

    # var j[] = [8, 7, 6, 5, 4, 3]

    # var j[] = 8, 7, 6, 5, 4, 3

    # var j[:,:] = [8, 7, 6; 5, 4, 3]


    s = token.pos[i]
end function

#define Token_compareKeyword(tok)                                              \
    # if (sizeof(#tok) - 1 == l and not strncmp(#tok, s, l)) {                   \
    #     token.kind = tkKeyword_##tok                                          \
    #     return                                                                \
    # end

# Check if an (ident) token.token matches a keyword and return its type
# accordingly.
static void Token_tryKeywordMatch(Token* token)
{
    # TODO USE A DICT OR MPH FOR THIS!
    if (token.kind != tkIdentifier) return

    const char* s = token.pos
    const int l = token.matchlen

    Token_compareKeyword(and)
    Token_compareKeyword(cheater)
    Token_compareKeyword(for)
    Token_compareKeyword(do)
    Token_compareKeyword(while)
    Token_compareKeyword(if)
    Token_compareKeyword(then)
    Token_compareKeyword(end)
    Token_compareKeyword(enum)
    Token_compareKeyword(function)
    Token_compareKeyword(declare)
    Token_compareKeyword(test)
    Token_compareKeyword(not)
    Token_compareKeyword(and)
    Token_compareKeyword(or)
    Token_compareKeyword(in)
    Token_compareKeyword(else)
    Token_compareKeyword(type)
    Token_compareKeyword(check)
    Token_compareKeyword(extends)
    Token_compareKeyword(var)
    Token_compareKeyword(let)
    Token_compareKeyword(import)
    Token_compareKeyword(return)
    Token_compareKeyword(result)
    Token_compareKeyword(as)
    # Token_compareKeyword(elif)

    #        Token_compareKeyword(print)
    #     if (sizeof("else if") - 1 == l and not strncmp("else if", s, l))
    # {
    #     token.kind = tkKeyword_elseif
    #     return
    # end
end

# Get the token kind based only on the char at the current position
# (or an offset).
function getType(token as Token, offs as Int) result (ret as TokenKind)
    const char c = token.pos[offset]
    const char cn = c ? token.pos[1 + offset]  0
    ret = (TokenKind)TokenKindTable[c]
    match token.pos
    let map = {
        `^<=` = TokenKind.opLE
        `^==` = .opEQ
    }
    do i, k, v = map
        if match(token.pos, regex = k) then ret = v
    end do

    case "<"
        match cn
        case "="
            return tkOpLE
        case else
            return tkOpLT
        end match
    case ">"
        match cn
        case "="
            return tkOpGE
        case else
            return tkOpGT
        end match
    case "="
        match cn
        case "="
            return tkOpEQ
        case ">"
            return tkOpResults
        case else
            return tkOpAssign
        end
    case "+"
        match cn
        case "="
            return tkPlusEq
        end
        return tkPlus
    case "-"
        match cn
        case "="
            return tkMinusEq
        end
        return tkMinus
    case "*"
        match cn
        case "="
            return tkTimesEq
        end
        return tkTimes
    case "/"
        match cn
        case "="
            return tkSlashEq
        end
        return tkSlash
    case "^"
        match cn
        case "="
            return tkPowerEq
        end
        return tkPower
    case "%"
        match cn
        case "="
            return tkOpModEq
        end
        return tkOpMod
    case "!"
        match cn
        case "="
            return tkOpNE
        end
        return tkExclamation
    case ""
        match cn
        case "="
            return tkColEq
        case else
            return tkOpColon
        end
    case else
        return ret
    end
end

static void Token_detect(Token* token)
{
    TokenKind tt = Token_getType(token, 0)
    TokenKind tt_ret = tkUnknown # = tt
    static TokenKind tt_last
        = tkUnknown # the previous token.token that was found
    static TokenKind tt_lastNonSpace
        = tkUnknown # the last non-space token.token found
    TokenKind tmp
    char* start = token.pos
    bool found_e = false, found_dot = false, found_cmt = false
    uint8_t found_spc = 0

    match tt
    case tkStringBoundary
    case tkInlineBoundary
    case tkRegexBoundary
        tmp = tt # remember which it is exactly

        # Incrementing pos is a side effect of getTypeAtCurrentPos(...)
        while (tt != tkNullChar) {
            # here we want to consume the ending " so we move next
            # before
            token.pos++
            tt = Token_getType(token, 0)
            if (tt == tkNullChar or tt == tmp) {
                *token.pos = 0
                token.pos++
                break
            end
            if (tt == tkBackslash)
                if (Token_getType(token, 1) == tmp) { # why if?
                    token.pos++
                end
        end
        match tmp
        case tkStringBoundary
            tt_ret = tkString
            break
        case tkInlineBoundary
            tt_ret = tkInline
            break
        case tkRegexBoundary
            tt_ret = tkRegex
            break
        case else
            tt_ret = tkUnknown
            printf("unreachable %s%d\n", __FILE__, __LINE__)
        end
        break

    case tkSpaces
        if (tt_last == tkOneSpace) # if prev char was a space return
                                   # this as a run of spaces
            while (tt != tkNullChar) {
                # here we dont want to consume the end char, so break
                # before
                tt = Token_getType(token, 1)
                token.pos++
                if (tt != tkSpaces) break
            end
        else
            token.pos++
        # else its a single space
        tt_ret = tkSpaces
        break

    case tkOpComma
    case tkOpSemiColon
        #        line continuation tokens
        tt_ret = tt

        while (tt != tkNullChar) {
            tt = Token_getType(token, 1)
            token.pos++
            # line number should be incremented for line continuations
            if (tt == tkSpaces) {
                found_spc++
            end
            if (tt == tkExclamation) {
                found_cmt = true
            end
            if (tt == tkNewline) {
                token.line++
                token.col = -found_spc - 1 # account for extra spaces
                                            # after , and for nl itself
                found_spc = 0
            end
            if (found_cmt and tt != tkNewline) {
                found_spc++
                continue
            end
            if (tt != tkSpaces and tt != tkNewline) break
        end
        break

    case tkArrayOpen
        # mergearraydims should be set only when reading func args
        if (not token.mergeArrayDims) goto defaultToken

        while (tt != tkNullChar) {
            tt = Token_getType(token, 1)
            token.pos++
            if (tt != tkOpColon and tt != tkOpComma) break
        end
        tt = Token_getType(token, 0)
        if (tt != tkArrayClose) {
            eprintf("expected a "]", found a "%c". now what?\n", *token.pos)
        end
        token.pos++
        tt_ret = tkArrayDims
        break

    case tkAlphabet
        # case tkPeriod
    case tkUnderscore
        while (tt != tkNullChar) {
            tt = Token_getType(token, 1)
            token.pos++
            if (tt != tkAlphabet and tt != tkDigit and tt != tkUnderscore)
                # and tt != tkPeriod)
                break #/ validate in parser not here
        end
        tt_ret = tkIdentifier
        break

    case tkHash # tkExclamation
        while (tt != tkNullChar) {
            tt = Token_getType(token, 1)
            token.pos++
            if (tt == tkNewline) break
        end
        tt_ret = tkLineComment
        break

    case tkPipe
        while (tt != tkNullChar) {
            tt = Token_getType(token, 1)
            token.pos++
            if (tt != tkAlphabet and tt != tkDigit and tt != tkSlash
                and tt != tkPeriod)
                break
        end
        tt_ret = tkUnits
        break

    case tkDigit
        tt_ret = tkNumber

        while (tt != tkNullChar) # EOF, basically null char
        {
            tt = Token_getType(token, 1)
            # numbers such as 1234500.00 are allowed
            # very crude, error-checking is parser"s job
            token.pos++

            if (*token.pos == "e" or *token.pos == "E" or *token.pos == "d"
                or *token.pos == "D") { # will all be changed to e btw
                found_e = true
                continue
            end
            if (found_e) {
                found_e = false
                continue
            end
            if (tt == tkPeriod) {
                found_dot = true
                continue
            end
            if (found_dot and tt == tkPeriod) tt_ret = tkMultiDotNumber

            if (tt != tkDigit and tt != tkPeriod and *token.pos != "i") break
        end
        break

    case tkMinus

        match tt_lastNonSpace
        case tkParenClose
        case tkIdentifier # TODO keywords too?
        case tkNumber
        case tkArrayClose
        case tkArrayDims
        case tkMultiDotNumber
            tt_ret = tt
            break
        case else
            tt_ret = tkUnaryMinus
            break
        end
        token.pos++
        break

    case tkOpNotResults
        # 3-char tokens
        token.pos++
    case tkOpEQ
    case tkOpGE
    case tkOpLE
    case tkOpNE
    case tkOpResults
    case tkBackslash
    case tkColEq
    case tkPlusEq
    case tkMinusEq
    case tkTimesEq
    case tkSlashEq
    case tkPowerEq
    case tkOpModEq

        # 2-char tokens
        token.pos++
    case else
    defaultToken
        tt_ret = tt
        token.pos++
        break
    end

    token.matchlen = (uint32_t)(token.pos - start)
    token.pos = start
    token.kind = tt_ret

    if (token.kind == tkIdentifier) Token_tryKeywordMatch(token)

    if (token.kind == tkSpaces and token.matchlen == 1) token.kind = tkOneSpace

    tt_last = token.kind
    if (tt_last != tkOneSpace and tt_last != tkSpaces)
        tt_lastNonSpace = tt_last
end

# Advance to the next token.token (skip whitespace if `skipws` is set).
static void Token_advance(Token* token)
{
    match token>ind) {
    case tkIdentifier
    case tkString
    case tkNumber
    case tkMultiDotNumber
    case tkFunctionCall
    case tkSubscript
    case tkDigit
    case tkAlphabet
    case tkRegex
    case tkInline
    case tkUnits
    case tkKeyword_cheater
    case tkKeyword_for
    case tkKeyword_while
    case tkKeyword_if
    case tkKeyword_end
    case tkKeyword_function
    case tkKeyword_test
    case tkKeyword_not
    case tkKeyword_and
    case tkKeyword_or
    case tkKeyword_in
    case tkKeyword_do
    case tkKeyword_then
    case tkKeyword_as
    case tkKeyword_else
    case tkKeyword_type
    case tkKeyword_return
    case tkKeyword_extends
    case tkKeyword_var
    case tkKeyword_let
    case tkKeyword_import
    case tkUnknown # bcz start of the file is this
        break
    case else
        *token.pos = 0 # trample it so that idents etc. can be assigned
                        # in-situ
    end

    token.pos += token.matchlen
    token.col += token.matchlen
    token.matchlen = 0
    Token_detect(token)

    if (token.kind == tkNewline) {
        # WHY don"t you do token.token advance here?
        token.line++
        token.col = 0 # position of the nl itself is 0
    end
    if (token.skipWhiteSpace
        and (token.kind == tkSpaces
            or (token.strictSpacing and token.kind == tkOneSpace)))
        Token_advance(token)
end
#