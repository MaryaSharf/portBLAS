/***************************************************************************
 *
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
 *  @filename transpose.cpp
 *
 **************************************************************************/

#include "blas_test.hpp"
#include "extension_reference.hpp"

template <typename scalar_t>
using combination_t =
    std::tuple<std::string, char, index_t, index_t, index_t, index_t, scalar_t>;

template <typename scalar_t, helper::AllocType mem_alloc>
void run_test(const combination_t<scalar_t>& combi) {
  std::string alloc;
  char place;
  index_t m, n, ld_in_m, ld_out_m;
  scalar_t unused; /* Work around dpcpp compiler bug
                      (https://github.com/intel/llvm/issues/7075) */
  std::tie(alloc, place, m, n, ld_in_m, ld_out_m, unused) = combi;

  // Compute leading dimensions using ld multipliers
  index_t ld_in = ld_in_m * m;
  index_t ld_out = ld_out_m * n;

  auto q = make_queue();
  blas::SB_Handle sb_handle(q);

  const index_t size_a = ld_in * n;
  const index_t size_b = ld_out * m;

  std::vector<scalar_t> A(size_a);
  std::vector<scalar_t> B(size_b);

  fill_random(A);

  std::vector<scalar_t> A_ref = A;
  std::vector<scalar_t> B_ref = B;

  // Reference implementation
  reference_blas::Transpose<scalar_t>(A.data(), ld_in, B_ref.data(), ld_out, m,
                                      n);

  if (place == 'o') {
    auto matrix_in = helper::allocate<mem_alloc, scalar_t>(size_a, q);
    auto matrix_out = helper::allocate<mem_alloc, scalar_t>(size_b, q);

    auto copy_in =
        helper::copy_to_device<scalar_t>(q, A.data(), matrix_in, size_a);
    auto copy_out =
        helper::copy_to_device<scalar_t>(q, B.data(), matrix_out, size_b);

    auto trans_event = blas::extension::_transpose<scalar_t>(sb_handle, m, n, matrix_in, ld_in,
                                          matrix_out, ld_out, {copy_in, copy_out});

    sb_handle.wait(trans_event);

    auto event = blas::helper::copy_to_host<scalar_t>(
        sb_handle.get_queue(), matrix_out, B.data(), size_b);
    sb_handle.wait(event);

    // Validate the result
    const bool isAlmostEqual = utils::compare_vectors(B, B_ref);
    ASSERT_TRUE(isAlmostEqual);

    helper::deallocate<mem_alloc>(matrix_in, q);
    helper::deallocate<mem_alloc>(matrix_out, q);

  } else {
    // Inplace Transpose currently disabled (TODO)
    GTEST_SKIP();
  }
}

template <typename scalar_t>
void run_test(const combination_t<scalar_t> combi) {
  std::string alloc;
  char place;
  index_t m, n, ld_in_m, ld_out_m;
  scalar_t unused; /* Work around dpcpp compiler bug
                      (https://github.com/intel/llvm/issues/7075) */
  std::tie(alloc, place, m, n, ld_in_m, ld_out_m, unused) = combi;

  if (alloc == "usm") {
#ifdef SB_ENABLE_USM
    run_test<scalar_t, helper::AllocType::usm>(combi);
#else
    GTEST_SKIP();
#endif
  } else {
    run_test<scalar_t, helper::AllocType::buffer>(combi);
  }
}

template <typename scalar_t>
const auto combi =
    ::testing::Combine(::testing::Values("usm", "buf"),      // allocation type
                       ::testing::Values('i', 'o'),  // Inplace | Outplace
                       ::testing::Values<index_t>(64, 129, 255),  // m
                       ::testing::Values<index_t>(64, 129, 255),  // n
                       ::testing::Values<index_t>(1, 2, 3),       // ld_in_m
                       ::testing::Values<index_t>(1, 2, 3),       // ld_in_n
                       ::testing::Values<scalar_t>(0));  // scalar_t unused

template <class T>
static std::string generate_name(
    const ::testing::TestParamInfo<combination_t<T>>& info) {
  std::string alloc;
  index_t m, n, ld_in_m, ld_out_m;
  T unused;
  char place;
  BLAS_GENERATE_NAME(info.param, alloc, place, m, n, ld_in_m, ld_out_m, unused);
}

BLAS_REGISTER_TEST_ALL(TransposeTest, combination_t, combi, generate_name);
