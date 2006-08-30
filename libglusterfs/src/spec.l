
%{

#include "xlator.h"
#include "y.tab.h"
#include <string.h>

%}

VOLUME [v][o][l][u][m][e]
END    [e][n][d]
SUB    [s][u][b]
OPTION [o][p][t][i][o][n]
TYPE   [t][y][p][e]
%%
\#.*                   ;
{VOLUME}               return SECTION_BEGIN;
{TYPE}                 return TYPE;
{END}[-]{VOLUME}          return SECTION_END;
{SUB}{VOLUME}[Ss]      return SUBSECTION;
{OPTION}               return OPTION;
[a-zA-Z0-9_\./\*,=\-\;\:]+   { yylval = (int)strdup (yytext) ; return ID; }
[ \t\n]+               ;
%%
