/*******************************************************************************
* Copyright 2020-2021 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions
* and limitations under the License.
*
*
* SPDX-License-Identifier: Apache-2.0
*******************************************************************************/

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#if __has_include(<sycl/sycl.hpp>)
#include <sycl/sycl.hpp>
#else
#include <CL/sycl.hpp>
#endif
#include "cblas.h"
#include "oneapi/mkl/detail/config.hpp"
#include "oneapi/mkl.hpp"
#include "onemkl_blas_helper.hpp"
#include "reference_blas_templates.hpp"
#include "test_common.hpp"
#include "test_helper.hpp"

#include <gtest/gtest.h>

using namespace sycl;
using std::vector;

extern std::vector<sycl::device*> devices;

namespace {

template <typename fp, typename fp_res>
int test(device* dev, oneapi::mkl::layout layout, int64_t N, int64_t incx) {
    // Prepare data.
    vector<fp> x;
    fp_res result = fp_res(-1), result_ref = fp_res(-1);

    rand_vector(x, N, incx);

    // Call Reference ASUM.
    using fp_ref = typename ref_type_info<fp>::type;
    const int N_ref = N, incx_ref = std::abs(incx);

    result_ref = ::asum<fp_ref, fp_res>(&N_ref, (fp_ref*)x.data(), &incx_ref);
    // Call DPC++ ASUM.

    // Catch asynchronous exceptions.
    auto exception_handler = [](exception_list exceptions) {
        for (std::exception_ptr const& e : exceptions) {
            try {
                std::rethrow_exception(e);
            }
            catch (exception const& e) {
                std::cout << "Caught asynchronous SYCL exception during ASUM:\n"
                          << e.what() << std::endl;
                print_error_code(e);
            }
        }
    };

    queue main_queue(*dev, exception_handler);

    buffer<fp, 1> x_buffer = make_buffer(x);
    buffer<fp_res, 1> result_buffer(&result, range<1>(1));

    try {
#ifdef CALL_RT_API
        switch (layout) {
            case oneapi::mkl::layout::column_major:
                oneapi::mkl::blas::column_major::asum(main_queue, N, x_buffer, incx, result_buffer);
                break;
            case oneapi::mkl::layout::row_major:
                oneapi::mkl::blas::row_major::asum(main_queue, N, x_buffer, incx, result_buffer);
                break;
            default: break;
        }
#else
        switch (layout) {
            case oneapi::mkl::layout::column_major:
                TEST_RUN_CT_SELECT(main_queue, oneapi::mkl::blas::column_major::asum, N, x_buffer,
                                   incx, result_buffer);
                break;
            case oneapi::mkl::layout::row_major:
                TEST_RUN_CT_SELECT(main_queue, oneapi::mkl::blas::row_major::asum, N, x_buffer,
                                   incx, result_buffer);
                break;
            default: break;
        }
#endif
    }
    catch (exception const& e) {
        std::cout << "Caught synchronous SYCL exception during ASUM:\n" << e.what() << std::endl;
        print_error_code(e);
    }

    catch (const oneapi::mkl::unimplemented& e) {
        return test_skipped;
    }

    catch (const std::runtime_error& error) {
        std::cout << "Error raised during execution of ASUM:\n" << error.what() << std::endl;
    }

    // Compare the results of reference implementation and DPC++ implementation.

    auto result_accessor = result_buffer.template get_host_access(read_only);
    bool good = check_equal(result_accessor[0], result_ref, N, std::cout);

    return (int)good;
}

class AsumTests : public ::testing::TestWithParam<std::tuple<sycl::device*, oneapi::mkl::layout>> {
};

TEST_P(AsumTests, RealSinglePrecision) {
    EXPECT_TRUEORSKIP(
        (::test<float, float>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 2)));
    EXPECT_TRUEORSKIP(
        (::test<float, float>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 1)));
    EXPECT_TRUEORSKIP(
        (::test<float, float>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, -3)));
}

TEST_P(AsumTests, RealDoublePrecision) {
    EXPECT_TRUEORSKIP(
        (::test<double, double>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 2)));
    EXPECT_TRUEORSKIP(
        (::test<double, double>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 1)));
    EXPECT_TRUEORSKIP(
        (::test<double, double>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, -3)));
}

TEST_P(AsumTests, ComplexSinglePrecision) {
    EXPECT_TRUEORSKIP((::test<std::complex<float>, float>(std::get<0>(GetParam()),
                                                          std::get<1>(GetParam()), 1357, 2)));
    EXPECT_TRUEORSKIP((::test<std::complex<float>, float>(std::get<0>(GetParam()),
                                                          std::get<1>(GetParam()), 1357, 1)));
    EXPECT_TRUEORSKIP((::test<std::complex<float>, float>(std::get<0>(GetParam()),
                                                          std::get<1>(GetParam()), 1357, -3)));
}

TEST_P(AsumTests, ComplexDoublePrecision) {
    EXPECT_TRUEORSKIP((test<std::complex<double>, double>(std::get<0>(GetParam()),
                                                          std::get<1>(GetParam()), 1357, 2)));
    EXPECT_TRUEORSKIP((test<std::complex<double>, double>(std::get<0>(GetParam()),
                                                          std::get<1>(GetParam()), 1357, 1)));
    EXPECT_TRUEORSKIP((test<std::complex<double>, double>(std::get<0>(GetParam()),
                                                          std::get<1>(GetParam()), 1357, -3)));
}

INSTANTIATE_TEST_SUITE_P(AsumTestSuite, AsumTests,
                         ::testing::Combine(testing::ValuesIn(devices),
                                            testing::Values(oneapi::mkl::layout::column_major,
                                                            oneapi::mkl::layout::row_major)),
                         ::LayoutDeviceNamePrint());

} // anonymous namespace
