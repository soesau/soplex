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
#pragma ident "@(#) $Id: spxredundantsm.cpp,v 1.1 2001/11/06 16:18:32 bzfkocht Exp $"

/*      \Section{Complex Methods}
 */

/*  Import system include files
 */
#include <stdlib.h>
#include <iostream>


/*  and class header files
 */
#include "spxredundantsm.h"


#include "dataarray.h"

namespace soplex
{

//@ ----------------------------------------------------------------------------
void SPxRedundantSM::load(SPxLP* l)
{
   lp = l;
   delta = 0;
}

void SPxRedundantSM::unload()
{
   lp = 0;
}

int SPxRedundantSM::simplify()
{
   int num, j, i, k;
   int upcnt, locnt;
   double up, lo, x, y;
   DataArray < int > rem(lp->nRows());
   double eps = 1e-10;

   num = 0;
   rem.reSize(lp->nCols());
   for (i = lp->nCols() - 1; i >= 0; --i)
   {
      const SVector& col = ((const SPxLP*)lp)->colVector(i);
      rem[i] = 0;
      if (lp->upper(i) != lp->lower(i))
      {
         up = 0;
         lo = 0;
         for (j = col.size() - 1; j >= 0 && (up == 0 || lo == 0); --j)
         {
            x = col.value(j);
            k = col.index(j);
            if (x > 0)
            {
               up += (lp->rhs(k) < lp->infinity);
               lo += (lp->lhs(k) > -lp->infinity);
            }
            else if (x < 0)
            {
               lo += (lp->rhs(k) < lp->infinity);
               up += (lp->lhs(k) > -lp->infinity);
            }
         }
         x = lp->maxObj(i);
         if (lo == 0 && x < 0)
         {
            if (lp->lower(i) <= -lp->infinity)
               return 1;           // LP is unbounded
            lp->changeUpper(i, lp->lower(i));
         }
         else if (up == 0 && x > 0)
         {
            if (lp->upper(i) >= lp->infinity)
               return 1;           // LP is unbounded
            lp->changeLower(i, lp->upper(i));
         }
         else if (x == 0)
         {
            up += (lp->upper(i) < lp->infinity);
            lo += (lp->lower(i) > -lp->infinity);
            if (lo == 0)
            {
               lp->changeUpper(i, lp->infinity);
               for (j = col.size() - 1; j >= 0; --j)
               {
                  x = col.value(j);
                  k = col.index(j);
                  if (x > 0)
                     lp->changeRhs(k, lp->infinity);
                  else
                     lp->changeLhs(k, -lp->infinity);
               }
            }
            if (up == 0)
            {
               lp->changeLower(i, -lp->infinity);
               for (j = col.size() - 1; j >= 0; --j)
               {
                  x = col.value(j);
                  k = col.index(j);
                  if (x < 0)
                     lp->changeRhs(k, lp->infinity);
                  else
                     lp->changeLhs(k, -lp->infinity);
               }
            }
         }
      }
      if ((x = lp->upper(i)) == lp->lower(i))
      {
         rem[i] = -1;
         num++;
         if (x != 0)
         {
            for (j = col.size() - 1; j >= 0; --j)
            {
               int k = col.index(j);
               if (lp->rhs(k) < lp->infinity)
                  lp->changeRhs(k, lp->rhs(k) - x*col.value(j));
               if (lp->lhs(k) > -lp->infinity)
                  lp->changeLhs(k, lp->lhs(k) - x*col.value(j));
            }
            delta += x * lp->obj(i);
         }
      }
   }
   if (num)
   {
      lp->removeCols(rem.get_ptr());
      std::cerr << "SPxRedundantSM: removed " << num << " column(s)\n";
      assert(lp->isConsistent());
   }

   num = 0;
   for (i = lp->nRows() - 1; i >= 0; --i)
   {
      if (lp->rhs(i) < lp->infinity || lp->lhs(i) > -lp->infinity)
      {
         const SVector& row = ((const SPxLP*)lp)->rowVector(i);

         rem[i] = 0;

         up = lo = 0;
         upcnt = locnt = 0;
         for (j = row.size() - 1; j >= 0; --j)
         {
            x = row.value(j);
            k = row.index(j);
            if (x > 0)
            {
               if (lp->upper(k) >= lp->infinity)
                  upcnt++;
               else
                  up += lp->upper(k) * x;
               if (lp->lower(k) <= -lp->infinity)
                  locnt++;
               else
                  lo += lp->lower(k) * x;
            }
            else if (x < 0)
            {
               if (lp->upper(k) >= lp->infinity)
                  locnt++;
               else
                  lo += lp->upper(k) * x;
               if (lp->lower(k) <= -lp->infinity)
                  upcnt++;
               else
                  up += lp->lower(k) * x;
            }
         }

         if (((lp->rhs(i) >= up - eps && upcnt <= 0)
               || lp->rhs(i) >= lp->infinity)
              && ((lp->lhs(i) <= lo + eps && locnt <= 0)
                  || lp->lhs(i) <= -lp->infinity))
         {
            rem[i] = -1;
            num++;
         }
         else if ((lp->rhs(i) < lo - eps && locnt <= 0)
                   || (lp->lhs(i) > up + eps && upcnt <= 0))
            return -1;
         else
         {
            /*
                if(lp->lhs(i) <= lo+eps && locnt <= 0)
                    lp->changeLhs(i, -lp->infinity);
                else if(lp->rhs(i) >= up-eps && upcnt <= 0)
                    lp->changeRhs(i, lp->infinity);
                else
             */
            if (upcnt < 2 || locnt < 2)
            {
               for (j = row.size() - 1; j >= 0; --j)
               {
                  x = row.value(j);
                  k = row.index(j);
                  if (x > 0)
                  {
                     if (lp->lhs(i) > -lp->infinity
                          && lp->lower(k) > -lp->infinity
                          && upcnt < 2)
                     {
                        y = -lp->infinity;
                        if (lp->upper(k) < lp->infinity && upcnt < 1)
                           y = lp->upper(k) + (lp->lhs(i) - up) / x;
                        else if (lp->upper(k) >= lp->infinity)
                           y = lp->lhs(i) - up;
                        if (y >= lp->lower(k))
                        {
                           lp->changeLower(k, -lp->infinity);
                           break;
                        }
                     }
                     if (lp->rhs(i) < lp->infinity
                          && lp->upper(k) < lp->infinity
                          && locnt < 2)
                     {
                        y = lp->infinity;
                        if (lp->lower(k) > -lp->infinity && locnt < 1)
                           y = lp->lower(k) + (lp->rhs(i) - lo) / x;
                        else if (lp->lower(k) <= -lp->infinity)
                           y = lp->rhs(i) - lo;
                        if (y <= lp->upper(k))
                        {
                           lp->changeUpper(k, lp->infinity);
                           break;
                        }
                     }
                  }
                  else if (x < 0)
                  {
                     if (lp->lhs(i) >= -lp->infinity
                          && lp->upper(k) < lp->infinity
                          && upcnt < 2)
                     {
                        y = lp->infinity;
                        if (lp->lower(k) > -lp->infinity && upcnt < 1)
                           y = lp->lower(k) + (lp->lhs(i) - up) / x;
                        else if (lp->lower(k) <= -lp->infinity)
                           y = -(lp->lhs(i) - up);
                        if (y <= lp->upper(k))
                        {
                           lp->changeUpper(k, lp->infinity);
                           break;
                        }
                     }
                     if (lp->rhs(i) <= lp->infinity
                          && lp->lower(k) > -lp->infinity
                          && locnt < 2)
                     {
                        y = -lp->infinity;
                        if (lp->upper(k) < lp->infinity && locnt < 1)
                           y = lp->upper(k) + (lp->rhs(i) - lo) / x;
                        else if (lp->upper(k) >= lp->infinity)
                           y = -(lp->rhs(i) - lo);
                        if (y >= lp->lower(k))
                        {
                           lp->changeLower(k, -lp->infinity);
                           break;
                        }
                     }
                  }
               }
            }
         }
      }
      else
         rem[i] = -1;
   }
   if (num)
   {
      lp->removeRows(rem.get_ptr());
      std::cerr << "SPxRedundantSM:\tremoved " << num << " row(s)\n";
      assert(lp->isConsistent());
   }

   return 0;
}

void SPxRedundantSM::unsimplify()
{
   std::cerr << "SPxRedundantSM::unsimplify() not implemented\n";
}
} // namespace soplex

//-----------------------------------------------------------------------------
//Emacs Local Variables:
//Emacs c-basic-offset:3
//Emacs tab-width:8
//Emacs indent-tabs-mode:nil
//Emacs End:
//-----------------------------------------------------------------------------
