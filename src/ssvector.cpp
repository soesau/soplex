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
#pragma ident "@(#) $Id: ssvector.cpp,v 1.14 2002/01/19 18:59:18 bzfkocht Exp $"

#include <assert.h>

#include "real.h"
#include "ssvector.h"
#include "svset.h"
#include "message.h"

/**@file ssvector.cpp
 * @todo There is a lot pointer arithmetic done here. It is not clear if
 *       this is an advantage at all. See all the function int() casts.
 */
namespace soplex
{

static const Real shortProductFactor = 0.5;

void SSVector::setMax(int newmax)
{
   assert(idx    != 0);
   assert(newmax != 0);
   assert(newmax >= IdxSet::size());

   // len = (newmax < IdxSet::max()) ? IdxSet::max() : newmax;
   len = newmax;

   spx_realloc(idx, len);
}

void SSVector::reDim (int newdim)
{
   for (int i = IdxSet::size() - 1; i >= 0; --i)
      if (index(i) >= newdim)
         remove(i);
   DVector::reDim(newdim);
   setMax(DVector::memSize() + 1);
   assert(isConsistent());
}

void SSVector::reMem(int newsize)
{
   DVector::reSize(newsize);
   assert(isConsistent());
   setMax(DVector::memSize() + 1);
}

void SSVector::clear ()
{
   if (isSetup())
   {
      int i = IdxSet::size();
      int* iptr;
      for (iptr = idx; i--; iptr++)
         val[*iptr] = 0;
   }
   else
      Vector::clear();

   IdxSet::clear();
   setupStatus = true;
   assert(isConsistent());
}

void SSVector::setValue(int i, Real x)
{
   assert(i >= 0 && i < DVector::dim());

   if (isSetup())
   {
      int n = number(i);

      if (n < 0)
      {
         if (x > epsilon || x < -epsilon)
            IdxSet::add(1, &i);
      }
      else if (x == 0)
         clearNum(n);
   }
   val[i] = x;

   assert(isConsistent());
}

void SSVector::setup()
{
   if (!isSetup())
   {
      IdxSet::clear();

      // #define      TWO_LOOPS
#ifdef  TWO_LOOPS
      int i = 0;
      int n = 0;
      int* id = idx;
      Real* v = val;
      const Real* end = val + dim();

      while (v < end)
      {
         id[n] = i++;
         n += (*v++ != 0);
      }

      Real x;
      int* ii = idx;
      int* last = idx + n;
      v = val;

      for (; id < last; ++id)
      {
         x = v[*id];
         if (fabs(x) > eps)
            *ii++ = *id;
         else
            v[*id] = 0;
      }
      num = ii - idx;

#else

      if (dim() <= 1)
      {
         if (dim())
         {
            if (fabs(*val) > epsilon)
               IdxSet::add(0);
            else
               *val = 0;
         }
      }
      else
      {
         int* ii = idx;
         Real* v = val;
         Real* end = v + dim() - 1;

         /* setze weissen Elefanten */
         Real last = *end;
         *end = 1e-100;

         /* erstes element extra */
         if (fabs(*v) > epsilon)
            *ii++ = 0;
         else
            *v = 0;

         for(;;)
         {
            while (!*++v);
            if (fabs(*v) > epsilon)
            {
               *ii++ = int(v - val);
            }
            else
            {
               *v = 0;
               if (v == end)
                  break;
            }

         }

         /* fange weissen Elefanten wieder ein */
         if (fabs(last) > epsilon)
         {
            *v = last;
            *ii++ = dim() - 1;
         }
         else
            *v = 0;

         num = int(ii - idx);
      }

#endif

      setupStatus = true;
      assert(isConsistent());
   }
}

SSVector& SSVector::operator+=(const Vector& vec)
{
   Vector::operator+=(vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator+=(const SVector& vec)
{
   Vector::operator+=(vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator+=(const SubSVector& vec)
{
   Vector::operator+=(vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator+=(const SSVector& vec)
{
   for (int i = vec.size() - 1; i >= 0; --i)
      val[vec.index(i)] += vec.value(i);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator-=(const Vector& vec)
{
   Vector::operator-=(vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator-=(const SVector& vec)
{
   Vector::operator-=(vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator-=(const SubSVector& vec)
{
   Vector::operator-=(vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator-=(const SSVector& vec)
{
   if (vec.isSetup())
   {
      for (int i = vec.size() - 1; i >= 0; --i)
         val[vec.index(i)] -= vec.value(i);
   }
   else
   {
      Vector::operator-=(Vector(vec));
   }

   if (isSetup())
   {
      setupStatus = false;
      setup();
   }

   return *this;
}

SSVector& SSVector::operator*=(Real x)
{
   for (int i = size() - 1; i >= 0; --i)
      val[index(i)] *= x;
   assert(isConsistent());
   return *this;
}

Real SSVector::maxAbs() const
{
   if (isSetup())
   {
      int* i = idx;
      int* end = idx + num;
      Real* v = val;
      Real absval = 0.0;

      for (; i < end; ++i)
      {
         Real x = v[*i];
         if (fabs(x) > absval)
            absval = fabs(x);
      }
      return absval;
   }
   else
      return Vector::maxAbs();
}

Real SSVector::length2() const
{
   if (isSetup())
   {
      int* i = idx;
      int* end = idx + num;
      Real* v = val;
      Real x = 0;

      for (; i < end; ++i)
         x += v[*i] * v[*i];
      return x;
   }
   else
      return Vector::length2();
}

Real SSVector::length() const
{
   return sqrt(length2());
}


SSVector& SSVector::multAdd(Real xx, const SSVector& svec)
{
   if (svec.isSetup())
   {
      if (isSetup())
      {
         int i, j;
         Real x;
         const Real eps = epsilon;

         for (i = svec.size() - 1; i >= 0; --i)
         {
            j = svec.index(i);
            if (val[j])
            {
               x = val[j] + xx * svec.value(i);
               if (x > eps || x < -eps)
                  val[j] = x;
               else
               {
                  val[j] = 0;
                  for (--i; i >= 0; --i)
                     val[svec.index(i)] += xx * svec.value(i);
                  unSetup();
                  break;
               }
            }
            else
            {
               x = xx * svec.value(i);
               if (x > eps || x < -eps)
               {
                  val[j] = x;
                  addIdx(j);
               }
            }
         }
      }
      else
         Vector::multAdd(xx, svec);
   }
   else
   {
      Real y;
      int* ii = idx;
      Real* v = val;
      Real* rv = static_cast<Real*>(svec.val);
      Real* last = rv + svec.dim() - 1;
      Real x = *last;
      const Real eps = epsilon;
      const Real meps = -eps;

      *last = 1e-100;
      for(;;)
      {
         while (!*rv)
         {
            ++rv;
            ++v;
         }
         y = *rv++ * xx;
         if (y < meps || y > eps)
         {
            *ii++ = int(v - val);
            *v++ = y;
         }
         else if (rv == last)
            break;
         else
            v++;
      }
      *rv = x;

      x *= xx;
      if (x < meps || x > eps)
      {
         *ii++ = int(v - val);
         *v = x;
      }
      num = int(ii - idx);

      setupStatus = true;
   }

   assert(isConsistent());
   return *this;
}

SSVector& SSVector::multAdd(Real xx, const SVector& svec)
{
   if (isSetup())
   {
      int i, j;
      Real x;
      Real* v = val;
      const Real eps = epsilon;
      const Real meps = -eps;
      const Real mark = 1e-100;
      int adjust = 0;

      for (i = svec.size() - 1; i >= 0; --i)
      {
         j = svec.index(i);
         if (v[j])
         {
            x = v[j] + xx * svec.value(i);
            if (x > eps || x < meps)
               v[j] = x;
            else
            {
               adjust = 1;
               v[j] = mark;
            }
         }
         else
         {
            x = xx * svec.value(i);
            if (x > eps || x < meps)
            {
               v[j] = x;
               addIdx(j);
            }
         }
      }

      if (adjust)
      {
         int* iptr = idx;
         int* iiptr = idx;
         int* endptr = idx + num;
         for (; iptr < endptr; ++iptr)
         {
            x = v[*iptr];
            if (x > eps || x < meps)
               *iiptr++ = *iptr;
            else
               v[*iptr] = 0;
         }
         num = int(iiptr - idx);
      }
   }
   else
      Vector::multAdd(xx, svec);

   assert(isConsistent());
   return *this;
}

SSVector& SSVector::multAdd(Real xx, const SubSVector& svec)
{
   if (isSetup())
   {
      int i, j;
      Real x;
      Real* v = val;
      const Real eps = epsilon;
      const Real meps = -epsilon;
      const Real mark = 1e-100;
      int adjust = 0;

      for (i = svec.size() - 1; i >= 0; --i)
      {
         j = svec.index(i);
         if (v[j])
         {
            x = v[j] + xx * svec.value(i);
            if (x > eps || x < meps)
               v[j] = x;
            else
            {
               adjust = 1;
               v[j] = mark;
            }
         }
         else
         {
            x = xx * svec.value(i);
            if (x > eps || x < meps)
            {
               v[j] = x;
               addIdx(j);
            }
         }
      }

      if (adjust)
      {
         int* iptr = idx;
         int* iiptr = idx;
         int* endptr = idx + num;
         for (; iptr < endptr; ++iptr)
         {
            x = v[*iptr];
            if (x > eps || x < meps)
               *iiptr++ = *iptr;
            else
               v[*iptr] = 0;
         }
         num = int(iiptr - idx);
      }
   }
   else
      Vector::multAdd(xx, svec);

   assert(isConsistent());
   return *this;
}

SSVector& SSVector::multAdd(Real x, const Vector& vec)
{
   Vector::multAdd(x, vec);
   if (isSetup())
   {
      setupStatus = false;
      setup();
   }
   return *this;
}

SSVector& SSVector::operator=(const SSVector& rhs)
{
   if (this != &rhs)
   {
      clear();

      setMax(rhs.max());
      IdxSet::operator=(rhs);
      DVector::reDim(rhs.dim());

      if (rhs.isSetup())
      {
         for (int i = size() - 1; i >= 0; --i)
         {
            int j = index(i);
            val[j] = rhs.val[j];
         }
      }
      else
      {
         int* ii = idx;
         Real* v = val;
         Real* rv = static_cast<Real*>(rhs.val);
         Real* last = rv + rhs.dim() - 1;
         Real x = *last;
         const Real eps = epsilon;
         const Real meps = -eps;
         
         *last = 1e-100;
         for(;;)
         {
            while (!*rv)
            {
               ++rv;
               ++v;
            }
            if (*rv < meps || *rv > eps)
            {
               *ii++ = int(v - val);
               *v++ = *rv++;
            }
            else if (rv == last)
               break;
            else
            {
               v++;
               rv++;
            }
         }
         *rv = x;
         
         if (x < meps || x > eps)
         {
            *ii++ = int(v - val);
            *v++ = x;
         }
         num = int(ii - idx);
      }
      setupStatus = true;
   }
   assert(isConsistent());

   return *this;
}

void SSVector::setup_and_assign(SSVector& rhs)
{
   clear();

   setMax(rhs.max());
   DVector::reDim(rhs.dim());

   if (rhs.isSetup())
   {
      int i, j;
      IdxSet::operator=(rhs);
      for (i = size() - 1; i >= 0; --i)
      {
         j = index(i);
         val[j] = rhs.val[j];
      }
   }
   else
   {
      int* ri = rhs.idx;
      int* ii = idx;
      Real* rv = rhs.val;
      Real* v = val;
      Real* last = rv + rhs.dim() - 1;
      Real x = *last;
      const Real eps = rhs.epsilon;
      const Real meps = -eps;

      *last = 1e-100;
      for(;;)
      {
         while (!*rv)
         {
            ++rv;
            ++v;
         }
         if (*rv < meps || *rv > eps)
         {
            *ri++ = *ii++ = int(v - val);
            *v++ = *rv++;
         }
         else if (rv == last)
            break;
         else
         {
            v++;
            *rv++ = 0;
         }
      }

      if (x < meps || x > eps)
      {
         *ri++ = *ii++ = int(v - val);
         *v++ = *rv = x;
      }
      else
         *rv = 0;
      num = rhs.num = int(ii - idx);
      rhs.setupStatus = true;
   }
   setupStatus = true;

   assert(isConsistent());
}

SSVector& SSVector::operator=(const SVector& rhs)
{
   clear();
   return assign(rhs);
}

SSVector& SSVector::assign(const SVector& rhs)
{
   assert(rhs.dim() <= Vector::dim());

   const SVector::Element* e = rhs.m_elem;
   int* p = idx;
   int i = rhs.size();

   while (i--)
   {
      val[*p = e->idx] = e->val;
      p += ((e++)->val != 0);
   }
   num = int(p - idx);
   setupStatus = true;

   assert(isConsistent());
   return *this;
}

SSVector& SSVector::assign2product1(const SVSet& A, const SSVector& x)
{
   assert(x.isSetup());

   const Real* vl = x.val;
   const int* xi = x.idx;

   int* ii = idx;
   SVector* svec = const_cast<SVector*>( & A[*xi] );
   const SVector::Element* e = &(svec->element(0));
   const SVector::Element* last = e + (num = svec->size());
   Real* v = val;
   Real y = vl[*xi];

   for (; e < last; ++e)
      v[ *ii++ = e->idx ] = y * e->val;

   return *this;
}

SSVector& SSVector::assign2productShort(const SVSet& A, const SSVector& x)
{
   assert(x.isSetup());

   int i, j;
   const Real* vl = x.val;
   const int* xi = x.idx;

   Real y;
   int* ii = idx;
   SVector* svec = const_cast<SVector*>( & A[*xi] );
   const SVector::Element* e = &(svec->element(0));
   const SVector::Element* last = e + (num = svec->size());
   Real* v = val;
   Real xx = vl[*xi++];
   for (; e < last; ++e)
   {
      v[ *ii = e->idx ] = y = xx * e->val;
      ii += (y != 0);
   }

   int k;
   Real mark = 1e-100;
   for (i = x.size(); --i > 0;)
   {
      xx = vl[*xi];
      svec = const_cast<SVector*>( & A[*xi++] );
      e = &(svec->element(0));
      for (k = svec->size(); --k >= 0;)
      {
         *ii = j = e->idx;
         ii += ((y = v[j]) == 0);
         y += xx * e++->val;
         v[j] = y + (y == 0) * mark;
      }
   }

   const Real eps = epsilon;
   const Real meps = -eps;
   int* is = idx;
   int* it = idx;
   for (; is < ii; ++is)
   {
      y = v[*is];
      if (y > eps || y < meps)
         *it++ = *is;
      else
         v[*is] = 0;
   }
   num = int(it - idx);

   assert(isConsistent());
   return *this;
}

SSVector& SSVector::assign2productFull(const SVSet& A, const SSVector& x)
{
   assert(x.isSetup());

   int i;
   const Real* vl = x.val;
   const int* xi = x.idx;

   SVector* svec;
   const SVector::Element* elem;
   const SVector::Element* last;
   Real y;
   Real* v = val;

   for (i = x.size(); i-- > 0; ++xi)
   {
      svec = const_cast<SVector*>( & A[*xi] );
      elem = &(svec->element(0));
      last = elem + svec->size();
      y = vl[*xi];
      for (; elem < last; ++elem)
         v[elem->idx] += y * elem->val;
   }

   return *this;
}

SSVector& SSVector::assign2product4setup(const SVSet& A, const SSVector& x)
{
   assert(A.num() == x.dim());

   assert(x.isSetup());

   clear();

   if (x.size() == 1)
   {
      assign2product1(A, x);
      setupStatus = true;
   }

   else if (Real(x.size())*A.memSize() <= shortProductFactor*dim()*A.num()
             && isSetup())
   {
      assign2productShort(A, x);
      setupStatus = true;
   }

   else
   {
      assign2productFull(A, x);
      setupStatus = false;
   }

   return *this;
}

SSVector& SSVector::assign2product(const SSVector& x, const SVSet& A)
{
   assert(A.num() == dim());
   const Real eps = epsilon;
   const Real minuseps = -epsilon;
   Real y;

   clear();
   for (int i = dim(); i-- > 0;)
   {
      y = A[i] * x;
      if (y > eps || y < minuseps)
      {
         val[i] = y;
         IdxSet::addIdx(i);
      }
   }

   return *this;
}

SSVector& SSVector::assign2productAndSetup(const SVSet& A, SSVector& x)
{
   if (x.isSetup())
      return assign2product4setup(A, x);

   SVector* svec;
   const SVector::Element* elem;
   const SVector::Element* last;
   Real y;
   Real* v = val;
   int* xi = x.idx;
   Real* xv = x.val;
   Real* end = xv + x.dim() - 1;
   const Real eps = epsilon;
   const Real meps = -epsilon;

   /* setze weissen Elefanten */
   Real lastval = *end;
   *end = 1e-100;

   for(;;)
   {
      while (!*xv)
         ++xv;
      if (*xv > eps || *xv < meps)
      {
         y = *xv;
         svec = const_cast<SVector*>( & A[ *xi++ = int(xv - x.val) ] );
         elem = &(svec->element(0));
         last = elem + svec->size();
         for (; elem < last; ++elem)
            v[elem->idx] += y * elem->val;
      }
      else
      {
         *xv = 0;
         if (xv == end)
            break;
      }
      xv++;
   }

   /* fange weissen Elefanten wieder ein */
   if (lastval > eps || lastval < meps)
   {
      y = *xv = lastval;
      svec = const_cast<SVector*>( & A[ *xi++ = int(xv - x.val) ] );
      elem = &(svec->element(0));
      last = elem + svec->size();
      for (; elem < last; ++elem)
         v[elem->idx] += y * elem->val;
   }
   else
      *xv = 0;

   x.num = int(xi - x.idx);
   x.setupStatus = true;
   setupStatus = false;

   return *this;
}

bool SSVector::isConsistent() const
{
   if (Vector::dim() > IdxSet::max())
      return MSGinconsistent("SSVector");

   if (Vector::dim() < IdxSet::dim())
      return MSGinconsistent("SSVector");

   if (isSetup())
   {
      for (int i = Vector::dim() - 1; i >= 0; --i)
         if (val[i] != 0 && number(i) < 0)
            return MSGinconsistent("SSVector");
   }

   return DVector::isConsistent() && IdxSet::isConsistent();
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
