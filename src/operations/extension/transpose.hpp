/***************************************************************************
 *  @license
 *  Copyright (C) Codeplay Software Limited
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  For your convenience, a copy of the License has been included in this
 *  repository.
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  portBLAS: BLAS implementation using SYCL
 *
 *  @filename transpose.hpp
 *
 **************************************************************************/

#ifndef PORTBLAS_EXTENSION_TRANSPOSE_HPP
#define PORTBLAS_EXTENSION_TRANSPOSE_HPP

#include "operations/extension/transpose.h"

namespace blas {

// Transpose
template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE bool
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::valid_thread(cl::sycl::nd_item<1> item) const {
  index_t idx = item.get_global_linear_id();
  return (idx < get_size());
}

template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE void
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::bind(cl::sycl::handler &cgh) {
  A_.bind(cgh);
  At_.bind(cgh);
}

template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE typename in_t::index_t
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::get_size() const {
  // Smallest TileSize square-multiple containing input/output matrices times
  // batch_size
  return (size_pad_ * batch_size_);
}

template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE void
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::adjust_access_displacement() {
  A_.adjust_access_displacement();
  At_.adjust_access_displacement();
}

/*!
 *@brief get_indices. This function is used in the non-local memory kernel to
 *compute global input & output indices.
 *
 * @param id [input] the cl::sycl::nd_item<1> of the current work_item
 * @param in_idx [output] the input global-memory index
 * @param out_idx [output] the output global-memory index
 * @param i [output] the global row-index
 * @param j [output] the global col-index
 */
template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE void
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::get_indices(cl::sycl::nd_item<1> id, index_t &in_idx,
                                  index_t &out_idx, index_t &i, index_t &j) {
  index_t idg = id.get_group(0);
  index_t idc = id.get_local_id(0);

  const index_t ibatch =
      (batch_size_ == index_t(1)) ? 0 : idg / tile_count_total_;

  const index_t relative_idg = idg - ibatch * tile_count_total_;

  const index_t jg = relative_idg / tile_count_m_;
  const index_t ig = relative_idg - jg * tile_count_m_;

  const index_t jl = idc / Tile_size;
  const index_t il = idc - jl * Tile_size;

  const index_t i_block_start = ig * Tile_size;
  const index_t j_block_start = jg * Tile_size;

  i = (i_block_start + il) * inc_a_;
  j = (j_block_start + jl) * inc_at_;

  in_idx = i_block_start * inc_a_ + j_block_start * lda_ + il * inc_a_ +
           jl * lda_ + ibatch * stride_a_;

  out_idx = i_block_start * ldat_ + j_block_start * inc_at_ + jl * inc_at_ +
            il * ldat_ + ibatch * stride_at_;
}

template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE void
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::eval(cl::sycl::nd_item<1> id) {
  index_t idx = id.get_global_linear_id();

  index_t in_index, out_index, i_id, j_id;

  get_indices(id, in_index, out_index, i_id, j_id);

  if (i_id < ((M_ - 1) * inc_a_ + 1)) {
    auto A = A_.get_pointer();
    auto At = At_.get_pointer();
    for (index_t l = 0; l < inner_tile_count_; l++) {
      if (j_id + l * inner_tile_size_ * inc_at_ < ((N_ - 1) * inc_at_ + 1)) {
        At[out_index + l * inner_tile_size_ * inc_at_] =
            alpha_ * A[in_index + l * inner_tile_size_ * lda_];
      }
    }
  }
}

/*!
 *@brief get_indices. This function is used in the local-memory kernel to
 *compute local & global input & output indices.
 *
 * @param id [input] the cl::sycl::nd_item<1> of the current work_item
 * @param in_idx [output] the input global-memory index
 * @param out_idx [output] the output global-memory index
 * @param in_local_idx [output] the input local-memory linear index
 * @param out_local_idx [output] the output local-memory linear index
 * @param i_block_start [output] the input global memory block row-index
 * @param j_block_start [output] the input global memory block col-index
 * @param il [output] the local row-index
 * @param jl [output] the local col-index
 *
 */
template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
PORTBLAS_INLINE void
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::get_indices(cl::sycl::nd_item<1> id, index_t &in_idx,
                                  index_t &in_local_idx, index_t &out_idx,
                                  index_t &out_local_idx,
                                  index_t &i_block_start,
                                  index_t &j_block_start, index_t &il,
                                  index_t &jl) {
  index_t idg = id.get_group(0);
  index_t idc = id.get_local_id(0);

  const index_t ibatch =
      (batch_size_ == index_t(1)) ? 0 : idg / tile_count_total_;

  const index_t relative_idg = idg - ibatch * tile_count_total_;

  const index_t jg = relative_idg / tile_count_m_;
  const index_t ig = relative_idg - jg * tile_count_m_;

  jl = idc / Tile_size;
  il = idc - jl * Tile_size;

  i_block_start = ig * Tile_size;
  j_block_start = jg * Tile_size;

  in_idx = i_block_start * inc_a_ + j_block_start * lda_ + il * inc_a_ +
           jl * lda_ + ibatch * stride_a_;

  index_t jl_cl = idc / get_non_bank_conflict_line_size();
  index_t il_cl = idc - jl_cl * get_non_bank_conflict_line_size();

  in_local_idx = jl_cl * (get_non_bank_conflict_line_size() + 1) + il_cl;

  out_idx = i_block_start * ldat_ + j_block_start * inc_at_ + il * inc_at_ +
            jl * ldat_ + ibatch * stride_at_;
  out_local_idx = il * Tile_size + jl + il / get_num_tiles_per_line();
}

template <bool in_place, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in_t, typename out_t, typename element_t>
template <typename local_memory_t>
PORTBLAS_INLINE void
Transpose<in_place, Tile_size, wg_size, cl_size, local_memory, in_t, out_t,
          element_t>::eval(local_memory_t local_mem, cl::sycl::nd_item<1> id) {
  value_t *local = local_mem.localAcc.get_pointer();
  auto A = A_.get_pointer();
  auto At = At_.get_pointer();
  index_t in_index, in_local_id, out_index, out_local_id, il, jl;
  index_t i_block_start, j_block_start;
  get_indices(id, in_index, in_local_id, out_index, out_local_id, i_block_start,
              j_block_start, il, jl);

  if (i_block_start + il < M_) {
    for (index_t l = 0; l < inner_tile_count_; l++) {
      // Copy input to local memory
      if (j_block_start + jl + l * inner_tile_size_ < N_) {
        local[in_local_id + l * (get_non_bank_conflict_line_size() + 1) *
                                (inner_tile_size_ / get_num_tiles_per_line())] =
            alpha_ * A[in_index + l * inner_tile_size_ * lda_];
      }
    }
  }
  id.barrier(cl::sycl::access::fence_space::local_space);

  if (j_block_start + il < N_) {
    for (index_t l = 0; l < inner_tile_count_; l++) {
      // Copy output from local memory
      if (i_block_start + jl + l * inner_tile_size_ < M_) {
        At[out_index + l * inner_tile_size_ * ldat_] =
            local[out_local_id + l * inner_tile_size_];
      }
    }
  }
}

// Transpose-Add
template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE bool TransposeAdd<
    both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t, in2_t, out_t,
    element_t>::valid_thread(cl::sycl::nd_item<1> item) const {
  auto idx = item.get_global_linear_id();
  return idx < get_size();
}

template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE void
TransposeAdd<both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t,
             in2_t, out_t, element_t>::bind(cl::sycl::handler &cgh) {
  A_.bind(cgh);
  B_.bind(cgh);
  C_.bind(cgh);
}

template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE typename in1_t::index_t
TransposeAdd<both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t,
             in2_t, out_t, element_t>::get_size() const {
  // Smallest TileSize square-multiple containing input/output matrices
  return (size_pad_ * batch_size_);
}

template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE void
TransposeAdd<both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t,
             in2_t, out_t, element_t>::adjust_access_displacement() {
  A_.adjust_access_displacement();
  B_.adjust_access_displacement();
  C_.adjust_access_displacement();
}

/*!
 *@brief get_indices. This function is used in the non-local memory kernel to
 *compute global input & output indices.
 *
 * @param id [input] the cl::sycl::nd_item<1> of the current work_item
 * @param in_a_idx [output] the input A global-memory index
 * @param in_b_idx [output] the input B global-memory index
 * @param out_idx [output] the output C global-memory index
 * @param i [output] the global row-index (A & B when both_trans -> [0,N_], B &
 *C otherwise -> [0,M_])
 * @param j [output] the global col-index (A & B when both_trans -> [0,M_], B &
 *C otherwise -> [0,N_])
 */
template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE void
TransposeAdd<both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t,
             in2_t, out_t, element_t>::get_indices(cl::sycl::nd_item<1> id,
                                                   index_t &in_a_idx,
                                                   index_t &in_b_idx,
                                                   index_t &out_idx, index_t &i,
                                                   index_t &j) {
  const index_t row_tiles = both_trans ? tile_count_n_ : tile_count_m_;

  index_t idg = id.get_group(0);
  index_t idc = id.get_local_id(0);

  const index_t ibatch =
      (batch_size_ == index_t(1)) ? 0 : idg / tile_count_total_;

  const index_t relative_idg = idg - ibatch * tile_count_total_;

  const index_t jg = relative_idg / row_tiles;
  const index_t ig = relative_idg - jg * row_tiles;

  const index_t jl = idc / Tile_size;
  const index_t il = idc - jl * Tile_size;

  const index_t i_block_start = ig * Tile_size;
  const index_t j_block_start = jg * Tile_size;

  i = i_block_start + il;
  j = j_block_start + jl;

  if constexpr (both_trans) {
    in_a_idx = i_block_start + j_block_start * lda_ + il + jl * lda_ +
               ibatch * stride_a_;
    in_b_idx = i_block_start + j_block_start * ldb_ + il + jl * ldb_ +
               ibatch * stride_b_;
    out_idx = i_block_start * ldc_ + j_block_start + jl + il * ldc_ +
              ibatch * stride_c_;
  } else {
    in_a_idx = i_block_start * lda_ + j_block_start + jl + il * lda_ +
               ibatch * stride_a_;
    in_b_idx = i_block_start + j_block_start * ldb_ + il + jl * ldb_ +
               ibatch * stride_b_;
    out_idx = i_block_start + j_block_start * ldc_ + il + jl * ldc_ +
              ibatch * stride_c_;
  }
}

template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE void
TransposeAdd<both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t,
             in2_t, out_t, element_t>::eval(cl::sycl::nd_item<1> id) {
  auto A = A_.get_pointer();
  auto B = B_.get_pointer();
  auto C = C_.get_pointer();
  index_t in_a_index, in_b_index, out_index, i_id, j_id;

  get_indices(id, in_a_index, in_b_index, out_index, i_id, j_id);

  if constexpr (both_trans) {
    if (i_id < N_) {
      for (index_t l = 0; l < inner_tile_count_; l++) {
        if (j_id + l * inner_tile_size_ < M_) {
          auto temp_sum = alpha_ * A[in_a_index + l * inner_tile_size_ * lda_] +
                          beta_ * B[in_b_index + l * inner_tile_size_ * ldb_];
          C[out_index + l * inner_tile_size_] = temp_sum;
        }
      }
    }
  } else {
    if (i_id < M_) {
      for (index_t l = 0; l < inner_tile_count_; l++) {
        if (j_id + l * inner_tile_size_ < N_) {
          auto temp_sum = alpha_ * A[in_a_index + l * inner_tile_size_] +
                          beta_ * B[in_b_index + l * inner_tile_size_ * ldb_];
          C[out_index + l * inner_tile_size_ * ldc_] = temp_sum;
        }
      }
    }
  }
}

/*!
 *@brief get_indices. This function is used in the local-memory kernel to
 *compute local & global input & output indices.
 *
 * @param id [input] the sycl::nd_item<1> of the current work_item
 * @param in_a_idx [output] the global index for input matrix A
 * @param in_b_idx [output] the global index for input matrix B
 * @param out_idx [output] the output global index
 * @param in_local_idx [output] the input local-memory index
 * @param out_local_idx [output] the output local-memory index
 * @param i_block_start [output] the input global memory block row-index
 * @param j_block_start [output] the input global memory block col-index
 * @param il [output] the local row-index
 * @param jl [output] the local col-index
 *
 */
template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
PORTBLAS_INLINE void TransposeAdd<
    both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t, in2_t, out_t,
    element_t>::get_indices(cl::sycl::nd_item<1> id, index_t &in_a_idx,
                            index_t &in_b_idx, index_t &in_local_idx,
                            index_t &out_idx, index_t &out_local_idx,
                            index_t &i_block_start, index_t &j_block_start,
                            index_t &il, index_t &jl) {
  const index_t row_tiles = both_trans ? tile_count_n_ : tile_count_m_;

  index_t idg = id.get_group(0);
  index_t idc = id.get_local_id(0);

  const index_t ibatch =
      (batch_size_ == index_t(1)) ? 0 : idg / tile_count_total_;

  const index_t relative_idg = idg - ibatch * tile_count_total_;

  const index_t jg = relative_idg / row_tiles;
  const index_t ig = relative_idg - jg * row_tiles;

  jl = idc / Tile_size;
  il = idc - jl * Tile_size;

  i_block_start = ig * Tile_size;
  j_block_start = jg * Tile_size;

  index_t jl_cl = idc / get_non_bank_conflict_line_size();
  index_t il_cl = idc - jl_cl * get_non_bank_conflict_line_size();

  if constexpr (both_trans) {
    in_a_idx = i_block_start + j_block_start * lda_ + il + jl * lda_ +
               ibatch * stride_a_;
    out_idx = i_block_start * ldc_ + j_block_start + il + jl * ldc_ +
              ibatch * stride_c_;

  } else {
    in_a_idx = j_block_start + i_block_start * lda_ + il + jl * lda_ +
               ibatch * stride_a_;
    out_idx = i_block_start + j_block_start * ldc_ + il + jl * ldc_ +
              ibatch * stride_c_;
  }

  in_b_idx = i_block_start + j_block_start * ldb_ + il + jl * ldb_ +
             ibatch * stride_b_;

  in_local_idx = jl_cl * (get_non_bank_conflict_line_size() + 1) + il_cl;

  out_local_idx = il * Tile_size + jl + il / get_num_tiles_per_line();
}

template <bool both_trans, int Tile_size, int wg_size, int cl_size,
          bool local_memory, typename in1_t, typename in2_t, typename out_t,
          typename element_t>
template <typename local_memory_t>
PORTBLAS_INLINE void
TransposeAdd<both_trans, Tile_size, wg_size, cl_size, local_memory, in1_t,
             in2_t, out_t, element_t>::eval(local_memory_t local_mem,
                                            cl::sycl::nd_item<1> id) {
  value_t *local = local_mem.localAcc.get_pointer();

  auto A = A_.get_pointer();
  auto B = B_.get_pointer();
  auto C = C_.get_pointer();

  index_t in_a_idx, in_b_idx, in_local_id, out_idx, out_local_id;
  index_t i_block_start, j_block_start;
  index_t il, jl;

  if constexpr (both_trans) {
    get_indices(id, in_a_idx, in_b_idx, in_local_id, out_idx, out_local_id,
                i_block_start, j_block_start, il, jl);

    if (i_block_start + il < N_) {
      // Compute & Copy sum/scaled input to local memory (before transpose)
      for (index_t l = 0; l < inner_tile_count_; l++) {
        if (j_block_start + jl + l * inner_tile_size_ < M_) {
          // Compute & Copy sum/scaled input to local memory (before transpose)
          local[in_local_id +
                l * (get_non_bank_conflict_line_size() + 1) *
                    (inner_tile_size_ / get_num_tiles_per_line())] =
              alpha_ * A[in_a_idx + l * inner_tile_size_ * lda_] +
              beta_ * B[in_b_idx + l * inner_tile_size_ * ldb_];
        }
      }
    }

    id.barrier(cl::sycl::access::fence_space::local_space);

    // Transposed copy of previous output from local memory
    if (j_block_start + il < M_) {
      for (index_t l = 0; l < inner_tile_count_; l++) {
        if (i_block_start + jl + l * inner_tile_size_ < N_) {
          C[out_idx + l * inner_tile_size_ * ldc_] =
              local[out_local_id + l * inner_tile_size_];
        }
      }
    }

  } else {
    get_indices(id, in_a_idx, in_b_idx, in_local_id, out_idx, out_local_id,
                i_block_start, j_block_start, il, jl);

    if (j_block_start + il < N_) {
      for (index_t l = 0; l < inner_tile_count_; l++) {
        if (i_block_start + jl + l * inner_tile_size_ < M_) {
          // Compute & Copy sum/scaled input to local memory (before transpose)
          local[in_local_id +
                l * (get_non_bank_conflict_line_size() + 1) *
                    (inner_tile_size_ / get_num_tiles_per_line())] =
              alpha_ * A[in_a_idx + l * inner_tile_size_ * lda_];
        }
      }
    }

    id.barrier(cl::sycl::access::fence_space::local_space);

    // Transposed copy of previous output from local memory and scaled addition
    // with 2nd non transposed matrix B
    if (i_block_start + il < M_) {
      for (index_t l = 0; l < inner_tile_count_; l++) {
        if (j_block_start + jl + l * inner_tile_size_ < N_) {
          C[out_idx + l * inner_tile_size_ * ldc_] =
              local[out_local_id + l * inner_tile_size_] +
              beta_ * B[in_b_idx + l * inner_tile_size_ * ldb_];
        }
      }
    }
  }
}

}  // namespace blas

#endif  // PORTBLAS_EXTENSION_TRANSPOSE_HPP
