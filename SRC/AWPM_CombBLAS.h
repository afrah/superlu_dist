#ifndef AWPM_CombBLAS_h
#define AWPM_CombBLAS_h


#include "CombBLAS/CombBLAS.h"
#include "CombBLAS/BipartiteMatchings/ApproxWeightPerfectMatching.h"
#include "superlu_ddefs.h"


using namespace combblas;

/*! \brief
 *
 * <pre>
 * Purpose
 * =======
 *   Re-distribute A from distributed CSR storage to 2D block storage
 *   conforming CombBLAS API.
 *
 * Arguments
 * =========
 *
 * A      (input) SuperMatrix*
 *        The distributed input matrix A of dimension (A->nrow, A->ncol).
 *        The type of A can be: Stype = SLU_NR_loc; Dtype = SLU_D; Mtype = SLU_GE.
 *
 * perm   (input) int_t*
 *        Permutation vector describing the transformation performed to
 *        the original matrix A.
 *
 * grid   (input) gridinfo_t*
 *        SuperLU's 2D process mesh.
 *
 *
 * Return value
 * ============
 * ScalePermstruct       = ScalePermstruct->perm_r stores the permutation
 *
 * </pre>
 */
int
GetAWPM(SuperMatrix *A, int_t *perm, gridinfo_t *grid, ScalePermstruct_t *ScalePermstruct)
{
    NRformat_loc *Astore;
    int_t  i, irow, fst_row, j, jcol, k, m, n, m_loc;
    int_t lirow, ljcol;
    int_t  nnz_loc;    /* number of local nonzeros */
    int_t  SendCnt; /* number of remote nonzeros to be sent */
    int_t  RecvCnt; /* number of remote nonzeros to be sent */
    double *nzval_a;
    int    iam, it, p, procs;
    procs = grid->nprow * grid->npcol;
    
    combblas::SpParMat < int_t, double, combblas::SpDCCols<int_t,double> > Adcsc(MPI_COMM_WORLD);
    std::vector< std::vector < std::tuple<int_t,int_t,double> > > data(procs);
    
    /* ------------------------------------------------------------
     INITIALIZATION.
     ------------------------------------------------------------*/
    iam = grid->iam;
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(iam, "Enter pdCSR_loc_to_2DBlock()");
#endif
    Astore = (NRformat_loc *) A->Store;
    n = A->ncol;
    m = A->nrow;
    m_loc = Astore->m_loc;
    fst_row = Astore->fst_row;
    
    /* ------------------------------------------------------------
     FIRST PASS OF A:
     COUNT THE NUMBER OF NONZEROS TO BE SENT TO EACH PROCESS,
     THEN ALLOCATE SPACE.
     ------------------------------------------------------------*/
    nzval_a = Astore->nzval;
    nnz_loc = 0;
    for (i = 0; i < m_loc; ++i) {
        for (j = Astore->rowptr[i]; j < Astore->rowptr[i+1]; ++j) {
            if(perm != NULL)
            {
                irow = perm[i+fst_row];         /* Row number in P*A*P^T */
                jcol = perm[Astore->colind[j]]; /* Column number in P*A*P^T */
            }
            else
            {
                irow = i+fst_row;
                jcol = Astore->colind[j];
            }
            p = Adcsc.Owner(m, n , irow, jcol, lirow, ljcol);
            //++nnzToSend[p];
            ++ nnz_loc;
            data[p].push_back(std::make_tuple(lirow,ljcol,nzval_a[j]));
            
        }
    }
    
    Adcsc.SparseCommon(data, nnz_loc, m, n, std::plus<double>());
    FullyDistVec<int_t, int_t> mateRow2Col ( Adcsc.getcommgrid(), m, (int_t) -1);
    FullyDistVec<int_t, int_t> mateCol2Row ( Adcsc.getcommgrid(), n, (int_t) -1);
    AWPM(Adcsc, mateRow2Col, mateCol2Row,true);
    
    // now gather the matching vector
    MPI_Comm World = mateRow2Col.getcommgrid()->GetWorld();
    int * rdispls = new int[procs];
    int sendcnt = mateRow2Col.LocArrSize();
    int * recvcnt = new int[procs];
    MPI_Allgather(&sendcnt, 1, MPI_INT, recvcnt, 1, MPI_INT, World);
    rdispls[0] = 0;
    for(int i=0; i<procs-1; ++i)
    {
        rdispls[i+1] = rdispls[i] + recvcnt[i];
    }
    int_t *senddata = (int_t *)mateRow2Col.GetLocArr();
    
    MPI_Allgatherv(senddata, sendcnt, MPIType<int_t>(), ScalePermstruct->perm_r, recvcnt, rdispls, MPIType<int_t>(), World);
    
#if ( DEBUGlevel>=1 )
    CHECK_MALLOC(iam, "Exit pdCSR_loc_to_2DBlock()");
#endif

    return 0;
}

#endif /* AWPM_CombBLAS_h */
