/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*    Copyright (C) 1996-2015 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SoPlex is distributed under the terms of the ZIB Academic Licence.       */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SoPlex; see the file COPYING. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file  unitvector.h
 * @brief Sparse vector \f$e_i\f$.
 */

#ifndef _UNITVECTOR_H_
#define _UNITVECTOR_H_

#include <assert.h>
#include "spxdefines.h"
#include "svector.h"

namespace soplex
{


/**@brief   Sparse vector \f$e_i\f$.
   @ingroup Algebra

   A UnitVector is an SVector that can take only one nonzero value with
   value 1 but arbitrary index.

   \todo Several SVector modification methods are still accessible for UnitVector. 
   They might be used to change the vector.

   \todo UnitVector memory management must be changed when SVector is redesigned.
*/
class UnitVector : public SVector
{
private:

   //------------------------------------
   /**@name Data */
   //@{
   Element themem;  ///< memory for sparse vector entry
   //@}

public:

   //------------------------------------
   /**@name Access */
   //@{
   /// returns value = 1
   /**\pre \c n must be 0.
    */
   /* ARGSUSED n */
   Real value(int n) const
   {
      assert( n == 0 );
      return 1;
   }
   //@}

   //------------------------------------
   /**@name Constructors / destructors */
   //@{
   /// construct \c i 'th unit vector.
   explicit
   UnitVector(int i = 0)
      : SVector(1, &themem)
   {
      add(i, 1.0);

      assert(isConsistent());
   }
   ///  copy constructor
   UnitVector(const UnitVector& rhs)
      : SVector(1, &themem)
   {
      themem = rhs.themem;

      assert(isConsistent());
   }
   /// assignment
   UnitVector& operator=(const UnitVector& rhs)
   {
      if ( this != &rhs )
      {
         themem = rhs.themem;

         assert(isConsistent());
      }
      return *this;
   }
   /// destructor
   ~UnitVector()
   {}
   //@}

   //------------------------------------
   /**@name Miscellaneous */
   //@{
   /// consistency check
   bool isConsistent() const;
   //@}
};


} // namespace soplex
#endif // _UNITVECTOR_H_
