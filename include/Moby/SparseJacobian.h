/****************************************************************************
 * Copyright 2015 Evan Drumwright
 * This library is distributed under the terms of the Apache V2.0 
 * License (obtainable from http://www.apache.org/licenses/LICENSE-2.0).
 ****************************************************************************/

#ifndef _SPARSE_JACOBIAN_H
#define _SPARSE_JACOBIAN_H

#include <pthread.h>
#include <vector>
#include <Ravelin/MatrixNd.h>

namespace Moby {

/// Matrix block
/**
 * The block itself is dense and starts at row st_row_idx and column 
 * st_col_idx of the dense matrix. 
 */
struct MatrixBlock
{
  unsigned st_col_idx, st_row_idx;
  unsigned rows() const { return block.rows(); }
  unsigned columns() const { return block.columns(); }
  Ravelin::MatrixNd block;
};

/// A Sparse Jacobian representation along with multiplication routines
class SparseJacobian
{
  public:
    Ravelin::VectorNd& mult(const Ravelin::VectorNd& x, Ravelin::VectorNd& result); 
    Ravelin::MatrixNd& mult(const Ravelin::MatrixNd& x, Ravelin::MatrixNd& result); 
    Ravelin::MatrixNd& mult(const std::vector<MatrixBlock>& M, unsigned result_cols, Ravelin::MatrixNd& result); 

    // vector of Jacobian blocks
    std::vector<MatrixBlock> blocks;

    // the number of rows and columns in the dense Jacobian
    unsigned _rows, _cols;
};

} // end namespace
#endif

