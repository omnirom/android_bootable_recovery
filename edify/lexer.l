%{
/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include "expr.h"
#include "parser.h"

int gLine = 1;
int gColumn = 1;

// TODO: enforce MAX_STRING_LEN during lexing
char string_buffer[MAX_STRING_LEN];
char* string_pos;
%}

%x STR

%option noyywrap

%%


\" {
    ++gColumn;
    BEGIN(STR);
    string_pos = string_buffer;
}

<STR>{
  \" {
      ++gColumn;
      BEGIN(INITIAL);
      *string_pos = '\0';
      yylval.str = strdup(string_buffer);
      return STRING;
  }

  \\n   { gColumn += yyleng; *string_pos++ = '\n'; }
  \\t   { gColumn += yyleng; *string_pos++ = '\t'; }
  \\\"  { gColumn += yyleng; *string_pos++ = '\"'; }
  \\\\  { gColumn += yyleng; *string_pos++ = '\\'; }

  \\x[0-9a-fA-F]{2} {
      gColumn += yyleng;
      int val;
      sscanf(yytext+2, "%x", &val);
      *string_pos++ = val;
  }

  \n {
      ++gLine;
      gColumn = 1;
      *string_pos++ = yytext[0];
  }

  . {
      ++gColumn;
      *string_pos++ = yytext[0];
  }
}

if                { gColumn += yyleng; return IF; }
then              { gColumn += yyleng; return THEN; }
else              { gColumn += yyleng; return ELSE; }
endif             { gColumn += yyleng; return ENDIF; }

[a-zA-Z0-9_:/.]+ {
  gColumn += yyleng;
  yylval.str = strdup(yytext);
  return STRING;
}

\&\&              { gColumn += yyleng; return AND; }
\|\|              { gColumn += yyleng; return OR; }
==                { gColumn += yyleng; return EQ; }
!=                { gColumn += yyleng; return NE; }

[+(),!;]          { gColumn += yyleng; return yytext[0]; }

[ \t]+            gColumn += yyleng;

(#.*)?\n          { ++gLine; gColumn = 1; }

.                 return BAD;
