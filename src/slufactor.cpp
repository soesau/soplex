/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*    Copyright (C) 1997-1999 Roland Wunderling                              */
/*                  1997-2002 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SoPlex is distributed under the terms of the ZIB Academic Licence.       */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SoPlex; see the file COPYING. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: slufactor.cpp,v 1.31 2002/12/08 11:09:21 bzfkocht Exp $"

/**@file slufactor.cpp
 * @todo SLUfactor seems to be partly an wrapper for CLUFactor (was C). 
 *       This should be properly integrated and demangled.
 * @todo Does is make sense, to call x.clear() when next x.altValues()
 *       is called.
 *       
 */
//#define DEBUGGING 1

#include <assert.h>

#include "spxdefines.h"
#include "slufactor.h"
#include "cring.h"
#include "spxalloc.h"

#ifdef DEBUGGING
#include <stdio.h>
#endif

namespace soplex
{
#define MINSTABILITY    1e-2
 
void SLUFactor::solveRight(Vector& x, const Vector& b) //const
{
   METHOD( "SLUFactor::solveRight()" );

   solveTime.start();

   vec = b;
   CLUFactor::solveRight(x.get_ptr(), vec.get_ptr());

   solveCount++;
   solveTime.stop();
}

void SLUFactor::solveRight(SSVector& x, const SVector& b) //const
{
   METHOD( "SLUFactor::solveRight()" );

   solveTime.start();

   vec.assign(b);
   x.clear();
   CLUFactor::solveRight(x.altValues(), vec.get_ptr());

   solveCount++;
   solveTime.stop();
}

void SLUFactor::solveRight4update(SSVector& x, const SVector& b)
{
   METHOD( "SLUFactor::solveRight4update()" );

   solveTime.start();

   int m;
   int n;
   int f;

   x.clear();
   ssvec = b;
   n = b.size();
   if (l.updateType == ETA)
   {
      m = vSolveRight4update(x.epsilon, x.altValues(), x.altIndexMem(),
         ssvec.altValues(), ssvec.altIndexMem(), n, 0, 0, 0);
      x.setSize(m);
      x.forceSetup();
      eta.setup_and_assign(x);
   }
   else
   {
      forest.clear();
      m = vSolveRight4update(x.epsilon, x.altValues(), x.altIndexMem(),
         ssvec.altValues(), ssvec.altIndexMem(), n,
         forest.altValues(), &f, forest.altIndexMem());
      forest.setSize(f);
      forest.forceSetup();
      x.setSize(m);
      x.forceSetup();
   }
   usetup = true;

   solveCount++;
   solveTime.stop();
}

void SLUFactor::solve2right4update(
   SSVector&      x,
   Vector&        y,
   const SVector& b,
   SSVector&      rhs)
{
   METHOD( "SLUFactor::solve2right4update()" );

   solveTime.start();

   int  m;
   int  n;
   int  f;
   int* sidx = ssvec.altIndexMem();
   int  rsize = rhs.size();
   int* ridx = rhs.altIndexMem();

   x.clear();
   y.clear();
   usetup = true;
   ssvec = b;

   if (l.updateType == ETA)
   {
      n = b.size();
      m = vSolveRight4update2(x.epsilon, x.altValues(), x.altIndexMem(), 
         ssvec.get_ptr(), sidx, n, y.get_ptr(),
         rhs.epsilon, rhs.altValues(), ridx, rsize, 0, 0, 0);
      x.setSize(m);
      x.forceSetup();
      eta.setup_and_assign(x);
   }
   else
   {
      forest.clear();
      n = ssvec.size();
      m = vSolveRight4update2(x.epsilon, x.altValues(), x.altIndexMem(), 
         ssvec.get_ptr(), sidx, n, y.get_ptr(),
         rhs.epsilon, rhs.altValues(), ridx, rsize,
         forest.altValues(), &f, forest.altIndexMem());
      x.setSize(m);
      x.forceSetup();
      forest.setSize(f);
      forest.forceSetup();
   }
   solveCount++;
   solveTime.stop();
}

void SLUFactor::solveLeft(Vector& x, const Vector& b) //const
{
   METHOD( "SLUFactor::solveLeft()" );

   solveTime.start();

   vec = b;
   ///@todo Why is x.clear() here used and not with solveRight() ?
   x.clear();
   CLUFactor::solveLeft(x.get_ptr(), vec.get_ptr());

   solveCount++;
   solveTime.stop();
}

void SLUFactor::solveLeft(SSVector& x, const SVector& b) //const
{
   METHOD( "SLUFactor::solveLeft()" );

   solveTime.start();

   ssvec.assign(b);

   x.clear();
   int sz = ssvec.size(); // see .altValues()
   int n = vSolveLeft(x.epsilon, x.altValues(), x.altIndexMem(),
      ssvec.altValues(), ssvec.altIndexMem(), sz);

   if (n > 0)
   {
      x.setSize(n);
      x.forceSetup();
   }
   else
      x.unSetup();

   ssvec.setSize(0);
   ssvec.forceSetup();

   solveCount++;
   solveTime.stop();
}

void SLUFactor::solveLeft(
   SSVector&      x,
   Vector&        y,
   const SVector& rhs1,
   SSVector&      rhs2) //const
{
   METHOD( "SLUFactor::solveLeft()" );

   solveTime.start();

   int   n;
   Real* svec = ssvec.altValues();
   int*  sidx = ssvec.altIndexMem();
   int   rn   = rhs2.size();
   int*  ridx = rhs2.altIndexMem();

   x.clear();
   y.clear();
   ssvec.assign(rhs1);
   n = ssvec.size(); // see altValues();
   n = vSolveLeft2(x.epsilon, x.altValues(), x.altIndexMem(), svec, sidx, n,
      y.get_ptr(), rhs2.altValues(), ridx, rn);

   x.setSize(n);

   if (n > 0)
      x.forceSetup();
   else
      x.unSetup();

   rhs2.setSize(0);
   rhs2.forceSetup();
   ssvec.setSize(0);
   ssvec.forceSetup();

   solveCount++;
   solveTime.stop();
}


Real SLUFactor::stability() const
{
   METHOD( "SLUFactor::stability()" );
   if (status() != OK)
      return 0;
   if (maxabs < initMaxabs)
      return 1;
   return initMaxabs / maxabs;
}

void SLUFactor::changeEta(int idx, SSVector& et)
{
   METHOD( "SLUFactor::changeEta()" );

   int es = et.size(); // see altValues()
   update(idx, et.altValues(), et.altIndexMem(), es);
   et.setSize(0);
   et.forceSetup();
}

SLUFactor::Status SLUFactor::change(
   int             idx,
   const SVector&  subst,
   const SSVector* e)
{
   METHOD( "SLUFactor::Status()" );

   if (usetup)
   {
      if (l.updateType == FOREST_TOMLIN)              // FOREST_TOMLIN updates
      {
         int fsize = forest.size(); // see altValues()
         forestUpdate(idx, forest.altValues(), fsize, forest.altIndexMem());
         forest.setSize(0);
         forest.forceSetup();
      }
      else                               
      {
         assert(l.updateType == ETA);
         changeEta(idx, eta);
      }
   }
   else if (e != 0)                                   // ETA updates
   {
      // Only to test if this happens ?
      assert(l.updateType == ETA);
      l.updateType = ETA;
      updateNoClear(idx, e->values(), e->indexMem(), e->size());
   }
   else if (l.updateType == FOREST_TOMLIN)            // FOREST_TOMLIN updates
   {
      forest = subst;
      CLUFactor::solveLright(forest.altValues());
      forestUpdate(idx, forest.altValues(), 0, 0);
      forest.setSize(0);
      forest.forceSetup();
   }
   else                                               // ETA updates
   {
      assert(l.updateType == ETA);
      vec = subst;
      eta.clear();
      CLUFactor::solveRight(eta.altValues(), vec.get_ptr());
      changeEta(idx, eta);
   }
   usetup = false;

   DEBUG({ std::cout << "\tupdated\t\tstability = " << stability()
                     << std::endl; });
   
   return status();
}

void SLUFactor::clear()
{
   METHOD( "SLUFactor::clear()" );

   rowMemMult    = 5;          /* factor of minimum Memory * #of nonzeros */
   colMemMult    = 5;          /* factor of minimum Memory * #of nonzeros */
   lMemMult      = 1;          /* factor of minimum Memory * #of nonzeros */

   l.firstUpdate = 0;
   l.firstUnused = 0;
   thedim        = 1;

   epsilon       = Param::epsilonFactorization(); 
   usetup        = false;
   maxabs        = 1;
   initMaxabs    = 1;
   minThreshold  = 0.01;
   lastThreshold = minThreshold;
   minStability  = MINSTABILITY;
   stat          = UNLOADED;

   vec.clear();
   eta.clear();
   ssvec.clear();
   forest.clear();

   u.row.size    = 100;
   u.col.size    = 100;
   l.size        = 100;
   l.startSize   = 100;

   if (l.val)
   {
      spx_free(u.row.val);
      spx_free(u.row.idx);
      spx_free(u.col.idx);
      spx_free(l.val);
      spx_free(l.idx);
      spx_free(l.start);
      spx_free(l.row);
   }
   spx_alloc(u.row.val, u.row.size);
   spx_alloc(u.row.idx, u.row.size);
   spx_alloc(u.col.idx, u.col.size);

   spx_alloc(l.val,   l.size);
   spx_alloc(l.idx,   l.size);
   spx_alloc(l.start, l.startSize);
   spx_alloc(l.row,   l.startSize);
}

/** assignment used to implement operator=() and copy constructor.
 *  If this is initialised, freeAll() has to be called before.
 *  Class objects from SLUFactor are not copied here.
 */
void SLUFactor::assign(const SLUFactor& old)
{
   METHOD( "SLUFactor::operator=()" );

   // slufactor
   uptype        = old.uptype;
   minThreshold  = old.minThreshold;
   minStability  = old.minStability;
   epsilon       = old.epsilon;
   lastThreshold = old.lastThreshold;

   // clufactor
   stat          = old.stat;
   thedim        = old.thedim;
   nzCnt         = old.nzCnt;
   initMaxabs    = old.initMaxabs;
   maxabs        = old.maxabs;
   rowMemMult    = old.rowMemMult;
   colMemMult    = old.colMemMult;
   lMemMult      = old.lMemMult;

   spx_alloc(row.perm, thedim);
   spx_alloc(row.orig, thedim);
   spx_alloc(col.perm, thedim);
   spx_alloc(col.orig, thedim);
   spx_alloc(diag,     thedim);

   memcpy(row.perm, old.row.perm, thedim * sizeof(*row.perm));
   memcpy(row.orig, old.row.orig, thedim * sizeof(*row.orig));
   memcpy(col.perm, old.col.perm, thedim * sizeof(*col.perm));
   memcpy(col.orig, old.col.orig, thedim * sizeof(*col.orig));
   memcpy(diag,     old.diag,     thedim * sizeof(*diag));

   work = vec.get_ptr();

   /* setup U
    */
   u.row.size = old.u.row.size;
   u.row.used = old.u.row.used;

   spx_alloc(u.row.elem,  thedim);
   spx_alloc(u.row.val,   u.row.size);
   spx_alloc(u.row.idx,   u.row.size);
   spx_alloc(u.row.start, thedim + 1);
   spx_alloc(u.row.len,   thedim + 1);
   spx_alloc(u.row.max,   thedim + 1);

   memcpy(u.row.elem,  old.u.row.elem,  thedim       * sizeof(*u.row.elem));
   memcpy(u.row.val,   old.u.row.val,   u.row.size   * sizeof(*u.row.val));
   memcpy(u.row.idx,   old.u.row.idx,   u.row.size   * sizeof(*u.row.idx));
   memcpy(u.row.start, old.u.row.start, (thedim + 1) * sizeof(*u.row.start));
   memcpy(u.row.len,   old.u.row.len,   (thedim + 1) * sizeof(*u.row.len));
   memcpy(u.row.max,   old.u.row.max,   (thedim + 1) * sizeof(*u.row.max));

   // need to make row list ok ?
   if (thedim > 0 && stat == OK)
   { 
      u.row.list.idx = old.u.row.list.idx; // .idx neu

      const Dring* oring = &old.u.row.list;
      Dring*       ring  = &u.row.list;

      while(oring->next != &old.u.row.list)
      {
         ring->next       = &u.row.elem[oring->next->idx];
         ring->next->prev = ring;
         oring            = oring->next;
         ring             = ring->next;
      }
      ring->next       = &u.row.list;
      ring->next->prev = ring;
   }

   u.col.size = old.u.col.size;
   u.col.used = old.u.col.used;

   spx_alloc(u.col.elem,  thedim);
   spx_alloc(u.col.idx,   u.col.size);
   spx_alloc(u.col.start, thedim + 1);
   spx_alloc(u.col.len,   thedim + 1);
   spx_alloc(u.col.max,   thedim + 1);

   if (old.u.col.val)
   {
      spx_alloc(u.col.val, u.col.size);
      memcpy(u.col.val, old.u.col.val, u.col.size * sizeof(*u.col.val));
   }
   else
      u.col.val = 0;

   memcpy(u.col.elem,  old.u.col.elem,  thedim       * sizeof(*u.col.elem));
   memcpy(u.col.idx,   old.u.col.idx,   u.col.size   * sizeof(*u.col.idx));
   memcpy(u.col.start, old.u.col.start, (thedim + 1) * sizeof(*u.col.start));
   memcpy(u.col.len,   old.u.col.len,   (thedim + 1) * sizeof(*u.col.len));
   memcpy(u.col.max,   old.u.col.max,   (thedim + 1) * sizeof(*u.col.max));

   // need to make col list ok
   if (thedim > 0 && stat == OK)
   {           
      u.col.list.idx = old.u.col.list.idx; // .idx neu

      const Dring* oring = &old.u.col.list;
      Dring*       ring  = &u.col.list;

      while(oring->next != &old.u.col.list)
      {
         ring->next       = &u.col.elem[oring->next->idx];
         ring->next->prev = ring;
         oring            = oring->next;
         ring             = ring->next;
      }
      ring->next       = &u.col.list;
      ring->next->prev = ring;
   }

   /* Setup L 
    */
   l.size        = old.l.size;
   l.startSize   = old.l.startSize;
   l.firstUpdate = old.l.firstUpdate;
   l.firstUnused = old.l.firstUnused;
   l.updateType  = old.l.updateType;

   spx_alloc(l.val,   l.size);
   spx_alloc(l.idx,   l.size);
   spx_alloc(l.start, l.startSize);
   spx_alloc(l.row,   l.startSize);

   memcpy(l.val,   old.l.val,   l.size      * sizeof(*l.val));
   memcpy(l.idx,   old.l.idx,   l.size      * sizeof(*l.idx));
   memcpy(l.start, old.l.start, l.startSize * sizeof(*l.start));
   memcpy(l.row,   old.l.row,   l.startSize * sizeof(*l.row));

   if (old.l.rval != 0)
   {
      assert(old.l.ridx != 0);
      assert(old.l.rbeg != 0);

      spx_alloc(l.rval,  l.firstUpdate);
      spx_alloc(l.ridx,  l.firstUpdate);
      spx_alloc(l.rbeg,  thedim + 1);
      spx_alloc(l.rorig, thedim);
      spx_alloc(l.rperm, thedim);

      memcpy(l.rval,  old.l.rval,  l.firstUpdate * sizeof(*l.rval));
      memcpy(l.ridx,  old.l.ridx,  l.firstUpdate * sizeof(*l.ridx));
      memcpy(l.rbeg,  old.l.rbeg, (thedim + 1)   * sizeof(*l.rbeg));
      memcpy(l.rorig, old.l.rorig, thedim        * sizeof(*l.rorig));
      memcpy(l.rperm, old.l.rperm, thedim        * sizeof(*l.rperm));
   }
   else
   {
      l.rval  = 0;
      l.ridx  = 0;
      l.rbeg  = 0;
      l.rorig = 0;
      l.rperm = 0;
   }

   assert(row.perm != 0);
   assert(row.orig != 0);
   assert(col.perm != 0);
   assert(col.orig != 0);
   assert(diag     != 0);

   assert(u.row.elem  != 0);
   assert(u.row.val   != 0);
   assert(u.row.idx   != 0);
   assert(u.row.start != 0);
   assert(u.row.len   != 0);
   assert(u.row.max   != 0);

   assert(u.col.elem  != 0);
   assert(u.col.idx   != 0);
   assert(u.col.start != 0);
   assert(u.col.len   != 0);
   assert(u.col.max   != 0);

   assert(l.val   != 0);
   assert(l.idx   != 0);
   assert(l.start != 0);
   assert(l.row   != 0);
}

SLUFactor& SLUFactor::operator=(const SLUFactor& old)
{
   METHOD( "SLUFactor::operator=()" );

   if (this != &old)
   {
      vec    = old.vec;
      ssvec  = old.ssvec;
      eta    = old.eta;
      forest = old.forest;

      freeAll();
      assign(old);
   }
   return *this;
}

SLUFactor::SLUFactor()
   : CLUFactor() 
   , vec (1)
   , ssvec (1)
   , usetup (false)
   , uptype (FOREST_TOMLIN)
   , eta (1)
   , forest (1)
{
   METHOD( "SLUFactor::SLUFactor()" );
#ifndef NDEBUG
   row.perm    = 0;
   row.orig    = 0;
   col.perm    = 0;
   col.orig    = 0;
   diag        = 0;
   u.row.elem  = 0;
   u.row.val   = 0;
   u.row.idx   = 0;
   u.row.start = 0;
   u.row.len   = 0;
   u.row.max   = 0;
   u.col.elem  = 0;
   u.col.idx   = 0;
   u.col.start = 0;
   u.col.len   = 0;
   u.col.max   = 0;
   l.val       = 0;
   l.idx       = 0;
   l.start     = 0;
   l.row       = 0;
   l.rval      = 0;
   l.ridx      = 0;
   l.rbeg      = 0;
   l.rorig     = 0;
   l.rperm     = 0;
#endif

   nzCnt  = 0;
   thedim = 1;

   spx_alloc(row.perm, thedim);
   spx_alloc(row.orig, thedim);
   spx_alloc(col.perm, thedim);
   spx_alloc(col.orig, thedim);

   spx_alloc(diag, thedim);

   work = vec.get_ptr();

   u.row.size = 1;
   u.row.used = 0;
   spx_alloc(u.row.elem,  thedim);
   spx_alloc(u.row.val,   u.row.size);
   spx_alloc(u.row.idx,   u.row.size);
   spx_alloc(u.row.start, thedim + 1);
   spx_alloc(u.row.len,   thedim + 1);
   spx_alloc(u.row.max,   thedim + 1);

   u.row.list.idx      = thedim;
   u.row.start[thedim] = 0;
   u.row.max[thedim]   = 0;
   u.row.len[thedim]   = 0;

   u.col.size = 1;
   u.col.used = 0;
   spx_alloc(u.col.elem,  thedim);
   spx_alloc(u.col.idx,   u.col.size);
   spx_alloc(u.col.start, thedim + 1);
   spx_alloc(u.col.len,   thedim + 1);
   spx_alloc(u.col.max,   thedim + 1);
   u.col.val = 0;

   u.col.list.idx      = thedim;
   u.col.start[thedim] = 0;
   u.col.max[thedim]   = 0;
   u.col.len[thedim]   = 0;

   l.size = 1;

   spx_alloc(l.val, l.size);
   spx_alloc(l.idx, l.size);

   l.startSize   = 1;
   l.firstUpdate = 0;
   l.firstUnused = 0;

   spx_alloc(l.start, l.startSize);
   spx_alloc(l.row,   l.startSize);

   l.rval  = 0;
   l.ridx  = 0;
   l.rbeg  = 0;
   l.rorig = 0;
   l.rperm = 0;

   SLUFactor::clear(); // clear() is virtual

   factorCount = 0;
   solveCount  = 0;

   assert(row.perm != 0);
   assert(row.orig != 0);
   assert(col.perm != 0);
   assert(col.orig != 0);
   assert(diag     != 0);

   assert(u.row.elem  != 0);
   assert(u.row.val   != 0);
   assert(u.row.idx   != 0);
   assert(u.row.start != 0);
   assert(u.row.len   != 0);
   assert(u.row.max   != 0);

   assert(u.col.elem  != 0);
   assert(u.col.idx   != 0);
   assert(u.col.start != 0);
   assert(u.col.len   != 0);
   assert(u.col.max   != 0);

   assert(l.val   != 0);
   assert(l.idx   != 0);
   assert(l.start != 0);
   assert(l.row   != 0);
}

SLUFactor::SLUFactor(const SLUFactor& old)
   : SLinSolver( old )
   , CLUFactor()
   , vec (old.vec)
   , ssvec (old.ssvec)
   , eta (old.eta)
   , forest(old.forest)
{
#ifndef NDEBUG
   row.perm    = 0;
   row.orig    = 0;
   col.perm    = 0;
   col.orig    = 0;
   diag        = 0;
   u.row.elem  = 0;
   u.row.val   = 0;
   u.row.idx   = 0;
   u.row.start = 0;
   u.row.len   = 0;
   u.row.max   = 0;
   u.col.elem  = 0;
   u.col.idx   = 0;
   u.col.start = 0;
   u.col.len   = 0;
   u.col.max   = 0;
   l.val       = 0;
   l.idx       = 0;
   l.start     = 0;
   l.row       = 0;
   l.rval      = 0;
   l.ridx      = 0;
   l.rbeg      = 0;
   l.rorig     = 0;
   l.rperm     = 0;
#endif

   assign(old);
}

void SLUFactor::freeAll()
{
   METHOD( "SLUFactor::freeAll()" );

   spx_free(row.perm);
   spx_free(row.orig);
   spx_free(col.perm);
   spx_free(col.orig);
   spx_free(u.row.elem);
   spx_free(u.row.val);
   spx_free(u.row.idx);
   spx_free(u.row.start);
   spx_free(u.row.len);
   spx_free(u.row.max);
   spx_free(u.col.elem);
   spx_free(u.col.idx);
   spx_free(u.col.start);
   spx_free(u.col.len);
   spx_free(u.col.max);
   spx_free(l.val);
   spx_free(l.idx);
   spx_free(l.start);
   spx_free(l.row);
   spx_free(diag);

   if (u.col.val)
      spx_free(u.col.val);

   if (l.rval)
   {
      spx_free(l.rval);
      spx_free(l.ridx);
      spx_free(l.rbeg);
      spx_free(l.rorig);
      spx_free(l.rperm);
   }
}

SLUFactor::~SLUFactor()
{
   METHOD( "SLUFactor::~SLUFactor()" );
   freeAll();
}

static Real betterThreshold(Real th)
{
   if (th < 0.1)
      th *= 10.0;
   else if (th < 0.9)
      th = (th + 1.0) / 2.0;
   else if (th < 0.999)
      th = 0.99999;
   return th;
}

SLUFactor::Status SLUFactor::load(const SVector* matrix[], int dm)
{
   METHOD( "SLUFactor::Status()" );
   assert(dm     > 0);
   assert(matrix != 0);

   Real lastStability = stability();

   initDR(u.row.list);
   initDR(u.col.list);

   usetup        = false;
   l.updateType  = uptype;
   l.firstUpdate = 0;
   l.firstUnused = 0;

   if (dm != thedim)
   {
      clear();

      thedim = dm;
      vec.reDim(thedim);
      ssvec.reDim(thedim);
      eta.reDim(thedim);
      forest.reDim(thedim);
      work = vec.get_ptr();

      spx_realloc(row.perm, thedim);
      spx_realloc(row.orig, thedim);
      spx_realloc(col.perm, thedim);
      spx_realloc(col.orig, thedim);
      spx_realloc(diag,     thedim);

      spx_realloc(u.row.elem,  thedim);
      spx_realloc(u.row.len,   thedim + 1);
      spx_realloc(u.row.max,   thedim + 1);
      spx_realloc(u.row.start, thedim + 1);

      spx_realloc(u.col.elem,  thedim);
      spx_realloc(u.col.len,   thedim + 1);
      spx_realloc(u.col.max,   thedim + 1);
      spx_realloc(u.col.start, thedim + 1);

      l.startSize = thedim + MAXUPDATES;

      spx_realloc(l.row,   l.startSize);
      spx_realloc(l.start, l.startSize);
   }
   else if (lastStability > 2.0 * minStability)
   {
      Real last   = minThreshold;
      Real better = betterThreshold(last);

      while (better < lastThreshold)
      {
         last   = better;
         better = betterThreshold(last);
      }
      minStability  = 2 * MINSTABILITY;
      lastThreshold = last;
   }

   u.row.list.idx      = thedim;
   u.row.start[thedim] = 0;
   u.row.max[thedim]   = 0;
   u.row.len[thedim]   = 0;

   u.col.list.idx      = thedim;
   u.col.start[thedim] = 0;
   u.col.max[thedim]   = 0;
   u.col.len[thedim]   = 0;

   for (;;)
   {
      stat = OK;

      factor(matrix, lastThreshold, epsilon);

      if (stability() >= minStability)
         break;

      Real x = lastThreshold;

      lastThreshold = betterThreshold(lastThreshold);

      if (x == lastThreshold)
         break;

      minStability /= 2.0;
   }

   DEBUG({ std::cout << "threshold = " << lastThreshold
                     << "\tstability = " << stability()
                     << "\tminStability = " << minStability << std::endl; });
   DEBUG({
      int i;
      FILE* fl = fopen("dump.lp", "w");
      std::cout << "Basis:\n";
      int j = 0;
      for (i = 0; i < dim(); ++i)
         j += matrix[i]->size();
      for (i = 0; i < dim(); ++i)
      {
         for (j = 0; j < matrix[i]->size(); ++j)
            fprintf(fl, "%8d  %8d  %16g\n",
                    i + 1, matrix[i]->index(j) + 1, matrix[i]->value(j));
      }
      fclose(fl);
      std::cout << "LU-Factors:" << std::endl;
      dump();
      
      std::cout << "threshold = " << lastThreshold 
                << "\tstability = " << stability() << std::endl;
   });

   assert(isConsistent());
   return Status(stat);
}


bool SLUFactor::isConsistent() const
{
   METHOD( "SLUFactor::isConsistent()" );
   return CLUFactor::isConsistent();
}

void SLUFactor::dump() const
{
   METHOD( "SLUFactor::dump()" );
   CLUFactor::dump();
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
