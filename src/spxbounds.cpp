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
#pragma ident "@(#) $Id: spxbounds.cpp,v 1.1 2001/11/06 16:18:31 bzfkocht Exp $"


/*  Import system include files
 */
#include <assert.h>
#include <iostream>

/*  and class header files
 */
#include "soplex.h"

namespace soplex
{


//@ ----------------------------------------------------------------------------
/*      \SubSection{Bounds}
 
    Setting up the feasiblity bound for normal primal variables is
    straightforward. However, slack variables need some more details on how we
    treate them. This is slightly different from usual textbook versions. Let
    $l_i \le A_i^T x \le u_i$. This can be transformed to $A_i^Tx + s_i = 0$,
    with $-u_i \le s_i \le -l_i$. Hence, with this definition of slack variables
    $s_i$, we can directly use vectors $l$ and $u$ as feasibility bounds.
 */
void SoPlex::setPrimalBounds()
{
   theUCbound = SPxLP::upper();
   theLCbound = SPxLP::lower();
   if (rep() == ROW)
   {
      theURbound = rhs();
      theLRbound = lhs();
   }
   else
   {
      theURbound = lhs();
      theLRbound = rhs();
      theURbound *= -1;
      theLRbound *= -1;
   }
}

/*
    Seting up the basis for dual simplex requires to install upper and lower
    feasibility bounds for dual variables (|Lbound| and |Ubound|). Here is a
    list of how these must be set for inequalities of type $l \le a^Tx \le u$:
 
    \centerline{
        \begin{tabular}{cccc}
            $l$         &       $u$     & |Lbound|      & |Ubound|      \\
        \hline
        $-\infty=l$     & $u=\infty$    &       0       &       0       \\
        $-\infty<l$     & $u=\infty$    &       0       & $\infty$      \\
        $-\infty=l$     & $u<\infty$    & $-\infty$     &       0       \\
        \multicolumn{2}{c}{
        $-\infty<l \ne u<\infty$}       &       0       &       0       \\
        \multicolumn{2}{c}{
        $-\infty<l  =  u<\infty$}       & $-\infty$     & $\infty$      \\
        \end{tabular}
    }
 
    The case $l = -\infty$, $u = \infty$ occurs for unbounded primal variables.
    Such must be treated differently from the general case.
 
    Given possible upper and lower bounds to a dual variable with |Status stat|,
    this function clears the bounds according to |stat| by setting them to
    $\infty$ or $-\infty$, respectively.
 */
void SoPlex::clearDualBounds
(
   SPxBasis::Desc::Status stat,
   double& upp,
   double& lw
)
{
   switch (stat)
   {
   case SPxBasis::Desc::P_ON_UPPER + SPxBasis::Desc::P_ON_LOWER :
   case SPxBasis::Desc::D_FREE :
      upp = SPxLP::infinity;
      lw = -SPxLP::infinity;
      break;
   case SPxBasis::Desc::P_ON_UPPER :
   case SPxBasis::Desc::D_ON_LOWER :
      upp = SPxLP::infinity;
      break;
   case SPxBasis::Desc::P_ON_LOWER :
   case SPxBasis::Desc::D_ON_UPPER :
      lw = -SPxLP::infinity;
      break;

   default:
      break;
   }
}

void SoPlex::setDualColBounds()
{
   assert(rep() == COLUMN);
   int i;
   const SPxBasis::Desc& ds = desc();

   for (i = nRows() - 1; i >= 0; --i)
   {
      theURbound[i] = theLRbound[i] = 0;
      clearDualBounds(ds.rowStatus(i), theURbound[i], theLRbound[i]);
   }

   for (i = nCols() - 1; i >= 0; --i)
   {
      theUCbound[i] = theLCbound[i] = -maxObj(i);
      clearDualBounds(ds.colStatus(i),
                       theLCbound[i],               // exchanged ...
                       theUCbound[i]               // ... due to definition of slacks!
                    );
      theUCbound[i] *= -1;
      theLCbound[i] *= -1;
   }
}

void SoPlex::setDualRowBounds()
{
   assert(rep() == ROW);

   int i;

   for (i = nRows() - 1; i >= 0; --i)
   {
      theURbound[i] = theLRbound[i] = 0;
      clearDualBounds(dualRowStatus(i), theURbound[i], theLRbound[i]);
   }

   for (i = nCols() - 1; i >= 0; --i)
   {
      theUCbound[i] = theLCbound[i] = 0;
      clearDualBounds(dualColStatus(i), theUCbound[i], theLCbound[i]);
   }
}


//@ ----------------------------------------------------------------------------
/*
    This sets up the bounds for basic variables for entering simplex algorithms.
    It requires, that all upper lower feasibility bounds have allready been
    setup. Method |setEnterBound4Row(i, n)| does so for the |i|-th basis
    variable being row index |n|. Equivalently, method
    |setEnterBound4Col(i, n)| does so for the |i|-th basis variable being
    column index |n|.
 */
void SoPlex::setEnterBound4Row(int i, int n)
{
   assert(baseId(i).isSPxRowId());
   assert(number(SPxRowId(baseId(i))) == n);
   switch (desc().rowStatus(n))
   {
   case SPxBasis::Desc::P_ON_LOWER :
      theLBbound[i] = -SPxLP::infinity;
      theUBbound[i] = theURbound[n];
      break;
   case SPxBasis::Desc::P_ON_UPPER :
      theLBbound[i] = theLRbound[n];
      theUBbound[i] = SPxLP::infinity;
      break;

   default:
      theUBbound[i] = theURbound[n];
      theLBbound[i] = theLRbound[n];
      break;
   }
}

void SoPlex::setEnterBound4Col(int i, int n)
{
   assert(baseId(i).isSPxColId());
   assert(number(SPxColId(baseId(i))) == n);
   switch (desc().colStatus(n))
   {
   case SPxBasis::Desc::P_ON_LOWER :
      theLBbound[i] = -SPxLP::infinity;
      theUBbound[i] = theUCbound[n];
      break;
   case SPxBasis::Desc::P_ON_UPPER :
      theLBbound[i] = theLCbound[n];
      theUBbound[i] = SPxLP::infinity;
      break;

   default:
      theUBbound[i] = theUCbound[n];
      theLBbound[i] = theLCbound[n];
      break;
   }
}

void SoPlex::setEnterBounds()
{
   int i;

   for (i = dim() - 1; i >= 0; --i)
   {
      SPxLP::Id id = baseId(i);
      if (id.isSPxRowId())
         setEnterBound4Row(i, number(SPxRowId(id)));
      else
         setEnterBound4Col(i, number(SPxColId(id)));
   }
}


/*
    This sets up the bounds for basic variables for leaving simplex algorithms.
    While method |setLeaveBound4Row(i,n)| does so for the |i|-th basic variable
    being the |n|-th row, |setLeaveBound4Col(i,n)| does so for the |i|-th basic
    variable being the |n|-th column.
 */
void SoPlex::setLeaveBound4Row(int i, int n)
{
   assert(baseId(i).isSPxRowId());
   assert(number(SPxRowId(baseId(i))) == n);
   switch (desc().rowStatus(n))
   {
   case SPxBasis::Desc::P_ON_LOWER :
      theLBbound[i] = -SPxLP::infinity;
      theUBbound[i] = 0;
      break;
   case SPxBasis::Desc::P_ON_UPPER :
      theLBbound[i] = 0;
      theUBbound[i] = SPxLP::infinity;
      break;
   case SPxBasis::Desc::P_ON_UPPER + SPxBasis::Desc::P_ON_LOWER :
      theLBbound[i] = -SPxLP::infinity;
      theUBbound[i] = SPxLP::infinity;
      break;
   case SPxBasis::Desc::P_FREE :
      theLBbound[i] = theUBbound[i] = 0;
      break;

   default:
      assert(rep() == COLUMN);
      theLBbound[i] = -rhs(n);                // slacks !
      theUBbound[i] = -lhs(n);                // slacks !
      break;
   }
}

void SoPlex::setLeaveBound4Col(int i, int n)
{
   assert(baseId(i).isSPxColId());
   assert(number(SPxColId(baseId(i))) == n);
   switch (desc().colStatus(n))
   {
   case SPxBasis::Desc::P_ON_LOWER :
      theLBbound[i] = -SPxLP::infinity;
      theUBbound[i] = 0;
      break;
   case SPxBasis::Desc::P_ON_UPPER :
      theLBbound[i] = 0;
      theUBbound[i] = SPxLP::infinity;
      break;
   case SPxBasis::Desc::P_ON_UPPER + SPxBasis::Desc::P_ON_LOWER :
      theLBbound[i] = -SPxLP::infinity;
      theUBbound[i] = SPxLP::infinity;
      break;
   case SPxBasis::Desc::P_FREE :
      theLBbound[i] = theUBbound[i] = 0;
      break;

   default:
      theUBbound[i] = SPxLP::upper(n);
      theLBbound[i] = SPxLP::lower(n);
      break;
   }
}

void SoPlex::setLeaveBounds()
{
   int i;

   for (i = dim() - 1; i >= 0; --i)
   {
      SPxLP::Id id = baseId(i);
      if (id.isSPxRowId())
         setLeaveBound4Row(i, number(SPxRowId(id)));
      else
         setLeaveBound4Col(i, number(SPxColId(id)));
   }
}

void SoPlex::testBounds() const
{
   double max = (1 + iterCount) * delta();
   int i;

   if (type() == ENTER)
   {
      for (i = dim(); i-- > 0;)
      {
         if ((*theFvec)[i] > theUBbound[i] + max
              //@ &&  theUBbound[i] != theLBbound[i])
           )
            std::cerr << i << ": invalid upper enter bound found ...\n";
         if ((*theFvec)[i] < theLBbound[i] - max
              //@ &&  theUBbound[i] != theLBbound[i])
           )
            std::cerr << i << ": invalid lower enter bound found ...\n";
      }
   }
   else
   {
      for (i = dim(); i-- > 0;)
      {
         if ((*theCoPvec)[i] > (*theCoUbound)[i] + max
              //@ &&  (*theCoUbound)[i] != (*theCoLbound)[i])
           )
            std::cerr << i << "invalid upper cobound found ...\n";
         if ((*theCoPvec)[i] < (*theCoLbound)[i] - max
              //@ &&  (*theCoUbound)[i] != (*theCoLbound)[i])
           )
            std::cerr << i << "invalid lower cobound found ...\n";
      }
      for (i = coDim(); i-- > 0;)
      {
         if ((*thePvec)[i] > (*theUbound)[i] + max
              //@ &&  (*theUbound)[i] != (*theLbound)[i])
           )
            std::cerr << i << "invalid upper bound found ...\n";
         if ((*thePvec)[i] < (*theLbound)[i] - max
              //@ &&  (*theUbound)[i] != (*theLbound)[i])
           )
            std::cerr << i << "invalid lower bound found ...\n";
      }
   }
}
} // namespace soplex

//-----------------------------------------------------------------------------
//Emacs Local Variables:
//Emacs c-basic-offset:3
//Emacs tab-width:8
//Emacs indent-tabs-mode:nil
//Emacs End:
//-----------------------------------------------------------------------------
