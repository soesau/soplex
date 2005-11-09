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
#pragma ident "@(#) $Id: spxweightpr.h,v 1.15 2005/11/09 13:53:50 bzforlow Exp $"

/**@file  spxweightpr.h
 * @brief Weighted pricing.
 */
#ifndef _SPXWEIGHTPR_H_
#define _SPXWEIGHTPR_H_

#include "spxdefines.h"
#include "spxpricer.h"

namespace soplex
{

/**@brief   Weighted pricing.
   @ingroup Algo
      
   Class SPxWeightPR is an implemantation class of SPxPricer that uses
   weights for columns and rows for selecting the Simplex pivots. The weights
   are computed by methods #computeCP() and #computeRP() which may be
   overridden by derived classes.
   
   The weights are interpreted as follows: The higher a value is, the more
   likely the corresponding row or column is set on one of its bounds.
   
   See SPxPricer for a class documentation.
*/
class SPxWeightPR : public SPxPricer
{
private:

   //-------------------------------------
   /**@name Data */
   //@{
   /// column penalties
   DVector cPenalty;              
   /// row penalties
   DVector rPenalty;
   /// penalties for leaving alg
   DVector leavePenalty;
   ///
   const Real* penalty;
   ///
   const Real* coPenalty;
   /// length of objective vector.
   Real objlength;
   //@}

   //-------------------------------------
   /**@name Private helpers */
   //@{
   /// compute leave penalties.
   void computeLeavePenalty(int start, int end);
   /// compute weights for columns.
   void computeCP(int start, int end);
   /// compute weights for rows.
   void computeRP(int start, int end);
   //@}

public:

   //-------------------------------------
   /**@name Construction / destruction */
   //@{
   /// default constructor
   SPxWeightPR() 
      : SPxPricer("Weight")
   {}   
   /// destructor
   virtual ~SPxWeightPR()
   {}
   //@}

   //-------------------------------------
   /**@name Access / modification */
   //@{
   /// sets the solver
   virtual void load(SPxSolver* base);
   /// set entering/leaving algorithm
   void setType(SPxSolver::Type tp);
   /// set row/column representation
   void setRep(SPxSolver::Representation rep);
   ///
   virtual int selectLeave();
   ///
   virtual SPxId selectEnter();
   /// \p n vectors have been added to the loaded LP.
   virtual void addedVecs (int n);
   /// \p n covectors have been added to the loaded LP.
   virtual void addedCoVecs(int n);
   /// \p the i'th vector has been removed from the loaded LP.
   virtual void removedVec(int i);
   /// \p the i'th covector has been removed from the loaded LP.
   virtual void removedCoVec(int i);
   /// \p n vectors have been removed from the loaded LP.
   virtual void removedVecs(const int perm[]);
   /// \p n covectors have been removed from the loaded LP.
   virtual void removedCoVecs(const int perm[]);
   //@}

#ifndef NO_CONSISTENCY_CHECKS
   //-------------------------------------
   /**@name Consistency check */
   //@{
   /// checks for consistency
   virtual bool isConsistent() const;
   //@}
#endif

private:

   //-------------------------------------
   /**@name Blocked */
   //@{
   /// copy constructor
   SPxWeightPR( const SPxWeightPR& );
   /// assignment operator
   SPxWeightPR& operator=( const SPxWeightPR& );
   //@}
};
} // namespace soplex
#endif // _SPXWEIGHTPR_H_

//-----------------------------------------------------------------------------
//Emacs Local Variables:
//Emacs mode:c++
//Emacs c-basic-offset:3
//Emacs tab-width:8
//Emacs indent-tabs-mode:nil
//Emacs End:
//-----------------------------------------------------------------------------
