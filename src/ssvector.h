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
#pragma ident "@(#) $Id: ssvector.h,v 1.7 2001/11/29 14:43:46 bzfpfend Exp $"


/**@file  ssvector.h
 * @brief Semi sparse vector.
 */
#ifndef _SSVECTOR_H_
#define _SSVECTOR_H_

#include <assert.h>

#include "dvector.h"
#include "subsvector.h"
#include "svector.h"
#include "didxset.h"

namespace soplex
{
class SVSet;

/**@brief   Semi sparse vector.
   @ingroup Algebra

   This class implements Semi Sparse Vectors. Such are
   #DVector%s where the indices of its nonzero elements can be stored in an
   extra #IdxSet. Only elements with absolute value > #epsilon are considered
   to be nonzero. Since really storing the nonzeros is not allways convenient,
   an #SSVector provides two different statuses: setup and not setup.
   An #SSVector being setup means that the nonzero indices are available,
   otherwise an #SSVector is just an ordinary #Vector with an empty #IdxSet.
*/
class SSVector : protected DVector, protected DIdxSet
{
private:
   int setupStatus;        ///< is the #SSVector set up?

   friend class DVector;
   friend class Vector;
   friend class DSVector;
   friend class SMoPlex;

public:
   /**@todo member variable epsilon should be private. */
   /// a value x with |x| < epsilon is considered zero.
   double epsilon;

   /**@name Status of an #SSVector
      An #SSVector can be set up or not. In case it is set up, its #IdxSet
      correctly contains all indices of nonzero elements of the #SSVector.
      Otherwise, it does not contain any usefull data. Wheter or not an
      #SSVector is setup can be determined with method #isSetup().
      
      There are three method for directly affecting the setup status of an
      #SSVector:
      - unSetup():     This method sets the status to ``not setup''.
      - setup():       This method initializes the #IdxSet# to the
                       #SSVector#s nonzero indices and sets the status
                       to ``setup''.
      - forceSetup():  This method sets the status to ``setup'' without
                       verifying, that the #IdxSet correctly contains
                       all nonzero indices. It may be used when the
                       nonzero indices have been computed externally.
   */
   //@{
   /// only used in slufactor.cpp
   double* get_ptr()
   {
      return DVector::get_ptr();
   }

   /// returns setup status.
   int isSetup() const
   {
      return setupStatus;
   }

   /// makes #SSVector not setup.
   void unSetup()
   {
      setupStatus = 0;
   }

   /// initializes nonzero indices
   /** Initializes nonzero indices for all elements with absolute values
       greater than #epsilon and sets all other elements to 0.
   */
   void setup();
   
   /// forces setup status.
   void forceSetup()
   {
      setupStatus = 1;
   }
   //@}


   /**@name Methods for setup SSVectors */
   //@{
   /// returns index of the \p n 'th nonzero element.
   int index(int n) const
   {
      assert(isSetup());
      return IdxSet::index(n);
   }

   /// returns value of the \p n 'th nonzero element.
   double value(int n) const
   {
      assert(isSetup());
      assert(n >= 0 && n < size());
      return val[idx[n]];
   }

   /// returns the position number of index \p i, or -1 if \p i doesn't exist.
   int number(int i) const
   {
      assert(isSetup());
      return IdxSet::number(i);
   }

   /// returns the number of nonzeros.
   int size() const
   {
      assert(isSetup());
      return IdxSet::size();
   }

   /// adds nonzero (\p i, \p x) to #SSVector.
   /** No nonzero with index \p i must exist in the #SSVector.
    */
   void add(int i, double x)
   {
      assert(val[i] == 0);
      assert(number(i) < 0);
      addIdx(i);
      val[i] = x;
   }

   /// sets \p i 'th element to \p x.
   void setValue(int i, double x);

   /// clears element \p i.
   void clearIdx(int i)
   {
      if (isSetup())
      {
         int n = number(i);
         if (n >= 0)
            remove(n);
      }
      val[i] = 0;
   }

   /// sets \p n 'th nonzero element to 0 (index \p n must exist!).
   void clearNum(int n)
   {
      assert(isSetup());
      assert(index(n) >= 0);
      val[index(n)] = 0;
      remove(n);
   }
   //@}


   /**@name Methods independend of the Status */
   //@{
   /// returns \p i 'th value.
   double operator[](int i) const
   {
      return val[i];
   }

   /// returns array indices.
   const int* indexMem() const
   {
      return IdxSet::indexMem();
   }

   /// returns array values.
   const double* values() const
   {
      return val;
   }

   /// returns indices.
   const IdxSet& indices() const
   {
      return *this;
   }

   /// returns array indices.
   int* altIndexMem()
   {
      unSetup();
      return IdxSet::indexMem();
   }

   /// returns array values.
   double* altValues()
   {
      unSetup();
      return val;
   }

   /// returns indices.
   IdxSet& altIndices()
   {
      unSetup();
      return *this;
   }
   //@}


   /**@name Mathematical opeations */
   //@{
   ///
   SSVector& operator+=(const Vector& vec);
   ///
   SSVector& operator+=(const SVector& vec);
   ///
   SSVector& operator+=(const SubSVector& vec);
   /// vector summation.
   SSVector& operator+=(const SSVector& vec);

   ///
   SSVector& operator-=(const Vector& vec);
   ///
   SSVector& operator-=(const SVector& vec);
   ///
   SSVector& operator-=(const SubSVector& vec);
   /// vector subtraction.
   SSVector& operator-=(const SSVector& vec);

   /// vector scaling.
   SSVector& operator*=(double x);

   ///
   SSVector& multAdd(double x, const SSVector& vec);
   ///
   SSVector& multAdd(double x, const SVector& vec);
   ///
   SSVector& multAdd(double x, const SubSVector& vec);
   /// adds scaled vector (+= \p x * \p vec).
   SSVector& multAdd(double x, const Vector& vec);

   /// assigns #SSVector to \f$x^T \cdot A\f$.
   SSVector& assign2product(const SSVector& x, const SVSet& A);
   /// assigns #SSVector to \f$A \cdot x\f$.
   SSVector& assign2product(const SVSet& A, const SSVector& x);
   /// assigns #SSVector to \f$A \cdot x\f$ for a setup \p x.
   SSVector& assign2product4setup(const SVSet& A, const SSVector& x);
   /// assigns #SSVector to \f$A \cdot x\f$ thereby setting up \p x.
   SSVector& assign2productAndSetup(const SVSet& A, SSVector& x);

   /// returns infinity norm of a Vector.
   double maxAbs() const;
   /// returns euclidian norm of a Vector.
   double length() const;
   /// returns squared norm of a Vector.
   double length2() const;
   //@}


   /**@name Miscellaneous */
   //@{
   /// returns dimension of Vector.
   int dim() const
   {
      return dimen;
   }

   /// resets dimension to \p newdim.
   void reDim (int newdim);

   /// sets number of nonzeros (thereby unSetup SSVector).
   void setSize(int n)
   {
      unSetup();
      IdxSet::setSize(n);
   }

   /// resets memory consumption to \p newsize.
   void reMem(int newsize);

   /// clears vector.
   void clear ();

   /// consistency check.
   int isConsistent() const;
   //@}


   /**@name Constructors / Destructors */
   //@{
   /// default constructor.
   SSVector(int pdim = 0, double peps = 1e-16)
      : DVector (pdim)
      , DIdxSet (pdim + 1)
      , setupStatus(1)
      , epsilon (peps)
   {
      Vector::clear();
   }

   /// copy constructor.
   SSVector(const SSVector& vec)
      : DVector (vec)
      , DIdxSet (vec.dim() + 1)
      , setupStatus(vec.setupStatus)
      , epsilon (vec.epsilon)
   {
      DIdxSet::operator= ( vec );
      //*((DIdxSet*)this) = vec;
   }

   /// constructs nonsetup copy of \p vec.
   SSVector(const Vector& vec, double eps = 1e-16)
      : DVector (vec)
      , DIdxSet (vec.dim() + 1)
      , setupStatus(0)
      , epsilon (eps)
   { }

   /// sets up \p rhs vector, and assigns it.
   void setup_and_assign(SSVector& rhs);

   /// assign only the elements of \p rhs.
   SSVector& assign(const SVector& rhs);

   ///
   SSVector& operator=(const SSVector& rhs);
   ///
   SSVector& operator=(const SVector& rhs);
   /// assignment operator.
   SSVector& operator=(const Vector& rhs)
   {
      unSetup();
      Vector::operator=(rhs);
      return *this;
   }

   //@}

private:
   SSVector& assign2product1(const SVSet& A, const SSVector& x);
   SSVector& assign2productShort(const SVSet& A, const SSVector& x);
   SSVector& assign2productFull(const SVSet& A, const SSVector& x);
};


// ----------------------------------------------------------------------------

inline Vector& Vector::multAdd(double x, const SSVector& svec)
{
   assert(svec.dim() <= dim());

   if (svec.isSetup())
   {
      const int* idx = svec.indexMem();

      for(int i = 0; i < svec.size(); i++)
         val[idx[i]] += x * svec[idx[i]];
   }
   else
   {
      assert(svec.dim() == dim());

      for(int i = 0; i < dim(); i++)
         val[i] += x * svec.val[i];
   }
   //multAdd(x, static_cast<const Vector&>(svec));

   return *this;
}

inline Vector& Vector::assign(const SSVector& svec)
{
   assert(svec.dim() <= dim());

   if (svec.isSetup())
   {
      const int* idx = svec.indexMem();

      for(int i = svec.size(); i > 0; i--)
      {
         val[*idx] = svec.val[*idx];
         idx++;
      }
   }
   else
      operator= (static_cast<const Vector&>(svec));

   return *this;
}

inline Vector& Vector::operator=(const SSVector& vec)
{
   if (vec.isSetup())
   {
      clear ();
      assign(vec);
   }
   else
      operator= (static_cast<const Vector&>(vec));

   return *this;
}

inline double Vector::operator*(const SSVector& v) const
{
   assert(dim() == v.dim());

   if (v.isSetup())
   {
      const int* idx = v.indexMem();
      double     x   = 0;

      for(int i = v.size(); i > 0; i--)
      {
         x += val[*idx] * v.val[*idx];
         idx++;
      }
      return x;
   }
   else
      return operator*(static_cast<const Vector&>(v));
}
} // namespace soplex
#endif // _SSVECTOR_H_

//-----------------------------------------------------------------------------
//Emacs Local Variables:
//Emacs mode:c++
//Emacs c-basic-offset:3
//Emacs tab-width:8
//Emacs indent-tabs-mode:nil
//Emacs End:
//-----------------------------------------------------------------------------
