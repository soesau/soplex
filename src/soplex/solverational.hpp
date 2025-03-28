/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*  Copyright 1996-2022 Zuse Institute Berlin                                */
/*                                                                           */
/*  Licensed under the Apache License, Version 2.0 (the "License");          */
/*  you may not use this file except in compliance with the License.         */
/*  You may obtain a copy of the License at                                  */
/*                                                                           */
/*      http://www.apache.org/licenses/LICENSE-2.0                           */
/*                                                                           */
/*  Unless required by applicable law or agreed to in writing, software      */
/*  distributed under the License is distributed on an "AS IS" BASIS,        */
/*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. */
/*  See the License for the specific language governing permissions and      */
/*  limitations under the License.                                           */
/*                                                                           */
/*  You should have received a copy of the Apache-2.0 license                */
/*  along with SoPlex; see the file LICENSE. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <iostream>
#include <assert.h>

#include "soplex.h"
#include "soplex/statistics.h"
#include "soplex/slufactor_rational.h"
#include "soplex/ratrecon.h"

namespace soplex
{

/// solves rational LP
template <class R>
void SoPlexBase<R>::_optimizeRational(volatile bool* interrupt)
{
#ifndef SOPLEX_WITH_BOOST
   MSG_ERROR(std::cerr << "ERROR: rational solve without Boost not defined!" << std::endl;)
   return;
#else
   bool hasUnboundedRay = false;
   bool infeasibilityNotCertified = false;
   bool unboundednessNotCertified = false;

   // start timing
   _statistics->solvingTime->start();
   _statistics->preprocessingTime->start();

   // remember that last solve was rational
   _lastSolveMode = SOLVEMODE_RATIONAL;

   // ensure that the solver has the original problemo
   if(!_isRealLPLoaded)
   {
      assert(_realLP != &_solver);

      _solver.loadLP(*_realLP);
      spx_free(_realLP);
      _realLP = &_solver;
      _isRealLPLoaded = true;
   }
   // during the rational solve, we always store basis information in the basis arrays
   else if(_hasBasis)
   {
      _basisStatusRows.reSize(numRows());
      _basisStatusCols.reSize(numCols());
      _solver.getBasis(_basisStatusRows.get_ptr(), _basisStatusCols.get_ptr(), _basisStatusRows.size(),
                       _basisStatusCols.size());
   }

   // store objective, bounds, and sides of Real LP in case they will be modified during iterative refinement
   _storeLPReal();

   // deactivate objective limit in floating-point solver
   if(realParam(SoPlexBase<R>::OBJLIMIT_LOWER) > -realParam(SoPlexBase<R>::INFTY)
         || realParam(SoPlexBase<R>::OBJLIMIT_UPPER) < realParam(SoPlexBase<R>::INFTY))
   {
      MSG_INFO2(spxout, spxout << "Deactivating objective limit.\n");
   }

   _solver.setTerminationValue(realParam(SoPlexBase<R>::INFTY));

   _statistics->preprocessingTime->stop();

   // apply lifting to reduce range of nonzero matrix coefficients
   if(boolParam(SoPlexBase<R>::LIFTING))
      _lift();

   // force column representation
   ///@todo implement row objectives with row representation
   int oldRepresentation = intParam(SoPlexBase<R>::REPRESENTATION);
   setIntParam(SoPlexBase<R>::REPRESENTATION, SoPlexBase<R>::REPRESENTATION_COLUMN);

   // force ratio test (avoid bound flipping)
   int oldRatiotester = intParam(SoPlexBase<R>::RATIOTESTER);
   setIntParam(SoPlexBase<R>::RATIOTESTER, SoPlexBase<R>::RATIOTESTER_FAST);

   ///@todo implement handling of row objectives in Cplex interface
#ifdef SOPLEX_WITH_CPX
   int oldEqtrans = boolParam(SoPlexBase<R>::EQTRANS);
   setBoolParam(SoPlexBase<R>::EQTRANS, true);
#endif

   // introduce slack variables to transform inequality constraints into equations
   if(boolParam(SoPlexBase<R>::EQTRANS))
      _transformEquality();

   _storedBasis = false;

   bool stoppedTime;
   bool stoppedIter;

   do
   {
      bool primalFeasible = false;
      bool dualFeasible = false;
      bool infeasible = false;
      bool unbounded = false;
      bool error = false;
      stoppedTime = false;
      stoppedIter = false;

      // solve problem with iterative refinement and recovery mechanism
      _performOptIRStable(_solRational, !unboundednessNotCertified, !infeasibilityNotCertified, 0,
                          primalFeasible, dualFeasible, infeasible, unbounded, stoppedTime, stoppedIter, error);

      // case: an unrecoverable error occured
      if(error)
      {
         _status = SPxSolverBase<R>::ERROR;
         break;
      }
      // case: stopped due to some limit
      else if(stoppedTime)
      {
         _status = SPxSolverBase<R>::ABORT_TIME;
         break;
      }
      else if(stoppedIter)
      {
         _status = SPxSolverBase<R>::ABORT_ITER;
         break;
      }
      // case: unboundedness detected for the first time
      else if(unbounded && !unboundednessNotCertified)
      {
         SolRational solUnbounded;

         _performUnboundedIRStable(solUnbounded, hasUnboundedRay, stoppedTime, stoppedIter, error);

         assert(!hasUnboundedRay || solUnbounded.hasPrimalRay());
         assert(!solUnbounded.hasPrimalRay() || hasUnboundedRay);

         if(error)
         {
            MSG_INFO1(spxout, spxout << "Error while testing for unboundedness.\n");
            _status = SPxSolverBase<R>::ERROR;
            break;
         }

         if(hasUnboundedRay)
         {
            MSG_INFO1(spxout, spxout << "Dual infeasible.  Primal unbounded ray available.\n");
         }
         else
         {
            MSG_INFO1(spxout, spxout << "Dual feasible.  Rejecting primal unboundedness.\n");
         }

         unboundednessNotCertified = !hasUnboundedRay;

         if(stoppedTime)
         {
            _status = SPxSolverBase<R>::ABORT_TIME;
            break;
         }
         else if(stoppedIter)
         {
            _status = SPxSolverBase<R>::ABORT_ITER;
            break;
         }

         _performFeasIRStable(_solRational, infeasible, stoppedTime, stoppedIter, error);

         ///@todo this should be stored already earlier, possible switch use solRational above and solFeas here
         if(hasUnboundedRay)
         {
            _solRational._primalRay = solUnbounded._primalRay;
            _solRational._hasPrimalRay = true;
         }

         if(error)
         {
            MSG_INFO1(spxout, spxout << "Error while testing for feasibility.\n");
            _status = SPxSolverBase<R>::ERROR;
            break;
         }
         else if(stoppedTime)
         {
            _status = SPxSolverBase<R>::ABORT_TIME;
            break;
         }
         else if(stoppedIter)
         {
            _status = SPxSolverBase<R>::ABORT_ITER;
            break;
         }
         else if(infeasible)
         {
            MSG_INFO1(spxout, spxout << "Primal infeasible.  Dual Farkas ray available.\n");
            _status = SPxSolverBase<R>::INFEASIBLE;
            break;
         }
         else if(hasUnboundedRay)
         {
            MSG_INFO1(spxout, spxout << "Primal feasible and unbounded.\n");
            _status = SPxSolverBase<R>::UNBOUNDED;
            break;
         }
         else
         {
            MSG_INFO1(spxout, spxout << "Primal feasible and bounded.\n");
            continue;
         }
      }
      // case: infeasibility detected
      else if(infeasible && !infeasibilityNotCertified)
      {
         _storeBasis();

         _performFeasIRStable(_solRational, infeasible, stoppedTime, stoppedIter, error);

         if(error)
         {
            MSG_INFO1(spxout, spxout << "Error while testing for infeasibility.\n");
            _status = SPxSolverBase<R>::ERROR;
            _restoreBasis();
            break;
         }

         infeasibilityNotCertified = !infeasible;

         if(stoppedTime)
         {
            _status = SPxSolverBase<R>::ABORT_TIME;
            _restoreBasis();
            break;
         }
         else if(stoppedIter)
         {
            _status = SPxSolverBase<R>::ABORT_ITER;
            _restoreBasis();
            break;
         }

         if(infeasible && boolParam(SoPlexBase<R>::TESTDUALINF))
         {
            SolRational solUnbounded;

            _performUnboundedIRStable(solUnbounded, hasUnboundedRay, stoppedTime, stoppedIter, error);

            assert(!hasUnboundedRay || solUnbounded.hasPrimalRay());
            assert(!solUnbounded.hasPrimalRay() || hasUnboundedRay);

            if(error)
            {
               MSG_INFO1(spxout, spxout << "Error while testing for dual infeasibility.\n");
               _status = SPxSolverBase<R>::ERROR;
               _restoreBasis();
               break;
            }

            if(hasUnboundedRay)
            {
               MSG_INFO1(spxout, spxout << "Dual infeasible.  Primal unbounded ray available.\n");
               _solRational._primalRay = solUnbounded._primalRay;
               _solRational._hasPrimalRay = true;
            }
            else if(solUnbounded._isDualFeasible)
            {
               MSG_INFO1(spxout, spxout << "Dual feasible.  Storing dual multipliers.\n");
               _solRational._dual = solUnbounded._dual;
               _solRational._redCost = solUnbounded._redCost;
               _solRational._isDualFeasible = true;
            }
            else
            {
               assert(false);
               MSG_INFO1(spxout, spxout << "Not dual infeasible.\n");
            }
         }

         _restoreBasis();

         if(infeasible)
         {
            MSG_INFO1(spxout, spxout << "Primal infeasible.  Dual Farkas ray available.\n");
            _status = SPxSolverBase<R>::INFEASIBLE;
            break;
         }
         else if(hasUnboundedRay)
         {
            MSG_INFO1(spxout, spxout << "Primal feasible and unbounded.\n");
            _status = SPxSolverBase<R>::UNBOUNDED;
            break;
         }
         else
         {
            MSG_INFO1(spxout, spxout << "Primal feasible.  Optimizing again.\n");
            continue;
         }
      }
      else if(primalFeasible && dualFeasible)
      {
         MSG_INFO1(spxout, spxout << "Solved to optimality.\n");
         _status = SPxSolverBase<R>::OPTIMAL;
         break;
      }
      else
      {
         MSG_INFO1(spxout, spxout << "Terminating without success.\n");
         break;
      }
   }
   while(!_isSolveStopped(stoppedTime, stoppedIter));

   ///@todo set status to ABORT_VALUE if optimal solution exceeds objective limit

   if(_status == SPxSolverBase<R>::OPTIMAL || _status == SPxSolverBase<R>::INFEASIBLE
         || _status == SPxSolverBase<R>::UNBOUNDED)
      _hasSolRational = true;

   // restore original problem
   if(boolParam(SoPlexBase<R>::EQTRANS))
      _untransformEquality(_solRational);

#ifdef SOPLEX_WITH_CPX
   setBoolParam(SoPlexBase<R>::EQTRANS, oldEqtrans);
#endif

   // reset representation and ratio test
   setIntParam(SoPlexBase<R>::REPRESENTATION, oldRepresentation);
   setIntParam(SoPlexBase<R>::RATIOTESTER, oldRatiotester);

   // undo lifting
   if(boolParam(SoPlexBase<R>::LIFTING))
      _project(_solRational);

   // restore objective, bounds, and sides of Real LP in case they have been modified during iterative refinement
   _restoreLPReal();

   // since the Real LP is loaded in the solver, we need to also pass the basis information to the solver if
   // available
   if(_hasBasis)
   {
      assert(_isRealLPLoaded);
      _solver.setBasis(_basisStatusRows.get_const_ptr(), _basisStatusCols.get_const_ptr());
      _hasBasis = (_solver.basis().status() > SPxBasisBase<R>::NO_PROBLEM);

      // since setBasis always sets the basis status to regular, we need to set it manually here
      switch(_status)
      {
      case SPxSolverBase<R>::OPTIMAL:
         _solver.setBasisStatus(SPxBasisBase<R>::OPTIMAL);
         break;

      case SPxSolverBase<R>::INFEASIBLE:
         _solver.setBasisStatus(SPxBasisBase<R>::INFEASIBLE);
         break;

      case SPxSolverBase<R>::UNBOUNDED:
         _solver.setBasisStatus(SPxBasisBase<R>::UNBOUNDED);
         break;

      default:
         break;
      }

   }

   // stop timing
   _statistics->solvingTime->stop();
#endif
}



/// stores floating-point solution of original LP as current rational solution and ensure that solution vectors have right dimension; ensure that solution is aligned with basis
template <class R>
void SoPlexBase<R>::_storeRealSolutionAsRational(
   SolRational& sol,
   VectorBase<R>& primalReal,
   VectorBase<R>& dualReal,
   int& dualSize)
{
   sol._primal.reDim(numColsRational(), false);
   sol._slacks.reDim(numRowsRational(), false);
   sol._dual.reDim(numRowsRational(), false);
   sol._redCost.reDim(numColsRational(), false);
   sol._isPrimalFeasible = true;
   sol._isDualFeasible = true;

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      typename SPxSolverBase<R>::VarStatus& basisStatusCol = _basisStatusCols[c];

      if(basisStatusCol == SPxSolverBase<R>::ON_LOWER)
         sol._primal[c] = lowerRational(c);
      else if(basisStatusCol == SPxSolverBase<R>::ON_UPPER)
         sol._primal[c] = upperRational(c);
      else if(basisStatusCol == SPxSolverBase<R>::FIXED)
      {
         // it may happen that lower and upper are only equal in the Real LP but different in the rational LP; we do
         // not check this to avoid rational comparisons, but simply switch the basis status to the lower bound; this
         // is necessary, because for fixed variables any reduced cost is feasible
         sol._primal[c] = lowerRational(c);
         basisStatusCol = SPxSolverBase<R>::ON_LOWER;
      }
      else if(basisStatusCol == SPxSolverBase<R>::ZERO)
         sol._primal[c] = 0;
      else
         sol._primal[c].assign(primalReal[c]);
   }

   _rationalLP->computePrimalActivity(sol._primal, sol._slacks);

   assert(dualSize == 0);

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      typename SPxSolverBase<R>::VarStatus& basisStatusRow = _basisStatusRows[r];

      // it may happen that left-hand and right-hand side are different in the rational, but equal in the Real LP,
      // leading to a fixed basis status; this is critical because rows with fixed basis status are ignored in the
      // computation of the dual violation; to avoid rational comparisons we do not check this but simply switch to
      // the left-hand side status
      if(basisStatusRow == SPxSolverBase<R>::FIXED)
         basisStatusRow = SPxSolverBase<R>::ON_LOWER;

      {
         sol._dual[r].assign(dualReal[r]);

         if(dualReal[r] != 0.0)
            dualSize++;
      }
   }

   // we assume that the objective function vector has less nonzeros than the reduced cost vector, and so multiplying
   // with -1 first and subtracting the dual activity should be faster than adding the dual activity and negating
   // afterwards
   _rationalLP->getObj(sol._redCost);
   _rationalLP->subDualActivity(sol._dual, sol._redCost);
}



/// computes violation of bounds during the refinement loop
template <class R>
void SoPlexBase<R>::_computeBoundsViolation(SolRational& sol, Rational& boundsViolation)
{
   boundsViolation = 0;

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      // lower bound
      assert((lowerRational(c) > _rationalNegInfty) == _lowerFinite(_colTypes[c]));

      if(_lowerFinite(_colTypes[c]))
      {
         if(lowerRational(c) == 0)
         {
            _modLower[c] = sol._primal[c];
            _modLower[c] *= -1;

            if(_modLower[c] > boundsViolation)
               boundsViolation = _modLower[c];
         }
         else
         {
            _modLower[c] = lowerRational(c);
            _modLower[c] -= sol._primal[c];

            if(_modLower[c] > boundsViolation)
               boundsViolation = _modLower[c];
         }
      }

      // upper bound
      assert((upperRational(c) < _rationalPosInfty) == _upperFinite(_colTypes[c]));

      if(_upperFinite(_colTypes[c]))
      {
         if(upperRational(c) == 0)
         {
            _modUpper[c] = sol._primal[c];
            _modUpper[c] *= -1;

            if(_modUpper[c] < -boundsViolation)
               boundsViolation = -_modUpper[c];
         }
         else
         {
            _modUpper[c] = upperRational(c);
            _modUpper[c] -= sol._primal[c];

            if(_modUpper[c] < -boundsViolation)
               boundsViolation = -_modUpper[c];
         }
      }
   }
}



/// computes violation of sides during the refinement loop
template <class R>
void SoPlexBase<R>::_computeSidesViolation(SolRational& sol, Rational& sideViolation)
{
   sideViolation = 0;

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      const typename SPxSolverBase<R>::VarStatus& basisStatusRow = _basisStatusRows[r];

      // left-hand side
      assert((lhsRational(r) > _rationalNegInfty) == _lowerFinite(_rowTypes[r]));

      if(_lowerFinite(_rowTypes[r]))
      {
         if(lhsRational(r) == 0)
         {
            _modLhs[r] = sol._slacks[r];
            _modLhs[r] *= -1;
         }
         else
         {
            _modLhs[r] = lhsRational(r);
            _modLhs[r] -= sol._slacks[r];
         }

         if(_modLhs[r] > sideViolation)
            sideViolation = _modLhs[r];
         // if the activity is feasible, but too far from the bound, this violates complementary slackness; we
         // count it as side violation here
         else if(basisStatusRow == SPxSolverBase<R>::ON_LOWER && _modLhs[r] < -sideViolation)
            sideViolation = -_modLhs[r];
      }

      // right-hand side
      assert((rhsRational(r) < _rationalPosInfty) == _upperFinite(_rowTypes[r]));

      if(_upperFinite(_rowTypes[r]))
      {
         if(rhsRational(r) == 0)
         {
            _modRhs[r] = sol._slacks[r];
            _modRhs[r] *= -1;
         }
         else
         {
            _modRhs[r] = rhsRational(r);
            _modRhs[r] -= sol._slacks[r];
         }

         if(_modRhs[r] < -sideViolation)
            sideViolation = -_modRhs[r];
         // if the activity is feasible, but too far from the bound, this violates complementary slackness; we
         // count it as side violation here
         else if(basisStatusRow == SPxSolverBase<R>::ON_UPPER && _modRhs[r] > sideViolation)
            sideViolation = _modRhs[r];
      }
   }
}



/// computes reduced cost violation
template <class R>
void SoPlexBase<R>::_computeReducedCostViolation(
   SolRational& sol,
   Rational& redCostViolation,
   const bool& maximizing)
{
   redCostViolation = 0;

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      if(_colTypes[c] == RANGETYPE_FIXED)
         continue;

      const typename SPxSolverBase<R>::VarStatus& basisStatusCol = _basisStatusCols[c];
      assert(basisStatusCol != SPxSolverBase<R>::FIXED);

      if(((maximizing && basisStatusCol != SPxSolverBase<R>::ON_LOWER) || (!maximizing
            && basisStatusCol != SPxSolverBase<R>::ON_UPPER))
            && sol._redCost[c] < -redCostViolation)
      {
         MSG_DEBUG(std::cout << "basisStatusCol = " << basisStatusCol
                   << ", lower tight = " << bool(sol._primal[c] <= lowerRational(c))
                   << ", upper tight = " << bool(sol._primal[c] >= upperRational(c))
                   << ", sol._redCost[c] = " << sol._redCost[c].str()
                   << "\n");
         redCostViolation = -sol._redCost[c];
      }

      if(((maximizing && basisStatusCol != SPxSolverBase<R>::ON_UPPER) || (!maximizing
            && basisStatusCol != SPxSolverBase<R>::ON_LOWER))
            && sol._redCost[c] > redCostViolation)
      {
         MSG_DEBUG(std::cout << "basisStatusCol = " << basisStatusCol
                   << ", lower tight = " << bool(sol._primal[c] <= lowerRational(c))
                   << ", upper tight = " << bool(sol._primal[c] >= upperRational(c))
                   << ", sol._redCost[c] = " << sol._redCost[c].str()
                   << "\n");
         redCostViolation = sol._redCost[c];
      }
   }
}



/// computes dual violation
template <class R>
void SoPlexBase<R>::_computeDualViolation(
   SolRational& sol,
   Rational& dualViolation,
   const bool& maximizing)
{
   dualViolation = 0;

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      if(_rowTypes[r] == RANGETYPE_FIXED)
         continue;

      const typename SPxSolverBase<R>::VarStatus& basisStatusRow = _basisStatusRows[r];
      assert(basisStatusRow != SPxSolverBase<R>::FIXED);

      if(((maximizing && basisStatusRow != SPxSolverBase<R>::ON_LOWER) || (!maximizing
            && basisStatusRow != SPxSolverBase<R>::ON_UPPER))
            && sol._dual[r] < -dualViolation)
      {
         MSG_DEBUG(std::cout << "basisStatusRow = " << basisStatusRow
                   << ", lower tight = " << bool(sol._slacks[r] <= lhsRational(r))
                   << ", upper tight = " << bool(sol._slacks[r] >= rhsRational(r))
                   << ", sol._dual[r] = " << sol._dual[r].str()
                   << "\n");
         dualViolation = -sol._dual[r];
      }

      if(((maximizing && basisStatusRow != SPxSolverBase<R>::ON_UPPER) || (!maximizing
            && basisStatusRow != SPxSolverBase<R>::ON_LOWER))
            && sol._dual[r] > dualViolation)
      {
         MSG_DEBUG(std::cout << "basisStatusRow = " << basisStatusRow
                   << ", lower tight = " << bool(sol._slacks[r] <= lhsRational(r))
                   << ", upper tight = " << bool(sol._slacks[r] >= rhsRational(r))
                   << ", sol._dual[r] = " << sol._dual[r].str()
                   << "\n");
         dualViolation = sol._dual[r];
      }
   }
}



/// checks termination criteria for refinement loop, return true if termination criteria is met, false otherwise
template <class R>
bool SoPlexBase<R>::_isRefinementOver(
   bool& primalFeasible,
   bool& dualFeasible,
   Rational& boundsViolation,
   Rational& sideViolation,
   Rational& redCostViolation,
   Rational& dualViolation,
   int minRounds,
   bool& stoppedTime,
   bool& stoppedIter,
   int numFailedRefinements)
{
   // terminate if tolerances are satisfied
   primalFeasible = (boundsViolation <= _rationalFeastol && sideViolation <= _rationalFeastol);
   dualFeasible = (redCostViolation <= _rationalOpttol && dualViolation <= _rationalOpttol);

   if(primalFeasible && dualFeasible)
   {
      if(minRounds < 0)
      {
         MSG_INFO1(spxout, spxout << "Tolerances reached.\n");
         return true;
      }
      else
      {
         MSG_INFO1(spxout, spxout <<
                   "Tolerances reached but minRounds forcing additional refinement rounds.\n");
      }
   }

   // terminate if some limit is reached
   if(_isSolveStopped(stoppedTime, stoppedIter) || numFailedRefinements > 2)
      return true;

   return false;
}



/// checks refinement loop progress
template <class R>
void SoPlexBase<R>::_checkRefinementProgress(
   Rational& boundsViolation,
   Rational& sideViolation,
   Rational& redCostViolation,
   Rational& dualViolation,
   Rational& maxViolation,
   Rational& bestViolation,
   const Rational& violationImprovementFactor,
   int& numFailedRefinements)
{
   maxViolation = boundsViolation;

   if(sideViolation > maxViolation)
      maxViolation = sideViolation;

   if(redCostViolation > maxViolation)
      maxViolation = redCostViolation;

   if(dualViolation > maxViolation)
      maxViolation = dualViolation;

   bestViolation /= violationImprovementFactor;

   if(maxViolation > bestViolation)
   {
      MSG_INFO2(spxout, spxout << "Failed to reduce violation significantly.\n");
      bestViolation *= violationImprovementFactor;
      numFailedRefinements++;
   }
   else
      bestViolation = maxViolation;
}



/// performs rational reconstruction and/or factorization
template <class R>
void SoPlexBase<R>::_ratrecAndOrRatfac(
   int& minRounds,
   int lastStallRefinements,
   bool& factorSolNewBasis,
   int& nextRatrecRefinement,
   const Rational& errorCorrectionFactor,
   Rational& errorCorrection,
   Rational& maxViolation,
   SolRational& sol,
   bool& primalFeasible,
   bool& dualFeasible,
   bool& stoppedTime,
   bool& stoppedIter,
   bool& error,
   bool& breakAfter,
   bool& continueAfter)
{
   breakAfter    = false;
   continueAfter = false;

   // decide whether to perform rational reconstruction and/or factorization
   bool forcebasic    = boolParam(SoPlexBase<R>::FORCEBASIC);
   bool performRatfac = boolParam(SoPlexBase<R>::RATFAC)
                        && lastStallRefinements >= intParam(SoPlexBase<R>::RATFAC_MINSTALLS) && _hasBasis
                        && factorSolNewBasis;
   bool performRatrec = boolParam(SoPlexBase<R>::RATREC)
                        && (_statistics->refinements >= nextRatrecRefinement || performRatfac);

   // if we want to force the solution to be basic we need to turn rational factorization on
   performRatfac = performRatfac || forcebasic;

   // attempt rational reconstruction
   errorCorrection *= errorCorrectionFactor;

   if(performRatrec && maxViolation > 0)
   {
      MSG_INFO1(spxout, spxout << "Performing rational reconstruction . . .\n");

      maxViolation *= errorCorrection; // only used for sign check later
      invert(maxViolation);

      if(_reconstructSolutionRational(sol, _basisStatusRows, _basisStatusCols, maxViolation))
      {
         MSG_INFO1(spxout, spxout << "Tolerances reached.\n");
         primalFeasible = true;
         dualFeasible = true;

         if(_hasBasis || !forcebasic)
         {
            breakAfter = true;
            return;
         }
      }

      nextRatrecRefinement = int(_statistics->refinements * realParam(SoPlexBase<R>::RATREC_FREQ)) + 1;
      MSG_DEBUG(spxout << "Next rational reconstruction after refinement " << nextRatrecRefinement <<
                ".\n");
   }

   // solve basis systems exactly
   if((performRatfac && maxViolation > 0) || (!_hasBasis && forcebasic))
   {
      MSG_INFO1(spxout, spxout << "Performing rational factorization . . .\n");

      bool optimal;
      _factorizeColumnRational(sol, _basisStatusRows, _basisStatusCols, stoppedTime, stoppedIter, error,
                               optimal);
      factorSolNewBasis = false;

      if(stoppedTime)
      {
         MSG_INFO1(spxout, spxout << "Stopped rational factorization.\n");
      }
      else if(error)
      {
         // message was already printed; reset error flag and continue without factorization
         error = false;
      }
      else if(optimal)
      {
         MSG_INFO1(spxout, spxout << "Tolerances reached.\n");
         primalFeasible = true;
         dualFeasible = true;
         breakAfter = true;
         return;
      }
      else if(boolParam(SoPlexBase<R>::RATFACJUMP))
      {
         MSG_INFO1(spxout, spxout << "Jumping to exact basic solution.\n");
         minRounds++;
         continueAfter = true;
         return;
      }
   }
}



/// forces value of given nonbasic variable to bound
template <class R>
void SoPlexBase<R>::_forceNonbasicToBound(
   SolRational& sol,
   int& c,
   const int& maxDimRational,
   bool toLower)
{
   int i = _primalDualDiff.size();
   _ensureDSVectorRationalMemory(_primalDualDiff, maxDimRational);
   _primalDualDiff.add(c);

   if(toLower)
      _primalDualDiff.value(i) = lowerRational(c);
   else
      _primalDualDiff.value(i) = upperRational(c);

   _primalDualDiff.value(i) -= sol._primal[c];

   if(toLower)
      sol._primal[c] = lowerRational(c);
   else
      sol._primal[c] = upperRational(c);
}



/// computes primal scaling factor; limit increase in scaling by tolerance used in floating point solve
template <class R>
void SoPlexBase<R>::_computePrimalScalingFactor(
   Rational& maxScale,
   Rational& primalScale,
   Rational& boundsViolation,
   Rational& sideViolation,
   Rational& redCostViolation)
{
   maxScale = primalScale;
   maxScale *= _rationalMaxscaleincr;

   primalScale = boundsViolation > sideViolation ? boundsViolation : sideViolation;

   if(primalScale < redCostViolation)
      primalScale = redCostViolation;

   assert(primalScale >= 0);

   if(primalScale > 0)
   {
      invert(primalScale);

      if(primalScale > maxScale)
         primalScale = maxScale;
   }
   else
      primalScale = maxScale;

   if(boolParam(SoPlexBase<R>::POWERSCALING))
      powRound(primalScale);
}



/// computes dual scaling factor; limit increase in scaling by tolerance used in floating point solve
template <class R>
void SoPlexBase<R>::_computeDualScalingFactor(
   Rational& maxScale,
   Rational& primalScale,
   Rational& dualScale,
   Rational& redCostViolation,
   Rational& dualViolation)
{
   maxScale = dualScale;
   maxScale *= _rationalMaxscaleincr;

   dualScale = redCostViolation > dualViolation ? redCostViolation : dualViolation;
   assert(dualScale >= 0);

   if(dualScale > 0)
   {
      invert(dualScale);

      if(dualScale > maxScale)
         dualScale = maxScale;
   }
   else
      dualScale = maxScale;

   if(boolParam(SoPlexBase<R>::POWERSCALING))
      powRound(dualScale);

   if(dualScale > primalScale)
      dualScale = primalScale;

   if(dualScale < 1)
      dualScale = 1;
   else
   {
      MSG_INFO2(spxout, spxout << "Scaling dual by " << dualScale.str() << ".\n");

      // perform dual scaling
      ///@todo remove _modObj and use dualScale * sol._redCost directly
      _modObj *= dualScale;
   }
}



/// applies scaled bounds
template <class R>
void SoPlexBase<R>::_applyScaledBounds(Rational& primalScale)
{
   if(primalScale < 1)
      primalScale = 1;

   if(primalScale > 1)
      MSG_INFO2(spxout, spxout << "Scaling primal by " << primalScale.str() << ".\n");

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      if(_lowerFinite(_colTypes[c]))
      {
         if(primalScale > 1)
            _modLower[c] *= primalScale;

         if(_modLower[c] <= _rationalNegInfty)
            _solver.changeLower(c, -realParam(SoPlexBase<R>::INFTY));
         else if(primalScale > 1)
            _solver.changeLower(c, R(_modLower[c]));
         else
            _solver.changeLower(c, static_cast<R>(_modLower[c]));
      }

      if(_upperFinite(_colTypes[c]))
      {
         if(primalScale > 1)
            _modUpper[c] *= primalScale;

         if(_modUpper[c] >= _rationalPosInfty)
            _solver.changeUpper(c, realParam(SoPlexBase<R>::INFTY));
         else
            _solver.changeUpper(c, R(_modUpper[c]));
      }
   }
}



/// applies scaled sides
template <class R>
void SoPlexBase<R>::_applyScaledSides(Rational& primalScale)
{
   assert(primalScale >= 1);

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      if(_lowerFinite(_rowTypes[r]))
      {
         if(primalScale != 1)
            _modLhs[r] *= primalScale;

         if(_modLhs[r] <= _rationalNegInfty)
            _solver.changeLhs(r, -realParam(SoPlexBase<R>::INFTY));
         else
            _solver.changeLhs(r, R(_modLhs[r]));
      }

      if(_upperFinite(_rowTypes[r]))
      {
         if(primalScale != 1)
            _modRhs[r] *= primalScale;

         if(_modRhs[r] >= _rationalPosInfty)
            _solver.changeRhs(r, realParam(SoPlexBase<R>::INFTY));
         else
            _solver.changeRhs(r, R(_modRhs[r]));
      }
   }
}



/// applies scaled objective function
template <class R>
void SoPlexBase<R>::_applyScaledObj(Rational& dualScale, SolRational& sol)
{
   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      if(_modObj[c] >= _rationalPosInfty)
         _solver.changeObj(c, realParam(SoPlexBase<R>::INFTY));
      else if(_modObj[c] <= _rationalNegInfty)
         _solver.changeObj(c, -realParam(SoPlexBase<R>::INFTY));
      else
         _solver.changeObj(c, R(_modObj[c]));
   }

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      Rational newRowObj;

      if(_rowTypes[r] == RANGETYPE_FIXED)
         _solver.changeRowObj(r, R(0.0));
      else
      {
         newRowObj = sol._dual[r];
         newRowObj *= dualScale;

         if(newRowObj >= _rationalPosInfty)
            _solver.changeRowObj(r, -realParam(SoPlexBase<R>::INFTY));
         else if(newRowObj <= _rationalNegInfty)
            _solver.changeRowObj(r, realParam(SoPlexBase<R>::INFTY));
         else
            _solver.changeRowObj(r, -R(newRowObj));
      }
   }
}



/// evaluates result of solve. Return true if the algorithm needs to stop, false otherwise.
template <class R>
bool SoPlexBase<R>::_evaluateResult(
   typename SPxSolverBase<R>::Status result,
   bool usingRefinedLP,
   SolRational& sol,
   VectorBase<R>& dualReal,
   bool& infeasible,
   bool& unbounded,
   bool& stoppedTime,
   bool& stoppedIter,
   bool& error)
{
   switch(result)
   {
   case SPxSolverBase<R>::OPTIMAL:
      MSG_INFO1(spxout, spxout << "Floating-point optimal.\n");
      return false;

   case SPxSolverBase<R>::INFEASIBLE:
      MSG_INFO1(spxout, spxout << "Floating-point infeasible.\n");

      // when not using refined LP
      // the floating-point solve returns a Farkas ray if and only if the simplifier was not used, which is exactly
      // the case when a basis could be returned
      if(usingRefinedLP || _hasBasis)
      {
         sol._dualFarkas = dualReal;
         sol._hasDualFarkas = true;
      }
      else
         sol._hasDualFarkas = false;

      if(usingRefinedLP)
         _solver.clearRowObjs();

      infeasible = true;
      return true;

   case SPxSolverBase<R>::UNBOUNDED:
      MSG_INFO1(spxout, spxout << "Floating-point unbounded.\n");

      if(usingRefinedLP)
         _solver.clearRowObjs();

      unbounded = true;
      return true;

   case SPxSolverBase<R>::ABORT_TIME:
      stoppedTime = true;
      return true;

   case SPxSolverBase<R>::ABORT_ITER:
      if(usingRefinedLP)
         _solver.clearRowObjs();

      stoppedIter = true;
      return true;

   default:
      if(usingRefinedLP)
         _solver.clearRowObjs();

      error = true;
      return true;
   }
}



/// corrects primal solution and aligns with basis
template <class R>
void SoPlexBase<R>::_correctPrimalSolution(
   SolRational& sol,
   Rational& primalScale,
   int& primalSize,
   const int& maxDimRational,
   VectorBase<R>& primalReal)
{
   MSG_DEBUG(std::cout << "Correcting primal solution.\n");

   primalSize = 0;
   Rational primalScaleInverse = primalScale;
   invert(primalScaleInverse);
   _primalDualDiff.clear();

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      // force values of nonbasic variables to bounds
      typename SPxSolverBase<R>::VarStatus& basisStatusCol = _basisStatusCols[c];

      if(basisStatusCol == SPxSolverBase<R>::ON_LOWER)
      {
         if(sol._primal[c] != lowerRational(c))
         {
            _forceNonbasicToBound(sol, c, maxDimRational, true);
         }
      }
      else if(basisStatusCol == SPxSolverBase<R>::ON_UPPER)
      {
         if(sol._primal[c] != upperRational(c))
         {
            _forceNonbasicToBound(sol, c, maxDimRational, false);
         }
      }
      else if(basisStatusCol == SPxSolverBase<R>::FIXED)
      {
         // it may happen that lower and upper are only equal in the Real LP but different in the rational LP; we
         // do not check this to avoid rational comparisons, but simply switch the basis status to the lower
         // bound; this is necessary, because for fixed variables any reduced cost is feasible
         basisStatusCol = SPxSolverBase<R>::ON_LOWER;

         if(sol._primal[c] != lowerRational(c))
         {
            _forceNonbasicToBound(sol, c, maxDimRational, true);
         }
      }
      else if(basisStatusCol == SPxSolverBase<R>::ZERO)
      {
         if(sol._primal[c] != 0)
         {
            int i = _primalDualDiff.size();
            _ensureDSVectorRationalMemory(_primalDualDiff, maxDimRational);
            _primalDualDiff.add(c);
            _primalDualDiff.value(i) = sol._primal[c];
            _primalDualDiff.value(i) *= -1;
            sol._primal[c] = 0;
         }
      }
      else
      {
         if(primalReal[c] == 1.0)
         {
            int i = _primalDualDiff.size();
            _ensureDSVectorRationalMemory(_primalDualDiff, maxDimRational);
            _primalDualDiff.add(c);
            _primalDualDiff.value(i) = primalScaleInverse;
            sol._primal[c] += _primalDualDiff.value(i);
         }
         else if(primalReal[c] == -1.0)
         {
            int i = _primalDualDiff.size();
            _ensureDSVectorRationalMemory(_primalDualDiff, maxDimRational);
            _primalDualDiff.add(c);
            _primalDualDiff.value(i) = primalScaleInverse;
            _primalDualDiff.value(i) *= -1;
            sol._primal[c] += _primalDualDiff.value(i);
         }
         else if(primalReal[c] != 0.0)
         {
            int i = _primalDualDiff.size();
            _ensureDSVectorRationalMemory(_primalDualDiff, maxDimRational);
            _primalDualDiff.add(c);
            _primalDualDiff.value(i).assign(primalReal[c]);
            _primalDualDiff.value(i) *= primalScaleInverse;
            sol._primal[c] += _primalDualDiff.value(i);
         }
      }

      if(sol._primal[c] != 0)
         primalSize++;
   }
}



/// updates or recomputes slacks depending on which looks faster
template <class R>
void SoPlexBase<R>::_updateSlacks(SolRational& sol, int& primalSize)
{
   if(_primalDualDiff.size() < primalSize)
   {
      _rationalLP->addPrimalActivity(_primalDualDiff, sol._slacks);
#ifndef NDEBUG
      {
         VectorRational activity(numRowsRational());
         _rationalLP->computePrimalActivity(sol._primal, activity);
         assert(sol._slacks == activity);
      }
#endif
   }
   else
      _rationalLP->computePrimalActivity(sol._primal, sol._slacks);
}



/// corrects dual solution and aligns with basis
template <class R>
void SoPlexBase<R>::_correctDualSolution(
   SolRational& sol,
   const bool& maximizing,
   VectorBase<R>& dualReal,
   Rational& dualScale,
   int& dualSize,
   const int& maxDimRational)
{
   MSG_DEBUG(std::cout << "Correcting dual solution.\n");

#ifndef NDEBUG
   {
      // compute reduced cost violation
      VectorRational debugRedCost(numColsRational());
      debugRedCost = VectorRational(_realLP->maxObj());
      debugRedCost *= -1;
      _rationalLP->subDualActivity(VectorRational(dualReal), debugRedCost);

      Rational debugRedCostViolation = 0;

      for(int c = numColsRational() - 1; c >= 0; c--)
      {
         if(_colTypes[c] == RANGETYPE_FIXED)
            continue;

         const typename SPxSolverBase<R>::VarStatus& basisStatusCol = _basisStatusCols[c];
         assert(basisStatusCol != SPxSolverBase<R>::FIXED);

         if(((maximizing && basisStatusCol != SPxSolverBase<R>::ON_LOWER) || (!maximizing
               && basisStatusCol != SPxSolverBase<R>::ON_UPPER))
               && debugRedCost[c] < -debugRedCostViolation)
         {
            MSG_DEBUG(std::cout << "basisStatusCol = " << basisStatusCol
                      << ", lower tight = " << bool(sol._primal[c] <= lowerRational(c))
                      << ", upper tight = " << bool(sol._primal[c] >= upperRational(c))
                      << ", obj[c] = " << _realLP->obj(c)
                      << ", debugRedCost[c] = " << debugRedCost[c].str()
                      << "\n");
            debugRedCostViolation = -debugRedCost[c];
         }

         if(((maximizing && basisStatusCol != SPxSolverBase<R>::ON_UPPER) || (!maximizing
               && basisStatusCol != SPxSolverBase<R>::ON_LOWER))
               && debugRedCost[c] > debugRedCostViolation)
         {
            MSG_DEBUG(std::cout << "basisStatusCol = " << basisStatusCol
                      << ", lower tight = " << bool(sol._primal[c] <= lowerRational(c))
                      << ", upper tight = " << bool(sol._primal[c] >= upperRational(c))
                      << ", obj[c] = " << _realLP->obj(c)
                      << ", debugRedCost[c] = " << debugRedCost[c].str()
                      << "\n");
            debugRedCostViolation = debugRedCost[c];
         }
      }

      // compute dual violation
      Rational debugDualViolation = 0;
      Rational debugBasicDualViolation = 0;

      for(int r = numRowsRational() - 1; r >= 0; r--)
      {
         if(_rowTypes[r] == RANGETYPE_FIXED)
            continue;

         const typename SPxSolverBase<R>::VarStatus& basisStatusRow = _basisStatusRows[r];
         assert(basisStatusRow != SPxSolverBase<R>::FIXED);

         Rational val = (-dualScale * sol._dual[r]) - Rational(dualReal[r]);

         if(((maximizing && basisStatusRow != SPxSolverBase<R>::ON_LOWER) || (!maximizing
               && basisStatusRow != SPxSolverBase<R>::ON_UPPER))
               && val > debugDualViolation)
         {
            MSG_DEBUG(std::cout << "basisStatusRow = " << basisStatusRow
                      << ", lower tight = " << bool(sol._slacks[r] <= lhsRational(r))
                      << ", upper tight = " << bool(sol._slacks[r] >= rhsRational(r))
                      << ", dualReal[r] = " << val.str()
                      << ", dualReal[r] = " << dualReal[r]
                      << "\n");
            debugDualViolation = val;
         }

         if(((maximizing && basisStatusRow != SPxSolverBase<R>::ON_UPPER) || (!maximizing
               && basisStatusRow != SPxSolverBase<R>::ON_LOWER))
               && val < -debugDualViolation)
         {
            MSG_DEBUG(std::cout << "basisStatusRow = " << basisStatusRow
                      << ", lower tight = " << bool(sol._slacks[r] <= lhsRational(r))
                      << ", upper tight = " << bool(sol._slacks[r] >= rhsRational(r))
                      << ", dualReal[r] = " << val.str()
                      << ", dualReal[r] = " << dualReal[r]
                      << "\n");
            debugDualViolation = -val;
         }

         if(basisStatusRow == SPxSolverBase<R>::BASIC && spxAbs(val) > debugBasicDualViolation)
         {
            MSG_DEBUG(std::cout << "basisStatusRow = " << basisStatusRow
                      << ", lower tight = " << bool(sol._slacks[r] <= lhsRational(r))
                      << ", upper tight = " << bool(sol._slacks[r] >= rhsRational(r))
                      << ", dualReal[r] = " << val.str()
                      << ", dualReal[r] = " << dualReal[r]
                      << "\n");
            debugBasicDualViolation = spxAbs(val);
         }
      }

      if(R(debugRedCostViolation) > _solver.opttol() || R(debugDualViolation) > _solver.opttol()
            || debugBasicDualViolation > 1e-9)
      {
         MSG_WARNING(spxout, spxout << "Warning: floating-point dual solution with violation "
                     << debugRedCostViolation.str() << " / "
                     << debugDualViolation.str() << " / "
                     << debugBasicDualViolation.str()
                     << " (red. cost, dual, basic).\n");
      }
   }
#endif

   Rational dualScaleInverseNeg = dualScale;
   invert(dualScaleInverseNeg);
   dualScaleInverseNeg *= -1;
   _primalDualDiff.clear();
   dualSize = 0;

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      typename SPxSolverBase<R>::VarStatus& basisStatusRow = _basisStatusRows[r];

      // it may happen that left-hand and right-hand side are different in the rational, but equal in the Real LP,
      // leading to a fixed basis status; this is critical because rows with fixed basis status are ignored in the
      // computation of the dual violation; to avoid rational comparisons we do not check this but simply switch
      // to the left-hand side status
      if(basisStatusRow == SPxSolverBase<R>::FIXED)
         basisStatusRow = SPxSolverBase<R>::ON_LOWER;

      {
         if(dualReal[r] != 0)
         {
            int i = _primalDualDiff.size();
            _ensureDSVectorRationalMemory(_primalDualDiff, maxDimRational);
            _primalDualDiff.add(r);
            _primalDualDiff.value(i).assign(dualReal[r]);
            _primalDualDiff.value(i) *= dualScaleInverseNeg;
            sol._dual[r] -= _primalDualDiff.value(i);

            dualSize++;
         }
         else
         {
            // we do not check whether the dual value is nonzero, because it probably is; this gives us an
            // overestimation of the number of nonzeros in the dual solution
            dualSize++;
         }
      }
   }
}



/// updates or recomputes reduced cost values depending on which looks faster; adding one to the length of the
/// dual vector accounts for the objective function vector
template <class R>
void SoPlexBase<R>::_updateReducedCosts(SolRational& sol, int& dualSize,
                                        const int& numCorrectedPrimals)
{

   if(_primalDualDiff.size() < dualSize + 1)
   {
      _rationalLP->addDualActivity(_primalDualDiff, sol._redCost);
#ifndef NDEBUG
      {
         VectorRational activity(_rationalLP->maxObj());
         activity *= -1;
         _rationalLP->subDualActivity(sol._dual, activity);
      }
#endif
   }
   else
   {
      // we assume that the objective function vector has less nonzeros than the reduced cost vector, and so multiplying
      // with -1 first and subtracting the dual activity should be faster than adding the dual activity and negating
      // afterwards
      _rationalLP->getObj(sol._redCost);
      _rationalLP->subDualActivity(sol._dual, sol._redCost);
   }

   const int numCorrectedDuals = _primalDualDiff.size();

   if(numCorrectedPrimals + numCorrectedDuals > 0)
   {
      MSG_INFO2(spxout, spxout << "Corrected " << numCorrectedPrimals << " primal variables and " <<
                numCorrectedDuals << " dual values.\n");
   }
}



/// solves current problem with iterative refinement and recovery mechanism
template <class R>
void SoPlexBase<R>::_performOptIRStable(
   SolRational& sol,
   bool acceptUnbounded,
   bool acceptInfeasible,
   int minRounds,
   bool& primalFeasible,
   bool& dualFeasible,
   bool& infeasible,
   bool& unbounded,
   bool& stoppedTime,
   bool& stoppedIter,
   bool& error)
{
   // start rational solving timing
   _statistics->rationalTime->start();

   primalFeasible = false;
   dualFeasible = false;
   infeasible = false;
   unbounded = false;
   stoppedTime = false;
   stoppedIter = false;
   error = false;

   // set working tolerances in floating-point solver
   _solver.setFeastol(realParam(SoPlexBase<R>::FPFEASTOL));
   _solver.setOpttol(realParam(SoPlexBase<R>::FPOPTTOL));

   // declare vectors and variables
   typename SPxSolverBase<R>::Status result = SPxSolverBase<R>::UNKNOWN;

   _modLower.reDim(numColsRational(), false);
   _modUpper.reDim(numColsRational(), false);
   _modLhs.reDim(numRowsRational(), false);
   _modRhs.reDim(numRowsRational(), false);
   _modObj.reDim(numColsRational(), false);

   VectorBase<R> primalReal(numColsRational());
   VectorBase<R> dualReal(numRowsRational());

   Rational boundsViolation;
   Rational sideViolation;
   Rational redCostViolation;
   Rational dualViolation;
   Rational primalScale;
   Rational dualScale;
   Rational maxScale;

   // solve original LP
   MSG_INFO1(spxout, spxout << "Initial floating-point solve . . .\n");

   if(_hasBasis)
   {
      assert(_basisStatusRows.size() == numRowsRational());
      assert(_basisStatusCols.size() == numColsRational());
      _solver.setBasis(_basisStatusRows.get_const_ptr(), _basisStatusCols.get_const_ptr());
      _hasBasis = (_solver.basis().status() > SPxBasisBase<R>::NO_PROBLEM);
   }

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      assert(_solver.maxRowObj(r) == 0.0);
   }

   _statistics->rationalTime->stop();
   result = _solveRealStable(acceptUnbounded, acceptInfeasible, primalReal, dualReal, _basisStatusRows,
                             _basisStatusCols);

   // evalute result
   if(_evaluateResult(result, false, sol, dualReal, infeasible, unbounded, stoppedTime, stoppedIter,
                      error))
      return;

   _statistics->rationalTime->start();

   int dualSize = 0;

   // stores floating-point solution of original LP as current rational solution
   // ensure that solution vectors have right dimension; ensure that solution is aligned with basis
   _storeRealSolutionAsRational(sol, primalReal, dualReal, dualSize);

   // initial scaling factors are one
   primalScale = _rationalPosone;
   dualScale = _rationalPosone;

   // control progress
   Rational maxViolation;
   Rational bestViolation = _rationalPosInfty;
   const Rational violationImprovementFactor = 16;
   const Rational errorCorrectionFactor = 1.1;
   Rational errorCorrection = 2;
   int numFailedRefinements = 0;

   // store basis status in case solving modified problem failed
   DataArray< typename SPxSolverBase<R>::VarStatus > basisStatusRowsFirst;
   DataArray< typename SPxSolverBase<R>::VarStatus > basisStatusColsFirst;

   // refinement loop
   const bool maximizing = (intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MAXIMIZE);
   const int maxDimRational = numColsRational() > numRowsRational() ? numColsRational() :
                              numRowsRational();
   SolRational factorSol;
   bool factorSolNewBasis = true;
   int lastStallRefinements = 0;
   int nextRatrecRefinement = 0;

   // used in _ratrecAndOrRatfac to order a break or a continue outside of the function.
   bool breakAfter;
   bool continueAfter;

   do
   {
      // decrement minRounds counter
      minRounds--;

      MSG_DEBUG(std::cout << "Computing primal violations.\n");

      // computes violation of bounds
      _computeBoundsViolation(sol, boundsViolation);

      // computes violation of sides
      _computeSidesViolation(sol, sideViolation);

      MSG_DEBUG(std::cout << "Computing dual violations.\n");

      // compute reduced cost violation
      _computeReducedCostViolation(sol, redCostViolation, maximizing);

      // compute dual violation
      _computeDualViolation(sol, dualViolation, maximizing);

      _modObj = sol._redCost;

      // output violations; the reduced cost violations for artificially introduced slack columns are actually violations of the dual multipliers
      MSG_INFO1(spxout, spxout
                << "Max. bound violation = " << boundsViolation.str() << "\n"
                << "Max. row violation = " << sideViolation.str() << "\n"
                << "Max. reduced cost violation = " << redCostViolation.str() << "\n"
                << "Max. dual violation = " << dualViolation.str() << "\n");

      MSG_DEBUG(spxout
                << std::fixed << std::setprecision(2) << std::setw(10)
                << "Progress table: "
                << std::setw(10) << _statistics->refinements << " & "
                << std::setw(10) << _statistics->iterations << " & "
                << std::setw(10) << _statistics->solvingTime->time() << " & "
                << std::setw(10) << _statistics->rationalTime->time() << " & "
                << std::setw(10) << boundsViolation > sideViolation ? boundsViolation :
                sideViolation << " & "
                << std::setw(10) << redCostViolation > dualViolation ? redCostViolation :
                dualViolation << "\n");

      // check termination criteria for refinement loop
      if(_isRefinementOver(primalFeasible, dualFeasible, boundsViolation, sideViolation, redCostViolation,
                           dualViolation,
                           minRounds, stoppedTime, stoppedIter, numFailedRefinements))
         break;

      // check refinement progress
      _checkRefinementProgress(boundsViolation, sideViolation, redCostViolation, dualViolation,
                               maxViolation, bestViolation, violationImprovementFactor, numFailedRefinements);

      // perform rational reconstruction and/or factorization
      _ratrecAndOrRatfac(minRounds, lastStallRefinements, factorSolNewBasis, nextRatrecRefinement,
                         errorCorrectionFactor, errorCorrection, maxViolation,
                         sol, primalFeasible, dualFeasible,
                         stoppedTime, stoppedIter, error, breakAfter, continueAfter);

      if(breakAfter)
         break;
      else if(continueAfter)
         continue;

      // start refinement

      // compute primal scaling factor; limit increase in scaling by tolerance used in floating point solve
      _computePrimalScalingFactor(maxScale, primalScale, boundsViolation, sideViolation,
                                  redCostViolation);

      // apply scaled bounds and scaled sides
      _applyScaledBounds(primalScale);
      _applyScaledSides(primalScale);

      // compute dual scaling factor; limit increase in scaling by tolerance used in floating point solve
      _computeDualScalingFactor(maxScale, primalScale, dualScale, redCostViolation, dualViolation);

      // apply scaled objective function
      _applyScaledObj(dualScale, sol);

      MSG_INFO1(spxout, spxout << "Refined floating-point solve . . .\n");

      // ensure that artificial slack columns are basic and inequality constraints are nonbasic; otherwise we may end
      // up with dual violation on inequality constraints after removing the slack columns; do not change this in the
      // floating-point solver, though, because the solver may require its original basis to detect optimality
      if(_slackCols.num() > 0 && _hasBasis)
      {
         int numOrigCols = numColsRational() - _slackCols.num();
         assert(_slackCols.num() <= 0 || boolParam(SoPlexBase<R>::EQTRANS));

         for(int i = 0; i < _slackCols.num(); i++)
         {
            int row = _slackCols.colVector(i).index(0);
            int col = numOrigCols + i;

            assert(row >= 0);
            assert(row < numRowsRational());

            if(_basisStatusRows[row] == SPxSolverBase<R>::BASIC
                  && _basisStatusCols[col] != SPxSolverBase<R>::BASIC)
            {
               _basisStatusRows[row] = _basisStatusCols[col];
               _basisStatusCols[col] = SPxSolverBase<R>::BASIC;
               _rationalLUSolver.clear();
            }
         }
      }

      // load basis
      if(_hasBasis && _solver.basis().status() < SPxBasisBase<R>::REGULAR)
      {
         MSG_DEBUG(spxout << "basis (status = " << _solver.basis().status() << ") desc before set:\n";
                   _solver.basis().desc().dump());
         _solver.setBasis(_basisStatusRows.get_const_ptr(), _basisStatusCols.get_const_ptr());
         MSG_DEBUG(spxout << "basis (status = " << _solver.basis().status() << ") desc after set:\n";
                   _solver.basis().desc().dump());

         _hasBasis = _solver.basis().status() > SPxBasisBase<R>::NO_PROBLEM;
         MSG_DEBUG(spxout << "setting basis in solver " << (_hasBasis ? "successful" : "failed") <<
                   " (3)\n");
      }

      // solve modified problem
      int prevIterations = _statistics->iterations;
      _statistics->rationalTime->stop();
      result = _solveRealStable(acceptUnbounded, acceptInfeasible, primalReal, dualReal, _basisStatusRows,
                                _basisStatusCols, primalScale > 1e20 || dualScale > 1e20);

      // count refinements and remember whether we moved to a new basis
      _statistics->refinements++;

      if(_statistics->iterations <= prevIterations)
      {
         lastStallRefinements++;
         _statistics->stallRefinements++;
      }
      else
      {
         factorSolNewBasis = true;
         lastStallRefinements = 0;
         _statistics->pivotRefinements = _statistics->refinements;
      }

      // evaluate result; if modified problem was not solved to optimality, stop refinement;
      if(_evaluateResult(result, true, sol, dualReal, infeasible, unbounded, stoppedTime, stoppedIter,
                         error))
         return;

      _statistics->rationalTime->start();

      int primalSize;

      // correct primal solution and align with basis
      _correctPrimalSolution(sol, primalScale, primalSize, maxDimRational, primalReal);

      // update or recompute slacks depending on which looks faster
      _updateSlacks(sol, primalSize);

      const int numCorrectedPrimals = _primalDualDiff.size();

      // correct dual solution and align with basis
      _correctDualSolution(sol, maximizing, dualReal, dualScale, dualSize, maxDimRational);

      // update or recompute reduced cost values depending on which looks faster
      // adding one to the length of the dual vector accounts for the objective function vector
      _updateReducedCosts(sol, dualSize, numCorrectedPrimals);
   }
   while(true);

   // correct basis status for restricted inequalities
   if(_hasBasis)
   {
      for(int r = numRowsRational() - 1; r >= 0; r--)
      {
         assert((lhsRational(r) == rhsRational(r)) == (_rowTypes[r] == RANGETYPE_FIXED));

         if(_rowTypes[r] != RANGETYPE_FIXED && _basisStatusRows[r] == SPxSolverBase<R>::FIXED)
            _basisStatusRows[r] = (maximizing == (sol._dual[r] < 0))
                                  ? SPxSolverBase<R>::ON_LOWER
                                  : SPxSolverBase<R>::ON_UPPER;
      }
   }

   // compute objective function values
   assert(sol._isPrimalFeasible == sol._isDualFeasible);

   if(sol._isPrimalFeasible)
   {
      sol._objVal = sol._primal * _rationalLP->maxObj();

      if(intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MINIMIZE)
         sol._objVal *= -1;
   }

   // set objective coefficients for all rows to zero
   _solver.clearRowObjs();

   // stop rational solving time
   _statistics->rationalTime->stop();
}



/// performs iterative refinement on the auxiliary problem for testing unboundedness
template <class R>
void SoPlexBase<R>::_performUnboundedIRStable(
   SolRational& sol,
   bool& hasUnboundedRay,
   bool& stoppedTime,
   bool& stoppedIter,
   bool& error)
{
   bool primalFeasible;
   bool dualFeasible;
   bool infeasible;
   bool unbounded;

   // move objective function to constraints and adjust sides and bounds
   _transformUnbounded();

   // invalidate solution
   sol.invalidate();

   // remember current number of refinements
   int oldRefinements = _statistics->refinements;

   // perform iterative refinement
   _performOptIRStable(sol, false, false, 0, primalFeasible, dualFeasible, infeasible, unbounded,
                       stoppedTime, stoppedIter, error);

   // update unbounded refinement counter
   _statistics->unbdRefinements += _statistics->refinements - oldRefinements;

   // stopped due to some limit
   if(stoppedTime || stoppedIter)
   {
      sol.invalidate();
      hasUnboundedRay = false;
      error = false;
   }
   // the unbounded problem should always be solved to optimality
   else if(error || unbounded || infeasible || !primalFeasible || !dualFeasible)
   {
      sol.invalidate();
      hasUnboundedRay = false;
      error = true;
   }
   else
   {
      const Rational& tau = sol._primal[numColsRational() - 1];

      MSG_DEBUG(std::cout << "tau = " << tau << " (roughly " << tau.str() << ")\n");

      assert(tau <= 1.0 + 2.0 * realParam(SoPlexBase<R>::FEASTOL));
      assert(tau >= -realParam(SoPlexBase<R>::FEASTOL));

      // because the right-hand side and all bounds (but tau's upper bound) are zero, tau should be approximately
      // zero if basic; otherwise at its upper bound 1
      error = !(tau >= _rationalPosone || tau <= _rationalFeastol);
      assert(!error);

      hasUnboundedRay = (tau >= 1);
   }

   // restore problem
   _untransformUnbounded(sol, hasUnboundedRay);
}



/// performs iterative refinement on the auxiliary problem for testing feasibility
template <class R>
void SoPlexBase<R>::_performFeasIRStable(
   SolRational& sol,
   bool& withDualFarkas,
   bool& stoppedTime,
   bool& stoppedIter,
   bool& error)
{
   bool primalFeasible;
   bool dualFeasible;
   bool infeasible;
   bool unbounded;
   bool success = false;
   error = false;

#if 0
   // if the problem has been found to be infeasible and an approximate Farkas proof is available, we compute a
   // scaled unit box around the origin that provably contains no feasible solution; this currently only works for
   // equality form
   ///@todo check whether approximate Farkas proof can be used
   _computeInfeasBox(_solRational, false);
   ///@todo if approx Farkas proof is good enough then exit without doing any transformation
#endif

   // remove objective function, shift, homogenize
   _transformFeasibility();

   // invalidate solution
   sol.invalidate();

   do
   {
      // remember current number of refinements
      int oldRefinements = _statistics->refinements;

      // perform iterative refinement
      _performOptIRStable(sol, false, false, 0, primalFeasible, dualFeasible, infeasible, unbounded,
                          stoppedTime, stoppedIter, error);

      // update feasible refinement counter
      _statistics->feasRefinements += _statistics->refinements - oldRefinements;

      // stopped due to some limit
      if(stoppedTime || stoppedIter)
      {
         sol.invalidate();
         withDualFarkas = false;
         error = false;
      }
      // the feasibility problem should always be solved to optimality
      else if(error || unbounded || infeasible || !primalFeasible || !dualFeasible)
      {
         sol.invalidate();
         withDualFarkas = false;
         error = true;
      }
      // else we should have either a refined Farkas proof or an approximate feasible solution to the original
      else
      {
         const Rational& tau = sol._primal[numColsRational() - 1];

         MSG_DEBUG(std::cout << "tau = " << tau << " (roughly " << tau.str() << ")\n");

         assert(tau >= -realParam(SoPlexBase<R>::FEASTOL));
         assert(tau <= 1.0 + realParam(SoPlexBase<R>::FEASTOL));

         error = (tau < -_rationalFeastol || tau > _rationalPosone + _rationalFeastol);
         withDualFarkas = (tau < _rationalPosone);

         if(withDualFarkas)
         {
            _solRational._hasDualFarkas = true;
            _solRational._dualFarkas = _solRational._dual;

#if 0
            // check if we can compute sufficiently large Farkas box
            _computeInfeasBox(_solRational, true);
#endif

            if(true)  //@todo check if computeInfeasBox found a sufficient box
            {

               success = true;
               sol._isPrimalFeasible = false;
            }
         }
         else
         {
            sol._isDualFeasible = false;
            success = true; //successfully found approximate feasible solution
         }
      }
   }
   while(!error && !success && !(stoppedTime || stoppedIter));

   // restore problem
   _untransformFeasibility(sol, withDualFarkas);
}



/// reduces matrix coefficient in absolute value by the lifting procedure of Thiele et al. 2013
template <class R>
void SoPlexBase<R>::_lift()
{
   MSG_DEBUG(std::cout << "Reducing matrix coefficients by lifting.\n");

   // start timing
   _statistics->transformTime->start();

   MSG_DEBUG(_realLP->writeFileLPBase("beforeLift.lp", 0, 0, 0));

   // remember unlifted state
   _beforeLiftCols = numColsRational();
   _beforeLiftRows = numRowsRational();

   // allocate vector memory
   DSVectorRational colVector;
   SVectorRational::Element liftingRowMem[2];
   SVectorRational liftingRowVector(2, liftingRowMem);

   // search each column for large nonzeros entries
   // @todo: rethink about the static_cast TODO
   const Rational maxValue = static_cast<Rational>(realParam(SoPlexBase<R>::LIFTMAXVAL));

   for(int i = 0; i < numColsRational(); i++)
   {
      MSG_DEBUG(std::cout << "in lifting: examining column " << i << "\n");

      // get column vector
      colVector = colVectorRational(i);

      bool addedLiftingRow = false;
      int liftingColumnIndex = -1;

      // go through nonzero entries of the column
      for(int k = colVector.size() - 1; k >= 0; k--)
      {
         const Rational& value = colVector.value(k);

         if(spxAbs(value) > maxValue)
         {
            MSG_DEBUG(std::cout << "   --> nonzero " << k << " has value " << value.str() << " in row " <<
                      colVector.index(k) << "\n");

            // add new column equal to maxValue times original column
            if(!addedLiftingRow)
            {
               MSG_DEBUG(std::cout << "            --> adding lifting row\n");

               assert(liftingRowVector.size() == 0);

               liftingColumnIndex = numColsRational();
               liftingRowVector.add(i, maxValue);
               liftingRowVector.add(liftingColumnIndex, -1);

               _rationalLP->addRow(LPRowRational(0, liftingRowVector, 0));
               _realLP->addRow(LPRowBase<R>(0.0, DSVectorBase<R>(liftingRowVector), 0.0));

               assert(liftingColumnIndex == numColsRational() - 1);
               assert(liftingColumnIndex == numCols() - 1);

               _rationalLP->changeBounds(liftingColumnIndex, _rationalNegInfty, _rationalPosInfty);
               _realLP->changeBounds(liftingColumnIndex, -realParam(SoPlexBase<R>::INFTY),
                                     realParam(SoPlexBase<R>::INFTY));

               liftingRowVector.clear();
               addedLiftingRow = true;
            }

            // get row index
            int rowIndex = colVector.index(k);
            assert(rowIndex >= 0);
            assert(rowIndex < _beforeLiftRows);
            assert(liftingColumnIndex == numColsRational() - 1);

            MSG_DEBUG(std::cout << "            --> changing matrix\n");

            // remove nonzero from original column
            _rationalLP->changeElement(rowIndex, i, 0);
            _realLP->changeElement(rowIndex, i, 0.0);

            // add nonzero divided by maxValue to new column
            Rational newValue(value);
            newValue /= maxValue;
            _rationalLP->changeElement(rowIndex, liftingColumnIndex, newValue);
            _realLP->changeElement(rowIndex, liftingColumnIndex, R(newValue));
         }
      }
   }

   // search each column for small nonzeros entries
   const Rational minValue = Rational(realParam(SoPlexBase<R>::LIFTMINVAL));

   for(int i = 0; i < numColsRational(); i++)
   {
      MSG_DEBUG(std::cout << "in lifting: examining column " << i << "\n");

      // get column vector
      colVector = colVectorRational(i);

      bool addedLiftingRow = false;
      int liftingColumnIndex = -1;

      // go through nonzero entries of the column
      for(int k = colVector.size() - 1; k >= 0; k--)
      {
         const Rational& value = colVector.value(k);

         if(spxAbs(value) < minValue)
         {
            MSG_DEBUG(std::cout << "   --> nonzero " << k << " has value " << value.str() << " in row " <<
                      colVector.index(k) << "\n");

            // add new column equal to maxValue times original column
            if(!addedLiftingRow)
            {
               MSG_DEBUG(std::cout << "            --> adding lifting row\n");

               assert(liftingRowVector.size() == 0);

               liftingColumnIndex = numColsRational();
               liftingRowVector.add(i, minValue);
               liftingRowVector.add(liftingColumnIndex, -1);

               _rationalLP->addRow(LPRowRational(0, liftingRowVector, 0));
               _realLP->addRow(LPRowBase<R>(0.0, DSVectorBase<R>(liftingRowVector), 0.0));

               assert(liftingColumnIndex == numColsRational() - 1);
               assert(liftingColumnIndex == numCols() - 1);

               _rationalLP->changeBounds(liftingColumnIndex, _rationalNegInfty, _rationalPosInfty);
               _realLP->changeBounds(liftingColumnIndex, -realParam(SoPlexBase<R>::INFTY),
                                     realParam(SoPlexBase<R>::INFTY));

               liftingRowVector.clear();
               addedLiftingRow = true;
            }

            // get row index
            int rowIndex = colVector.index(k);
            assert(rowIndex >= 0);
            assert(rowIndex < _beforeLiftRows);
            assert(liftingColumnIndex == numColsRational() - 1);

            MSG_DEBUG(std::cout << "            --> changing matrix\n");

            // remove nonzero from original column
            _rationalLP->changeElement(rowIndex, i, 0);
            _realLP->changeElement(rowIndex, i, 0.0);

            // add nonzero divided by maxValue to new column
            Rational newValue(value);
            newValue /= minValue;
            _rationalLP->changeElement(rowIndex, liftingColumnIndex, newValue);
            _realLP->changeElement(rowIndex, liftingColumnIndex, R(newValue));
         }
      }
   }

   // adjust basis
   if(_hasBasis)
   {
      assert(numColsRational() >= _beforeLiftCols);
      assert(numRowsRational() >= _beforeLiftRows);

      _basisStatusCols.append(numColsRational() - _beforeLiftCols, SPxSolverBase<R>::BASIC);
      _basisStatusRows.append(numRowsRational() - _beforeLiftRows, SPxSolverBase<R>::FIXED);
      _rationalLUSolver.clear();
   }

   MSG_DEBUG(_realLP->writeFileLPBase("afterLift.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();

   if(numColsRational() > _beforeLiftCols || numRowsRational() > _beforeLiftRows)
   {
      MSG_INFO1(spxout, spxout << "Added " << numColsRational() - _beforeLiftCols << " columns and "
                << numRowsRational() - _beforeLiftRows << " rows to reduce large matrix coefficients\n.");
   }
}



/// undoes lifting
template <class R>
void SoPlexBase<R>::_project(SolRational& sol)
{
   // start timing
   _statistics->transformTime->start();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("beforeProject.lp", 0, 0, 0));

   assert(numColsRational() >= _beforeLiftCols);
   assert(numRowsRational() >= _beforeLiftRows);

   // shrink rational LP to original size
   _rationalLP->removeColRange(_beforeLiftCols, numColsRational() - 1);
   _rationalLP->removeRowRange(_beforeLiftRows, numRowsRational() - 1);

   // shrink real LP to original size
   _realLP->removeColRange(_beforeLiftCols, numColsReal() - 1);
   _realLP->removeRowRange(_beforeLiftRows, numRowsReal() - 1);

   // adjust solution
   if(sol.isPrimalFeasible())
   {
      sol._primal.reDim(_beforeLiftCols);
      sol._slacks.reDim(_beforeLiftRows);
   }

   if(sol.hasPrimalRay())
   {
      sol._primalRay.reDim(_beforeLiftCols);
   }

   ///@todo if we know the mapping between original and lifting columns, we simply need to add the reduced cost of
   ///      the lifting column to the reduced cost of the original column; this is not implemented now, because for
   ///      optimal solutions the reduced costs of the lifting columns are zero
   const Rational maxValue = Rational(realParam(SoPlexBase<R>::LIFTMAXVAL));

   for(int i = _beforeLiftCols; i < numColsRational() && sol._isDualFeasible; i++)
   {
      if(spxAbs(Rational(maxValue * sol._redCost[i])) > _rationalOpttol)
      {
         MSG_INFO1(spxout, spxout << "Warning: lost dual solution during project phase.\n");
         sol._isDualFeasible = false;
      }
   }

   if(sol.isDualFeasible())
   {
      sol._redCost.reDim(_beforeLiftCols);
      sol._dual.reDim(_beforeLiftRows);
   }

   if(sol.hasDualFarkas())
   {
      sol._dualFarkas.reDim(_beforeLiftRows);
   }

   // adjust basis
   for(int i = _beforeLiftCols; i < numColsRational() && _hasBasis; i++)
   {
      if(_basisStatusCols[i] != SPxSolverBase<R>::BASIC)
      {
         MSG_INFO1(spxout, spxout <<
                   "Warning: lost basis during project phase because of nonbasic lifting column.\n");
         _hasBasis = false;
         _rationalLUSolver.clear();
      }
   }

   for(int i = _beforeLiftRows; i < numRowsRational() && _hasBasis; i++)
   {
      if(_basisStatusRows[i] == SPxSolverBase<R>::BASIC)
      {
         MSG_INFO1(spxout, spxout <<
                   "Warning: lost basis during project phase because of basic lifting row.\n");
         _hasBasis = false;
         _rationalLUSolver.clear();
      }
   }

   if(_hasBasis)
   {
      _basisStatusCols.reSize(_beforeLiftCols);
      _basisStatusRows.reSize(_beforeLiftRows);
      _rationalLUSolver.clear();
   }

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("afterProject.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();
}



/// stores objective, bounds, and sides of real LP
template <class R>
void SoPlexBase<R>::_storeLPReal()
{
#ifndef SOPLEX_MANUAL_ALT

   if(intParam(SoPlexBase<R>::SYNCMODE) == SYNCMODE_MANUAL)
   {
      _manualRealLP = *_realLP;
      return;
   }

#endif

   _manualLower = _realLP->lower();
   _manualUpper = _realLP->upper();
   _manualLhs = _realLP->lhs();
   _manualRhs = _realLP->rhs();
   _manualObj.reDim(_realLP->nCols());
   _realLP->getObj(_manualObj);
}



/// restores objective, bounds, and sides of real LP
template <class R>
void SoPlexBase<R>::_restoreLPReal()
{
   if(intParam(SoPlexBase<R>::SYNCMODE) == SYNCMODE_MANUAL)
   {
#ifndef SOPLEX_MANUAL_ALT
      _solver.loadLP(_manualRealLP);
#else
      _realLP->changeLower(_manualLower);
      _realLP->changeUpper(_manualUpper);
      _realLP->changeLhs(_manualLhs);
      _realLP->changeRhs(_manualRhs);
      _realLP->changeObj(_manualObj);
#endif

      if(_hasBasis)
      {
         // in manual sync mode, if the right-hand side of an equality constraint is not floating-point
         // representable, the user might have constructed the constraint in the real LP by rounding down the
         // left-hand side and rounding up the right-hand side; if the basis status is fixed, we need to adjust it
         for(int i = 0; i < _solver.nRows(); i++)
         {
            if(_basisStatusRows[i] == SPxSolverBase<R>::FIXED && _solver.lhs(i) != _solver.rhs(i))
            {
               assert(_solver.rhs(i) == spxNextafter(_solver.lhs(i), R(infinity)));

               if(_hasSolRational && _solRational.isDualFeasible()
                     && ((intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MAXIMIZE
                          && _solRational._dual[i] > 0)
                         || (intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MINIMIZE
                             && _solRational._dual[i] < 0)))
               {
                  _basisStatusRows[i] = SPxSolverBase<R>::ON_UPPER;
               }
               else
               {
                  _basisStatusRows[i] = SPxSolverBase<R>::ON_LOWER;
               }
            }
         }

         _solver.setBasis(_basisStatusRows.get_const_ptr(), _basisStatusCols.get_const_ptr());
         _hasBasis = (_solver.basis().status() > SPxBasisBase<R>::NO_PROBLEM);
      }
   }
   else
   {
      _realLP->changeLower(_manualLower);
      _realLP->changeUpper(_manualUpper);
      _realLP->changeLhs(_manualLhs);
      _realLP->changeRhs(_manualRhs);
      _realLP->changeObj(_manualObj);
   }
}



/// introduces slack variables to transform inequality constraints into equations for both rational and real LP,
/// which should be in sync
template <class R>
void SoPlexBase<R>::_transformEquality()
{
   MSG_DEBUG(std::cout << "Transforming rows to equation form.\n");

   // start timing
   _statistics->transformTime->start();

   MSG_DEBUG(_realLP->writeFileLPBase("beforeTransEqu.lp", 0, 0, 0));

   // clear array of slack columns
   _slackCols.clear();

   // add artificial slack variables to convert inequality to equality constraints
   for(int i = 0; i < numRowsRational(); i++)
   {
      assert((lhsRational(i) == rhsRational(i)) == (_rowTypes[i] == RANGETYPE_FIXED));

      if(_rowTypes[i] != RANGETYPE_FIXED)
      {
         _slackCols.add(_rationalZero, -rhsRational(i), *_unitVectorRational(i), -lhsRational(i));

         if(_rationalLP->lhs(i) != 0)
            _rationalLP->changeLhs(i, _rationalZero);

         if(_rationalLP->rhs(i) != 0)
            _rationalLP->changeRhs(i, _rationalZero);

         assert(_rationalLP->lhs(i) == 0);
         assert(_rationalLP->rhs(i) == 0);
         _realLP->changeRange(i, R(0.0), R(0.0));
         _colTypes.append(_switchRangeType(_rowTypes[i]));
         _rowTypes[i] = RANGETYPE_FIXED;
      }
   }

   _rationalLP->addCols(_slackCols);
   _realLP->addCols(_slackCols);

   // adjust basis
   if(_hasBasis)
   {
      for(int i = 0; i < _slackCols.num(); i++)
      {
         int row = _slackCols.colVector(i).index(0);

         assert(row >= 0);
         assert(row < numRowsRational());

         switch(_basisStatusRows[row])
         {
         case SPxSolverBase<R>::ON_LOWER:
            _basisStatusCols.append(SPxSolverBase<R>::ON_UPPER);
            break;

         case SPxSolverBase<R>::ON_UPPER:
            _basisStatusCols.append(SPxSolverBase<R>::ON_LOWER);
            break;

         case SPxSolverBase<R>::BASIC:
         case SPxSolverBase<R>::FIXED:
         default:
            _basisStatusCols.append(_basisStatusRows[row]);
            break;
         }

         _basisStatusRows[row] = SPxSolverBase<R>::FIXED;
      }

      _rationalLUSolver.clear();
   }

   MSG_DEBUG(_realLP->writeFileLPBase("afterTransEqu.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();

   if(_slackCols.num() > 0)
   {
      MSG_INFO1(spxout, spxout << "Added " << _slackCols.num() <<
                " slack columns to transform rows to equality form.\n");
   }
}



/// restores original problem
template <class R>
void SoPlexBase<R>::_untransformEquality(SolRational& sol)
{
   // start timing
   _statistics->transformTime->start();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("beforeUntransEqu.lp", 0, 0, 0));

   int numCols = numColsRational();
   int numOrigCols = numColsRational() - _slackCols.num();

   // adjust solution
   if(sol.isPrimalFeasible())
   {
      for(int i = 0; i < _slackCols.num(); i++)
      {
         int col = numOrigCols + i;
         int row = _slackCols.colVector(i).index(0);

         assert(row >= 0);
         assert(row < numRowsRational());

         sol._slacks[row] -= sol._primal[col];
      }

      sol._primal.reDim(numOrigCols);
   }

   if(sol.hasPrimalRay())
   {
      sol._primalRay.reDim(numOrigCols);
   }

   // adjust basis
   if(_hasBasis)
   {
      for(int i = 0; i < _slackCols.num(); i++)
      {
         int col = numOrigCols + i;
         int row = _slackCols.colVector(i).index(0);

         assert(row >= 0);
         assert(row < numRowsRational());
         assert(_basisStatusRows[row] != SPxSolverBase<R>::UNDEFINED);
         assert(_basisStatusRows[row] != SPxSolverBase<R>::ZERO || lhsRational(row) == 0);
         assert(_basisStatusRows[row] != SPxSolverBase<R>::ZERO || rhsRational(row) == 0);
         assert(_basisStatusRows[row] != SPxSolverBase<R>::BASIC
                || _basisStatusCols[col] != SPxSolverBase<R>::BASIC);

         MSG_DEBUG(std::cout << "slack column " << col << " for row " << row
                   << ": col status=" << _basisStatusCols[col]
                   << ", row status=" << _basisStatusRows[row]
                   << ", redcost=" << sol._redCost[col].str()
                   << ", dual=" << sol._dual[row].str() << "\n");

         if(_basisStatusRows[row] != SPxSolverBase<R>::BASIC)
         {
            switch(_basisStatusCols[col])
            {
            case SPxSolverBase<R>::ON_LOWER:
               _basisStatusRows[row] = SPxSolverBase<R>::ON_UPPER;
               break;

            case SPxSolverBase<R>::ON_UPPER:
               _basisStatusRows[row] = SPxSolverBase<R>::ON_LOWER;
               break;

            case SPxSolverBase<R>::BASIC:
            case SPxSolverBase<R>::FIXED:
            default:
               _basisStatusRows[row] = _basisStatusCols[col];
               break;
            }
         }
      }

      _basisStatusCols.reSize(numOrigCols);

      if(_slackCols.num() > 0)
         _rationalLUSolver.clear();
   }

   // not earlier because of debug message
   if(sol.isDualFeasible())
   {
      sol._redCost.reDim(numOrigCols);
   }

   // restore sides and remove slack columns
   for(int i = 0; i < _slackCols.num(); i++)
   {
      int col = numOrigCols + i;
      int row = _slackCols.colVector(i).index(0);

      if(upperRational(col) != 0)
         _rationalLP->changeLhs(row, -upperRational(col));

      if(lowerRational(col) != 0)
         _rationalLP->changeRhs(row, -lowerRational(col));

      assert(_rationalLP->lhs(row) == -upperRational(col));
      assert(_rationalLP->rhs(row) == -lowerRational(col));
      _rowTypes[row] = _switchRangeType(_colTypes[col]);
   }

   _rationalLP->removeColRange(numOrigCols, numCols - 1);
   _realLP->removeColRange(numOrigCols, numCols - 1);
   _colTypes.reSize(numOrigCols);

   // objective, bounds, and sides of real LP are restored only after _solveRational()

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("afterUntransEqu.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();
}



/// transforms LP to unboundedness problem by moving the objective function to the constraints, changing right-hand
/// side and bounds to zero, and adding an auxiliary variable for the decrease in the objective function
template <class R>
void SoPlexBase<R>::_transformUnbounded()
{
   MSG_INFO1(spxout, spxout << "Setting up LP to compute primal unbounded ray.\n");

   // start timing
   _statistics->transformTime->start();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("beforeTransUnbounded.lp", 0, 0, 0));

   // store bounds
   _unboundedLower.reDim(numColsRational());
   _unboundedUpper.reDim(numColsRational());

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      if(_lowerFinite(_colTypes[c]))
         _unboundedLower[c] = lowerRational(c);

      if(_upperFinite(_colTypes[c]))
         _unboundedUpper[c] = upperRational(c);
   }

   // store sides
   _unboundedLhs.reDim(numRowsRational());
   _unboundedRhs.reDim(numRowsRational());

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      if(_lowerFinite(_rowTypes[r]))
         _unboundedLhs[r] = lhsRational(r);

      if(_upperFinite(_rowTypes[r]))
         _unboundedRhs[r] = rhsRational(r);
   }

   // make right-hand side zero
   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      assert((lhsRational(r) > _rationalNegInfty) == _lowerFinite(_rowTypes[r]));

      if(_lowerFinite(_rowTypes[r]))
      {
         _rationalLP->changeLhs(r, Rational(0));
         _realLP->changeLhs(r, R(0.0));
      }
      else if(_realLP->lhs(r) > -realParam(SoPlexBase<R>::INFTY))
         _realLP->changeLhs(r, -realParam(SoPlexBase<R>::INFTY));

      assert((rhsRational(r) < _rationalPosInfty) == _upperFinite(_rowTypes[r]));

      if(_upperFinite(_rowTypes[r]))
      {
         _rationalLP->changeRhs(r, Rational(0));
         _realLP->changeRhs(r, R(0.0));
      }
      else if(_realLP->rhs(r) < realParam(SoPlexBase<R>::INFTY))
         _realLP->changeRhs(r, realParam(SoPlexBase<R>::INFTY));
   }

   // transform objective function to constraint and add auxiliary variable
   int numOrigCols = numColsRational();
   DSVectorRational obj(numOrigCols + 1);
   ///@todo implement this without copying the objective function
   obj = _rationalLP->maxObj();
   obj.add(numOrigCols, -1);
   _rationalLP->addRow(LPRowRational(0, obj, 0));
   _realLP->addRow(LPRowBase<R>(0, DSVectorBase<R>(obj), 0));
   _rowTypes.append(RANGETYPE_FIXED);

   assert(numColsRational() == numOrigCols + 1);

   // set objective coefficient and bounds for auxiliary variable
   _rationalLP->changeMaxObj(numOrigCols, Rational(1));
   _realLP->changeMaxObj(numOrigCols, R(1.0));

   _rationalLP->changeBounds(numOrigCols, _rationalNegInfty, 1);
   _realLP->changeBounds(numOrigCols, -realParam(SoPlexBase<R>::INFTY), 1.0);
   _colTypes.append(RANGETYPE_UPPER);

   // set objective coefficients to zero and adjust bounds for problem variables
   for(int c = numColsRational() - 2; c >= 0; c--)
   {
      _rationalLP->changeObj(c, Rational(0));
      _realLP->changeObj(c, R(0.0));

      assert((lowerRational(c) > _rationalNegInfty) == _lowerFinite(_colTypes[c]));

      if(_lowerFinite(_colTypes[c]))
      {
         _rationalLP->changeLower(c, Rational(0));
         _realLP->changeLower(c, R(0.0));
      }
      else if(_realLP->lower(c) > -realParam(SoPlexBase<R>::INFTY))
         _realLP->changeLower(c, -realParam(SoPlexBase<R>::INFTY));

      assert((upperRational(c) < _rationalPosInfty) == _upperFinite(_colTypes[c]));

      if(_upperFinite(_colTypes[c]))
      {
         _rationalLP->changeUpper(c, Rational(0));
         _realLP->changeUpper(c, R(0.0));
      }
      else if(_realLP->upper(c) < realParam(SoPlexBase<R>::INFTY))
         _realLP->changeUpper(c, realParam(SoPlexBase<R>::INFTY));
   }

   // adjust basis
   if(_hasBasis)
   {
      _basisStatusCols.append(SPxSolverBase<R>::ON_UPPER);
      _basisStatusRows.append(SPxSolverBase<R>::BASIC);
      _rationalLUSolver.clear();
   }

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("afterTransUnbounded.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();
}



/// undoes transformation to unboundedness problem
template <class R>
void SoPlexBase<R>::_untransformUnbounded(SolRational& sol, bool unbounded)
{
   // start timing
   _statistics->transformTime->start();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("beforeUntransUnbounded.lp", 0, 0, 0));

   int numOrigCols = numColsRational() - 1;
   int numOrigRows = numRowsRational() - 1;
   const Rational& tau = sol._primal[numOrigCols];

   // adjust solution and basis
   if(unbounded)
   {
      assert(tau >= _rationalPosone);

      sol._isPrimalFeasible = false;
      sol._hasPrimalRay = true;
      sol._isDualFeasible = false;
      sol._hasDualFarkas = false;

      if(tau != 1)
         sol._primal /= tau;

      sol._primalRay = sol._primal;
      sol._primalRay.reDim(numOrigCols);

      _hasBasis = (_basisStatusCols[numOrigCols] != SPxSolverBase<R>::BASIC
                   && _basisStatusRows[numOrigRows] == SPxSolverBase<R>::BASIC);
      _basisStatusCols.reSize(numOrigCols);
      _basisStatusRows.reSize(numOrigRows);
   }
   else if(boolParam(SoPlexBase<R>::TESTDUALINF) && tau < _rationalFeastol)
   {
      const Rational& alpha = sol._dual[numOrigRows];

      assert(sol._isDualFeasible);
      assert(alpha <= _rationalFeastol - _rationalPosone);

      sol._isPrimalFeasible = false;
      sol._hasPrimalRay = false;
      sol._hasDualFarkas = false;

      if(alpha != -1)
      {
         sol._dual /= -alpha;
         sol._redCost /= -alpha;
      }

      sol._dual.reDim(numOrigRows);
      sol._redCost.reDim(numOrigCols);
   }
   else
   {
      sol.invalidate();
      _hasBasis = false;
      _basisStatusCols.reSize(numOrigCols);
      _basisStatusCols.reSize(numOrigRows);
   }

   // recover objective function
   const SVectorRational& objRowVector = _rationalLP->rowVector(numOrigRows);

   for(int i = objRowVector.size() - 1; i >= 0; i--)
   {
      _rationalLP->changeMaxObj(objRowVector.index(i), objRowVector.value(i));
      _realLP->changeMaxObj(objRowVector.index(i), R(objRowVector.value(i)));
   }

   // remove objective function constraint and auxiliary variable
   _rationalLP->removeRow(numOrigRows);
   _realLP->removeRow(numOrigRows);
   _rowTypes.reSize(numOrigRows);

   _rationalLP->removeCol(numOrigCols);
   _realLP->removeCol(numOrigCols);
   _colTypes.reSize(numOrigCols);

   // restore objective, sides and bounds
   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      if(_lowerFinite(_rowTypes[r]))
      {
         _rationalLP->changeLhs(r, _unboundedLhs[r]);
         _realLP->changeLhs(r, R(_unboundedLhs[r]));
      }

      if(_upperFinite(_rowTypes[r]))
      {
         _rationalLP->changeRhs(r, _unboundedRhs[r]);
         _realLP->changeRhs(r, R(_unboundedRhs[r]));
      }

      assert((lhsRational(r) > _rationalNegInfty) == _lowerFinite(_rowTypes[r]));
      assert((rhsRational(r) < _rationalPosInfty) == _upperFinite(_rowTypes[r]));
      assert((lhsReal(r) > -realParam(SoPlexBase<R>::INFTY)) == _lowerFinite(_rowTypes[r]));
      assert((rhsReal(r) < realParam(SoPlexBase<R>::INFTY)) == _upperFinite(_rowTypes[r]));
   }

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      if(_lowerFinite(_colTypes[c]))
      {
         _rationalLP->changeLower(c, _unboundedLower[c]);
         _realLP->changeLower(c, R(_unboundedLower[c]));
      }

      if(_upperFinite(_colTypes[c]))
      {
         _rationalLP->changeUpper(c, _unboundedUpper[c]);
         _realLP->changeUpper(c, R(_unboundedUpper[c]));
      }

      assert((lowerRational(c) > _rationalNegInfty) == _lowerFinite(_colTypes[c]));
      assert((upperRational(c) < _rationalPosInfty) == _upperFinite(_colTypes[c]));
      assert((lowerReal(c) > -realParam(SoPlexBase<R>::INFTY)) == _lowerFinite(_colTypes[c]));
      assert((upperReal(c) < realParam(SoPlexBase<R>::INFTY)) == _upperFinite(_colTypes[c]));
   }

   // invalidate rational basis factorization
   _rationalLUSolver.clear();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("afterUntransUnbounded.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();
}



/// store basis
template <class R>
void SoPlexBase<R>::_storeBasis()
{
   assert(!_storedBasis);

   if(_hasBasis)
   {
      _storedBasis = true;
      _storedBasisStatusCols = _basisStatusCols;
      _storedBasisStatusRows = _basisStatusRows;
   }
   else
      _storedBasis = false;
}



/// restore basis
template <class R>
void SoPlexBase<R>::_restoreBasis()
{
   if(_storedBasis)
   {
      _hasBasis = true;
      _basisStatusCols = _storedBasisStatusCols;
      _basisStatusRows = _storedBasisStatusRows;
      _storedBasis = false;
   }
}



/// transforms LP to feasibility problem by removing the objective function, shifting variables, and homogenizing the
/// right-hand side
template <class R>
void SoPlexBase<R>::_transformFeasibility()
{
   MSG_INFO1(spxout, spxout << "Setting up LP to test for feasibility.\n");

   // start timing
   _statistics->transformTime->start();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("beforeTransFeas.lp", 0, 0, 0));

   // store objective function
   _feasObj.reDim(numColsRational());

   for(int c = numColsRational() - 1; c >= 0; c--)
      _feasObj[c] = _rationalLP->maxObj(c);

   // store bounds
   _feasLower.reDim(numColsRational());
   _feasUpper.reDim(numColsRational());

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      if(_lowerFinite(_colTypes[c]))
         _feasLower[c] = lowerRational(c);

      if(_upperFinite(_colTypes[c]))
         _feasUpper[c] = upperRational(c);
   }

   // store sides
   _feasLhs.reDim(numRowsRational());
   _feasRhs.reDim(numRowsRational());

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      if(_lowerFinite(_rowTypes[r]))
         _feasLhs[r] = lhsRational(r);

      if(_upperFinite(_rowTypes[r]))
         _feasRhs[r] = rhsRational(r);
   }

   // set objective coefficients to zero; shift primal space such as to guarantee that the zero solution is within
   // the bounds
   Rational shiftValue;
   Rational shiftValue2;

   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      _rationalLP->changeMaxObj(c, Rational(0));
      _realLP->changeMaxObj(c, R(0.0));

      if(lowerRational(c) > 0)
      {
         const SVectorRational& colVector = colVectorRational(c);

         for(int i = 0; i < colVector.size(); i++)
         {
            shiftValue = colVector.value(i);
            shiftValue *= lowerRational(c);
            int r = colVector.index(i);

            assert((lhsRational(r) > _rationalNegInfty) == _lowerFinite(_rowTypes[r]));
            assert((rhsRational(r) < _rationalPosInfty) == _upperFinite(_rowTypes[r]));

            if(_lowerFinite(_rowTypes[r]) && _upperFinite(_rowTypes[r]))
            {
               shiftValue2 = lhsRational(r);
               shiftValue2 -= shiftValue;
               _rationalLP->changeLhs(r, shiftValue2);
               _realLP->changeLhs(r, R(shiftValue2));

               shiftValue -= rhsRational(r);
               shiftValue *= -1;
               _rationalLP->changeRhs(r, shiftValue);
               _realLP->changeRhs(r, R(shiftValue));
            }
            else if(_lowerFinite(_rowTypes[r]))
            {
               shiftValue -= lhsRational(r);
               shiftValue *= -1;
               _rationalLP->changeLhs(r, shiftValue);
               _realLP->changeLhs(r, R(shiftValue));
            }
            else if(_upperFinite(_rowTypes[r]))
            {
               shiftValue -= rhsRational(r);
               shiftValue *= -1;
               _rationalLP->changeRhs(r, shiftValue);
               _realLP->changeRhs(r, R(shiftValue));
            }
         }

         assert((upperRational(c) < _rationalPosInfty) == _upperFinite(_colTypes[c]));

         if(_upperFinite(_colTypes[c]))
         {
            _rationalLP->changeBounds(c, 0, upperRational(c) - lowerRational(c));
            _realLP->changeBounds(c, 0.0, R(upperRational(c)));
         }
         else if(_realLP->upper(c) < realParam(SoPlexBase<R>::INFTY))
         {
            _rationalLP->changeLower(c, Rational(0));
            _realLP->changeBounds(c, 0.0, realParam(SoPlexBase<R>::INFTY));
         }
         else
         {
            _rationalLP->changeLower(c, Rational(0));
            _realLP->changeLower(c, R(0.0));
         }
      }
      else if(upperRational(c) < 0)
      {
         const SVectorRational& colVector = colVectorRational(c);

         for(int i = 0; i < colVector.size(); i++)
         {
            shiftValue = colVector.value(i);
            shiftValue *= upperRational(c);
            int r = colVector.index(i);

            assert((lhsRational(r) > _rationalNegInfty) == _lowerFinite(_rowTypes[r]));
            assert((rhsRational(r) < _rationalPosInfty) == _upperFinite(_rowTypes[r]));

            if(_lowerFinite(_rowTypes[r]) && _upperFinite(_rowTypes[r]))
            {
               shiftValue2 = lhsRational(r);
               shiftValue2 -= shiftValue;
               _rationalLP->changeLhs(r, shiftValue2);
               _realLP->changeLhs(r, R(shiftValue2));

               shiftValue -= rhsRational(r);
               shiftValue *= -1;
               _rationalLP->changeRhs(r, shiftValue);
               _realLP->changeRhs(r, R(shiftValue));
            }
            else if(_lowerFinite(_rowTypes[r]))
            {
               shiftValue -= lhsRational(r);
               shiftValue *= -1;
               _rationalLP->changeLhs(r, shiftValue);
               _realLP->changeLhs(r, R(shiftValue));
            }
            else if(_upperFinite(_rowTypes[r]))
            {
               shiftValue -= rhsRational(r);
               shiftValue *= -1;
               _rationalLP->changeRhs(r, shiftValue);
               _realLP->changeRhs(r, R(shiftValue));
            }
         }

         assert((lowerRational(c) > _rationalNegInfty) == _lowerFinite(_colTypes[c]));

         if(_lowerFinite(_colTypes[c]))
         {
            _rationalLP->changeBounds(c, lowerRational(c) - upperRational(c), 0);
            _realLP->changeBounds(c, R(lowerRational(c)), 0.0);
         }
         else if(_realLP->lower(c) > -realParam(SoPlexBase<R>::INFTY))
         {
            _rationalLP->changeUpper(c, Rational(0));
            _realLP->changeBounds(c, -realParam(SoPlexBase<R>::INFTY), 0.0);
         }
         else
         {
            _rationalLP->changeUpper(c, Rational(0));
            _realLP->changeUpper(c, R(0.0));
         }
      }
      else
      {
         if(_lowerFinite(_colTypes[c]))
            _realLP->changeLower(c, R(lowerRational(c)));
         else if(_realLP->lower(c) > -realParam(SoPlexBase<R>::INFTY))
            _realLP->changeLower(c, -realParam(SoPlexBase<R>::INFTY));

         if(_upperFinite(_colTypes[c]))
            _realLP->changeUpper(c, R(upperRational(c)));
         else if(_realLP->upper(c) < realParam(SoPlexBase<R>::INFTY))
            _realLP->changeUpper(c, realParam(SoPlexBase<R>::INFTY));
      }

      assert(lowerReal(c) <= upperReal(c));
   }

   // homogenize sides
   _tauColVector.clear();

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      if(lhsRational(r) > 0)
      {
         _tauColVector.add(r, lhsRational(r));
         assert((rhsRational(r) < _rationalPosInfty) == _upperFinite(_rowTypes[r]));

         if(_upperFinite(_rowTypes[r]))
         {
            _rationalLP->changeRange(r, 0, rhsRational(r) - lhsRational(r));
            _realLP->changeRange(r, 0.0, R(rhsRational(r)));
         }
         else
         {
            _rationalLP->changeLhs(r, Rational(0));
            _realLP->changeLhs(r, R(0.0));

            if(_realLP->rhs(r) < realParam(SoPlexBase<R>::INFTY))
               _realLP->changeRhs(r, realParam(SoPlexBase<R>::INFTY));
         }
      }
      else if(rhsRational(r) < 0)
      {
         _tauColVector.add(r, rhsRational(r));
         assert((lhsRational(r) > _rationalNegInfty) == _lowerFinite(_rowTypes[r]));

         if(_lowerFinite(_rowTypes[r]))
         {
            _rationalLP->changeRange(r, lhsRational(r) - rhsRational(r), 0);
            _realLP->changeRange(r, R(lhsRational(r)), 0.0);
         }
         else
         {
            _rationalLP->changeRhs(r, Rational(0));
            _realLP->changeRhs(r, R(0.0));

            if(_realLP->lhs(r) > -realParam(SoPlexBase<R>::INFTY))
               _realLP->changeLhs(r, -realParam(SoPlexBase<R>::INFTY));
         }
      }
      else
      {
         if(_lowerFinite(_rowTypes[r]))
            _realLP->changeLhs(r, R(lhsRational(r)));
         else if(_realLP->lhs(r) > -realParam(SoPlexBase<R>::INFTY))
            _realLP->changeLhs(r, -realParam(SoPlexBase<R>::INFTY));

         if(_upperFinite(_rowTypes[r]))
            _realLP->changeRhs(r, R(rhsRational(r)));
         else if(_realLP->rhs(r) < realParam(SoPlexBase<R>::INFTY))
            _realLP->changeRhs(r, realParam(SoPlexBase<R>::INFTY));
      }

      assert(rhsReal(r) <= rhsReal(r));
   }

   ///@todo exploit this case by returning without LP solving
   if(_tauColVector.size() == 0)
   {
      MSG_INFO3(spxout, spxout << "LP is trivially feasible.\n");
   }

   // add artificial column
   SPxColId id;
   _tauColVector *= -1;
   _rationalLP->addCol(id,
                       LPColRational((intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MAXIMIZE ?
                                      _rationalPosone : _rationalNegone),
                                     _tauColVector, 1, 0));
   _realLP->addCol(id,
                   LPColBase<R>((intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MAXIMIZE ? 1.0 : -1.0),
                                DSVectorBase<R>(_tauColVector), 1.0, 0.0));
   _colTypes.append(RANGETYPE_BOXED);

   // adjust basis
   if(_hasBasis)
   {
      _basisStatusCols.append(SPxSolverBase<R>::ON_UPPER);
   }

   // invalidate rational basis factorization
   _rationalLUSolver.clear();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("afterTransFeas.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();
}



/// undoes transformation to feasibility problem
template <class R>
void SoPlexBase<R>::_untransformFeasibility(SolRational& sol, bool infeasible)
{
   // start timing
   _statistics->transformTime->start();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("beforeUntransFeas.lp", 0, 0, 0));

   int numOrigCols = numColsRational() - 1;

   // adjust solution and basis
   if(infeasible)
   {
      assert(sol._isDualFeasible);
      assert(sol._primal[numOrigCols] < 1);

      sol._isPrimalFeasible = false;
      sol._hasPrimalRay = false;
      sol._isDualFeasible = false;
      sol._hasDualFarkas = true;

      sol._dualFarkas = sol._dual;

      _hasBasis = false;
      _basisStatusCols.reSize(numOrigCols);
   }
   else if(sol._isPrimalFeasible)
   {
      assert(sol._primal[numOrigCols] >= 1);

      sol._hasPrimalRay = false;
      sol._isDualFeasible = false;
      sol._hasDualFarkas = false;

      if(sol._primal[numOrigCols] != 1)
      {
         sol._slacks /= sol._primal[numOrigCols];

         for(int i = 0; i < numOrigCols; i++)
            sol._primal[i] /= sol._primal[numOrigCols];

         sol._primal[numOrigCols] = 1;
      }

      sol._primal.reDim(numOrigCols);
      sol._slacks -= _rationalLP->colVector(numOrigCols);

      _hasBasis = (_basisStatusCols[numOrigCols] != SPxSolverBase<R>::BASIC);
      _basisStatusCols.reSize(numOrigCols);
   }
   else
   {
      _hasBasis = false;
      _basisStatusCols.reSize(numOrigCols);
   }

   // restore right-hand side
   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      assert(rhsRational(r) >= _rationalPosInfty || lhsRational(r) <= _rationalNegInfty
             || _feasLhs[r] - lhsRational(r) == _feasRhs[r] - rhsRational(r));

      if(_lowerFinite(_rowTypes[r]))
      {
         _rationalLP->changeLhs(r, _feasLhs[r]);
         _realLP->changeLhs(r, R(_feasLhs[r]));
      }
      else if(_realLP->lhs(r) > -realParam(SoPlexBase<R>::INFTY))
         _realLP->changeLhs(r, -realParam(SoPlexBase<R>::INFTY));

      assert(_lowerFinite(_rowTypes[r]) == (lhsRational(r) > _rationalNegInfty));
      assert(_lowerFinite(_rowTypes[r]) == (lhsReal(r) > -realParam(SoPlexBase<R>::INFTY)));

      if(_upperFinite(_rowTypes[r]))
      {
         _rationalLP->changeRhs(r, _feasRhs[r]);
         _realLP->changeRhs(r, R(_feasRhs[r]));
      }
      else if(_realLP->rhs(r) < realParam(SoPlexBase<R>::INFTY))
         _realLP->changeRhs(r, realParam(SoPlexBase<R>::INFTY));

      assert(_upperFinite(_rowTypes[r]) == (rhsRational(r) < _rationalPosInfty));
      assert(_upperFinite(_rowTypes[r]) == (rhsReal(r) < realParam(SoPlexBase<R>::INFTY)));

      assert(lhsReal(r) <= rhsReal(r));
   }

   // unshift primal space and restore objective coefficients
   Rational shiftValue;

   for(int c = numOrigCols - 1; c >= 0; c--)
   {
      bool shifted = (_lowerFinite(_colTypes[c]) && _feasLower[c] > 0) || (_upperFinite(_colTypes[c])
                     && _feasUpper[c] < 0);
      assert(shifted || !_lowerFinite(_colTypes[c]) || _feasLower[c] == lowerRational(c));
      assert(shifted || !_upperFinite(_colTypes[c]) || _feasUpper[c] == upperRational(c));
      assert(upperRational(c) >= _rationalPosInfty || lowerRational(c) <= _rationalNegInfty
             || _feasLower[c] - lowerRational(c) == _feasUpper[c] - upperRational(c));

      if(shifted)
      {
         if(_lowerFinite(_colTypes[c]))
         {
            shiftValue = _feasLower[c];
            shiftValue -= lowerRational(c);
         }
         else if(_upperFinite(_colTypes[c]))
         {
            shiftValue = _feasUpper[c];
            shiftValue -= upperRational(c);
         }

         if(sol._isPrimalFeasible)
         {
            sol._primal[c] += shiftValue;
            sol._slacks.multAdd(shiftValue, _rationalLP->colVector(c));
         }
      }

      if(_lowerFinite(_colTypes[c]))
      {
         if(shifted)
            _rationalLP->changeLower(c, _feasLower[c]);

         _realLP->changeLower(c, R(_feasLower[c]));
      }
      else if(_realLP->lower(c) > -realParam(SoPlexBase<R>::INFTY))
         _realLP->changeLower(c, -realParam(SoPlexBase<R>::INFTY));

      assert(_lowerFinite(_colTypes[c]) == (lowerRational(c) > -_rationalPosInfty));
      assert(_lowerFinite(_colTypes[c]) == (lowerReal(c) > -realParam(SoPlexBase<R>::INFTY)));

      if(_upperFinite(_colTypes[c]))
      {
         if(shifted)
            _rationalLP->changeUpper(c, _feasUpper[c]);

         _realLP->changeUpper(c, R(upperRational(c)));
      }
      else if(_realLP->upper(c) < realParam(SoPlexBase<R>::INFTY))
         _realLP->changeUpper(c, realParam(SoPlexBase<R>::INFTY));

      assert(_upperFinite(_colTypes[c]) == (upperRational(c) < _rationalPosInfty));
      assert(_upperFinite(_colTypes[c]) == (upperReal(c) < realParam(SoPlexBase<R>::INFTY)));

      _rationalLP->changeMaxObj(c, _feasObj[c]);
      _realLP->changeMaxObj(c, R(_feasObj[c]));

      assert(lowerReal(c) <= upperReal(c));
   }

   // remove last column
   _rationalLP->removeCol(numOrigCols);
   _realLP->removeCol(numOrigCols);
   _colTypes.reSize(numOrigCols);

   // invalidate rational basis factorization
   _rationalLUSolver.clear();

   // print LP if in debug mode
   MSG_DEBUG(_realLP->writeFileLPBase("afterUntransFeas.lp", 0, 0, 0));

   // stop timing
   _statistics->transformTime->stop();

#ifndef NDEBUG

   if(sol._isPrimalFeasible)
   {
      VectorRational activity(numRowsRational());
      _rationalLP->computePrimalActivity(sol._primal, activity);
      assert(sol._slacks == activity);
   }

#endif
}

/** computes radius of infeasibility box implied by an approximate Farkas' proof

 Given constraints of the form \f$ lhs <= Ax <= rhs \f$, a farkas proof y should satisfy \f$ y^T A = 0 \f$ and
 \f$ y_+^T lhs - y_-^T rhs > 0 \f$, where \f$ y_+, y_- \f$ denote the positive and negative parts of \f$ y \f$.
 If \f$ y \f$ is approximate, it may not satisfy \f$ y^T A = 0 \f$ exactly, but the proof is still valid as long
 as the following holds for all potentially feasible \f$ x \f$:

 \f[
    y^T Ax < (y_+^T lhs - y_-^T rhs)              (*)
 \f]

 we may therefore calculate \f$ y^T A \f$ and \f$ y_+^T lhs - y_-^T rhs \f$ exactly and check if the upper and lower
 bounds on \f$ x \f$ imply that all feasible \f$ x \f$ satisfy (*), and if not then compute bounds on \f$ x \f$ to
 guarantee (*).  The simplest way to do this is to compute

 \f[
    B = (y_+^T lhs - y_-^T rhs) / \sum_i(|(y^T A)_i|)
 \f]

 noting that if every component of \f$ x \f$ has \f$ |x_i| < B \f$, then (*) holds.

 \f$ B \f$ can be increased by iteratively including variable bounds smaller than \f$ B \f$.  The speed of this
 method can be further improved by using interval arithmetic for all computations.  For related information see
 Sec. 4 of Neumaier and Shcherbina, Mathematical Programming A, 2004.

 Set transformed to true if this method is called after _transformFeasibility().
*/
template <class R>
void SoPlexBase<R>::_computeInfeasBox(SolRational& sol, bool transformed)
{
   assert(sol.hasDualFarkas());

   const VectorRational& lower = transformed ? _feasLower : lowerRational();
   const VectorRational& upper = transformed ? _feasUpper : upperRational();
   const VectorRational& lhs = transformed ? _feasLhs : lhsRational();
   const VectorRational& rhs = transformed ? _feasRhs : rhsRational();
   const VectorRational& y = sol._dualFarkas;

   const int numRows = numRowsRational();
   const int numCols = transformed ? numColsRational() - 1 : numColsRational();

   SSVectorRational ytransA(numColsRational());
   Rational ytransb;
   Rational temp;

   // prepare ytransA and ytransb; since we want exact arithmetic, we set the zero threshold of the semi-sparse
   // vector to zero
   ytransA.setEpsilon(0);
   ytransA.clear();
   ytransb = 0;

   ///@todo this currently works only if all constraints are equations aggregate rows and sides using the multipliers of the Farkas ray
   for(int r = 0; r < numRows; r++)
   {
      ytransA += y[r] * _rationalLP->rowVector(r);
      ytransb += y[r] * (y[r] > 0 ? lhs[r] : rhs[r]);
   }

   // if we work on the feasibility problem, we ignore the last column
   if(transformed)
      ytransA.reDim(numCols);

   MSG_DEBUG(std::cout << "ytransb = " << ytransb.str() << "\n");

   // if we choose minus ytransb as vector of multipliers for the bound constraints on the variables, we obtain an
   // exactly feasible dual solution for the LP with zero objective function; we aggregate the bounds of the
   // variables accordingly and store its negation in temp
   temp = 0;
   bool isTempFinite = true;

   for(int c = 0; c < numCols && isTempFinite; c++)
   {
      const Rational& minusRedCost = ytransA[c];

      if(minusRedCost > 0)
      {
         assert((upper[c] < _rationalPosInfty) == _upperFinite(_colTypes[c]));

         if(_upperFinite(_colTypes[c]))
            temp += minusRedCost * upper[c];
         else
            isTempFinite = false;
      }
      else if(minusRedCost < 0)
      {
         assert((lower[c] > _rationalNegInfty) == _lowerFinite(_colTypes[c]));

         if(_lowerFinite(_colTypes[c]))
            temp += minusRedCost * lower[c];
         else
            isTempFinite = false;
      }
   }

   MSG_DEBUG(std::cout << "max ytransA*[x_l,x_u] = " << (isTempFinite ? temp.str() : "infinite") <<
             "\n");

   // ytransb - temp is the increase in the dual objective along the Farkas ray; if this is positive, the dual is
   // unbounded and certifies primal infeasibility
   if(isTempFinite && temp < ytransb)
   {
      MSG_INFO1(spxout, spxout << "Farkas infeasibility proof verified exactly. (1)\n");
      return;
   }

   // ensure that array of nonzero elements in ytransA is available
   assert(ytransA.isSetup());
   ytransA.setup();

   // if ytransb is negative, try to make it zero by including a positive lower bound or a negative upper bound
   if(ytransb < 0)
   {
      for(int c = 0; c < numCols; c++)
      {
         if(lower[c] > 0)
         {
            ytransA.setValue(c, ytransA[c] - ytransb / lower[c]);
            ytransb = 0;
            break;
         }
         else if(upper[c] < 0)
         {
            ytransA.setValue(c, ytransA[c] - ytransb / upper[c]);
            ytransb = 0;
            break;
         }
      }
   }

   // if ytransb is still zero then the zero solution is inside the bounds and cannot be cut off by the Farkas
   // constraint; in this case, we cannot compute a Farkas box
   if(ytransb < 0)
   {
      MSG_INFO1(spxout, spxout <<
                "Approximate Farkas proof to weak.  Could not compute Farkas box. (1)\n");
      return;
   }

   // compute the one norm of ytransA
   temp = 0;
   const int size = ytransA.size();

   for(int n = 0; n < size; n++)
      temp += spxAbs(ytransA.value(n));

   // if the one norm is zero then ytransA is zero the Farkas proof should have been verified above
   assert(temp != 0);

   // initialize variables in loop: size of Farkas box B, flag whether B has been increased, and number of current
   // nonzero in ytransA
   Rational B = ytransb / temp;
   bool success = false;
   int n = 0;

   // loop through nonzeros of ytransA
   MSG_DEBUG(std::cout << "B = " << B.str() << "\n");
   assert(ytransb >= 0);

   while(true)
   {
      // if all nonzeros have been inspected once without increasing B, we abort; otherwise, we start another round
      if(n >= ytransA.size())
      {
         if(!success)
            break;

         success = false;
         n = 0;
      }

      // get Farkas multiplier of the bound constraint as minus the value in ytransA
      const Rational& minusRedCost = ytransA.value(n);
      int colIdx = ytransA.index(n);

      // if the multiplier is positive we inspect the lower bound: if it is finite and within the Farkas box, we can
      // increase B by including it in the Farkas proof
      assert((upper[colIdx] < _rationalPosInfty) == _upperFinite(_colTypes[colIdx]));
      assert((lower[colIdx] > _rationalNegInfty) == _lowerFinite(_colTypes[colIdx]));

      if(minusRedCost < 0 && lower[colIdx] > -B && _lowerFinite(_colTypes[colIdx]))
      {
         ytransA.clearNum(n);
         ytransb -= minusRedCost * lower[colIdx];
         temp += minusRedCost;

         assert(ytransb >= 0);
         assert(temp >= 0);
         assert(temp == 0 || ytransb / temp > B);

         // if ytransA and ytransb are zero, we have 0^T x >= 0 and cannot compute a Farkas box
         if(temp == 0 && ytransb == 0)
         {
            MSG_INFO1(spxout, spxout <<
                      "Approximate Farkas proof to weak.  Could not compute Farkas box. (2)\n");
            return;
         }
         // if ytransb is positive and ytransA is zero, we have 0^T x > 0, proving infeasibility
         else if(temp == 0)
         {
            assert(ytransb > 0);
            MSG_INFO1(spxout, spxout << "Farkas infeasibility proof verified exactly. (2)\n");
            return;
         }
         else
         {
            B = ytransb / temp;
            MSG_DEBUG(std::cout << "B = " << B.str() << "\n");
         }

         success = true;
      }
      // if the multiplier is negative we inspect the upper bound: if it is finite and within the Farkas box, we can
      // increase B by including it in the Farkas proof
      else if(minusRedCost > 0 && upper[colIdx] < B && _upperFinite(_colTypes[colIdx]))
      {
         ytransA.clearNum(n);
         ytransb -= minusRedCost * upper[colIdx];
         temp -= minusRedCost;

         assert(ytransb >= 0);
         assert(temp >= 0);
         assert(temp == 0 || ytransb / temp > B);

         // if ytransA and ytransb are zero, we have 0^T x >= 0 and cannot compute a Farkas box
         if(temp == 0 && ytransb == 0)
         {
            MSG_INFO1(spxout, spxout <<
                      "Approximate Farkas proof to weak.  Could not compute Farkas box. (2)\n");
            return;
         }
         // if ytransb is positive and ytransA is zero, we have 0^T x > 0, proving infeasibility
         else if(temp == 0)
         {
            assert(ytransb > 0);
            MSG_INFO1(spxout, spxout << "Farkas infeasibility proof verified exactly. (2)\n");
            return;
         }
         else
         {
            B = ytransb / temp;
            MSG_DEBUG(std::cout << "B = " << B.str() << "\n");
         }

         success = true;
      }
      // the multiplier is zero, we can ignore the bound constraints on this variable
      else if(minusRedCost == 0)
         ytransA.clearNum(n);
      // currently this bound cannot be used to increase B; we will check it again in the next round, because B might
      // have increased by then
      else
         n++;
   }

   if(B > 0)
   {
      MSG_INFO1(spxout, spxout <<
                "Computed Farkas box: provably no feasible solutions with components less than "
                << B.str() << " in absolute value.\n");
   }
}




// General specializations
/// solves real LP during iterative refinement
template <class R>
typename SPxSolverBase<R>::Status SoPlexBase<R>::_solveRealForRational(bool fromscratch,
      VectorBase<R>& primal, VectorBase<R>& dual,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusRows,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusCols)
{
   assert(_isConsistent());

   assert(_solver.nRows() == numRowsRational());
   assert(_solver.nCols() == numColsRational());
   assert(primal.dim() == numColsRational());
   assert(dual.dim() == numRowsRational());

   typename SPxSolverBase<R>::Status result = SPxSolverBase<R>::UNKNOWN;

#ifndef SOPLEX_MANUAL_ALT

   if(fromscratch || !_hasBasis)
      _enableSimplifierAndScaler();
   else
      _disableSimplifierAndScaler();

#else
   _disableSimplifierAndScaler();
#endif

   // reset basis to slack basis when solving from scratch
   if(fromscratch)
      _solver.reLoad();

   // start timing
   _statistics->syncTime->start();

   // if preprocessing is applied, we need to restore the original LP at the end
   SPxLPRational* rationalLP = 0;

   if(_simplifier != 0 || _scaler != nullptr)
   {
      spx_alloc(rationalLP);
      rationalLP = new(rationalLP) SPxLPRational(_solver);
   }

   // with preprocessing or solving from scratch, the basis may change, hence invalidate the
   // rational basis factorization
   if(_simplifier != nullptr || _scaler != nullptr || fromscratch)
      _rationalLUSolver.clear();

   // stop timing
   _statistics->syncTime->stop();

   try
   {
      // apply problem simplification
      typename SPxSimplifier<R>::Result simplificationStatus = SPxSimplifier<R>::OKAY;

      if(_simplifier != 0)
      {
         // do not remove bounds of boxed variables or sides of ranged rows if bound flipping is used
         bool keepbounds = intParam(SoPlexBase<R>::RATIOTESTER) == SoPlexBase<R>::RATIOTESTER_BOUNDFLIPPING;
         Real remainingTime = _solver.getMaxTime() - _solver.time();
         simplificationStatus = _simplifier->simplify(_solver, realParam(SoPlexBase<R>::EPSILON_ZERO),
                                realParam(SoPlexBase<R>::FPFEASTOL), realParam(SoPlexBase<R>::FPOPTTOL), remainingTime, keepbounds,
                                _solver.random.getSeed());
      }

      // apply scaling after the simplification
      if(_scaler != nullptr && simplificationStatus == SPxSimplifier<R>::OKAY)
         _scaler->scale(_solver, false);

      // run the simplex method if problem has not been solved by the simplifier
      if(simplificationStatus == SPxSimplifier<R>::OKAY)
      {
         MSG_INFO1(spxout, spxout << std::endl);

         _solveRealLPAndRecordStatistics();

         MSG_INFO1(spxout, spxout << std::endl);
      }

      ///@todo move to private helper methods
      // evaluate status flag
      if(simplificationStatus == SPxSimplifier<R>::INFEASIBLE)
         result = SPxSolverBase<R>::INFEASIBLE;
      else if(simplificationStatus == SPxSimplifier<R>::DUAL_INFEASIBLE)
         result = SPxSolverBase<R>::INForUNBD;
      else if(simplificationStatus == SPxSimplifier<R>::UNBOUNDED)
         result = SPxSolverBase<R>::UNBOUNDED;
      else if(simplificationStatus == SPxSimplifier<R>::VANISHED
              || simplificationStatus == SPxSimplifier<R>::OKAY)
      {
         result = simplificationStatus == SPxSimplifier<R>::VANISHED ? SPxSolverBase<R>::OPTIMAL :
                  _solver.status();

         ///@todo move to private helper methods
         // process result
         switch(result)
         {
         case SPxSolverBase<R>::OPTIMAL:

            // unsimplify if simplifier is active and LP is solved to optimality; this must be done here and not at solution
            // query, because we want to have the basis for the original problem
            if(_simplifier != 0)
            {
               assert(!_simplifier->isUnsimplified());
               assert(simplificationStatus == SPxSimplifier<R>::VANISHED
                      || simplificationStatus == SPxSimplifier<R>::OKAY);

               bool vanished = simplificationStatus == SPxSimplifier<R>::VANISHED;

               // get solution vectors for transformed problem
               VectorBase<R> tmpPrimal(vanished ? 0 : _solver.nCols());
               VectorBase<R> tmpSlacks(vanished ? 0 : _solver.nRows());
               VectorBase<R> tmpDual(vanished ? 0 : _solver.nRows());
               VectorBase<R> tmpRedCost(vanished ? 0 : _solver.nCols());

               if(!vanished)
               {
                  assert(_solver.status() == SPxSolverBase<R>::OPTIMAL);

                  _solver.getPrimalSol(tmpPrimal);
                  _solver.getSlacks(tmpSlacks);
                  _solver.getDualSol(tmpDual);
                  _solver.getRedCostSol(tmpRedCost);

                  // unscale vectors
                  if(_scaler != nullptr)
                  {
                     _scaler->unscalePrimal(_solver, tmpPrimal);
                     _scaler->unscaleSlacks(_solver, tmpSlacks);
                     _scaler->unscaleDual(_solver, tmpDual);
                     _scaler->unscaleRedCost(_solver, tmpRedCost);
                  }

                  // get basis of transformed problem
                  basisStatusRows.reSize(_solver.nRows());
                  basisStatusCols.reSize(_solver.nCols());
                  _solver.getBasis(basisStatusRows.get_ptr(), basisStatusCols.get_ptr(), basisStatusRows.size(),
                                   basisStatusCols.size());
               }

               ///@todo catch exception
               _simplifier->unsimplify(tmpPrimal, tmpDual, tmpSlacks, tmpRedCost, basisStatusRows.get_ptr(),
                                       basisStatusCols.get_ptr());

               // store basis for original problem
               basisStatusRows.reSize(numRowsRational());
               basisStatusCols.reSize(numColsRational());
               _simplifier->getBasis(basisStatusRows.get_ptr(), basisStatusCols.get_ptr(), basisStatusRows.size(),
                                     basisStatusCols.size());
               _hasBasis = true;

               primal = _simplifier->unsimplifiedPrimal();
               dual = _simplifier->unsimplifiedDual();
            }
            else
            {
               _solver.getPrimalSol(primal);
               _solver.getDualSol(dual);

               // unscale vectors
               if(_scaler != nullptr)
               {
                  _scaler->unscalePrimal(_solver, primal);
                  _scaler->unscaleDual(_solver, dual);
               }

               // get basis of transformed problem
               basisStatusRows.reSize(_solver.nRows());
               basisStatusCols.reSize(_solver.nCols());
               _solver.getBasis(basisStatusRows.get_ptr(), basisStatusCols.get_ptr(), basisStatusRows.size(),
                                basisStatusCols.size());
               _hasBasis = true;
            }

            break;

         case SPxSolverBase<R>::ABORT_CYCLING:
            if(_simplifier == 0 && boolParam(SoPlexBase<R>::ACCEPTCYCLING))
            {
               _solver.getPrimalSol(primal);
               _solver.getDualSol(dual);

               // unscale vectors
               if(_scaler != nullptr)
               {
                  _scaler->unscalePrimal(_solver, primal);
                  _scaler->unscaleDual(_solver, dual);
               }
            }

         // intentional fallthrough
         case SPxSolverBase<R>::ABORT_TIME:
         case SPxSolverBase<R>::ABORT_ITER:
         case SPxSolverBase<R>::ABORT_VALUE:
         case SPxSolverBase<R>::REGULAR:
         case SPxSolverBase<R>::RUNNING:
         case SPxSolverBase<R>::UNBOUNDED:
            _hasBasis = (_solver.basis().status() > SPxBasisBase<R>::NO_PROBLEM);

            if(_hasBasis && _simplifier == 0)
            {
               basisStatusRows.reSize(_solver.nRows());
               basisStatusCols.reSize(_solver.nCols());
               _solver.getBasis(basisStatusRows.get_ptr(), basisStatusCols.get_ptr(), basisStatusRows.size(),
                                basisStatusCols.size());
            }
            else
            {
               _hasBasis = false;
               _rationalLUSolver.clear();
            }

            break;

         case SPxSolverBase<R>::INFEASIBLE:

            // if simplifier is active we can currently not return a Farkas ray or basis
            if(_simplifier != 0)
            {
               _hasBasis = false;
               _rationalLUSolver.clear();
               break;
            }

            // return Farkas ray as dual solution
            _solver.getDualfarkas(dual);

            // unscale vectors
            if(_scaler != nullptr)
               _scaler->unscaleDual(_solver, dual);

            // get basis of transformed problem
            basisStatusRows.reSize(_solver.nRows());
            basisStatusCols.reSize(_solver.nCols());
            _solver.getBasis(basisStatusRows.get_ptr(), basisStatusCols.get_ptr(), basisStatusRows.size(),
                             basisStatusCols.size());
            _hasBasis = true;
            break;

         case SPxSolverBase<R>::INForUNBD:
         case SPxSolverBase<R>::SINGULAR:
         default:
            _hasBasis = false;
            _rationalLUSolver.clear();
            break;
         }
      }
   }
   catch(...)
   {
      MSG_INFO1(spxout, spxout << "Exception thrown during floating-point solve.\n");
      result = SPxSolverBase<R>::ERROR;
      _hasBasis = false;
      _rationalLUSolver.clear();

   }

   // restore original LP if necessary
   if(_simplifier != 0 || _scaler != nullptr)
   {
      assert(rationalLP != 0);
      _solver.loadLP((SPxLPBase<R>)(*rationalLP));
      rationalLP->~SPxLPRational();
      spx_free(rationalLP);

      if(_hasBasis)
         _solver.setBasis(basisStatusRows.get_ptr(), basisStatusCols.get_ptr());
   }

   return result;
}

/// solves real LP with recovery mechanism
template <class R>
typename SPxSolverBase<R>::Status SoPlexBase<R>::_solveRealStable(bool acceptUnbounded,
      bool acceptInfeasible, VectorBase<R>& primal, VectorBase<R>& dual,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusRows,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusCols,
      const bool forceNoSimplifier)
{
   typename SPxSolverBase<R>::Status result = SPxSolverBase<R>::UNKNOWN;

   bool fromScratch = false;
   bool solved = false;
   bool solvedFromScratch = false;
   bool initialSolve = true;
   bool increasedMarkowitz = false;
   bool relaxedTolerances = false;
   bool tightenedTolerances = false;
   bool switchedScaler = false;
   bool switchedSimplifier = false;
   bool switchedRatiotester = false;
   bool switchedPricer = false;
   bool turnedoffPre = false;

   R markowitz = _slufactor.markowitz();
   int ratiotester = intParam(SoPlexBase<R>::RATIOTESTER);
   int pricer = intParam(SoPlexBase<R>::PRICER);
   int simplifier = intParam(SoPlexBase<R>::SIMPLIFIER);
   int scaler = intParam(SoPlexBase<R>::SCALER);
   int type = intParam(SoPlexBase<R>::ALGORITHM);

   if(forceNoSimplifier)
      setIntParam(SoPlexBase<R>::SIMPLIFIER, SoPlexBase<R>::SIMPLIFIER_OFF);

   while(true)
   {
      assert(!increasedMarkowitz || GE(_slufactor.markowitz(), R(0.9)));

      result = _solveRealForRational(fromScratch, primal, dual, basisStatusRows, basisStatusCols);

      solved = (result == SPxSolverBase<R>::OPTIMAL)
               || (result == SPxSolverBase<R>::INFEASIBLE && acceptInfeasible)
               || (result == SPxSolverBase<R>::UNBOUNDED && acceptUnbounded);

      if(solved || result == SPxSolverBase<R>::ABORT_TIME || result == SPxSolverBase<R>::ABORT_ITER)
         break;

      if(initialSolve)
      {
         MSG_INFO1(spxout, spxout << "Numerical troubles during floating-point solve." << std::endl);
         initialSolve = false;
      }

      if(!turnedoffPre
            && (intParam(SoPlexBase<R>::SIMPLIFIER) != SoPlexBase<R>::SIMPLIFIER_OFF
                || intParam(SoPlexBase<R>::SCALER) != SoPlexBase<R>::SCALER_OFF))
      {
         MSG_INFO1(spxout, spxout << "Turning off preprocessing." << std::endl);

         turnedoffPre = true;

         setIntParam(SoPlexBase<R>::SCALER, SoPlexBase<R>::SCALER_OFF);
         setIntParam(SoPlexBase<R>::SIMPLIFIER, SoPlexBase<R>::SIMPLIFIER_OFF);

         fromScratch = true;
         solvedFromScratch = true;
         continue;
      }

      setIntParam(SoPlexBase<R>::SCALER, scaler);
      setIntParam(SoPlexBase<R>::SIMPLIFIER, simplifier);

      if(!increasedMarkowitz)
      {
         MSG_INFO1(spxout, spxout << "Increasing Markowitz threshold." << std::endl);

         _slufactor.setMarkowitz(0.9);
         increasedMarkowitz = true;

         try
         {
            _solver.factorize();
            continue;
         }
         catch(...)
         {
            MSG_DEBUG(std::cout << std::endl << "Factorization failed." << std::endl);
         }
      }

      if(!solvedFromScratch)
      {
         MSG_INFO1(spxout, spxout << "Solving from scratch." << std::endl);

         fromScratch = true;
         solvedFromScratch = true;
         continue;
      }

      setIntParam(SoPlexBase<R>::RATIOTESTER, ratiotester);
      setIntParam(SoPlexBase<R>::PRICER, pricer);

      if(!switchedScaler)
      {
         MSG_INFO1(spxout, spxout << "Switching scaling." << std::endl);

         if(scaler == int(SoPlexBase<R>::SCALER_OFF))
            setIntParam(SoPlexBase<R>::SCALER, SoPlexBase<R>::SCALER_BIEQUI);
         else
            setIntParam(SoPlexBase<R>::SCALER, SoPlexBase<R>::SCALER_OFF);

         fromScratch = true;
         solvedFromScratch = true;
         switchedScaler = true;
         continue;
      }

      if(!switchedSimplifier && !forceNoSimplifier)
      {
         MSG_INFO1(spxout, spxout << "Switching simplification." << std::endl);

         if(simplifier == int(SoPlexBase<R>::SIMPLIFIER_OFF))
            setIntParam(SoPlexBase<R>::SIMPLIFIER, SoPlexBase<R>::SIMPLIFIER_INTERNAL);
         else
            setIntParam(SoPlexBase<R>::SIMPLIFIER, SoPlexBase<R>::SIMPLIFIER_OFF);

         fromScratch = true;
         solvedFromScratch = true;
         switchedSimplifier = true;
         continue;
      }

      setIntParam(SoPlexBase<R>::SIMPLIFIER, SoPlexBase<R>::SIMPLIFIER_OFF);

      if(!relaxedTolerances)
      {
         MSG_INFO1(spxout, spxout << "Relaxing tolerances." << std::endl);

         setIntParam(SoPlexBase<R>::ALGORITHM, SoPlexBase<R>::ALGORITHM_PRIMAL);
         _solver.setDelta((_solver.feastol() * 1e3 > 1e-3) ? 1e-3 : (_solver.feastol() * 1e3));
         relaxedTolerances = _solver.feastol() >= 1e-3;
         solvedFromScratch = false;
         continue;
      }

      if(!tightenedTolerances && result != SPxSolverBase<R>::INFEASIBLE)
      {
         MSG_INFO1(spxout, spxout << "Tightening tolerances." << std::endl);

         setIntParam(SoPlexBase<R>::ALGORITHM, SoPlexBase<R>::ALGORITHM_DUAL);
         _solver.setDelta(_solver.feastol() * 1e-3 < 1e-9 ? 1e-9 : _solver.feastol() * 1e-3);
         tightenedTolerances = _solver.feastol() <= 1e-9;
         solvedFromScratch = false;
         continue;
      }

      setIntParam(SoPlexBase<R>::ALGORITHM, type);

      if(!switchedRatiotester)
      {
         MSG_INFO1(spxout, spxout << "Switching ratio test." << std::endl);

         _solver.setType(_solver.type() == SPxSolverBase<R>::LEAVE ? SPxSolverBase<R>::ENTER :
                         SPxSolverBase<R>::LEAVE);

         if(_solver.ratiotester() != (SPxRatioTester<R>*)&_ratiotesterTextbook)
            setIntParam(SoPlexBase<R>::RATIOTESTER, RATIOTESTER_TEXTBOOK);
         else
            setIntParam(SoPlexBase<R>::RATIOTESTER, RATIOTESTER_FAST);

         switchedRatiotester = true;
         solvedFromScratch = false;
         continue;
      }

      if(!switchedPricer)
      {
         MSG_INFO1(spxout, spxout << "Switching pricer." << std::endl);

         _solver.setType(_solver.type() == SPxSolverBase<R>::LEAVE ? SPxSolverBase<R>::ENTER :
                         SPxSolverBase<R>::LEAVE);

         if(_solver.pricer() != (SPxPricer<R>*)&_pricerDevex)
            setIntParam(SoPlexBase<R>::PRICER, PRICER_DEVEX);
         else
            setIntParam(SoPlexBase<R>::PRICER, PRICER_STEEP);

         switchedPricer = true;
         solvedFromScratch = false;
         continue;
      }

      MSG_INFO1(spxout, spxout << "Giving up." << std::endl);

      break;
   }

   _solver.setFeastol(realParam(SoPlexBase<R>::FPFEASTOL));
   _solver.setOpttol(realParam(SoPlexBase<R>::FPOPTTOL));
   _slufactor.setMarkowitz(markowitz);

   setIntParam(SoPlexBase<R>::RATIOTESTER, ratiotester);
   setIntParam(SoPlexBase<R>::PRICER, pricer);
   setIntParam(SoPlexBase<R>::SIMPLIFIER, simplifier);
   setIntParam(SoPlexBase<R>::SCALER, scaler);
   setIntParam(SoPlexBase<R>::ALGORITHM, type);

   return result;
}

/// computes rational inverse of basis matrix as defined by _rationalLUSolverBind
template <class R>
void SoPlexBase<R>::_computeBasisInverseRational()
{
   assert(_rationalLUSolver.status() == SLinSolverRational::UNLOADED
          || _rationalLUSolver.status() == SLinSolverRational::TIME);

   const int matrixdim = numRowsRational();
   assert(_rationalLUSolverBind.size() == matrixdim);

   Array< const SVectorRational* > matrix(matrixdim);
   _rationalLUSolverBind.reSize(matrixdim);

   for(int i = 0; i < matrixdim; i++)
   {
      if(_rationalLUSolverBind[i] >= 0)
      {
         assert(_rationalLUSolverBind[i] < numColsRational());
         matrix[i] = &colVectorRational(_rationalLUSolverBind[i]);
      }
      else
      {
         assert(-1 - _rationalLUSolverBind[i] >= 0);
         assert(-1 - _rationalLUSolverBind[i] < numRowsRational());
         matrix[i] = _unitVectorRational(-1 - _rationalLUSolverBind[i]);
      }
   }

   // load and factorize rational basis matrix
   if(realParam(SoPlexBase<R>::TIMELIMIT) < realParam(SoPlexBase<R>::INFTY))
      _rationalLUSolver.setTimeLimit((double)realParam(SoPlexBase<R>::TIMELIMIT) -
                                     _statistics->solvingTime->time());
   else
      _rationalLUSolver.setTimeLimit(-1.0);

   _rationalLUSolver.load(matrix.get_ptr(), matrixdim);

   // record statistics
   _statistics->luFactorizationTimeRational += _rationalLUSolver.getFactorTime();
   _statistics->luFactorizationsRational += _rationalLUSolver.getFactorCount();
   _rationalLUSolver.resetCounters();

   if(_rationalLUSolver.status() == SLinSolverRational::TIME)
   {
      MSG_INFO2(spxout, spxout << "Rational factorization hit time limit.\n");
   }
   else if(_rationalLUSolver.status() != SLinSolverRational::OK)
   {
      MSG_INFO1(spxout, spxout << "Error performing rational LU factorization.\n");
   }

   return;
}



/// factorizes rational basis matrix in column representation
template <class R>
void SoPlexBase<R>::_factorizeColumnRational(SolRational& sol,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusRows,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusCols, bool& stoppedTime,
      bool& stoppedIter, bool& error, bool& optimal)
{
   // start rational solving time
   _statistics->rationalTime->start();

   stoppedTime = false;
   stoppedIter = false;
   error = false;
   optimal = false;

   const bool maximizing = (intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MAXIMIZE);
   const int matrixdim = numRowsRational();
   bool loadMatrix = (_rationalLUSolver.status() == SLinSolverRational::UNLOADED
                      || _rationalLUSolver.status() == SLinSolverRational::TIME);
   int numBasicRows;

   assert(loadMatrix || matrixdim == _rationalLUSolver.dim());
   assert(loadMatrix || matrixdim == _rationalLUSolverBind.size());

   if(!loadMatrix && (matrixdim != _rationalLUSolver.dim()
                      || matrixdim != _rationalLUSolverBind.size()))
   {
      MSG_WARNING(spxout, spxout <<
                  "Warning: dimensioning error in rational matrix factorization (recovered).\n");
      loadMatrix = true;
   }

   _workSol._primal.reDim(matrixdim);
   _workSol._slacks.reDim(matrixdim);
   _workSol._dual.reDim(matrixdim);
   _workSol._redCost.reDim(numColsRational() > matrixdim ? numColsRational() : matrixdim);

   if(loadMatrix)
      _rationalLUSolverBind.reSize(matrixdim);

   VectorRational& basicPrimalRhs = _workSol._slacks;
   VectorRational& basicDualRhs = _workSol._redCost;
   VectorRational& basicPrimal = _workSol._primal;
   VectorRational& basicDual = _workSol._dual;

   Rational violation;
   Rational primalViolation;
   Rational dualViolation;
   bool primalFeasible = false;
   bool dualFeasible = false;

   assert(basisStatusCols.size() == numColsRational());
   assert(basisStatusRows.size() == numRowsRational());

   int j = 0;

   for(int i = 0; i < basisStatusRows.size(); i++)
   {
      if(basisStatusRows[i] == SPxSolverBase<R>::BASIC && j < matrixdim)
      {
         basicPrimalRhs[i] = 0;
         basicDualRhs[j] = 0;

         if(loadMatrix)
            _rationalLUSolverBind[j] = -1 - i;

         j++;
      }
      else if(basisStatusRows[i] == SPxSolverBase<R>::ON_LOWER)
         basicPrimalRhs[i] = lhsRational(i);
      else if(basisStatusRows[i] == SPxSolverBase<R>::ON_UPPER)
         basicPrimalRhs[i] = rhsRational(i);
      else if(basisStatusRows[i] == SPxSolverBase<R>::ZERO)
         basicPrimalRhs[i] = 0;
      else if(basisStatusRows[i] == SPxSolverBase<R>::FIXED)
      {
         assert(lhsRational(i) == rhsRational(i));
         basicPrimalRhs[i] = lhsRational(i);
      }
      else if(basisStatusRows[i] == SPxSolverBase<R>::UNDEFINED)
      {
         MSG_INFO1(spxout, spxout << "Undefined basis status of row in rational factorization.\n");
         error = true;
         goto TERMINATE;
      }
      else
      {
         assert(basisStatusRows[i] == SPxSolverBase<R>::BASIC);
         MSG_INFO1(spxout, spxout << "Too many basic rows in rational factorization.\n");
         error = true;
         goto TERMINATE;
      }
   }

   numBasicRows = j;

   for(int i = 0; i < basisStatusCols.size(); i++)
   {
      if(basisStatusCols[i] == SPxSolverBase<R>::BASIC && j < matrixdim)
      {
         basicDualRhs[j] = objRational(i);

         if(loadMatrix)
            _rationalLUSolverBind[j] = i;

         j++;
      }
      else if(basisStatusCols[i] == SPxSolverBase<R>::ON_LOWER)
         basicPrimalRhs.multAdd(-lowerRational(i), colVectorRational(i));
      else if(basisStatusCols[i] == SPxSolverBase<R>::ON_UPPER)
         basicPrimalRhs.multAdd(-upperRational(i), colVectorRational(i));
      else if(basisStatusCols[i] == SPxSolverBase<R>::ZERO)
      {}
      else if(basisStatusCols[i] == SPxSolverBase<R>::FIXED)
      {
         assert(lowerRational(i) == upperRational(i));
         basicPrimalRhs.multAdd(-lowerRational(i), colVectorRational(i));
      }
      else if(basisStatusCols[i] == SPxSolverBase<R>::UNDEFINED)
      {
         MSG_INFO1(spxout, spxout << "Undefined basis status of column in rational factorization.\n");
         error = true;
         goto TERMINATE;
      }
      else
      {
         assert(basisStatusCols[i] == SPxSolverBase<R>::BASIC);
         MSG_INFO1(spxout, spxout << "Too many basic columns in rational factorization.\n");
         error = true;
         goto TERMINATE;
      }
   }

   if(j != matrixdim)
   {
      MSG_INFO1(spxout, spxout << "Too few basic entries in rational factorization.\n");
      error = true;
      goto TERMINATE;
   }

   // load and factorize rational basis matrix
   if(loadMatrix)
      _computeBasisInverseRational();

   if(_rationalLUSolver.status() == SLinSolverRational::TIME)
   {
      stoppedTime = true;
      return;
   }
   else if(_rationalLUSolver.status() != SLinSolverRational::OK)
   {
      error = true;
      return;
   }

   assert(_rationalLUSolver.status() == SLinSolverRational::OK);

   // solve for primal solution
   if(realParam(SoPlexBase<R>::TIMELIMIT) < realParam(SoPlexBase<R>::INFTY))
      _rationalLUSolver.setTimeLimit(Real(realParam(SoPlexBase<R>::TIMELIMIT)) -
                                     _statistics->solvingTime->time());
   else
      _rationalLUSolver.setTimeLimit(-1.0);

   _rationalLUSolver.solveRight(basicPrimal, basicPrimalRhs);

   // record statistics
   _statistics->luSolveTimeRational += _rationalLUSolver.getSolveTime();
   _rationalLUSolver.resetCounters();

   if(_isSolveStopped(stoppedTime, stoppedIter))
   {
      MSG_INFO2(spxout, spxout << "Rational factorization hit time limit while solving for primal.\n");
      return;
   }

   // check bound violation on basic rows and columns
   j = 0;
   primalViolation = 0;
   primalFeasible = true;

   for(int i = 0; i < basisStatusRows.size(); i++)
   {
      if(basisStatusRows[i] == SPxSolverBase<R>::BASIC)
      {
         assert(j < matrixdim);
         assert(_rationalLUSolverBind[j] == -1 - i);
         violation = lhsRational(i);
         violation += basicPrimal[j];

         if(violation > primalViolation)
         {
            primalFeasible = false;
            primalViolation = violation;
         }

         violation = rhsRational(i);
         violation += basicPrimal[j];
         violation *= -1;

         if(violation > primalViolation)
         {
            primalFeasible = false;
            primalViolation = violation;
         }

         j++;
      }
   }

   for(int i = 0; i < basisStatusCols.size(); i++)
   {
      if(basisStatusCols[i] == SPxSolverBase<R>::BASIC)
      {
         assert(j < matrixdim);
         assert(_rationalLUSolverBind[j] == i);

         if(basicPrimal[j] < lowerRational(i))
         {
            violation = lowerRational(i);
            violation -= basicPrimal[j];

            if(violation > primalViolation)
            {
               primalFeasible = false;
               primalViolation = violation;
            }
         }

         if(basicPrimal[j] > upperRational(i))
         {
            violation = basicPrimal[j];
            violation -= upperRational(i);

            if(violation > primalViolation)
            {
               primalFeasible = false;
               primalViolation = violation;
            }
         }

         j++;
      }
   }

   if(!primalFeasible)
   {
      MSG_INFO1(spxout, spxout << "Rational solution primal infeasible.\n");
   }

   // solve for dual solution
   if(realParam(SoPlexBase<R>::TIMELIMIT) < realParam(SoPlexBase<R>::INFTY))
      _rationalLUSolver.setTimeLimit(Real(realParam(SoPlexBase<R>::TIMELIMIT)) -
                                     _statistics->solvingTime->time());
   else
      _rationalLUSolver.setTimeLimit(-1.0);

   _rationalLUSolver.solveLeft(basicDual, basicDualRhs);

   // record statistics
   _statistics->luSolveTimeRational += _rationalLUSolver.getSolveTime();
   _rationalLUSolver.resetCounters();

   if(_isSolveStopped(stoppedTime, stoppedIter))
   {
      MSG_INFO2(spxout, spxout << "Rational factorization hit time limit while solving for dual.\n");
      return;
   }

   // check dual violation on nonbasic rows
   dualViolation = 0;
   dualFeasible = true;

   for(int i = 0; i < basisStatusRows.size(); i++)
   {
      if(_rowTypes[i] == RANGETYPE_FIXED
            && (basisStatusRows[i] == SPxSolverBase<R>::ON_LOWER
                || basisStatusRows[i] == SPxSolverBase<R>::ON_UPPER))
      {
         assert(lhsRational(i) == rhsRational(i));
         basisStatusRows[i] = SPxSolverBase<R>::FIXED;
      }

      assert(basisStatusRows[i] != SPxSolverBase<R>::BASIC || basicDual[i] == 0);

      if(basisStatusRows[i] == SPxSolverBase<R>::BASIC || basisStatusRows[i] == SPxSolverBase<R>::FIXED)
         continue;
      else if(basicDual[i] < 0)
      {
         if(((maximizing && basisStatusRows[i] != SPxSolverBase<R>::ON_LOWER) || (!maximizing
               && basisStatusRows[i] != SPxSolverBase<R>::ON_UPPER))
               && (basisStatusRows[i] != SPxSolverBase<R>::ZERO || rhsRational(i) != 0))
         {
            dualFeasible = false;
            violation = -basicDual[i];

            if(violation > dualViolation)
               dualViolation = violation;

            MSG_DEBUG(spxout << "negative dual multliplier for row " << i
                      << " with dual " << basicDual[i].str()
                      << " and status " << basisStatusRows[i]
                      << " and [lhs,rhs] = [" << lhsRational(i).str() << "," << rhsRational(i).str() << "]"
                      << "\n");
         }
      }
      else if(basicDual[i] > 0)
      {
         if(((maximizing && basisStatusRows[i] != SPxSolverBase<R>::ON_UPPER) || (!maximizing
               && basisStatusRows[i] != SPxSolverBase<R>::ON_LOWER))
               && (basisStatusRows[i] != SPxSolverBase<R>::ZERO || lhsRational(i) == 0))
         {
            dualFeasible = false;

            if(basicDual[i] > dualViolation)
               dualViolation = basicDual[i];

            MSG_DEBUG(spxout << "positive dual multliplier for row " << i
                      << " with dual " << basicDual[i].str()
                      << " and status " << basisStatusRows[i]
                      << " and [lhs,rhs] = [" << lhsRational(i).str() << "," << rhsRational(i).str() << "]"
                      << "\n");
         }
      }
   }

   // check reduced cost violation on nonbasic columns
   for(int i = 0; i < basisStatusCols.size(); i++)
   {
      if(_colTypes[i] == RANGETYPE_FIXED
            && (basisStatusCols[i] == SPxSolverBase<R>::ON_LOWER
                || basisStatusCols[i] == SPxSolverBase<R>::ON_UPPER))
      {
         assert(lowerRational(i) == upperRational(i));
         basisStatusCols[i] = SPxSolverBase<R>::FIXED;
      }

      assert(basisStatusCols[i] != SPxSolverBase<R>::BASIC
             || basicDual * colVectorRational(i) == objRational(i));

      if(basisStatusCols[i] == SPxSolverBase<R>::BASIC || basisStatusCols[i] == SPxSolverBase<R>::FIXED)
         continue;
      else
      {
         _workSol._redCost[i] = basicDual * colVectorRational(i);
         _workSol._redCost[i] -= objRational(i);

         if(_workSol._redCost[i] > 0)
         {
            if(((maximizing && basisStatusCols[i] != SPxSolverBase<R>::ON_LOWER) || (!maximizing
                  && basisStatusCols[i] != SPxSolverBase<R>::ON_UPPER))
                  && (basisStatusCols[i] != SPxSolverBase<R>::ZERO || upperRational(i) != 0))
            {
               dualFeasible = false;

               if(_workSol._redCost[i] > dualViolation)
                  dualViolation = _workSol._redCost[i];
            }

            _workSol._redCost[i] *= -1;
         }
         else if(_workSol._redCost[i] < 0)
         {
            _workSol._redCost[i] *= -1;

            if(((maximizing && basisStatusCols[i] != SPxSolverBase<R>::ON_UPPER) || (!maximizing
                  && basisStatusCols[i] != SPxSolverBase<R>::ON_LOWER))
                  && (basisStatusCols[i] != SPxSolverBase<R>::ZERO || lowerRational(i) != 0))
            {
               dualFeasible = false;

               if(_workSol._redCost[i] > dualViolation)
                  dualViolation = _workSol._redCost[i];
            }
         }
         else
            _workSol._redCost[i] *= -1;
      }
   }

   if(!dualFeasible)
   {
      MSG_INFO1(spxout, spxout << "Rational solution dual infeasible.\n");
   }

   // store solution
   optimal = primalFeasible && dualFeasible;

   if(optimal || boolParam(SoPlexBase<R>::RATFACJUMP))
   {
      _hasBasis = true;

      if(&basisStatusRows != &_basisStatusRows)
         _basisStatusRows = basisStatusRows;

      if(&basisStatusCols != &_basisStatusCols)
         _basisStatusCols = basisStatusCols;

      sol._primal.reDim(numColsRational());
      j = numBasicRows;

      for(int i = 0; i < basisStatusCols.size(); i++)
      {
         if(basisStatusCols[i] == SPxSolverBase<R>::BASIC)
         {
            assert(j < matrixdim);
            assert(_rationalLUSolverBind[j] == i);
            sol._primal[i] = basicPrimal[j];
            j++;
         }
         else if(basisStatusCols[i] == SPxSolverBase<R>::ON_LOWER)
            sol._primal[i] = lowerRational(i);
         else if(basisStatusCols[i] == SPxSolverBase<R>::ON_UPPER)
            sol._primal[i] = upperRational(i);
         else if(basisStatusCols[i] == SPxSolverBase<R>::ZERO)
            sol._primal[i] = 0;
         else if(basisStatusCols[i] == SPxSolverBase<R>::FIXED)
         {
            assert(lowerRational(i) == upperRational(i));
            sol._primal[i] = lowerRational(i);
         }
         else
         {
            assert(basisStatusCols[i] == SPxSolverBase<R>::UNDEFINED);
            MSG_INFO1(spxout, spxout << "Undefined basis status of column in rational factorization.\n");
            error = true;
            goto TERMINATE;
         }
      }

      sol._slacks.reDim(numRowsRational());
      _rationalLP->computePrimalActivity(sol._primal, sol._slacks);
      sol._isPrimalFeasible = true;

      sol._dual = basicDual;

      for(int i = 0; i < numColsRational(); i++)
      {
         if(basisStatusCols[i] == SPxSolverBase<R>::BASIC)
            sol._redCost[i] = 0;
         else if(basisStatusCols[i] == SPxSolverBase<R>::FIXED)
         {
            sol._redCost[i] = basicDual * colVectorRational(i);
            sol._redCost[i] -= objRational(i);
            sol._redCost[i] *= -1;
         }
         else
            sol._redCost[i] = _workSol._redCost[i];
      }

      sol._isDualFeasible  = true;
   }
   else
   {
      _rationalLUSolver.clear();
   }


TERMINATE:
   // stop rational solving time
   _statistics->rationalTime->stop();
   return;
}

/// attempts rational reconstruction of primal-dual solution
template <class R>
bool SoPlexBase<R>::_reconstructSolutionRational(SolRational& sol,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusRows,
      DataArray< typename SPxSolverBase<R>::VarStatus >& basisStatusCols,
      const Rational& denomBoundSquared)
{
   bool success;
   bool isSolBasic;
   DIdxSet basicIndices(numColsRational());

   success = false;
   isSolBasic = true;

   if(!sol.isPrimalFeasible() || !sol.isDualFeasible())
      return success;

   // start timing and increment statistics counter
   _statistics->reconstructionTime->start();
   _statistics->rationalReconstructions++;

   // reconstruct primal vector
   _workSol._primal = sol._primal;

   for(int j = 0; j < numColsRational(); ++j)
   {
      if(basisStatusCols[j] == SPxSolverBase<R>::BASIC)
         basicIndices.addIdx(j);
   }

   success = reconstructVector(_workSol._primal, denomBoundSquared, &basicIndices);

   if(!success)
   {
      MSG_INFO1(spxout, spxout << "Rational reconstruction of primal solution failed.\n");
      _statistics->reconstructionTime->stop();
      return success;
   }

   MSG_DEBUG(spxout << "Rational reconstruction of primal solution successful.\n");

   // check violation of bounds
   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      // we want to notify the user whether the reconstructed solution is basic; otherwise, this would be redundant
      typename SPxSolverBase<R>::VarStatus& basisStatusCol = _basisStatusCols[c];

      if((basisStatusCol == SPxSolverBase<R>::FIXED && _workSol._primal[c] != lowerRational(c))
            || (basisStatusCol == SPxSolverBase<R>::ON_LOWER && _workSol._primal[c] != lowerRational(c))
            || (basisStatusCol == SPxSolverBase<R>::ON_UPPER && _workSol._primal[c] != upperRational(c))
            || (basisStatusCol == SPxSolverBase<R>::ZERO && _workSol._primal[c] != 0)
            || (basisStatusCol == SPxSolverBase<R>::UNDEFINED))
      {
         isSolBasic = false;
      }

      if(_lowerFinite(_colTypes[c]) && _workSol._primal[c] < lowerRational(c))
      {
         MSG_DEBUG(std::cout << "Lower bound of variable " << c << " violated by " <<
                   (lowerRational(c) - _workSol._primal[c]).str() << "\n");
         MSG_INFO1(spxout, spxout << "Reconstructed solution primal infeasible (1).\n");
         _statistics->reconstructionTime->stop();
         return false;
      }

      if(_upperFinite(_colTypes[c]) && _workSol._primal[c] > upperRational(c))
      {
         MSG_DEBUG(std::cout << "Upper bound of variable " << c << " violated by " <<
                   (_workSol._primal[c] - upperRational(c)).str() << "\n");
         MSG_INFO1(spxout, spxout << "Reconstructed solution primal infeasible (2).\n");
         _statistics->reconstructionTime->stop();
         return false;
      }
   }

   // compute slacks
   ///@todo we should compute them one by one so we can abort when encountering an infeasibility
   _workSol._slacks.reDim(numRowsRational());
   _rationalLP->computePrimalActivity(_workSol._primal, _workSol._slacks);

   // check violation of sides
   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      // we want to notify the user whether the reconstructed solution is basic; otherwise, this would be redundant
      typename SPxSolverBase<R>::VarStatus& basisStatusRow = _basisStatusRows[r];

      if((basisStatusRow == SPxSolverBase<R>::FIXED && _workSol._slacks[r] != lhsRational(r))
            || (basisStatusRow == SPxSolverBase<R>::ON_LOWER && _workSol._slacks[r] != lhsRational(r))
            || (basisStatusRow == SPxSolverBase<R>::ON_UPPER && _workSol._slacks[r] != rhsRational(r))
            || (basisStatusRow == SPxSolverBase<R>::ZERO && _workSol._slacks[r] != 0)
            || (basisStatusRow == SPxSolverBase<R>::UNDEFINED))
      {
         isSolBasic = false;
      }

      if(_lowerFinite(_rowTypes[r]) && _workSol._slacks[r] < lhsRational(r))
      {
         MSG_DEBUG(std::cout << "Lhs of row " << r << " violated by " <<
                   (lhsRational(r) - _workSol._slacks[r]).str() << "\n");
         MSG_INFO1(spxout, spxout << "Reconstructed solution primal infeasible (3).\n");
         _statistics->reconstructionTime->stop();
         return false;
      }

      if(_upperFinite(_rowTypes[r]) && _workSol._slacks[r] > rhsRational(r))
      {
         MSG_DEBUG(std::cout << "Rhs of row " << r << " violated by " <<
                   (_workSol._slacks[r] - rhsRational(r)) << "\n");
         MSG_INFO1(spxout, spxout << "Reconstructed solution primal infeasible (4).\n");
         _statistics->reconstructionTime->stop();
         return false;
      }
   }

   // reconstruct dual vector
   _workSol._dual = sol._dual;

   success = reconstructVector(_workSol._dual, denomBoundSquared);

   if(!success)
   {
      MSG_INFO1(spxout, spxout << "Rational reconstruction of dual solution failed.\n");
      _statistics->reconstructionTime->stop();
      return success;
   }

   MSG_DEBUG(spxout << "Rational reconstruction of dual vector successful.\n");

   // check dual multipliers before reduced costs because this check is faster since it does not require the
   // computation of reduced costs
   const bool maximizing = (intParam(SoPlexBase<R>::OBJSENSE) == SoPlexBase<R>::OBJSENSE_MAXIMIZE);

   for(int r = numRowsRational() - 1; r >= 0; r--)
   {
      int sig = sign(_workSol._dual[r]);

      if((!maximizing && sig > 0) || (maximizing && sig < 0))
      {
         if(!_lowerFinite(_rowTypes[r]) || _workSol._slacks[r] > lhsRational(r))
         {
            MSG_DEBUG(std::cout << "complementary slackness violated by row " << r
                      << " with dual " << _workSol._dual[r].str()
                      << " and slack " << _workSol._slacks[r].str()
                      << " not at lhs " << lhsRational(r).str()
                      << "\n");
            MSG_INFO1(spxout, spxout << "Reconstructed solution dual infeasible (1).\n");
            _statistics->reconstructionTime->stop();
            return false;
         }

         if(_basisStatusRows[r] != SPxSolverBase<R>::ON_LOWER
               && _basisStatusRows[r] != SPxSolverBase<R>::FIXED)
         {
            if(_basisStatusRows[r] == SPxSolverBase<R>::BASIC
                  || _basisStatusRows[r] == SPxSolverBase<R>::UNDEFINED)
               isSolBasic = false;
            else
               _basisStatusRows[r] = SPxSolverBase<R>::ON_LOWER;
         }
      }
      else if((!maximizing && sig < 0) || (maximizing && sig > 0))
      {
         if(!_upperFinite(_rowTypes[r]) || _workSol._slacks[r] < rhsRational(r))
         {
            MSG_DEBUG(std::cout << "complementary slackness violated by row " << r
                      << " with dual " << _workSol._dual[r].str()
                      << " and slack " << _workSol._slacks[r].str()
                      << " not at rhs " << rhsRational(r).str()
                      << "\n");
            MSG_INFO1(spxout, spxout << "Reconstructed solution dual infeasible (2).\n");
            _statistics->reconstructionTime->stop();
            return false;
         }

         if(_basisStatusRows[r] != SPxSolverBase<R>::ON_UPPER
               && _basisStatusRows[r] != SPxSolverBase<R>::FIXED)
         {
            if(_basisStatusRows[r] == SPxSolverBase<R>::BASIC
                  || _basisStatusRows[r] == SPxSolverBase<R>::UNDEFINED)
               isSolBasic = false;
            else
               _basisStatusRows[r] = SPxSolverBase<R>::ON_UPPER;
         }
      }
   }

   // compute reduced cost vector; we assume that the objective function vector has less nonzeros than the reduced
   // cost vector, and so multiplying with -1 first and subtracting the dual activity should be faster than adding
   // the dual activity and negating afterwards
   ///@todo we should compute them one by one so we can abort when encountering an infeasibility
   _workSol._redCost.reDim(numColsRational());
   _rationalLP->getObj(_workSol._redCost);
   _rationalLP->subDualActivity(_workSol._dual, _workSol._redCost);

   // check reduced cost violation
   for(int c = numColsRational() - 1; c >= 0; c--)
   {
      int sig = sign(_workSol._redCost[c]);

      if((!maximizing && sig > 0) || (maximizing && sig < 0))
      {
         if(!_lowerFinite(_colTypes[c]) || _workSol._primal[c] > lowerRational(c))
         {
            MSG_DEBUG(std::cout << "complementary slackness violated by column " << c
                      << " with reduced cost " << _workSol._redCost[c].str()
                      << " and value " << _workSol._primal[c].str()
                      << " not at lower bound " << lowerRational(c).str()
                      << "\n");
            MSG_INFO1(spxout, spxout << "Reconstructed solution dual infeasible (3).\n");
            _statistics->reconstructionTime->stop();
            return false;
         }

         if(_basisStatusCols[c] != SPxSolverBase<R>::ON_LOWER
               && _basisStatusCols[c] != SPxSolverBase<R>::FIXED)
         {
            if(_basisStatusCols[c] == SPxSolverBase<R>::BASIC
                  || _basisStatusCols[c] == SPxSolverBase<R>::UNDEFINED)
               isSolBasic = false;
            else
               _basisStatusCols[c] = SPxSolverBase<R>::ON_LOWER;
         }
      }
      else if((!maximizing && sig < 0) || (maximizing && sig > 0))
      {
         if(!_upperFinite(_colTypes[c]) || _workSol._primal[c] < upperRational(c))
         {
            MSG_DEBUG(std::cout << "complementary slackness violated by column " << c
                      << " with reduced cost " << _workSol._redCost[c].str()
                      << " and value " << _workSol._primal[c].str()
                      << " not at upper bound " << upperRational(c).str()
                      << "\n");
            MSG_INFO1(spxout, spxout << "Reconstructed solution dual infeasible (4).\n");
            _statistics->reconstructionTime->stop();
            return false;
         }

         if(_basisStatusCols[c] != SPxSolverBase<R>::ON_UPPER
               && _basisStatusCols[c] != SPxSolverBase<R>::FIXED)
         {
            if(_basisStatusCols[c] == SPxSolverBase<R>::BASIC
                  || _basisStatusCols[c] == SPxSolverBase<R>::UNDEFINED)
               isSolBasic = false;
            else
               _basisStatusCols[c] = SPxSolverBase<R>::ON_UPPER;
         }
      }
   }

   // update solution
   sol._primal = _workSol._primal;
   sol._slacks = _workSol._slacks;
   sol._dual = _workSol._dual;
   sol._redCost = _workSol._redCost;

   if(!isSolBasic)
   {
      MSG_WARNING(spxout, spxout << "Warning: Reconstructed solution not basic.\n");
      _hasBasis = false;
   }

   // stop timing
   _statistics->reconstructionTime->stop();

   return success;
}
} // namespace soplex
