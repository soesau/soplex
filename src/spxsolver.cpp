/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*    Copyright (C) 1997-1999 Roland Wunderling                              */
/*                  1997-2001 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SoPlex is distributed under the terms of the ZIB Academic Licence.       */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SoPlex; see the file COPYING. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: spxsolver.cpp,v 1.3 2001/11/12 16:42:11 bzfpfend Exp $"

/*      \Section{Complex Methods}
 */

/*  Import system include files
 */
#include <stdlib.h>
#include <iostream>


/*  and class header files
 */
#include "spxsolver.h"

namespace soplex
{

//@ ----------------------------------------------------------------------------
/*  \Section{Miscellanous Methods}
 */
SPxSolver::SPxSolver (Type p_type, SoPlex::Representation p_rep)
   : SoPlex(p_type, p_rep)
{
   load(&rt);
   load(&pr);
   load(&st);
   load(&slu);
}
} // namespace soplex

//-----------------------------------------------------------------------------
//Emacs Local Variables:
//Emacs mode:c++
//Emacs c-basic-offset:3
//Emacs tab-width:8
//Emacs indent-tabs-mode:nil
//Emacs End:
//-----------------------------------------------------------------------------
