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
#pragma ident "@(#) $Id: soplex.h,v 1.1 2001/11/06 16:18:33 bzfkocht Exp $"


#ifndef _SOPLEX_H_
#define _SOPLEX_H_

//@ ----------------------------------------------------------------------------
/*      \Section{Imports}
    Import required system include files
 */
#include <assert.h>


/*  and class header files
 */

#include "cachelpsolver.h"
#include "timer.h"
#include "spxlp.h"
#include "spxbasis.h"
#include "array.h"
#include "random.h"
#include "unitvector.h"
#include "updatevector.h"
#include "subsvector.h"

namespace soplex
{

class SPxPricer;
class SPxRatioTester;
class SPxStarter;
class SPxSimplifier;


//@ ----------------------------------------------------------------------------
/* \Section{Class Declaration}
 */

/** {\bf S}equential {\bf o}bjectoriented sim{\bf Plex}.
    #SoPlex# is an LP solver class using the revised Simplex algorithm. It
    provids two basis representations, namely a column basis and a row basis
    (see \Ref{Representation}). For both representation, a primal and
    dual algorithm is available (see \Ref{Type}).
 
    In addition, #SoPlex# can be custumized with various respects:
    \begin{itemize}
    \item       pricing algorithms using \Ref{SPxPricer}
    \item       ratio test using class \Ref{SPxRatioTester}
    \item       computation of a start basis using class \Ref{SPxStarter}
    \item       preprocessing of the LP using class \Ref{SPxSimplifier}
    \item       termination criteria by overriding 
    \end{itemize}
 
    #SoPlex# is derived from #SPxLP# that is used to store the LP to be solved.
    Hence, the LPs solved with #SoPlex# have the general format
 
    \[
    \begin{array}{rl}
        \hbox{max}      & #maxObj#^T x          \\
        \hbox{s.t.}     & #lhs# \le Ax \le #rhs#        \\
                        & #low# \le x \le #up#
    \end{array}
    \]
 
    Also, #SPxLP# provide all manipulation methods for the LP. They allow
    #SoPlex# to be used within cutting plane algorithms.
 
    Finally, #SoPlex# is preperred for parallelization. Class \Ref{DoPlex} is a
    parallel version of this code implemented as a derived class.
 */
class SoPlex : public CacheLPSolver, public SPxLP, protected SPxBasis
{
   friend class SPxFastRT;

   Timer theTime;
public:

   /**@name Data Types */
   //@{

   /** LP basis representation.
       Solving LPs with the Simplex algorithm requires the definition of a
       {\em basis}. A basis can be defined as a set of column vectors or a
       set of row vectors building a nonsingular matrix. We will refer to
       the first case as the {\em columnwise representation} and the latter
       case will be called the {\em rowwise representation}.

       Type #Representation# determines the representation of #SoPlex#, i.e.
       a columnwise (#COLUMN == 1#) or rowwise (#ROW == -1#) one.
    */
   enum Representation
   {
      /// rowwise representation.
      ROW = -1,
      /// columnwise representation.
      COLUMN = 1
   };

   /// return the current basis representation.
   Representation rep() const
   {
      return therep;
   }

private:
   Representation therep;

public:
   /** Algorithmic type.
       #SoPlex# uses the reviesed Simplex algorithm to solve LPs.
       Mathematically, one distinguishes the {\em primal} from the
       {\em dual} algorihm. Algorithmically, these relate to the two
       types #ENTER# or #LEAVE#. How they relate, depends on the chosen
       basis representation. This is desribed by the following table:

       \begin{center}
       \begin{tabular}{lcc}
                  & ENTER      & LEAVE \\
       \hline
           ROW    & DUAL       & PRIMAL\\
           COLUMN & PRIMAL     & DUAL  
       \end{tabular}
       \end{center}
    */
   enum Type
   {
      /** Entering Simplex.
          The Simplex loop for the entering Simplex can be sketched
          as follows:
          \begin{enumerate}
          \item       {\em Pricing}:    Select a variable to #ENTER# the
                      basis.
          \item       {\em Ratio Test}: Select variable to #LEAVE# the
                      basis such that the basis remains feasible.
          \item       Perform the basis update.
          \end{enumerate}
       */
      ENTER = -1,
      /** Leaving Simplex.
          The Simplex loop for the leaving Simplex can be sketched
          as follows:
          \begin{enumerate}
          \item       {\em Pricing}: Select a variable to #LEAVE# the
                      basis.
          \item       {\em Ratio Test}: Select variable to #ENTER# the
                      basis such that the basis remains priced.
          \item       Perform the basis update.
          \end{enumerate}
       */
      LEAVE = 1
   };
   /// return current \Ref{Type}.
   Type type() const
   {
      return theType;
   }

   /** Pricing type.
       In case of the #ENTER#ing Simplex algorithm, for performance
       reasons it may be advisable not to compute and maintain up to
       date vectors #pVec()# and #test()# and instead compute only some
       of its elements explicitely. This is constroled by the #Pricing#
       type.
    */
   enum Pricing
   {
      /** Full pricing.
          If #FULL# pricing in selected for the #ENTER#ing Simplex,
          vectors #pVec()# and #test()# are kept up to date by
          #SoPlex#. An #SPxPricer# only needs to select an #Id# such
          that the #test()# or #coTest()# value is #<0#.
       */
      FULL,
      /** Partial pricing.
          When #PARTIAL# pricing in selected for the #ENTER#ing
          Simplex, vectors #pVec()# and #test()# are not set up and
          updated by #SoPlex#. However, vectors #coPvec()# and
          #coTest()# are still kept up to date by #SoPlex#.
          An #SPxPricer# object needs to compute the values for
          #pVec()# and #test()# itself in order to select an
          appropriate pivot with #test() < 0#. Methods #computePvec(i)#
          and #computeTest(i)# will assist the used to do so. Note,
          that it may be feasbile for a pricer to return an #Id# with
          #test()>0#; such will be rejected by #SoPlex#.
       */
      PARTIAL
   };
   /// return current \Ref{Pricing}.
   Pricing pricing() const
   {
      return thePricing;
   }

private:
   Type theType;
   Pricing thePricing;

   //@}


public:
   /**@name Setup
       Before solving an LP with an instance of #SoPlex#, the following steps must
       be performed:
       \begin{enumerate}
       \item   Load the LP by copying an external LP or reading it from an
               input stream.
       \item   Setup the pricer to use by loading an \Ref{SPxPricer} object
               (only neccessary, if not done in a previous call).
       \item   Setup the ratio test method to use by loading an
               \Ref{SPxRatioTester} object (only neccessary, if not done in a
               previous call).
       \item   Setup the linear system solver to use by loading an
               \Ref{SLinSolver} object (only neccessary, if not done in a previous call).
       \item   Optionally setup an LP simplifier by loading an
               \Ref{SPxSimplifier} object.
       \item   Optionally setup an start basis generation method by loading an
               \Ref{SPxStarter} object.
       \item   Optionally setup a start basis by loading a \Ref{SPxBasis::Desc} object.
       \item   Optionally switch to another basis \Ref{Representation} by calling
               method #setRep()#.
       \item   Optionally switch to another algorithm \Ref{Type} by calling
               method #setType()#.
       \end{enumerate}
       Now the solver is ready for execution. If the loaded LP is to be solved
       again from scratch, this can be done with method #reLoad()#. Finally,
       #clear()# removes the LP from the solver.
    */
   //@{
   /// read LP from input stream.
   friend std::istream& operator>>(std::istream& is, SoPlex& lp)
   {
      lp.read(is);
      return is;
   }
   ///
   virtual void read(std::istream& in, NameSet* rowNames = 0,
                      NameSet* colNames = 0, DIdxSet* intVars = 0);

   /// copy LP.
   void load(const SPxLP& LP);

   /// setup linear solver to use.
   void load(SLinSolver* slu);
   ///
   void load(SPxPricer*);
   ///
   void load(SPxRatioTester*);
   ///
   void load(SPxStarter*);
   ///
   void load(SPxSimplifier*);
   ///
   void load(const SPxBasis::Desc&);
   ///
   SPxBasis::readBasis;

   /// #ROW# or #COLUMN#.
   void setRep (int rep);
   /// #LEAVE# or #ENTER#.
   void setType(Type tp);
   /// #FULL# or #PARTIAL#.
   void setPricing(Pricing pr);

   /// reload LP.
   virtual void reLoad();

   /// load LP from #filename# in MPS or LP format.
   void readFile(char* filename);

   /// dump loaded LP to #filename# in LP format.
   void dumpFile(char* filename) const;

   /// clear all data in solver.
   void clear();
   //@}


   /**@name Solving LPs */
   //@{
   /** solve loaded LP.
       Solves the loaded LP by processing the Simplex iteration until
       the termination criteria is fullfilled (see
       \pageref{terminate.doc}). The \Ref{SPxStatus} of the solver will
       indicate the reason for termination.
    */
   LPSolver::Status solve();

   /// \Ref{Status} of basis.
   LPSolver::Status status() const;

   /** current objective value.
       Returns the objective value of the current solution vector (see
       #getPrimal()# below).
    */
   virtual double value() const;

   /** get solution vector for primal variables.
       This method returns the \Ref{Status} of the basis.
       If it is #REGULAR# or better,
       the primal solution vector of the current basis will be copied
       to the argument #vector#. Hence, #vector# must be of dimension
       #nCols()#.
    */
   LPSolver::Status getPrimal (Vector& vector) const;

   /** get vector of slack variables.
       This method returns the \Ref{Status} of the basis.
       If it is #REGULAR# or better,
       the slack variables of the current basis will be copied
       to the argument #vector#. Hence, #vector# must be of dimension
       #nRows()#.

       WARNING: Because #SoPlex# supports range constraints as its
       default, slack variables are defined in a nonstandard way:
       Let $x$ be the current solution vector and $A$ the constraint
       matrix. Then the vector of slack variables is defined as
       $s = Ax$.
    */
   LPSolver::Status getSlacks (Vector& vector) const;

   /** get current solution vector for dual variables.
       This method returns the \Ref{Status} of the basis.
       If it is #REGULAR# or better,
       the vector of dual variables of the current basis will be copied
       to the argument #vector#. Hence, #vector# must be of dimension
       #nRows()#.

       WARNING: Even though mathematically, each range constraint would
       account for two dual variables (one for each inequaility), only
       #nRows()# dual variables are setup via the following
       construction: Given a range constraint, there are three possible
       situations:
       \begin{enumerate}
       \item   None of its inequalities is tight: The dual variables
               for both are 0. However, when shifting (see below)
               occurs, it may be set to a value other than 0, which
               models a perturbed objective vector.
       \item   Both of its inequalities are tight: In this case the
               range constraint models an equality and we adopt the
               standard definition.
       \item   One of its inequalities is tight while the other is not:
               In this case only the dual variable for the tight
               constraint is given with the standard definition, while
               the other constraint is implicitely set to 0.
       \end{enumerate}
    */
   LPSolver::Status getDual (Vector& vector) const;

   /** get current solution vector for dual variables.
       This method returns the \Ref{Status} of the basis.
       If it is #REGULAR# or better,
       the vector of reduced costs of the current basis will be copied
       to the argument #vector#. Hence, #vector# must be of dimension
       #nCols()#.

       Let $d$ denote the vector of dual variables, as defined above,
       and $A$ the LPs constraint matrix. Then the reduced cost vector
       $r$ is defined as $r^T = c^T - d^TA$.
    */
   LPSolver::Status getRdCost (Vector& vector) const;

   /** Termination criterion.
       \label{terminate.doc}
       This method is called in each Simplex iteration to determine, if
       the algorithm is to terminate. In this case a nonzero value is
       returned.

       This method is declared #virutal# to allow for implementation of
       other stopping criteria or using it as callback method within the
       Simplex loop, by overriding the method in a derived class.
       However, all implementations must terminate with the
       statement #return DoPlex::terminate()#, if no own termination
       criteria is encountered.

       Note, that the Simplex loop stopped even when #terminate()#
       returns 0, if the LP has been solved to optimality (i.e. no
       further pricing succeeds and no shift is present).
    */
   virtual int terminate ();
   //@}

   /**@name Control Parameters */
   //@{
private:
   double thedelta;

public:
   /// values $|x| <$ #epsilon# are considered go be 0.
   double epsilon() const
   {
      return primVec.delta().epsilon;
   }
   /// set parameter #epsilon#.
   void setEpsilon(double eps);
   /** allowed bound violation for optimal Solution.
       When all vectors do not violate their bounds by more than #delta#,
       the basis is considered optimal.
    */
   double delta() const
   {
      return thedelta;
   }
   /// set parameter #delta#.
   void setDelta(double d);

   /** cycling prevention.
       #SoPlex# consideres a Simplex step as degenerate, if the
       steplength does not exceed #epsilon#. Cycling occurs, if only
       degenerate steps are taken. To prevent this situation, #SoPlex#
       perturbs the problem such that nondegenerate steps are ensured.

       Parameter #maxCycle# controls, how agressive such perturbation is
       performed, since no more than #maxCycle# degenerate steps are
       accepted before perturbing the LP. The current number of consequtive
       degenerate steps is counted in variable #numCycle#.
    */
   int maxCycle;

   ///
   int numCycle;
   //@}

public:
   /**@name LP Modification
       These methods are the overridden counterparts of the base class
       #SPxLP#. See \Ref{SPxLP} for a more detailed documentation of these
       methods.
    */
   //@{
private:
   void localAddRows(int start);
   void localAddCols(int start);
protected:
   ///
   virtual void addedRows(int n);
   ///
   virtual void addedCols(int n);

   ///
   virtual void doRemoveRow(int i);
   ///
   virtual void doRemoveRows(int perm[]);
   ///
   virtual void doRemoveCol(int i);
   ///
   virtual void doRemoveCols(int perm[]);

public:
   ///
   virtual void changeObj(const Vector& newObj);
   ///
   virtual void changeObj(int i, double newVal);
   ///
   virtual void changeObj(SPxLP::SPxColId id, double newVal)
   {
      changeObj(number(id), newVal);
   }

   ///
   virtual void changeLower(const Vector& newLower);
   ///
   virtual void changeLower(int i, double newLower);
   ///
   virtual void changeLower(SPxLP::SPxColId id, double newLower)
   {
      changeLower(number(id), newLower);
   }

   ///
   virtual void changeUpper(const Vector& newUpper);
   ///
   virtual void changeUpper(int i, double newUpper);
   ///
   virtual void changeUpper(SPxLP::SPxColId id, double newUpper)
   {
      changeUpper(number(id), newUpper);
   }

   ///
   virtual void changeBounds(const Vector& newLower, const Vector& newUpper);

   ///
   virtual void changeBounds(int i, double newLower, double newUpper);

   ///
   virtual void changeBounds(SPxLP::SPxColId id, double newLower, double newUpper)
   {
      changeBounds(number(id), newLower, newUpper);
   }


   ///
   virtual void changeLhs(const Vector& newLhs);
   ///
   virtual void changeLhs(int i, double newLhs);
   ///
   virtual void changeLhs(SPxLP::SPxRowId id, double newLhs)
   {
      changeLhs(number(id), newLhs);
   }

   ///
   virtual void changeRhs(const Vector& newRhs);
   ///
   virtual void changeRhs(int i, double newRhs);
   ///
   virtual void changeRhs(SPxLP::SPxRowId id, double newRhs)
   {
      changeRhs(number(id), newRhs);
   }

   ///
   virtual void changeRange(const Vector& newLhs, const Vector& newRhs);

   ///
   virtual void changeRange(int i, double newLhs, double newRhs);

   ///
   virtual void changeRange(SPxLP::SPxRowId id, double newLhs, double newRhs)
   {
      changeRange(number(id), newLhs, newRhs);
   }


   ///
   virtual void changeRow(int i, const LPRow& newRow);
   ///
   virtual void changeRow(SPxLP::SPxRowId id, const LPRow& newRow)
   {
      changeRow(number(id), newRow);
   }

   ///
   virtual void changeCol(int i, const LPCol& newCol);
   ///
   virtual void changeCol(SPxLP::SPxColId id, const LPCol& newCol)
   {
      changeCol(number(id), newCol);
   }

   ///
   virtual void changeElement(int i, int j, double val);
   ///
   virtual void changeElement(SPxLP::SPxRowId rid, SPxLP::SPxColId cid, double val)
   {
      changeElement(number(rid), number(cid), val);
   }

   ///
   virtual void changeSense(SPxLP::SPxSense sns);
   //@}


public:
   /**@name Programming with #SoPlex#
       The following sections are dedicated to users, that want to provide own
       pricers, ratio test, start basis generation codes or LP simplifiers to use
       with #SoPlex# or that want to derive own implementations (e.g. parallel
       versions) using #SoPlex#.
    
       \paragraph{Virtualizing the Representation}\strut\\
       The primal Simplex on the columnwise representation is structurely
       equivalent to the dual Simplex on the rowwise representation and vice versa
       (see above). Hence, it is desirable to treate both cases in a very similar
       manner. This is supported by the programmers interface of #Soplex#, that
       provides access methods for all internal data in two ways: one is relative
       to the ``physical'' representation of the LP in rows and columns, while the
       other is relative to the chosen basis \Ref{Representation}. If e.g. a
       \Ref{SPxPricer} is written using the second type of methods only (which will
       generally be the case), the same code can be used for running #SoPlex#'s the
       Simplex algorithm for both \Ref{Representation}s. We will now give two
       examples for this virtualization from the chosen representation.
    
       Methods #vector# will return a column or a row vector,
       corresponding to the chosen basis representation. The other ``vectors'' will
       be referred to as #covector#s:
       \begin{center}
       \begin{tabular}{lcc}
                     & ROW       & COLUMN  \\
       \hline
           vector    & rowVector & colVector       \\
           coVector  & colVector & rowVector       \\
       \end{tabular}
       \end{center}
    
       Wheather the \Ref{SPxBasis::Desc::Status} of a variable indicates that the
       corresponding #vector# is in the basis matrix or not also depends on the
       chosen representation. Hence, methods #isBasic# are provided to get the
       correct answer for both representations.
    */
   //@{
   /// dimension of basis matrix.
   int dim() const
   {
      return thecovectors->num();
   }

   /// codimension.
   int coDim() const
   {
      return thevectors->num();
   }

   /// number of row #id#.
   int number(SPxRowId id) const
   {
      return SPxLP::number(id);
   }
   /// number of column #id#.
   int number(SPxColId id) const
   {
      return SPxLP::number(id);
   }
   /// number of column #id#.
   int number(Id id) const
   {
      return SPxLP::number(id);
   }

   /**@name Variables and Covariables
       Class \Ref{SPxLP} introduces #Id#s to identify row or column data of
       an LP. #SoPlex# uses this concept to access data with respect to the
       chosen representation.
    */
   //@{
   /** id of #i#-th vector.
       The #i#-th #id# is the #i#-th #SPxRowId# for a rowwise and the
       #i#-th #SPxColId# for a columnwise basis represenation. Hence,
       #0 <= i < coDim()#.
    */
   Id id(int i) const
   {
      if (rep() == ROW)
      {
         SPxLP::SPxRowId rid = SPxLP::rId(i);
         return Id(rid);
      }
      else
      {
         SPxLP::SPxColId cid = SPxLP::cId(i);
         return Id(cid);
      }
   }

   /** id of #i#-th covector.
       The #i#-th #coId# is the #i#-th #SPxColId# for a rowwise and the
       #i#-th #SPxRowId# for a columnwise basis represenation. Hence,
       #0 <= i < dim()#.
    */
   Id coId(int i) const
   {
      if (rep() == ROW)
      {
         SPxLP::SPxColId cid = SPxLP::cId(i);
         return Id(cid);
      }
      else
      {
         SPxLP::SPxRowId rid = SPxLP::rId(i);
         return Id(rid);
      }
   }

   /** Is #id# an Id.
       This method returns wheather or not #id# identifies a vector
       with respect to the chosen representation.
    */
   int isId(SPxLP::Id id) const
   {
      return id.info * therep > 0;
   }

   /** Is #id# a CoId.
       This method returns wheather or not #id# identifies a coVector
       with respect to the chosen representation.
    */
   int isCoId(SPxLP::Id id) const
   {
      return id.info * therep < 0;
   }
   //@}



   /**@name Vectors and Covectors */
   //@{
protected:
   Array < UnitVector > unitVecs;               // array of unit vectors
   const SVSet* thevectors;
   const SVSet* thecovectors;

   int nNZEs;          // number of nonzero elements
   int coVecDim;
   Array < Array < SubSVector > > subcovectors;
   int sortLP (int pe, int nPes);
   void splitLP(int pe, int nPes);
   virtual void splitLP();

public:
   /** $i$-th vector.
       Returns a reference to the #i#-th, #0 <= i < coDim()#, vector of
       the loaded LP (with respect to the chosen representation).
    */
   const SVector& vector(int i) const
   {
      return (*thevectors)[i];
   }

   ///
   const SVector& vector(const SPxLP::SPxRowId& rid) const
   {
      assert(rid.isValid());
      return (rep() == ROW)
             ? (*thevectors)[number(rid)]
          : *(const SVector*)&(unitVecs[number(rid)]);
   }

   ///
   const SVector& vector(const SPxLP::SPxColId& cid) const
   {
      assert(cid.isValid());
      return (rep() == COLUMN)
             ? (*thevectors)[number(cid)]
          : *(const SVector*)&(unitVecs[number(cid)]);
   }

   /** vector associated to #id#.
       Returns a reference to the vector of the loaded LP corresponding
       to #id# (with respect to the chosen representation). If #id# is
       an id, a vector of the constraint matrix is returned, otherwise
       the corresponding unit vector (of the slack variable or bound
       inequality) is returned.
    */
   const SVector& vector(const Id& id) const
   {
      assert(id.isValid());
      return id.isSPxRowId()
             ? vector(SPxLP::SPxRowId(id))
          : vector(SPxLP::SPxColId(id));
   }

   /** $i$-th covector of LP.
       Returns a reference to the #i#-th, #0 <= i < dim()#, covector of
       the loaded LP (with respect to the chosen representation).
    */
   const SVector& coVector(int i) const
   {
      return (*thecovectors)[i];
   }

   ///
   const SVector& coVector(const SPxLP::SPxRowId& rid) const
   {
      assert(rid.isValid());
      return (rep() == COLUMN)
             ? (*thecovectors)[number(rid)]
          : *(const SVector*)&(unitVecs[number(rid)]);
   }

   ///
   const SVector& coVector(const SPxLP::SPxColId& cid) const
   {
      assert(cid.isValid());
      return (rep() == ROW)
             ? (*thecovectors)[number(cid)]
          : *(const SVector*)&(unitVecs[number(cid)]);
   }

   /** coVector associated to #id#.
       Returns a reference to the covector of the loaded LP
       corresponding to #id# (with respect to the chosen
       representation). If #id# is a coid, a covector of the constraint
       matrix is returned, otherwise the corresponding unit vector is
       returned.
    */
   const SVector& coVector(const Id& id) const
   {
      assert(id.isValid());
      return id.isSPxRowId()
             ? coVector(SPxLP::SPxRowId(id))
          : coVector(SPxLP::SPxColId(id));
   }
   /// return #i#-th unit vector.
   const SVector& unitVector(int i) const
   {
      return unitVecs[i];
   }
   //@}


   /**@name Variable status
       The Simplex basis assigns a \Ref{SPxBasis::Desc::Status} to each
       variable and covariable. Depending on the representation, the status
       indicates that the corresponding vector is in the basis matrix or
       not.
    */
   //@{
   /// #Status# of #i#-th variable.
   SPxBasis::Desc::Status status(int i);

   /// #Status# of #i#-th covariable.
   SPxBasis::Desc::Status coStatus(int i);

   /// does #stat# describe a basic index?.
   int isBasic(SPxBasis::Desc::Status stat) const
   {
      return (stat * rep() > 0);
   }

   /// is $id$-th vector basic?.
   int isBasic(SPxLP::Id id) const
   {
      assert(id.isValid());
      return id.isSPxRowId()
             ? isBasic(SPxLP::SPxRowId(id))
          : isBasic(SPxLP::SPxColId(id));
   }

   /// is $rid$-th vector basic?.
   int isBasic(SPxLP::SPxRowId rid) const
   {
      return isBasic(desc().rowStatus(number(rid)));
   }

   /// is $cid$-th vector basic?.
   int isBasic(SPxLP::SPxColId cid) const
   {
      return isBasic(desc().colStatus(number(cid)));
   }

   /// is $i$-th row vector basic?.
   int isRowBasic(int i) const
   {
      return isBasic(desc().rowStatus(i));
   }

   /// is $i$-th column vector basic?.
   int isColBasic(int i) const
   {
      return isBasic(desc().colStatus(i));
   }

   /// is $i$-th vector basic?.
   int isBasic(int i) const
   {
      return isBasic(desc().status(i));
   }

   /// is $i$-th covector basic?.
   int isCoBasic(int i) const
   {
      return isBasic(desc().coStatus(i));
   }

   SPxBasis::dualRowStatus;
   SPxBasis::dualColStatus;
   //@}

   /**@name Simplex Vectors and Bounds
       The Simplex algorithms keeps three vectors, that are defined to a basis.
       Two of them are required for the pricing, while the other is needed for
       detecting feasibility of the basis. For all three vectors, bounds are
       defined. The Simplex alogrithm changes the basis until all three vectors
       satisfy their bounds. In this case the optimal solution is found.

       Whith each update of the basis, also the three vectors need to be
       updated. This is best supported by the use of \Ref{UpdateVector}s.
    */
   //@{
protected:
   /* \SubSection{Variables}
       The Simplex algorithm works with two types of variables, primals and
       duals.  The primal variables are the ones associated to each column of
       an LP, whereas the dual variables are the ones associated to each row.
       However, to each row a slack variable must be added to the set of
       primals (to represent inequalities), and a reduced cost variable must be
       added for each column (to represent upper or lower bounds). Note, that
       mathematically, on dual variable for each bound (upper and lower) should
       be added. However, this variable would always yield the same value and
       can, hence, be implemented as one.

       To summarize, we have a primal variable for each LP column and row
       (i.e.~its slack) as well as a dual variable for each LP row and column
       (i.e.~its bounds). However, not all these values need to be stored and
       computed, since the structure of the Simplex algorithms allow to
       implicitely keep them.
       
       If the #SPxBasis#'s #Status# of a row or column is one of #P_ON_LOWER#,
       #P_ON_UPPER#, #P_FIXED# or #P_FREE# the value of the corresponding
       primal variable is the lower, upper or both bound(s) or 0, respectively.
       The corresponding dual variable needs to be computed. Equivalently, for
       a #Status# of #D_FREE#, #D_ON_UPPER#, #D_ON_LOWER#, #D_ON_BOTH# or
       #D_UNDEFINED# the corresponding dual variable is 0, whereas the primal one
       needs to be computed.

       We declare the following vectors for holding the values to be computed.
    */
   // Primal variables (dimension nCols()):
   DVector primRhs;        // rhs vector for computing the primal vector
   UpdateVector primVec;        // primal vector

   // Dual variables (dimension nRows()):
   DVector dualRhs;        // rhs vector for computing the dual vector
   UpdateVector dualVec;        // dual vector

   // Additional variables depending on representation (dimension coDim()):
   UpdateVector addVec;         // additional vector


   /*      \SubSection{Bounds}
       Dual and primal variables are bounded (including $\pm\infty$ as bounds).
       If all primal variables are within their bounds, the Simplex basis is
       said to be primal feasible. Analogously, if all dual variables are
       within their bounds, its is called dual feasible.  If a basis is both,
       primal and dual feasible, the optimal solution is been found.

       In the dual Simplex, the basis is maintained to be dual, while primal
       feasibility is improved via basis updates. However, for numerical
       reasons dual feasibility must from time to time be relaxed.
       Equivalently, primal feasibility will be relaxed to retain numerical
       stability in the primal Simplex algorithm.

       Relaxation of (dual or primal) feasibility is acchieved by enlarging the
       bounds to primal or dual variables. However, for each type of Simplex
       only the corresponding bounds need to be enlarged. Hence, we define
       only one vector of upper and lower bound for each row and column and
       initialize it with primal or dual bound, depending on the Simplex type.
    */
   DVector theURbound;             // Upper Row    Feasibility bound
   DVector theLRbound;             // Lower Row    Feasibility bound
   DVector theUCbound;             // Upper Column Feasibility bound
   DVector theLCbound;             // Lower Column Feasibility bound


   /*
       In entering Simplex algorithm, the ratio test must ensure that all {\em
       basic variables} remain within their feasibility bounds. To give fast
       acces to them, the bounds of basic variables are copied into the
       following two vectors.
    */
   DVector theUBbound;             // Upper Basic  Feasibility bound
   DVector theLBbound;             // Lower Basic  Feasibility bound

   DVector* theFrhs;
   UpdateVector* theFvec;

   DVector* theCoPrhs;
   UpdateVector* theCoPvec;
   UpdateVector* thePvec;

   UpdateVector* theRPvec;       // row    pricing vector
   // set on #theCoPvec# or #thePvec#
   UpdateVector* theCPvec;       // column pricing vector
   // set on #thePvec# or #theCoPvec#

   /*  The following vectors serve for the virtualization of shift bounds
    */
   DVector* theUbound;      // Upper bound for vars
   DVector* theLbound;      // Lower bound for vars
   DVector* theCoUbound;    // Upper bound for covars
   //
   DVector* theCoLbound;    // Lower bound for covars
   //

   /*  The following vectors serve for the virtualization of testing vectors
    */
   DVector theCoTest;
   DVector theTest;

public:
   /** feasibility vector.
       This method return the {\em feasibility vector}. If it satisfies its
       bound, the basis is called feasible (independently of the chosen
       representation). The feasibility vector has dimenstin #dim()#.

       For the entering Simplex, #fVec# is kept within its bounds. In
       contrast to this, the pricing of the leaving Simplex selects an
       element of #fVec#, that violates its bounds.
    */
   UpdateVector& fVec() const
   {
      return *theFvec;
   }

   /** right-hand side vector for #fVec#.
       The feasibility vector is computed by solving a linear system with the
       basis matrix. The right-hand side vector of this system is refferd to as
       {\em feasibility right-hand side vector} #fRhs()#.

       For a row basis, #fRhs()# is the objective vector (ignoring shifts).
       For a column basis, it is the sum of all nonbasic vectors scaled by
       the factor of their bound.
    */
   const Vector& fRhs() const
   {
      return *theFrhs;
   }

   ///
   const Vector& ubBound() const
   {
      return theUBbound;
   }
   /** upper bound for #fVec#.
       This method returns the upper bound for the feasibility vector.
       It may only be called for the #ENTER#ing Simplex.
       
       For the #ENTER#ing Simplex algorithms, the feasibility vector is
       maintained to fullfill its bounds. As #fVec# itself, also its
       bounds depend on the chosen representation. Furhter, they may
       need to be shifted (see below).
    */
   Vector& ubBound()
   {
      return theUBbound;
   }

   ///
   const Vector& lbBound() const
   {
      return theLBbound;
   }
   /** lower bound for #fVec#.
       This method returns the lower bound for the feasibility vector.
       It may only be called for the #ENTER#ing Simplex.

       For the #ENTER#ing Simplex algorithms, the feasibility vector is
       maintained to fullfill its bounds. As #fVec# itself, also its
       bound depend on the chosen representation. Further, they may
       need to be shifted (see below).
    */
   Vector& lbBound()
   {
      return theLBbound;
   }

   /** Violations of #fVec#.
       For the leaving Simplex algorithm, pricing involves selecting a
       variable from #fVec# that violates its bounds that is to leave
       the basis. When a \Ref{SPxPricer} is called to select such a
       leaving variable, #fTest# contains the vector of violations:
       For #fTest()[i] < 0#, the #i#-th basic variable violates one of
       its bounds by the given value. Otherwise no bound is violated.
    */
   const Vector& fTest() const
   {
      assert(type() == LEAVE);
      return theCoTest;
   }

   /** copricing vector.
       The copricing vector #coPvec# along with the pricing vector
       #pVec# are used for pricing in the #ENTER#ing Simplex algorithm,
       i.e. one variable is selected, that violates its bounds. In
       contrast to this, the #LEAVE#ing Simplex algorithm keeps both
       vectors within their bounds.
    */
   UpdateVector& coPvec() const
   {
      return *theCoPvec;
   }

   /** Right-hand side vector for #coPvec#.
       Vector #coPvec# is computed by solving a linear system with the
       basis matrix and #coPrhs# as the right-hand side vector. For
       column basis representation, #coPrhs# is build up of the
       objective vector elements of all basic variables. For a row
       basis, it consists of the thight bounds of all basic
       constraints.
    */
   const Vector& coPrhs() const
   {
      return *theCoPrhs;
   }

   ///
   const Vector& ucBound() const
   {
      assert(theType == LEAVE);
      return *theCoUbound;
   }
   /** upper bound for #coPvec#.
       This method returns the upper bound for #coPvec#. It may only be
       called for the leaving Simplex algorithm.

       For the leaving Simplex algorithms #coPvec# is maintained to
       fullfill its bounds. As #coPvec# itself, also its bound depend
       on the chosen representation. Further, they may need to be
       shifted (see below).
    */
   Vector& ucBound()
   {
      assert(theType == LEAVE);
      return *theCoUbound;
   }

   ///
   const Vector& lcBound() const
   {
      assert(theType == LEAVE);
      return *theCoLbound;
   }
   /** lower bound for #coPvec#.
       This method returns the lower bound for #coPvec#. It may only be
       called for the leaving Simplex algorithm.

       For the leaving Simplex algorithms #coPvec# is maintained to
       fullfill its bounds. As #coPvec# itself, also its bound depend
       on the chosen representation. Further, they may need to be
       shifted (see below).
    */
   Vector& lcBound()
   {
      assert(theType == LEAVE);
      return *theCoLbound;
   }

   /** violations of #coPvec#.
       In entering Simplex pricing selects checks vectors #coPvec()#
       and #pVec()# for violation of its bounds. #coTest()# contains
       the violations for #coPvec()# which are indicated by a negative
       value. I.e.~if #coTest(i) < 0#, the #i#-th element of #coPvec()#
       is violated by #-coTest(i)#.
    */
   const Vector& coTest() const
   {
      assert(type() == ENTER);
      return theCoTest;
   }


   /** pricing vector.
       The pricing vector #pVec# is the product of #coPvec# with the
       constraint matrix. As #coPvec#, also #pVec# is maintained within
       its bound for the leaving Simplex algorithm, while the bounds
       are tested for the entering Simplex. #pVec# is of dimension
       #dim()#. Vector #pVec()# is only up to date for #LEAVE#ing
       Simplex or #FULL# pricing in #ENTER#ing Simplex.
    */
   UpdateVector& pVec() const
   {
      return *thePvec;
   }

   ///
   const Vector& upBound() const
   {
      assert(theType == LEAVE);
      return *theUbound;
   }
   /** upper bound for #pVec#.
       This method returns the upper bound for #pVec#. It may only be
       called for the leaving Simplex algorithm.

       For the leaving Simplex algorithms #pVec# is maintained to
       fullfill its bounds. As #pVec# itself, also its bound depend
       on the chosen representation. Further, they may need to be
       shifted (see below).
    */
   Vector& upBound()
   {
      assert(theType == LEAVE);
      return *theUbound;
   }

   ///
   const Vector& lpBound() const
   {
      assert(theType == LEAVE);
      return *theLbound;
   }
   /** lower bound for #pVec#.
       This method returns the lower bound for #pVec#. It may only be
       called for the leaving Simplex algorithm.

       For the leaving Simplex algorithms #pVec# is maintained to
       fullfill its bounds. As #pVec# itself, also its bound depend
       on the chosen representation. Further, they may need to be
       shifted (see below).
    */
   Vector& lpBound()
   {
      assert(theType == LEAVE);
      return *theLbound;
   }

   /** Violations of #pVec#.
       In entering Simplex pricing selects checks vectors #coPvec()#
       and #pVec()# for violation of its bounds. Vector #test()#
       contains the violations for #pVec()#, i.e.~if #test(i) < 0#,
       the #i#-th element of #pVec()# is violated by #-test(i)#.
       Vector #test()# is only up to date for #FULL# pricing.
    */
   const Vector& test() const
   {
      assert(type() == ENTER);
      return theTest;
   }

   /// compute and return #pVec(i)#.
   double computePvec(int i);
   /// compute entire #pVec()#.
   void computePvec();
   /// compute and return #test(i)# in #ENTER#ing Simplex.
   double computeTest(int i);
   /// compute test vector in #ENTER#ing Simplex.
   void computeTest();
   //@}



   /**@name Shifting
       The task of the ratio test (implemented in \Ref{SPxRatioTester} classes)
       is to select a variable for the basis update, such that the basis
       remains priced (i.e. both, the pricing and copricing vectors satisfy
       their bounds) or feasible (i.e. the feasibility vector satisfies its
       bounds). However, this can lead to numerically instable basis matrices
       or --- after accumulation of various errors --- even to a singular basis
       matrix.

       The key to overcome this problem is to allow the basis to become ``a
       bit'' infeasible or unpriced, in order provide a better choice for the
       ratio test to select a stable variable. This is equivalent to enlarging
       the bounds by a small amount. This is referred to as {\em shifting}.

       The following methods are used to shift individual bounds. They are
       mainly intended for stable implenentations of \Ref{SPxRatioTester}.
    */
   //@{
private:
   /*
       These methods serve for shifting feasibility bounds, either in order
       to maintain numerical stability or initially for computation of
       phase 1. The sum of all shifts applied to any bound is stored in
       #theShift#.
    */
   double theShift;                       // shift of r/lhs or objective
   // for forcing feasibility
   double lastShift;

protected:
   int leaveCount;             // number of LEAVE iterations
   int enterCount;             // number of ENTER iterations

public:
   /*  These method perform an initial shifting to optaion an feasible or
       pricable basis.
    */
   void shiftFvec();
   void shiftPvec();

   /// shift #i#-th #ubBound# to #to#.
   void shiftUBbound(int i, double to)
   {
      assert(theType == ENTER);
      theShift += to - theUBbound[i];
      theUBbound[i] = to;
   }
   /// shift #i#-th #lbBound# to #to#.
   void shiftLBbound(int i, double to)
   {
      assert(theType == ENTER);
      theShift += theLBbound[i] - to;
      theLBbound[i] = to;
   }
   /// shift #i#-th #upBound# to #to#.
   void shiftUPbound(int i, double to)
   {
      assert(theType == LEAVE);
      theShift += to - (*theUbound)[i];
      (*theUbound)[i] = to;
   }
   /// shift #i#-th #lpBound# to #to#.
   void shiftLPbound(int i, double to)
   {
      assert(theType == LEAVE);
      theShift += (*theLbound)[i] - to;
      (*theLbound)[i] = to;
   }
   /// shift #i#-th #ucBound# to #to#.
   void shiftUCbound(int i, double to)
   {
      assert(theType == LEAVE);
      theShift += to - (*theCoUbound)[i];
      (*theCoUbound)[i] = to;
   }
   /// shift #i#-th #lcBound# to #to#.
   void shiftLCbound(int i, double to)
   {
      assert(theType == LEAVE);
      theShift += (*theCoLbound)[i] - to;
      (*theCoLbound)[i] = to;
   }
   void testBounds() const;

   /// total current shift amount.
   virtual double shift() const
   {
      return theShift;
   }

   /// remove shift as much as possible.
   virtual void unShift(void);

private:
   void perturbMin(const UpdateVector& vec, Vector& low, Vector& up, double eps,
                    int start = 0, int incr = 1);

   void perturbMax(const UpdateVector& vec, Vector& low, Vector& up, double eps,
                    int start = 0, int incr = 1);

   double perturbMin(const UpdateVector& uvec,
                      Vector& low,
                      Vector& up,
                      double eps,
                      double delta,
                      const SPxBasis::Desc::Status* stat,
                      int start,
                      int incr);

   double perturbMax(const UpdateVector& uvec,
                      Vector& low,
                      Vector& up,
                      double eps,
                      double delta,
                      const SPxBasis::Desc::Status* stat,
                      int start,
                      int incr);
public:
   //@}


   /**@name The Simplex Loop
       We now present a set of methods that may be usefull when implementing
       own \Ref{SPxPricer} or \Ref{SPxRatioTester} classes. Here is, how
       #SoPlex# will call methods from its loaded \Ref{SPxPricer} end
       \Ref{SPxRatioTester}.
       
       For the entering Simplex:
       \begin{enumerate}
       \item   #SPxPricer::selectEnter()#
       \item   #SPxRatioTester::selectLeave()#
       \item   #SPxPricer::entered4()#
       \end{enumerate}
       
       For the leaving Simplex:
       \begin{enumerate}
       \item   #SPxPricer::selectLeave()#
       \item   #SPxRatioTester::selectEnter()#
       \item   #SPxPricer::left4()#
       \end{enumerate}
    */
   //@{
   /**@name maximal infeasibility of basis
       This method is called for prooving optimality. Since it is
       possible, that some stable implementation of class
       \Ref{SPxRatioTester} yielded a slightly infeasible (or unpriced)
       basis, this must be checked before terminating with an optimal
       solution.
    */
   virtual double maxInfeas() const;

   /** Return current basis.
       Note, that the basis can be used to solve linear systems or use
       any other of its (#const#) methods.  It is, however, encuraged
       to use methods #setup4solve()# and #setup4coSolve()# for solving
       systems, since this is likely to have perfomance advantages.
    */
   const SPxBasis& basis() const
   {
      return *this;
   }
   /// return loaded \Ref{SPxPricer}.
   const SPxPricer* pricer() const
   {
      return thepricer;
   }
   /// return loaded \Ref{SLinSolver}.
   const SLinSolver* slinSolver() const
   {
      return SPxBasis::factor;
   }
   /// return loaded \Ref{SPxRatioTester}.
   const SPxRatioTester* ratiotester() const
   {
      return theratiotester;
   }
   /// return loaded \Ref{SPxStarter}.
   const SPxStarter* starter() const
   {
      return thestarter;
   }
   /// return loaded \Ref{SPxSimplifier}.
   const SPxSimplifier* simplifier() const
   {
      return thesimplifier;
   }

protected:
   /// Factorize basis matrix.
   virtual void factorize();

private:
   ///
   SPxPricer* thepricer;
   ///
   SPxRatioTester* theratiotester;
   ///
   SPxStarter* thestarter;
   ///
   SPxSimplifier* thesimplifier;

   int leave(int i);
   int enter(Id& id);

   /// test coVector #i# with status #stat#.
   double coTest(int, SPxBasis::Desc::Status) const;
   /// compute coTest vector.
   void computeCoTest();
   /// recompute coTest vector.
   void updateCoTest();

   /// test vector #i# with status #stat#.
   double test(int i, SPxBasis::Desc::Status stat) const;
   /// recompute test vector.
   void updateTest();

   /// compute basis feasibility test vector.
   void computeFtest();
   /// update basis feasibility test vector.
   void updateFtest();

   Vector* solveVector2;           // when 2 systems are to be solved at a time
   SSVector* solveVector2rhs;      // when 2 systems are to be solved at a time
   Vector* coSolveVector2;         // when 2 systems are to be solved at a time
   SSVector* coSolveVector2rhs;    // when 2 systems are to be solved at a time

public:
   /** Setup vectors to be solved within Simplex loop.
    *  Load vector #y# to be #solve#d with the basis matrix during the
    *  #LEAVE# Simplex. The system will be solved after #SoPlex#'s call
    *  to \Ref{SPxRatioTester}.  The system will be solved along with
    *  another system. Solving two linear system at a time has
    *  performance advantages over solving the two linear systems
    *  seperately.
    */
   void setup4solve(Vector* y, SSVector* rhs) const
   {
      assert(type() == LEAVE);
      ((SoPlex*)this)->solveVector2 = y;
      ((SoPlex*)this)->solveVector2rhs = rhs;
   }
   /** Setup vectors to be cosolved within Simplex loop.
    *  Load vector #y# to be #coSolve#d with the basis matrix during
    *  the #ENTER# Simplex. The system will be solved after #SoPlex#'s
    *  call to \Ref{SPxRatioTester}.  The system will be solved along
    *  with another system. Solving two linear system at a time has
    *  performance advantages over solving the two linear systems
    *  seperately.
    */
   void setup4coSolve(Vector* y, SSVector* rhs) const
   {
      assert(type() == ENTER);
      ((SoPlex*)this)->coSolveVector2 = y;
      ((SoPlex*)this)->coSolveVector2rhs = rhs;
   }
   //@}

protected:
   /**@name Parallelization
       In this section we present the methods, that are provided in order to
       allow a parallel version to be implemented as a derived class, thereby
       inheriting most of the code of #SoPlex#.
    */
   //@{
   /**@name Initialization
       These methods are used to setup all the vectors used in the Simplex
       loop, that where described in the previous sectios.
    */
   //@{
private:
   int initialized;    // 1, if all vectors are setup 0 otherwise

protected:
   /** Has the internal data been initialized.
       As long as an instance of #SoPlex# is not #initialized#, no member
       contains setup data. Initialization is performed via method
       #init()#.  Afterwards all data structures are kept up to date (even
       for all manipulation methods), until #unInit()# is called. However,
       some manipulation methods call #unInit()# themselfs.
    */
   int isInitialized() const
   {
      return initialized;
   }
   /** Intialize data structures.
       If #SoPlex# is not #isInitialized()#, method solve calls
       #init()# to setup all vectors and internal data structures.
       Most of the other methods within this section are called by
       #init()#.

       Derived classes should add the initialization of additional
       data structures by overriding this method. Don't forget,
       however to call #SoPlex::init()#.
    */
public:
   virtual void init();
protected:
   /// unintialize data structures.
   virtual void unInit()
   {
      initialized = 0;
   }

   /// reset dimensions of vectors according to loaded LP.
   virtual void reDim();


   /// compute feasibility vector from scratch.
   void computeFrhs();
   ///
   virtual void computeFrhsXtra();
   ///
   virtual void computeFrhs1(const Vector&, const Vector&);
   ///
   void computeFrhs2(const Vector&, const Vector&);

   /// compute #theCoPrhs# for entering Simplex.
   virtual void computeEnterCoPrhs();
   void computeEnterCoPrhs4Row(int i, int n);
   void computeEnterCoPrhs4Col(int i, int n);

   /// compute #theCoPrhs# for leaving Simplex.
   virtual void computeLeaveCoPrhs();
   void computeLeaveCoPrhs4Row(int i, int n);
   void computeLeaveCoPrhs4Col(int i, int n);
   //@}

   /** Compute part of objective value.
       This method is called from #value()# in order to compute the part of
       the objective value resulting form nonbasic variables for #COLUMN#
       \Ref{Representation}.
    */
   double nonbasicValue() const;

   /// Get pointer to the #id#'th vector
   virtual const SVector* enterVector(const Id& id)
   {
      assert(id.isValid());
      return id.isSPxRowId()
             ? &vector(SPxRowId(id))
          : &vector(SPxColId(id));
   }

   virtual void getLeaveVals(int i,
                              SPxBasis::Desc::Status& leaveStat,
                              Id& leaveId,
                              double& leaveMax,
                              double& leavebound,
                              int& leaveNum);
   virtual void getLeaveVals2(double leaveMax,
                              Id enterId,
                              double& enterBound,
                              double& newUBbound,
                              double& newLBbound,
                              double& newCoPrhs);

   virtual void getEnterVals(Id id,
                              double& enterTest,
                              double& enterUB,
                              double& enterLB,
                              double& enterVal,
                              double& enterMax,
                              double& enterPric,
                              SPxBasis::Desc::Status& enterStat,
                              double& enterRO);
   virtual void getEnterVals2(int leaveIdx,
                              double enterMax,
                              double& leaveBound);
   virtual void ungetEnterVal(Id enterId,
                              SPxBasis::Desc::Status enterStat,
                              double leaveVal,
                              const SVector& vec);

   virtual void rejectEnter(Id enterId,
                            double enterTest,
                            SPxBasis::Desc::Status enterStat);
   virtual void rejectLeave(int leaveNum,
                             Id leaveId,
                             SPxBasis::Desc::Status leaveStat,
                             const SVector* newVec = 0);

   ///
   virtual void setupPupdate(void);
   ///
   virtual void doPupdate(void);
   ///
   virtual void clearUpdateVecs(void);

   ///
   virtual void perturbMinEnter(void);
   /// perturb basis bounds.
   virtual void perturbMaxEnter(void);

   ///
   virtual void perturbMinLeave(void);
   /// perturb nonbasic bounds.
   virtual void perturbMaxLeave(void);

   /*
       The following methods serve for initializing the bounds for dual or
       primal Simplex algorithm of entering or leaving type.
    */
   void clearDualBounds(SPxBasis::Desc::Status, double&, double&);

   void setDualColBounds();
   void setDualRowBounds();
   void setPrimalBounds();

   void setEnterBound4Col(int, int);
   void setEnterBound4Row(int, int);
   virtual void setEnterBounds();

   void setLeaveBound4Row(int i, int n);
   void setLeaveBound4Col(int i, int n);
   virtual void setLeaveBounds();
   //@}
   //@}


private:
   int maxIters;
   double maxTime;

public:
   /**@name Derived from #LPSolver# */
   //@{
   /// adjust conditions for termination.
   void setTermination(double value = LPSolver::infinity,
                       double time = -1 ,
                       int iteration = -1);

   /// get adjusted conditions for termination.
   virtual void getTermination(double* value = 0 ,
                               double* time = 0 ,
                               int* iteration = 0) const;

   /// get objective value of current solution.
   double objValue() const
   {
      return value();
   }

   /// get all results of last solve.
   LPSolver::Status getResult(double* value = 0,
                              Vector* primal = 0,
                              Vector* slacks = 0,
                              Vector* dual = 0,
                              Vector* reduCost = 0) const;

   /// set #LPSolver#s basis.
   void setBasis(const signed char rows[],
                 const signed char cols[]);

   /// get current basis.
   LPSolver::Status getBasis(signed char rows[],
                             signed char cols[]) const;

   /// get number of iterations of current solution.
   int iterations() const
   {
      return basis().iteration();
   }

   /// time spent in last call to method #solve()#.
   double time() const
   {
      return theTime.userTime();
   }

   ///
   void addRow(const LPRow& row)
   {
      SPxLP::addRow(row);
   }
   /// add #row# to #LPSolver#s LP.
   void addRow(LPSolver::RowId& id, const LPRow& row)
   {
      SPxLP::addRow(*(SPxRowId*)&id, row);
   }

   ///
   void addRows(const LPRowSet& set)
   {
      SPxLP::addRows(set);
   }
   /// add all #LPRow#s of #set# to #LPSolver#s LP.
   void addRows(LPSolver::RowId id[], const LPRowSet& set)
   {
      SPxLP::addRows((SPxRowId*)id, set);
   }

   ///
   void addCol(const LPCol& col)
   {
      SPxLP::addCol(col);
   }
   /// add #col# to #LPSolver#s LP.
   void addCol(LPSolver::ColId& id, const LPCol& col)
   {
      SPxLP::addCol(*(SPxColId*)&id, col);
   }

   ///
   void addCols(const LPColSet& set)
   {
      SPxLP::addCols(set);
   }
   /// add all #LPCol#s of #set# to #LPSolver#s LP.
   void addCols(LPSolver::ColId id[], const LPColSet& set)
   {
      SPxLP::addCols((SPxColId*)id, set);
   }


   /// remove #i#-th row.
   void removeRow(int i)
   {
      SPxLP::removeRow(i);
   }
   /// remove row with #RowId id#.
   void removeRow(LPSolver::RowId id)
   {
      SPxLP::removeRow(*(SPxRowId*)&id);
   }

   /// remove #i#-th column.
   void removeCol(int i)
   {
      SPxLP::removeCol(i);
   }
   /// remove column with #LPSolver::ColId id#.
   void removeCol(LPSolver::ColId id)
   {
      SPxLP::removeCol(*(SPxColId*)&id);
   }

   /// remove #n# rows.
   void removeRows(LPSolver::RowId id[], int n, int perm[] = 0)
   {
      SPxLP::removeRows((SPxRowId*)id, n, perm);
   }
   /// remove #n# rows.
   void removeRows(int nums[], int n, int perm[] = 0)
   {
      SPxLP::removeRows(nums, n, perm);
   }
   /// remove multiple rows.
   void removeRows(int perm[])
   {
      SPxLP::removeRows(perm);
   }
   /// remove rows from #start# to #end# (including both).
   void removeRowRange(int start, int end, int perm[] = 0)
   {
      SPxLP::removeRowRange(start, end, perm);
   }

   /// remove #n# columns.
   void removeCols(LPSolver::ColId id[], int n, int perm[] = 0)
   {
      SPxLP::removeCols((SPxColId*) & id, n, perm);
   }
   /// remove #n# columns.
   void removeCols(int nums[], int n, int perm[] = 0)
   {
      SPxLP::removeCols(nums, n, perm);
   }
   /// remove multiple columns.
   void removeCols(int perm[])
   {
      SPxLP::removeCols(perm);
   }
   /// remove columns from #start# to #end# (including both).
   void removeColRange(int start, int end, int perm[] = 0)
   {
      SPxLP::removeColRange(start, end, perm);
   }


   /// change objective value to variable with #ColId id#.
   void changeObj(LPSolver::ColId id, double newVal)
   {
      changeObj(*(SPxColId*)&id, newVal);
   }

   /// change lower bound of variable with #ColId id#.
   void changeLower(LPSolver::ColId id, double newLower)
   {
      changeLower(*(SPxColId*)&id, newLower);
   }

   /// change #id#-th upper bound.
   void changeUpper(LPSolver::ColId id, double newUpper)
   {
      changeUpper(*(SPxColId*)&id, newUpper);
   }

   /// change #id#-th lower and upper bound.
   void changeBounds(LPSolver::ColId id, double newLower, double newUpper)
   {
      changeBounds(*(SPxColId*)&id, newLower, newUpper);
   }

   /// change #id#-th lhs value.
   void changeLhs(LPSolver::RowId id, double newLhs)
   {
      changeLhs(*(SPxRowId*)&id, newLhs);
   }

   /// change #id#-th rhs value.
   void changeRhs(LPSolver::RowId id, double newRhs)
   {
      changeRhs(*(SPxRowId*)&id, newRhs);
   }

   /// change #id#-th lhs and rhs value.
   void changeRange(LPSolver::RowId id, double newLhs, double newRhs)
   {
      changeRange(*(SPxRowId*)&id, newLhs, newRhs);
   }

   /// change #id#-th row of LP.
   void changeRow(LPSolver::RowId id, const LPRow& newRow)
   {
      changeRow(*(SPxRowId*)&id, newRow);
   }

   /// change #id#-th column of LP.
   void changeCol(LPSolver::ColId id, const LPCol& newCol)
   {
      changeCol(*(SPxColId*)&id, newCol);
   }

   /// change LP element (#rid#, #cid#).
   void changeElement(LPSolver::RowId rid, LPSolver::ColId cid, double val)
   {
      changeElement(*(SPxRowId*)&rid, *(SPxColId*)&cid, val);
   }

   /// change optimization sense to #sns#.
   void changeSense(LPSolver::Sense sns)
   {
      changeSense(SPxSense(int(sns)));
   }


   /// get #i#-th row.
   void getRow(int i, LPRow& row) const
   {
      SPxLP::getRow(i, row);
   }

   /// get #id#-th row.
   void getRow(LPSolver::RowId id, LPRow& row) const
   {
      SPxLP::getRow(*(SPxRowId*)&id, row);
   }

   /// get rows #start# .. #end#.
   void getRows(int start, int end, LPRowSet& set) const
   {
      SPxLP::getRows(start, end, set);
   }

   /// return const #i#-th row if available.
   const SVector& rowVector(int i) const
   {
      return SPxLP::rowVector(i);
   }

   /// return const #id#-th row if available.
   const SVector& rowVector(LPSolver::RowId id) const
   {
      return SPxLP::rowVector(*(SPxRowId*)&id);
   }

   /// return const lp's rows if available.
   const LPRowSet& rows() const
   {
      return *lprowset();
   }

   /// get #i#-th column.
   void getCol(int i, LPCol& column) const
   {
      SPxLP::getCol(i, column);
   }

   /// get #id#-th column.
   void getCol(LPSolver::ColId id, LPCol& column) const
   {
      SPxLP::getCol(*(SPxColId*)&id, column);
   }

   /// get columns #start# .. #end#.
   void getCols(int start, int end, LPColSet& set) const
   {
      SPxLP::getCols(start, end, set);
   }

   /// return const #i#-th col if available.
   const SVector& colVector(int i) const
   {
      return SPxLP::colVector(i);
   }

   /// return const #id#-th col if available.
   const SVector& colVector(LPSolver::ColId id) const
   {
      return SPxLP::colVector(*(SPxColId*)&id);
   }

   /// return const lp's cols if available.
   const LPColSet& cols() const
   {
      return *lpcolset();
   }

   /// #i#-th value of objective vector.
   double obj(int i) const
   {
      return SPxLP::obj(i);
   }

   /// #id#-th value of objective vector.
   double obj(LPSolver::ColId id) const
   {
      return SPxLP::obj(*(SPxColId*)&id);
   }

   /// copy objective vector to #obj#.
   void getObj(Vector& obj) const
   {
      SPxLP::getObj(obj);
   }

   /// #i#-th lower bound.
   double lower(int i) const
   {
      return SPxLP::lower(i);
   }

   /// #id#-th lower bound.
   double lower(LPSolver::ColId id) const
   {
      return SPxLP::lower(*(SPxColId*)&id);
   }

   /// copy lower bound vector to #low#.
   void getLower(Vector& lw) const
   {
      lw = SPxLP::lower();
   }

   /// return const lower bound vector.
   const Vector& lower() const
   {
      return SPxLP::lower();
   }


   /// #i#-th upper bound.
   double upper(int i) const
   {
      return SPxLP::upper(i);
   }

   /// #id#-th upper bound.
   double upper(LPSolver::ColId id) const
   {
      return SPxLP::upper(*(SPxColId*)&id);
   }

   /// copy upper bound vector to #up#.
   void getUpper(Vector& upp) const
   {
      upp = SPxLP::upper();
   }

   /// return const upper bound vector.
   const Vector& upper() const
   {
      return SPxLP::upper();
   }


   /// #i#-th lhs value.
   double lhs(int i) const
   {
      return SPxLP::lhs(i);
   }

   /// #id#-th lhs value.
   double lhs(LPSolver::RowId id) const
   {
      return SPxLP::lhs(*(SPxRowId*)&id);
   }

   /// copy lhs value vector to #lhs#.
   void getLhs(Vector& lhs) const
   {
      lhs = SPxLP::lhs();
   }

   /// return const lhs vector.
   const Vector& lhs() const
   {
      return SPxLP::lhs();
   }


   /// #i#-th rhs value.
   double rhs(int i) const
   {
      return SPxLP::rhs(i);
   }

   /// #id#-th rhs value.
   double rhs(LPSolver::RowId id) const
   {
      return SPxLP::rhs(*(SPxRowId*)&id);
   }

   /// copy rhs value vector to #rhs#.
   void getRhs(Vector& rhs) const
   {
      rhs = SPxLP::rhs();
   }

   /// return const rhs vector.
   const Vector& rhs() const
   {
      return SPxLP::rhs();
   }


   /// optimization sense.
   LPSolver::Sense sense() const
   {
      return LPSolver::Sense(spxSense());
   }


   /// number of rows of loaded LP.
   int nofRows() const
   {
      return nRows();
   }
   /// number of columns of loaded LP.
   int nofCols() const
   {
      return nCols();
   }
   /// number of nonzeros of loaded LP.
   int nofNZEs() const;

   /// #RowId# of #i#-th inequality.
   LPSolver::RowId rowId(int i) const
   {
      SPxRowId id = rId(i);
      return *(LPSolver::RowId*)&id;
   }
   /// #ColId# of #i#-th column.
   LPSolver::ColId colId(int i) const
   {
      SPxColId id = cId(i);
      return *(LPSolver::ColId*)&id;
   }

   /// number of row #id#.
   int number(LPSolver::RowId id) const
   {
      return number(*(SPxRowId*)&id);
   }
   /// number of column #id#.
   int number(LPSolver::ColId id) const
   {
      return number(*(SPxColId*)&id);
   }

   /// does #LPSolver# have row with #id#.
   int has(LPSolver::RowId id) const
   {
      return number(id) >= 0;
   }
   /// does #LPSolver# have column with #id#.
   int has(LPSolver::ColId id) const
   {
      return number(id) >= 0;
   }
   //@}

   /**@name Miscellaneous */
   //@{
   /// assignment operator.
   SoPlex& operator=(const SoPlex& base);

   /// copy constructor.
   SoPlex(const SoPlex& base);

   /// default constructor.
   SoPlex(Type type = LEAVE, Representation rep = ROW,
           SPxPricer* pric = 0, SPxRatioTester* rt = 0,
           SPxStarter* start = 0, SPxSimplifier* simple = 0
        );

   /// check consistency.
   int isConsistent() const;
   //@}

private:
   void testVecs();

   double cacheProductFactor;
};

} // namespace soplex
#endif // _SOPLEX_H_

//-----------------------------------------------------------------------------
//Emacs Local Variables:
//Emacs c-basic-offset:3
//Emacs tab-width:8
//Emacs indent-tabs-mode:nil
//Emacs End:
//-----------------------------------------------------------------------------
