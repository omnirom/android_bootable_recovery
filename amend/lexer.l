/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

%{
    #include <stdio.h>
    #include <stdlib.h>
    #include "ast.h"
    #include "lexer.h"
    #include "parser.h"

    const char *tokenToString(int token)
    {
        static char scratch[128];

        switch (token) {
        case TOK_AND:
            return "&&";
        case TOK_OR:
            return "||";
        case TOK_EQ:
            return "==";
        case TOK_NE:
            return "!=";
        case TOK_GE:
            return ">=";
        case TOK_LE:
            return "<=";
        case TOK_EOF:
            return "EOF";
        case TOK_EOL:
            return "EOL\n";
        case TOK_STRING:
            snprintf(scratch, sizeof(scratch),
                    "STRING<%s>", yylval.literalString);
            return scratch;
        case TOK_IDENTIFIER:
            snprintf(scratch, sizeof(scratch), "IDENTIFIER<%s>",
                    yylval.literalString);
            return scratch;
        case TOK_WORD:
            snprintf(scratch, sizeof(scratch), "WORD<%s>",
                    yylval.literalString);
            return scratch;
        default:
            if (token > ' ' && token <= '~') {
                scratch[0] = (char)token;
                scratch[1] = '\0';
            } else {
                snprintf(scratch, sizeof(scratch), "??? <%d>", token);
            }
            return scratch;
        }
    }

    typedef struct {
        char *value;
        char *nextc;
        unsigned int alloc_size;
    } AmString;

    static int addCharToString(AmString *str, char c)
    {
        if ((unsigned int)(str->nextc - str->value) >= str->alloc_size) {
            char *new_value;
            unsigned int new_size;

            new_size = (str->alloc_size + 1) * 2;
            if (new_size < 64) {
                new_size = 64;
            }

            new_value = (char *)realloc(str->value, new_size);
            if (new_value == NULL) {
                yyerror("out of memory");
                return -1;
            }
            str->nextc = str->nextc - str->value + new_value;
            str->value = new_value;
            str->alloc_size = new_size;
        }
        *str->nextc++ = c;
        return 0;
    }

    static int setString(AmString *str, const char *p)
    {
        str->nextc = str->value;
        while (*p != '\0') {
//TODO: add the whole string at once
            addCharToString(str, *p++);
        }
        return addCharToString(str, '\0');
    }

    static AmString gStr = { NULL, NULL, 0 };
    static int gLineNumber = 1;
    static AmArgumentType gArgumentType = AM_UNKNOWN_ARGS;
    static const char *gErrorMessage = NULL;

#if AMEND_LEXER_BUFFER_INPUT
    static const char *gInputBuffer;
    static const char *gInputBufferNext;
    static const char *gInputBufferEnd;

# define YY_INPUT(buf, result, max_size) \
    do { \
        int nbytes = gInputBufferEnd - gInputBufferNext; \
        if (nbytes > 0) { \
            if (nbytes > max_size) { \
                nbytes = max_size; \
            } \
            memcpy(buf, gInputBufferNext, nbytes); \
            gInputBufferNext += nbytes; \
            result = nbytes; \
        } else { \
            result = YY_NULL; \
        } \
    } while (false)
#endif  // AMEND_LEXER_BUFFER_INPUT

%}

%option noyywrap

%x QUOTED_STRING BOOLEAN WORDS

ident [a-zA-Z_][a-zA-Z_0-9]*
word [^ \t\r\n"]+

%%
    /* This happens at the beginning of each call to yylex().
     */
    if (gArgumentType == AM_WORD_ARGS) {
        BEGIN(WORDS);
    } else if (gArgumentType == AM_BOOLEAN_ARGS) {
        BEGIN(BOOLEAN);
    }

        /*xxx require everything to be 7-bit-clean, printable characters */
<INITIAL>{
        {ident}/[ \t\r\n] {
                /* The only token we recognize in the initial
                 * state is an identifier followed by whitespace.
                 */
                setString(&gStr, yytext);
                yylval.literalString = gStr.value;
                return TOK_IDENTIFIER;
            }
    }

<BOOLEAN>{
        {ident} {
                /* Non-quoted identifier-style string */
                setString(&gStr, yytext);
                yylval.literalString = gStr.value;
                return TOK_IDENTIFIER;
            }
        "&&"    return TOK_AND;
        "||"    return TOK_OR;
        "=="    return TOK_EQ;
        "!="    return TOK_NE;
        ">="    return TOK_GE;
        "<="    return TOK_LE;
        [<>()!,] return yytext[0];
    }

    /* Double-quoted string handling */

<WORDS,BOOLEAN>\"  {
        /* Initial quote */
        gStr.nextc = gStr.value;
        BEGIN(QUOTED_STRING);
    }

<QUOTED_STRING>{
        \"  {
                /* Closing quote */
                BEGIN(INITIAL);
                addCharToString(&gStr, '\0');
                yylval.literalString = gStr.value;
                if (gArgumentType == AM_WORD_ARGS) {
                    return TOK_WORD;
                } else {
                    return TOK_STRING;
                }
            }

        <<EOF>> |
        \n  {
                /* Unterminated string */
                yyerror("unterminated string");
                return TOK_ERROR;
            }

        \\\" {
                /* Escaped quote */
                addCharToString(&gStr, '"');
            }

        \\\\ {
                /* Escaped backslash */
                addCharToString(&gStr, '\\');
            }

        \\. {
                /* No other escapes allowed. */
                gErrorMessage = "illegal escape";
                return TOK_ERROR;
            }

        [^\\\n\"]+ {
                /* String contents */
                char *p = yytext;
                while (*p != '\0') {
        /* TODO: add the whole string at once */
                    addCharToString(&gStr, *p++);
                }
            }
    }

<WORDS>{
        /*xxx look out for backslashes; escape backslashes and quotes */
        /*xxx if a quote is right against a char, we should append */
        {word} {
                /* Whitespace-separated word */
                setString(&gStr, yytext);
                yylval.literalString = gStr.value;
                return TOK_WORD;
            }
    }

<INITIAL,WORDS,BOOLEAN>{
        \n  {
                /* Count lines */
                gLineNumber++;
                gArgumentType = AM_UNKNOWN_ARGS;
                BEGIN(INITIAL);
                return TOK_EOL;
            }

        /*xxx backslashes to extend lines? */
            /* Skip whitespace and comments.
             */
        [ \t\r]+ ;
        #.*      ;

        .   {
                /* Fail on anything we didn't expect. */
                gErrorMessage = "unexpected character";
                return TOK_ERROR;
            }
    }
%%

void
yyerror(const char *msg)
{
    if (!strcmp(msg, "syntax error") && gErrorMessage != NULL) {
        msg = gErrorMessage;
        gErrorMessage = NULL;
    }
    fprintf(stderr, "line %d: %s at '%s'\n", gLineNumber, msg, yytext);
}

#if AMEND_LEXER_BUFFER_INPUT
void
setLexerInputBuffer(const char *buf, size_t buflen)
{
    gLineNumber = 1;
    gInputBuffer = buf;
    gInputBufferNext = gInputBuffer;
    gInputBufferEnd = gInputBuffer + buflen;
}
#endif  // AMEND_LEXER_BUFFER_INPUT

void
setLexerArgumentType(AmArgumentType type)
{
    gArgumentType = type;
}

int
getLexerLineNumber(void)
{
    return gLineNumber;
}
