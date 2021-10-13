/** @file gsExprAssembler.h

    @brief Generic expressions matrix assembly

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): A. Mantzaflaris
*/

#pragma once

#include <gsUtils/gsPointGrid.h>
#include <gsAssembler/gsQuadrature.h>
#include <gsAssembler/gsExprHelper.h>

namespace gismo
{

/**
   Assembler class for generating matrices and right-hand sides based
   on isogeometric expressions
*/
template<class T>
class gsExprAssembler
{
private:
    typename gsExprHelper<T>::Ptr m_exprdata;

    gsOptionList m_options;

    expr::gsFeElement<T> m_element;

    gsSparseMatrix<T> m_matrix;
    gsMatrix<T>       m_rhs;

    std::list<gsFeSpaceData<T> > m_sdata;
    std::vector<gsFeSpaceData<T>*> m_vrow;
    std::vector<gsFeSpaceData<T>*> m_vcol;

    typedef typename gsExprHelper<T>::nullExpr    nullExpr;

public:

    typedef typename gsSparseMatrix<T>::BlockView matBlockView;

    typedef typename gsSparseMatrix<T>::constBlockView matConstBlockView;

    typedef typename gsBoundaryConditions<T>::bcRefList   bcRefList;
    typedef typename gsBoundaryConditions<T>::bcContainer bcContainer;
    //typedef typename gsBoundaryConditions<T>::ppContainer ifContainer;
    typedef gsBoxTopology::ifContainer ifContainer;

    typedef typename gsExprHelper<T>::element     element;     ///< Current element
    typedef typename gsExprHelper<T>::geometryMap geometryMap; ///< Geometry map type
    typedef typename gsExprHelper<T>::variable    variable;    ///< Variable type
    typedef typename gsExprHelper<T>::space       space;       ///< Space type
    typedef typename expr::gsFeSolution<T>        solution;    ///< Solution type

    /*
    typedef typename gsExprHelper<T>::function    function;    ///< Variable type
    typedef typename gsExprHelper<T>::variable    variable;    ///< Space type
    typedef typename expr::gsFeSolution<T>        solution;    ///< Solution type
    */
public:

    void cleanUp()
    {
        m_exprdata->cleanUp();
    }

    /// Constructor
    /// \param _rBlocks Number of spaces for test functions
    /// \param _cBlocks Number of spaces for solution variables
    gsExprAssembler(index_t _rBlocks = 1, index_t _cBlocks = 1)
    : m_exprdata(gsExprHelper<T>::make()), m_options(defaultOptions()),
      m_vrow(_rBlocks,nullptr), m_vcol(_cBlocks,nullptr)
    { }

    // The copy constructor replicates the same environemnt but does
    // not copy any matrix data

    /// @brief Returns the list of default options for assembly
    static gsOptionList defaultOptions();

    /// Returns the number of degrees of freedom (after initialization)
    index_t numDofs() const
    {
        GISMO_ASSERT( m_vcol.back()->mapper.isFinalized(),
                      "gsExprAssembler::numDofs() says: initSystem() has not been called.");
        return m_vcol.back()->mapper.firstIndex() +
	  m_vcol.back()->mapper.freeSize();
    }

    /// Returns the number of test functions (after initialization)
    index_t numTestDofs() const
    {
        GISMO_ASSERT( m_vrow.back()->mapper.isFinalized(),
                      "initSystem() has not been called.");
        return m_vrow.back()->mapper.firstIndex() +
	  m_vrow.back()->mapper.freeSize();
    }

    /// Returns the number of blocks in the matrix, corresponding to
    /// variables/components
    index_t numBlocks() const
    {
        index_t nb = 0;
        for (size_t i = 0; i!=m_vrow.size(); ++i)
            nb += m_vrow[i]->dim;
        return nb;
    }

    /// Returns a reference to the options structure
    gsOptionList & options() {return m_options;}

    /// @brief Returns the left-hand global matrix
    const gsSparseMatrix<T> & matrix() const { return m_matrix; }

    /// @brief Writes the resulting matrix in \a out. The internal matrix is moved.
    void matrix_into(gsSparseMatrix<T> & out) { out = give(m_matrix); }

    EIGEN_STRONG_INLINE gsSparseMatrix<T> giveMatrix()
    {
         gsSparseMatrix<T> rvo;
         rvo.swap(m_matrix);
          return rvo;
    }

    /// @brief Returns the right-hand side vector(s)
    const gsMatrix<T> & rhs() const { return m_rhs; }

    /// @brief Writes the resulting vector in \a out. The internal data is moved.
    void rhs_into(gsMatrix<T> & out) { out = give(m_rhs); }

    /// \brief Sets the domain of integration.
    /// \warning Must be called before any computation is requested
    void setIntegrationElements(const gsMultiBasis<T> & mesh)
    { m_exprdata->setMultiBasis(mesh); }

#if EIGEN_HAS_RVALUE_REFERENCES
    void setIntegrationElements(const gsMultiBasis<T> &&) = delete;
    //const gsMultiBasis<T> * c++98
#endif

    /// \brief Returns the domain of integration
    const gsMultiBasis<T> & integrationElements() const
    { return m_exprdata->multiBasis(); }

    const typename gsExprHelper<T>::Ptr exprData() const { return m_exprdata; }

    /// Registers \a mp as an isogeometric geometry map and return a handle to it
    geometryMap getMap(const gsMultiPatch<T> & mp) //conv->tmp->error
    { return m_exprdata->getMap(mp); }

    /// Registers \a g as an isogeometric geometry map and return a handle to it
    geometryMap getMap(const gsFunction<T> & g)
    { return m_exprdata->getMap(g); }

    /// Registers \a mp as an isogeometric (both trial and test) space
    /// and return a handle to it
    space getSpace(const gsFunctionSet<T> & mp, index_t dim = 1, index_t id = 0)
    {
        //if multiBasisSet() then check domainDom
        GISMO_ASSERT(1==mp.targetDim(), "Expecting scalar source space");
        GISMO_ASSERT(static_cast<size_t>(id)<m_vrow.size(),
                     "Given ID "<<id<<" exceeds "<<m_vrow.size()-1 );

        expr::gsFeSpace<T> u = m_exprdata->getSpace(mp,dim);
        m_sdata.emplace_back(mp,dim,id);
        u.setSpaceData(m_sdata.back());
        m_vrow[id] = m_vcol[id] = &m_sdata.back();
        return u;
    }

    /// \brief Registers \a mp as an isogeometric test space
    /// corresponding to trial space \a u and return a handle to it
    ///
    /// \note Both test and trial spaces are registered at once by
    /// gsExprAssembler::getSpace.
    ///
    ///Use this function after calling gsExprAssembler::getSpace when
    /// a distinct test space is requred (eg. Petrov-Galerkin
    /// methods).
    ///
    /// \note The dimension is set to the same as \a u, unless the caller
    /// sets as a third argument a new value.
    space getTestSpace(space u, const gsFunctionSet<T> & mp, index_t dim = -1)
    {
        expr::gsFeSpace<T> s = m_exprdata->getSpace(mp,(-1 == dim ? u.dim() : dim));
        m_sdata.emplace_back(mp,s.dim(),u.id());
        s.setSpaceData(m_sdata.back());
        m_vrow[s.id()] = &m_sdata.back();
        return s;
    }

    /// Return a variable handle (previously created by getSpace) for
    /// unknown \a id
    space trialSpace(const index_t id) const
    {
        GISMO_ASSERT(NULL!=m_vcol[id], "Not set.");
        expr::gsFeSpace<T> s = m_exprdata->
            getSpace(*m_vcol[id]->fs,m_vcol[id]->dim);
        s.setSpaceData(*m_vcol[id]);
        return s;
    }

    /// Return the trial space of a pre-existing test space \a v
    space trialSpace(space & v) const { return trialSpace(v.id()); }

    /// Return the variable (previously created by getTrialSpace) with the given \a id
    space testSpace(const index_t id)
    {
        GISMO_ASSERT(NULL!=m_vrow[id], "Not set.");
        expr::gsFeSpace<T> s = m_exprdata->
            getSpace(*m_vrow[id]->fs,m_vrow[id]->dim());
        s.setSpaceData(*m_vrow[id]);
        return *m_vrow[id];
    }

    /// Return the test space of a pre-existing trial space \a u
    space testSpace(space u) const { return testSpace(u.id()); }

    /// Registers \a func as a variable and returns a handle to it
    ///
    variable getCoeff(const gsFunctionSet<T> & func)
    { return m_exprdata->getVar(func, 1); }

    /// Registers \a func as a variable defined on \a G and returns a handle to it
    ///
    expr::gsComposition<T> getCoeff(const gsFunctionSet<T> & func, geometryMap & G)
    { return m_exprdata->getVar(func,G); }

    /// \brief Registers a representation of a solution variable from
    /// space \a s, based on the vector \a cf.
    ///
    /// The vector \a cf should have the structure of the columns of
    /// the system matrix this->matrix(). The returned handle
    /// corresponds to a function in the space \a s
    solution getSolution(const expr::gsFeSpace<T> & s, gsMatrix<T> & cf) const
    { return solution(s, cf); }

    variable getBdrFunction() const { return m_exprdata->getMutVar(); }

    variable getBdrFunction(const gsBoundaryConditions<T> & bc, const std::string & tag) const
    { return m_exprdata->getMutVar(); }

    element getElement() const { return m_element; }

    void setFixedDofVector(gsMatrix<T> & dof, short_t unk = 0);
    void setFixedDofs(const gsMatrix<T> & coefMatrix, short_t unk = 0, size_t patch = 0);

    /// \brief Initializes the sparse system (sparse matrix and rhs)
    void initSystem()
    {
        // Check spaces.nPatches==mesh.patches
        initMatrix();
        m_rhs.setZero(numDofs(), 1);
    }

    /// \brief Initializes the sparse matrix only
    void initMatrix()
    {
        resetDimensions();
        m_matrix = gsSparseMatrix<T>(numTestDofs(), numDofs());

        if ( 0 == m_matrix.rows() || 0 == m_matrix.cols() )
            gsWarn << " No internal DOFs, zero sized system.\n";
        else
        {
            // Pick up values from options
            const T bdA       = m_options.getReal("bdA");
            const index_t bdB = m_options.getInt("bdB");
            const T bdO       = m_options.getReal("bdO");
            T nz = 1;
            const short_t dim = m_exprdata->multiBasis().domainDim();
            for (short_t i = 0; i != dim; ++i)
                nz *= bdA * m_exprdata->multiBasis().maxDegree(i) + bdB;

            m_matrix.reservePerColumn(numBlocks()*cast<T,index_t>(nz*(1.0+bdO)) );
        }
    }

    /// \brief Initializes the right-hand side vector only
    void initVector(const index_t numRhs = 1)
    {
        resetDimensions();
        m_rhs.setZero(numDofs(), numRhs);
    }

    /// Returns a block view of the system matrix, each block
    /// corresponding to a different space, or to different groups of
    /// dofs, in case of calar problems
    matBlockView matrixBlockView()
    {
        GISMO_ASSERT( m_vcol.back()->mapper.isFinalized(),
                      "initSystem() has not been called.");
        gsVector<index_t> rowSizes, colSizes;
        _blockDims(rowSizes, colSizes);
        return m_matrix.blockView(rowSizes,colSizes);
    }

    /// Returns a const block view of the system matrix, each block
    /// corresponding to a different space, or to different groups of
    /// dofs, in case of calar problems
    matConstBlockView matrixBlockView() const
    {
        GISMO_ASSERT( m_vcol.back()->mapper.isFinalized(),
                      "initSystem() has not been called.");
        gsVector<index_t> rowSizes, colSizes;
        _blockDims(rowSizes, colSizes);
        return m_matrix.blockView(rowSizes,colSizes);
    }

    /// Set the assembler options
    void setOptions(gsOptionList opt) { m_options = opt; } // gsOptionList opt
    // .swap(opt) todo

#   if(__cplusplus >= 201103L || _MSC_VER >= 1600 || defined(__DOXYGEN__))
    /// Adds the expressions \a args to the system matrix/rhs
    ///
    /// The arguments are considered as integrals over the whole domain
    /// \sa gsExprAssembler::setIntegrationElements
    template<class... expr> void assemble(const expr &... args);

    /// Adds the expressions \a args to the system matrix/rhs
    ///
    /// The arguments are considered as integrals over the boundary parts in \a BCs
    template<class... expr> void assemble(const bcRefList & BCs, expr&... args);

    /*
      template<class... expr> void assemble(const ifContainer & iFaces, expr... args);
      template<class... expr> void collocate(expr... args);// eg. collocate(-ilapl(u), f)
    */
#else
    template<class E1> void assemble(const expr::_expr<E1> & a1)
    {assemble(a1,nullExpr(),nullExpr(),nullExpr(),nullExpr());}
    template <class E1, class E2>
    void assemble(const expr::_expr<E1> & a1, const expr::_expr<E2> & a2)
    {assemble(a1,a2,nullExpr(),nullExpr(),nullExpr());}
    template <class E1, class E2, class E3>
    void assemble(const expr::_expr<E1> & a1, const expr::_expr<E2> & a2,
                  const expr::_expr<E3> & a3)
    {assemble(a1,a2,a3,nullExpr(),nullExpr());}
    template <class E1, class E2, class E3, class E4, class E5>
    void assemble(const expr::_expr<E1> & a1, const expr::_expr<E2> & a2,
                  const expr::_expr<E3> & a3, const expr::_expr<E4> & a4)
    {assemble(a1,a2,a3,a4,nullExpr());}
    template <class E1, class E2, class E3, class E4, class E5>
    void assemble(const expr::_expr<E1> & a1, const expr::_expr<E2> & a2,
                  const expr::_expr<E3> & a3, const expr::_expr<E4> & a4,
                  const expr::_expr<E5> & a5 );

    template<class E1> void assemble(const bcRefList & BCs, const expr::_expr<E1> & a1);
#   endif

    template<class E1, class E2>
    void assembleLhsRhsBc(const expr::_expr<E1> & exprLhs,
                          const expr::_expr<E2> & exprRhs,
                          const bcContainer & BCs)
    {
        space rvar = static_cast<space>(exprLhs.rowVar());
        //GISMO_ASSERT(m_exprdata->exists(rvar), "Error - inexistent variable.");
        space cvar = static_cast<space>(exprLhs.colVar());
        //GISMO_ASSERT(m_exprdata->exists(cvar), "Error - inexistent variable.");
        GISMO_ASSERT(rvar.id()==exprRhs.rowVar().id(), "Inconsistent left and right hand side"<<rvar.id() <<"!="<<exprRhs.rowVar().id() );
        assembleLhsRhsBc_impl<true,true>(exprLhs, exprRhs, rvar, cvar, BCs);
    }

    template<class E1>
    void assembleRhsBc(const expr::_expr<E1> & exprRhs, const bcContainer & BCs)
    {
        space var = static_cast<space>(exprRhs.rowVar());
        //GISMO_ASSERT(m_exprdata->exists(var), "Error - inexistent variable.");
        assembleLhsRhsBc_impl<false,true>(nullExpr(), exprRhs, var, var, BCs);
    }

    template<class E1>
    void assembleInterface(const expr::_expr<E1> & exprInt)
    {
        space rvar = static_cast<space>(exprInt.rowVar());
        space cvar = static_cast<space>(exprInt.colVar());
        assembleInterface_impl<true,false>(exprInt, nullExpr(), rvar, rvar, m_exprdata->multiBasis().topology().interfaces() );
    }

    template<class E1>
    void assembleRhsInterface(const expr::_expr<E1> & exprInt, const ifContainer & iFaces)
    {
        space rvar = static_cast<space>(exprInt.rowVar());
        //GISMO_ASSERT(m_exprdata->exists(rvar), "Error - inexistent variable.");
        assembleInterface_impl<false,true>(nullExpr(), exprInt, rvar, rvar, iFaces);
    }

private:

    void _blockDims(gsVector<index_t> & rowSizes,
                    gsVector<index_t> & colSizes)
    {
        if (1==m_vcol.size() && 1==m_vrow.size())
        {
            const gsDofMapper & dm = m_vcol.back()->mapper;
            rowSizes.resize(3);
            colSizes.resize(3);
            rowSizes[0]=colSizes[0] = dm.freeSize()-dm.coupledSize();
            rowSizes[1]=colSizes[1] = dm.coupledSize();
            rowSizes[2]=colSizes[2] = dm.boundarySize();
        }
        else
        {
            rowSizes.resize(m_vrow.size());
            for (index_t r = 0; r != rowSizes.size(); ++r) // for all row-blocks
                rowSizes[r] = m_vrow[r]->dim() * m_vrow[r]->mapper.freeSize();
            colSizes.resize(m_vcol.size());
            for (index_t c = 0; c != colSizes.size(); ++c) // for all col-blocks
                colSizes[c] = m_vcol[c]->dim() * m_vcol[c]->mapper.freeSize();
        }
    }

    /// \brief Reset the dimensions of all involved spaces.
    /// Called internally by the init* functions
    void resetDimensions();

    // template<bool left, bool right, class E1, class E2>
    // void assembleLhsRhs_impl(const expr::_expr<E1> & exprLhs,
    //                          const expr::_expr<E2> & exprRhs,
    //                          space rvar, space cvar);

    template<bool left, bool right, class E1, class E2>
    void assembleLhsRhsBc_impl(const expr::_expr<E1> & exprLhs,
                               const expr::_expr<E2> & exprRhs,
                               space rvar, space cvar,
                               const bcContainer & BCs);

    template<bool left, bool right, class E1, class E2>
    void assembleInterface_impl(const expr::_expr<E1> & exprLhs,
                                const expr::_expr<E2> & exprRhs,
                                space rvar, space cvar,
                                const ifContainer & iFaces);

// #if __cplusplus >= 201103L || _MSC_VER >= 1600 // c++11
//     template <class op, class E1>
//     void _apply(op _op, const expr::_expr<E1> & firstArg) {_op(firstArg);}
//     template <class op, class E1, class... Rest>
//     void _apply(op _op, const expr::_expr<E1> & firstArg, Rest... restArgs)
//     { _op(firstArg); _apply<op>(_op, restArgs...); }
// #endif

    struct __printExpr
    {
        template <typename E> void operator() (const gismo::expr::_expr<E> & v)
        { v.print(gsInfo);gsInfo<<"\n"; }
    } _printExpr;

    struct _eval
    {
        gsSparseMatrix<T> & m_matrix;
        gsMatrix<T>       & m_rhs;
        const gsVector<T> & m_quWeights;
        index_t             m_patchInd;
        gsMatrix<T>         localMat;

        _eval(gsSparseMatrix<T> & _matrix,
              gsMatrix<T>       & _rhs,
              const gsVector<>  & _quWeights)
        : m_matrix(_matrix), m_rhs(_rhs),
          m_quWeights(_quWeights), m_patchInd(0)
        { }

        void setPatch(const index_t p) { m_patchInd=p; }

        template <typename E> void operator() (const gismo::expr::_expr<E> & ee)
        {
            // ------- Compute  -------
            const T * w = m_quWeights.data();
            localMat.noalias() = (*w) * ee.eval(0);
            for (index_t k = 1; k != m_quWeights.rows(); ++k)
                localMat.noalias() += (*(++w)) * ee.eval(k);

            //  ------- Accumulate  -------
            if (E::isMatrix())
                push<true>(ee.rowVar(), ee.colVar(), m_patchInd);
            else if (E::isVector())
                push<false>(ee.rowVar(), ee.colVar(), m_patchInd);
            else
            {
                GISMO_ERROR("Something went terribly wrong at this point");
                //GISMO_ASSERTrowSpan() && (!colSpan())
            }

        }// operator()

        void operator() (const expr::_expr<expr::gsNullExpr<T> > &) {}

        template<bool isMatrix> void push(const expr::gsFeSpace<T> & v,
                                          const expr::gsFeSpace<T> & u,
                                          const index_t patchInd)
        {
            GISMO_ASSERT(v.isValid(), "The row space is not valid");
            GISMO_ASSERT(!isMatrix || u.isValid(), "The column space is not valid");
            GISMO_ASSERT(isMatrix || (0!=m_rhs.size()), "The right-hand side vector is not initialized");

            const index_t cd            = u.dim();
            const index_t rd            = v.dim();
            const gsDofMapper  & rowMap = v.mapper();
            const gsDofMapper  & colMap = (isMatrix ? u.mapper() : rowMap);
            gsMatrix<index_t> & rowInd0 = const_cast<gsMatrix<index_t>&>(v.data().actives);
            gsMatrix<index_t> & colInd0 = (isMatrix ? const_cast<gsMatrix<index_t>&>(u.data().actives) : rowInd0);
            const gsMatrix<T> & fixedDofs = (isMatrix ? u.fixedPart() : gsMatrix<T>());

            gsMatrix<index_t> rowInd, colInd;
            rowMap.localToGlobal(rowInd0, patchInd, rowInd);

            if (isMatrix)
            {
                GISMO_ASSERT( rowInd0.rows()*rd==localMat.rows() && colInd0.rows()*cd==localMat.cols(),
                              "Invalid local matrix (expected "<<rowInd0.rows()*rd <<"x"<< colInd0.rows()*cd <<"), got\n" << localMat );
                                
                //if (&rowInd0!=&colInd0)
                colMap.localToGlobal(colInd0, patchInd, colInd);
                GISMO_ASSERT( colMap.boundarySize()==fixedDofs.size(),
                              "Invalid values for fixed part");
            }
            for (index_t r = 0; r != rd; ++r)
            {
                const index_t rls = r * rowInd0.rows();     //local stride
                for (index_t i = 0; i != rowInd0.rows(); ++i)
                {
                    const index_t ii = rowMap.index(rowInd0.at(i),patchInd,r); //N_i
                    if ( rowMap.is_free_index(ii) )
                    {
                        if (isMatrix)
                        {
                            for (index_t c = 0; c != cd; ++c)
                            {
                                const index_t cls = c * colInd0.rows();     //local stride

                                for (index_t j = 0; j != colInd0.rows(); ++j)
                                {
                                    if ( 0 == localMat(rls+i,cls+j) ) continue;

                                    const index_t jj = colMap.index(colInd0.at(j),patchInd,c); // N_j
                                    if ( colMap.is_free_index(jj) )
                                    {
                                        // If matrix is symmetric, we could
                                        // store only lower triangular part
                                        //if ( (!symm) || jj <= ii )
#                                       pragma omp critical (acc_m_matrix)
                                        m_matrix.coeffRef(ii, jj) += localMat(rls+i,cls+j);
                                    }
                                    else // colMap.is_boundary_index(jj) )
                                    {
                                        // Symmetric treatment of eliminated BCs
                                        // GISMO_ASSERT(1==m_rhs.cols(), "-");
#                                       pragma omp atomic
                                        m_rhs.at(ii) -= localMat(rls+i,cls+j) *
                                            fixedDofs.at(colMap.global_to_bindex(jj));
                                    }
                                }
                            }
                        }
                        else
                        {
#                           pragma omp atomic
                            m_rhs.at(ii) += localMat.at(rls+i);
                        }
                    }
                }
            }
        }//push

    };

}; // gsExprAssembler

template<class T>
gsOptionList gsExprAssembler<T>::defaultOptions()
{
    gsOptionList opt;
    opt.addInt("DirichletValues"  , "Method for computation of Dirichlet DoF values [100..103]", 101);
    opt.addReal("quA", "Number of quadrature points: quA*deg + quB", 1.0  );
    opt.addInt ("quB", "Number of quadrature points: quA*deg + quB", 1    );
    opt.addReal("bdA", "Estimated nonzeros per column of the matrix: bdA*deg + bdB", 2.0  );
    opt.addInt ("bdB", "Estimated nonzeros per column of the matrix: bdA*deg + bdB", 1    );
    opt.addReal("bdO", "Overhead of sparse mem. allocation: (1+bdO)(bdA*deg + bdB) [0..1]", 0.333);
    return opt;
}

template<class T>
void gsExprAssembler<T>::setFixedDofVector(gsMatrix<T> & vals, short_t unk)
{
    gsMatrix<T> & fixedDofs = m_vcol[unk]->fixedDofs;
    fixedDofs.swap(vals);
    vals.resize(0, 0);
    // Assuming that the DoFs are already set by the user
    GISMO_ENSURE( fixedDofs.size() == m_vcol[unk]->mapper.boundarySize(),
                     "The Dirichlet DoFs were not provided correctly.");
}

template<class T>
void gsExprAssembler<T>::setFixedDofs(const gsMatrix<T> & coefMatrix, short_t unk, size_t patch)
{
    GISMO_ASSERT( m_options.getInt("DirichletValues") == dirichlet::user, "Incorrect options");

    expr::gsFeSpace<T> & u = *m_vcol[unk];
    //const index_t dirStr = m_options.getInt("DirichletStrategy");
    const gsMultiBasis<T> & mbasis = *dynamic_cast<const gsMultiBasis<T>* >(m_vcol[unk]->fs);

    //const gsBoundaryConditions<> & bbc = u.hasBc() ? u.bc() : gsBoundaryConditions<>();

    const gsDofMapper & mapper = m_vcol[unk]->mapper;
//    const gsDofMapper & mapper =
//        dirichlet::elimination == dirStr ? u.mapper
//        : mbasis.getMapper(dirichlet::elimination,
//                           static_cast<iFace::strategy>(m_options.getInt("InterfaceStrategy")),
//                           bbc, u.id()) ;

    gsMatrix<T> & fixedDofs = m_vcol[unk]->fixedDofs;
    GISMO_ASSERT(fixedDofs.size() == m_vcol[unk]->mapper.boundarySize(),
                 "Fixed DoFs were not initialized.");

    // for every side with a Dirichlet BC
    // for ( typename gsBoundaryConditions<T>::const_iterator
    //       it =  bbc.dirichletBegin();
    //       it != bbc.dirichletEnd()  ; ++it )
    typedef typename gsBoundaryConditions<T>::bcRefList bcRefList;
    for ( typename bcRefList::const_iterator it =  u.bc().dirichletBegin();
          it != u.bc().dirichletEnd()  ; ++it )
    {
        const index_t com = it->unkComponent();
        const index_t k = it->patch();
        if ( k == patch )
        {
            // Get indices in the patch on this boundary
            const gsMatrix<index_t> boundary =
                    mbasis[k].boundary(it->side());

            //gsInfo <<"Setting the value for: "<< boundary.transpose() <<"\n";

            for (index_t i=0; i!= boundary.size(); ++i)
            {
                // Note: boundary.at(i) is the patch-local index of a
                // control point on the patch
                const index_t ii  = mapper.bindex( boundary.at(i) , k, com );

                fixedDofs.at(ii) = coefMatrix(boundary.at(i), com);
            }
        }
    }
} // setFixedDofs

template<class T> void gsExprAssembler<T>::resetDimensions()
{
    for (size_t i = 1; i!=m_vcol.size(); ++i)
    {
        m_vcol[i]->mapper.setShift(m_vcol[i-1]->mapper.firstIndex() +
                                     m_vcol[i-1]->dim*m_vcol[i-1]->mapper.freeSize() );

        if ( m_vcol[i] != m_vrow[i] )
            m_vrow[i]->mapper.setShift(m_vrow[i-1]->mapper.firstIndex() +
                                         m_vrow[i-1]->dim*m_vrow[i-1]->mapper.freeSize() );
    }
}

template<size_t I, class op, typename... Ts>
void op_tuple_impl (op _op, const std::tuple<Ts...> &tuple)
{
    _op(std::get<I>(tuple));
    if (I + 1 < sizeof... (Ts))
        op_tuple_impl<(I+1 < sizeof... (Ts) ? I+1 : I)> (_op, tuple);
}

template<class op, typename... Ts>
void op_tuple (op _op, const std::tuple<Ts...> &tuple)
{ op_tuple_impl<0>(_op,tuple); }
//template<class op> void op_tuple<> (op,const std::tuple<> &) { }

template<class T>
#if(__cplusplus >= 201103L || _MSC_VER >= 1600 || defined(__DOXYGEN__)) // c++11
template<class... expr>
void gsExprAssembler<T>::assemble(const expr &... args)
#else
    template <class E1, class E2, class E3, class E4, class E5>
    void gsExprAssembler<T>::assemble( const expr::_expr<E1> & a1, const expr::_expr<E2> & a2,
    const expr::_expr<E3> & a3, const expr::_expr<E4> & a4, const expr::_expr<E5> & a5)
#endif
{
    GISMO_ASSERT(matrix().cols()==numDofs(), "System not initialized");

#pragma omp parallel
{
#   ifdef _OPENMP
    const int tid = omp_get_thread_num();
    const int nt  = omp_get_num_threads();
#   endif
    auto arg_tpl = std::make_tuple(args...);

    m_exprdata->parse(arg_tpl);
    //op_tuple(__printExpr(), arg_tpl);

    typename gsQuadRule<T>::uPtr QuRule; // Quadrature rule  ---->OUT

    gsVector<T> quWeights; // quadrature weights

    _eval ee(m_matrix, m_rhs, quWeights);

    // Note: omp thread will loop over all patches and will work on Ep/nt
    // elements, where Ep is the elements on the patch.
    for (unsigned patchInd = 0; patchInd < m_exprdata->multiBasis().nBases(); ++patchInd) //todo: distribute in parallel somehow?
    {
        ee.setPatch(patchInd);
        QuRule = gsQuadrature::getPtr(m_exprdata->multiBasis().basis(patchInd), m_options);

        // Initialize domain element iterator for current patch
        typename gsBasis<T>::domainIter domIt =  // add patchInd to domainiter ?
            m_exprdata->multiBasis().basis(patchInd).makeDomainIterator();
//        m_element.set(*domIt);

        // Start iteration over elements of patchInd
#       ifdef _OPENMP
        for ( domIt->next(tid); domIt->good(); domIt->next(nt) )
#       else
        for (; domIt->good(); domIt->next() )
#       endif
        {
            // Map the Quadrature rule to the element
            QuRule->mapTo( domIt->lowerCorner(), domIt->upperCorner(),
                           m_exprdata->points(), quWeights);

            if (m_exprdata->points().cols()==0)
                continue;

            // Perform required pre-computations on the quadrature nodes
            m_exprdata->precompute(patchInd);
            //m_exprdata->precompute(patchInd, QuRule, *domIt); // todo

            // Assemble contributions of the element
#           if __cplusplus >= 201103L || _MSC_VER >= 1600
            op_tuple(ee, arg_tpl);
#           else
            ee(a1);ee(a2);ee(a3);ee(a4);ee(a5);
#           endif
        }
    }

}//omp parallel

    m_matrix.makeCompressed();
}

template<class T>
#if __cplusplus >= 201103L || _MSC_VER >= 1600 // c++11
template<class... expr>
void gsExprAssembler<T>::assemble(const bcRefList & BCs, expr&... args)
#else
template <class E1>
void gsExprAssembler<T>::assemble(const bcRefList & BCs, const expr::_expr<E1> & a1)
#endif
{
        GISMO_ASSERT(matrix().cols()==numDofs(), "System not initialized");

// #pragma omp parallel
// {
// #   ifdef _OPENMP
//     const int tid = omp_get_thread_num();
//     const int nt  = omp_get_num_threads();
// #   endif
    auto arg_tpl = std::make_tuple(args...);

    m_exprdata->parse(arg_tpl);

    typename gsQuadRule<T>::uPtr QuRule; // Quadrature rule  ---->OUT
    gsVector<T> quWeights;               // quadrature weights

    _eval ee(m_matrix, m_rhs, quWeights);

//#   pragma omp parallel for
    for (typename bcRefList::const_iterator iit = BCs.begin(); iit!= BCs.end(); ++iit)
    {
        const boundary_condition<T> * it = &iit->get();

        QuRule = gsQuadrature::getPtr(m_exprdata->multiBasis().basis(it->patch()), m_options, it->side().direction());
        m_exprdata->mapData.side = it->side();

        // Update boundary function source
        m_exprdata->setMutSource(*it->function(), it->parametric());
        //mutVar.registerVariable(func, mutData);

        typename gsBasis<T>::domainIter domIt =
            m_exprdata->multiBasis().basis(it->patch()).makeDomainIterator(it->side());
        m_element.set(*domIt);

        // Start iteration over elements
        for (; domIt->good(); domIt->next() )
        {
            // Map the Quadrature rule to the element
            QuRule->mapTo( domIt->lowerCorner(), domIt->upperCorner(),
                           m_exprdata->points(), quWeights);

            if (m_exprdata->points().cols()==0)
                continue;

            // Perform required pre-computations on the quadrature nodes
            m_exprdata->precompute(it->patch());

            // Assemble contributions of the element
#           if __cplusplus >= 201103L || _MSC_VER >= 1600
            _apply(ee, arg_tpl);
#           else
            ee(a1);
#           endif
        }
    }

//}//omp parallel

    m_matrix.makeCompressed();
}


template<class T>
template<bool left, bool right, class E1, class E2>
void gsExprAssembler<T>::assembleLhsRhsBc_impl(const expr::_expr<E1> & exprLhs,
                                               const expr::_expr<E2> & exprRhs,
                                               space rvar, space cvar,//unused?
                                               const bcContainer & BCs)
{
    //GISMO_ASSERT( exprRhs.isVector(), "Expecting vector expression");
    
    auto arg_lhs(exprLhs.derived());//copying expressions
    auto arg_rhs(exprRhs.derived());

    if (left ) m_exprdata->parse(arg_lhs);
    if (right) m_exprdata->parse(arg_rhs);

    gsVector<T> quWeights;// quadrature weights
    typename gsQuadRule<T>::uPtr QuRule;
    _eval ee(m_matrix, m_rhs, quWeights);

    for (typename bcContainer::const_iterator it = BCs.begin(); it!= BCs.end(); ++it)
    {
        QuRule = gsQuadrature::getPtr(m_exprdata->multiBasis().basis(it->patch()), m_options, it->side().direction());

        // Update boundary function source
        m_exprdata->setMutSource(*it->function(), it->parametric());
        //mutVar.registerVariable(func, mutData);

        typename gsBasis<T>::domainIter domIt =
            m_exprdata->multiBasis().basis(it->patch())
            .makeDomainIterator(it->side());
        m_element.set(*domIt);

        // Start iteration over elements
        for (; domIt->good(); domIt->next() )
        {
            // Map the Quadrature rule to the element
            QuRule->mapTo( domIt->lowerCorner(), domIt->upperCorner(),
                           m_exprdata->points(), quWeights);

            if (m_exprdata->points().cols()==0)
                continue;

            // Perform required pre-computations on the quadrature nodes
            m_exprdata->precompute(it->patch(), it->side());

            ee.setPatch(it->patch());
            ee(arg_lhs);
            ee(arg_rhs);
        }
    }

    m_matrix.makeCompressed();
    //g_bd.clear();
    //mutVar.clear();
}

template<class T>
template<bool left, bool right, class E1, class E2>
void gsExprAssembler<T>::assembleInterface_impl(const expr::_expr<E1> & exprLhs,
                                                const expr::_expr<E2> & exprRhs,
                                                space rvar, space cvar,
                                                const ifContainer & iFaces)
{
    // auto lhs11 = std::make_tuple(exprLhs);
    // auto lhs12 = std::make_tuple(exprLhs);
    // auto lhs21 = std::make_tuple(exprLhs);
    // auto lhs22 = std::make_tuple(exprLhs);

    auto arg_lhs = std::make_tuple(exprLhs);
    auto arg_rhs = std::make_tuple(exprRhs);
    if (left ) m_exprdata->parse(arg_lhs);
    if (right) m_exprdata->parse(arg_rhs);

    gsVector<T> quWeights;// quadrature weights
    typename gsQuadRule<T>::uPtr QuRule;
    _eval ee(m_matrix, m_rhs, quWeights);

    for (gsBoxTopology::const_iiterator it = iFaces.begin();
         it != iFaces.end(); ++it )
    {
        const boundaryInterface & iFace = *it;
        const index_t patch1 = iFace.first() .patch;
        const index_t patch2 = iFace.second().patch;
        //const gsAffineFunction<T> interfaceMap(m_pde_ptr->patches().getMapForInterface(bi));

        QuRule = gsQuadrature::getPtr(m_exprdata->multiBasis().basis(patch1),
                                   m_options, iFace.first().side().direction());

        m_exprdata->setSide        ( iFace.first() .side() );
        m_exprdata->iface().setSide( iFace.second().side() );
                
        typename gsBasis<T>::domainIter domIt =
            m_exprdata->multiBasis().basis(patch1)
            .makeDomainIterator(iFace.first().side());
        m_element.set(*domIt);

        // Start iteration over elements
        for (; domIt->good(); domIt->next() )
        {
            // Map the Quadrature rule to the element
            QuRule->mapTo( domIt->lowerCorner(), domIt->upperCorner(),
                           m_exprdata->points(), quWeights);

            if (m_exprdata->points().cols()==0)
                continue;

            // Perform required pre-computations on the quadrature nodes
            m_exprdata->precompute(patch1);
            m_exprdata->iface().precompute(patch2);
            
            // interfaceMap.eval_into(m_exprdata->points(),
            //                        m_exprdata->iface().points());

            // uL*vL/2 + uR*vL/2  - uL*vR/2 - uR*vR/2
            //[ B11 B21 ]
            //[ B12 B22 ]
            // arg_lhs.setTestSide (true)
            // arg_lhs.setTrialSide(true)

            ee.setPatch(patch1);
            ee(arg_lhs);
            ee(arg_rhs);
        }
    }

    m_matrix.makeCompressed();
}

} //namespace gismo
