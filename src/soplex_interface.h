
#ifdef __cplusplus
extern "C" {
#endif

void* SoPlex_create();

void SoPlex_free(void *soplex);

void SoPlex_clearLPReal(void *soplex);

int SoPlex_numRows(void *soplex);

int SoPlex_numCols(void *soplex);

void SoPlex_setRational(void *soplex);

void SoPlex_setIntParam(void *soplex, int paramcode, int paramvalue);

void SoPlex_addColReal(void *soplex, double* colentries, int colsize, int nnonzeros, double objval, double lb, double ub);

void SoPlex_addColRational(void *soplex, int* colnums, int* coldenoms, int colsize, int nnonzeros, int objvalnum, int objvaldenom, int lbnum, int lbdenom, int ubnum, int ubdenom);

void SoPlex_addRowReal(void *soplex, double* rowentries, int rowsize, int nnonzeros, double lb, double ub);

void SoPlex_addRowRational(void *soplex, int* rownums, int* rowdenoms, int rowsize, int nnonzeros, int lbnum, int lbdenom, int ubnum, int ubdenom);

void SoPlex_getPrimalReal(void *soplex, double* primal, int dim);

char* SoPlex_getPrimalRationalString(void *soplex, int dim);

void SoPlex_getDualReal(void *soplex, double* dual, int dim);

int SoPlex_optimize(void *soplex);

void SoPlex_changeObjReal(void *soplex, double* obj, int dim);

void SoPlex_changeObjRational(void *soplex, int* objnums, int* objdenoms, int dim);

void SoPlex_changeLhsReal(void *soplex, double* lhs, int dim);

void SoPlex_changeLhsRational(void *soplex, int* lhsnums, int* lhsdenoms, int dim);

void SoPlex_changeRhsReal(void *soplex, double* rhs, int dim);

void SoPlex_changeRhsRational(void *soplex, int* rhsnums, int* rhsdenoms, int dim);

void SoPlex_writeFileReal(void *soplex, char* filename);

double SoPlex_objValueReal(void *soplex);

char* SoPlex_objValueRationalString(void *soplex);

void SoPlex_changeBoundsReal(void *soplex, double* lb, double* ub, int dim);

void SoPlex_changeVarBoundsReal(void *soplex, int colidx, double lb, double ub);

void SoPlex_changeVarBoundsRational(void *soplex, int colidx, int lbnum, int lbdenom, int ubnum, int ubdenom);

void SoPlex_changeVarUpperReal(void *soplex, int colidx, double ub);

void SoPlex_getUpperReal(void *soplex, double* ub, int dim);

#ifdef __cplusplus
}
#endif
