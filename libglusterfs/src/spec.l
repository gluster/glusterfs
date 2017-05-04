/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


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
{END}[-]{VOLUME}       return SECTION_END;
{SUB}{VOLUME}[Ss]      return SUBSECTION;
{OPTION}               return OPTION;
[a-zA-Z0-9_\./\*,=\-\;\:]+   { yylval = (int)strdup (yytext) ; return ID; }
[ \t\n]+               ;
%%
