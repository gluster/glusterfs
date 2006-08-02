
%{

#include "xlator.h"
#include "y.tab.h"
#include <string.h>

%}

VOLUME [Vv][Oo][Ll][Uu][Mm][Ee]
END    [Ee][Nn][Dd]
SUB    [Ss][Uu][Bb]
OPTION [Oo][Pp][Tt][Ii][Oo][Nn]
TYPE   [Tt][Yy][Pp][Ee]
%%
\#.*                 ;
{VOLUME}             return SECTION_BEGIN;
{TYPE}               return TYPE;
{END}{VOLUME}        return SECTION_END;
{SUB}{VOLUME}[Ss]    return SUBSECTION;
{OPTION}             return OPTION;
[a-zA-Z0-9_\./]+     { yylval = strdup (yytext) ; return ID; }
[ \t\n]+               ;
%%
